/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   Shared memory functions
   Copyright (C) Erik Devriendt 1996-1997
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#include "includes.h"


#if FAST_SHARE_MODES


extern int DEBUGLEVEL;


#define SMB_SHM_MAGIC 0x53484100
/* = "SHM" in hex */

#define SMB_SHM_VERSION 1

/* WARNING : offsets are used because mmap() does not guarantee that all processes have the 
   shared memory mapped to the same address */

struct SmbShmHeader
{
   int smb_shm_magic;
   int smb_shm_version;
   int total_size;	/* in bytes */
   BOOL consistent;
   smb_shm_offset_t first_free_off;
   smb_shm_offset_t userdef_off;    /* a userdefined offset. can be used to store root of tree or list */
   struct {		/* a cell is a range of bytes of sizeof(struct SmbShmBlockDesc) size */
      int cells_free;
      int cells_used;
      int cells_system; /* number of cells used as allocated block descriptors */
   } statistics;
};

#define SMB_SHM_NOT_FREE_OFF (-1)
struct SmbShmBlockDesc
{
   smb_shm_offset_t next;	/* offset of next block in the free list or SMB_SHM_NOT_FREE_OFF when block in use  */
   int          size;   /* user size in BlockDescSize units */
};

#define	EOList_Addr	(struct SmbShmBlockDesc *)( 0 )
#define EOList_Off      (NULL_OFFSET)

#define	CellSize	sizeof(struct SmbShmBlockDesc)

/* HeaderSize aligned on 8 byte boundary */
#define	AlignedHeaderSize  	((sizeof(struct SmbShmHeader)+7) & ~7)

static int  smb_shm_fd = -1;
static pstring smb_shm_processreg_name = "";

static struct SmbShmHeader *smb_shm_header_p = (struct SmbShmHeader *)0;
static int smb_shm_times_locked = 0;

static BOOL smb_shm_register_process(char *processreg_file, pid_t pid, BOOL *other_processes)
{
   int smb_shm_processes_fd = -1;
   int nb_read;
   pid_t other_pid;
   int free_slot = -1;
   int erased_slot;   
   
   smb_shm_processes_fd = open(processreg_file, O_RDWR | O_CREAT, 0666);
   if ( smb_shm_processes_fd < 0 )
   {
      DEBUG(0,("ERROR smb_shm_register_process : processreg_file open failed with code %d\n",errno));
      return False;
   }
   
   *other_processes = False;
   
   while ((nb_read = read(smb_shm_processes_fd, &other_pid, sizeof(other_pid))) > 0)
   {
      if(other_pid)
      {
	 if(process_exists(other_pid))
	    *other_processes = True;
	 else
	 {
	    /* erase old pid */
            DEBUG(2,("smb_shm_register_process : erasing stale record for pid %d\n",other_pid));
	    other_pid = (pid_t)0;
	    erased_slot = lseek(smb_shm_processes_fd, -sizeof(other_pid), SEEK_CUR);
	    write(smb_shm_processes_fd, &other_pid, sizeof(other_pid));
	    if(free_slot < 0)
	       free_slot = erased_slot;
	 }
      }
      else 
	 if(free_slot < 0)
	    free_slot = lseek(smb_shm_processes_fd, -sizeof(other_pid), SEEK_CUR);
   }
   if (nb_read < 0)
   {
      DEBUG(0,("ERROR smb_shm_register_process : processreg_file read failed with code %d\n",errno));
      close(smb_shm_processes_fd);
      return False;
   }
   
   if(free_slot < 0)
      free_slot = lseek(smb_shm_processes_fd, 0, SEEK_END);

   DEBUG(2,("smb_shm_register_process : writing record for pid %d at offset %d\n",pid,free_slot));
   lseek(smb_shm_processes_fd, free_slot, SEEK_SET);
   if(write(smb_shm_processes_fd, &pid, sizeof(pid)) < 0)
   {
      DEBUG(0,("ERROR smb_shm_register_process : processreg_file write failed with code %d\n",errno));
      close(smb_shm_processes_fd);
      return False;
   }

   close(smb_shm_processes_fd);

   return True;
}

static BOOL smb_shm_unregister_process(char *processreg_file, pid_t pid)
{
   int old_umask;
   int smb_shm_processes_fd = -1;
   int nb_read;
   pid_t other_pid;
   int erased_slot;
   BOOL found = False;
   
   
   old_umask = umask(0);
   smb_shm_processes_fd = open(processreg_file, O_RDWR);
   umask(old_umask);
   if ( smb_shm_processes_fd < 0 )
   {
      DEBUG(0,("ERROR smb_shm_unregister_process : processreg_file open failed with code %d\n",errno));
      return False;
   }
   
   while ((nb_read = read(smb_shm_processes_fd, &other_pid, sizeof(other_pid))) > 0)
   {
      if(other_pid == pid)
      {
	 /* erase pid */
         DEBUG(2,("smb_shm_unregister_process : erasing record for pid %d\n",other_pid));
	 other_pid = (pid_t)0;
	 erased_slot = lseek(smb_shm_processes_fd, -sizeof(other_pid), SEEK_CUR);
	 if(write(smb_shm_processes_fd, &other_pid, sizeof(other_pid)) < 0)
	 {
	    DEBUG(0,("ERROR smb_shm_unregister_process : processreg_file write failed with code %d\n",errno));
	    close(smb_shm_processes_fd);
	    return False;
	 }
	 
	 found = True;
	 break;
      }
   }
   if (nb_read < 0)
   {
      DEBUG(0,("ERROR smb_shm_unregister_process : processreg_file read failed with code %d\n",errno));
      close(smb_shm_processes_fd);
      return False;
   }
   
   if(!found)
   {
      DEBUG(0,("ERROR smb_shm_unregister_process : couldn't find pid %d in file %s\n",pid,processreg_file));
      close(smb_shm_processes_fd);
      return False;
   }
      
   
   close(smb_shm_processes_fd);

   return True;
}


static BOOL smb_shm_validate_header(int size)
{
   if( !smb_shm_header_p )
   {
      /* not mapped yet */
      DEBUG(0,("ERROR smb_shm_validate_header : shmem not mapped\n"));
      return False;
   }
   
   if(smb_shm_header_p->smb_shm_magic != SMB_SHM_MAGIC)
   {
      DEBUG(0,("ERROR smb_shm_validate_header : bad magic\n"));
      return False;
   }
   if(smb_shm_header_p->smb_shm_version != SMB_SHM_VERSION)
   {
      DEBUG(0,("ERROR smb_shm_validate_header : bad version %X\n",smb_shm_header_p->smb_shm_version));
      return False;
   }
   
   if(smb_shm_header_p->total_size != size)
   {
      DEBUG(0,("ERROR smb_shm_validate_header : shmem size mismatch (old = %d, new = %d)\n",smb_shm_header_p->total_size,size));
      return False;
   }

   if(!smb_shm_header_p->consistent)
   {
      DEBUG(0,("ERROR smb_shm_validate_header : shmem not consistent\n"));
      return False;
   }
   return True;
}

static BOOL smb_shm_initialize(int size)
{
   struct SmbShmBlockDesc * first_free_block_p;
   
   DEBUG(2,("smb_shm_initialize : initializing shmem file of size %d\n",size));
   
   if( !smb_shm_header_p )
   {
      /* not mapped yet */
      DEBUG(0,("ERROR smb_shm_initialize : shmem not mapped\n"));
      return False;
   }
   
   smb_shm_header_p->smb_shm_magic = SMB_SHM_MAGIC;
   smb_shm_header_p->smb_shm_version = SMB_SHM_VERSION;
   smb_shm_header_p->total_size = size;
   smb_shm_header_p->first_free_off = AlignedHeaderSize;
   smb_shm_header_p->userdef_off = NULL_OFFSET;
   
   first_free_block_p = (struct SmbShmBlockDesc *)smb_shm_offset2addr(smb_shm_header_p->first_free_off);
   first_free_block_p->next = EOList_Off;
   first_free_block_p->size = ( size - AlignedHeaderSize - CellSize ) / CellSize ;
   
   smb_shm_header_p->statistics.cells_free = first_free_block_p->size;
   smb_shm_header_p->statistics.cells_used = 0;
   smb_shm_header_p->statistics.cells_system = 1;
   
   smb_shm_header_p->consistent = True;
   
   return True;
}
   
static void smb_shm_solve_neighbors(struct SmbShmBlockDesc *head_p )
{
   struct SmbShmBlockDesc *next_p;
   
   /* Check if head_p and head_p->next are neighbors and if so join them */
   if ( head_p == EOList_Addr ) return ;
   if ( head_p->next == EOList_Off ) return ;
   
   next_p = (struct SmbShmBlockDesc *)smb_shm_offset2addr(head_p->next);
   if ( ( head_p + head_p->size + 1 ) == next_p)
   {
      head_p->size += next_p->size +1 ;	/* adapt size */
      head_p->next = next_p->next	  ; /* link out */
      
      smb_shm_header_p->statistics.cells_free += 1;
      smb_shm_header_p->statistics.cells_system -= 1;
   }
}



BOOL smb_shm_open( char *file_name, int size)
{
   int filesize;
   BOOL created_new = False;
   BOOL other_processes = True;
   int old_umask;
   
   DEBUG(2,("smb_shm_open : using shmem file %s to be of size %d\n",file_name,size));

   old_umask = umask(0);
   smb_shm_fd = open(file_name, O_RDWR | O_CREAT, 0666);
   umask(old_umask);
   if ( smb_shm_fd < 0 )
   {
      DEBUG(0,("ERROR smb_shm_open : open failed with code %d\n",errno));
      return False;
   }
   
   if (!smb_shm_lock())
   {
      DEBUG(0,("ERROR smb_shm_open : can't do smb_shm_lock\n"));
      return False;
   }
   
   if( (filesize = lseek(smb_shm_fd, 0, SEEK_END)) < 0)
   {
      DEBUG(0,("ERROR smb_shm_open : lseek failed with code %d\n",errno));
      smb_shm_unlock();
      close(smb_shm_fd);
      return False;
   }

   /* return the file offset to 0 to save on later seeks */
   lseek(smb_shm_fd,0,SEEK_SET);

   if (filesize == 0)
   {
      /* we just created a new one */
      created_new = True;
   }
   
   /* to find out if some other process is already mapping the file,
      we use a registration file containing the processids of the file mapping processes
      */

   /* construct processreg file name */
   strcpy(smb_shm_processreg_name, file_name);
   strcat(smb_shm_processreg_name, ".processes");

   if (! smb_shm_register_process(smb_shm_processreg_name, getpid(), &other_processes))
   {
      smb_shm_unlock();
      close(smb_shm_fd);
      return False;
   }

   if (created_new || !other_processes)
   {
      /* we just created a new one, or are the first opener, lets set it size */
      if( ftruncate(smb_shm_fd, size) <0)
      {
         DEBUG(0,("ERROR smb_shm_open : ftruncate failed with code %d\n",errno));
	 smb_shm_unregister_process(smb_shm_processreg_name, getpid());
	 smb_shm_unlock();
	 close(smb_shm_fd);
	 return False;
      }

      /* paranoia */
      lseek(smb_shm_fd,0,SEEK_SET);

      filesize = size;
   }
   
   if (size != filesize )
   {
      /* the existing file has a different size and we are not the first opener.
	 Since another process is still using it, we will use the file size */
      DEBUG(0,("WARNING smb_shm_open : filesize (%d) != expected size (%d), using filesize\n",filesize,size));
      size = filesize;
   }
   
   smb_shm_header_p = (struct SmbShmHeader *)mmap( NULL, size, PROT_READ | PROT_WRITE, MAP_FILE | MAP_SHARED, smb_shm_fd, 0);
   /* WARNING, smb_shm_header_p can be different for different processes mapping the same file ! */
   if (smb_shm_header_p  == (struct SmbShmHeader *)(-1))
   {
      DEBUG(0,("ERROR smb_shm_open : mmap failed with code %d\n",errno));
      smb_shm_unregister_process(smb_shm_processreg_name, getpid());
      smb_shm_unlock();
      close(smb_shm_fd);
      return False;
   }      
   
      
   if (created_new || !other_processes)
   {
      smb_shm_initialize(size);
   }
   else if (!smb_shm_validate_header(size) )
   {
      /* existing file is corrupt, samba admin should remove it by hand */
      DEBUG(0,("ERROR smb_shm_open : corrupt shared mem file, remove it manually\n"));
      munmap((caddr_t)smb_shm_header_p, size);
      smb_shm_unregister_process(smb_shm_processreg_name, getpid());
      smb_shm_unlock();
      close(smb_shm_fd);
      return False;
   }
   
   smb_shm_unlock();
   return True;
      
}


BOOL smb_shm_close( void )
{
   
   DEBUG(2,("smb_shm_close\n"));
   if(smb_shm_times_locked > 0)
      DEBUG(0,("WARNING smb_shm_close : shmem was still locked %d times\n",smb_shm_times_locked));;
   if ( munmap((caddr_t)smb_shm_header_p, smb_shm_header_p->total_size) < 0)
   {
      DEBUG(0,("ERROR smb_shm_close : munmap failed with code %d\n",errno));
   }

   smb_shm_lock();
   smb_shm_unregister_process(smb_shm_processreg_name, getpid());
   smb_shm_unlock();
   
   close(smb_shm_fd);
   
   smb_shm_fd = -1;
   smb_shm_processreg_name[0] = '\0';

   smb_shm_header_p = (struct SmbShmHeader *)0;
   smb_shm_times_locked = 0;
   
   return True;
}

smb_shm_offset_t smb_shm_alloc(int size)
{
   unsigned num_cells ;
   struct SmbShmBlockDesc *scanner_p;
   struct SmbShmBlockDesc *prev_p;
   struct SmbShmBlockDesc *new_p;
   smb_shm_offset_t result_offset;
   
   
   if( !smb_shm_header_p )
   {
      /* not mapped yet */
      DEBUG(0,("ERROR smb_shm_alloc : shmem not mapped\n"));
      return NULL_OFFSET;
   }
   
   if( !smb_shm_header_p->consistent)
   {
      DEBUG(0,("ERROR smb_shm_alloc : shmem not consistent\n"));
      return NULL_OFFSET;
   }
   
   
   /* calculate	the number of cells */
   num_cells = (size + CellSize -1) / CellSize;

   /* set start	of scan */
   prev_p = (struct SmbShmBlockDesc *)smb_shm_offset2addr(smb_shm_header_p->first_free_off);
   scanner_p =	prev_p ;
   
   /* scan the free list to find a matching free space */
   while ( ( scanner_p != EOList_Addr ) && ( scanner_p->size < num_cells ) )
   {
      prev_p = scanner_p;
      scanner_p = (struct SmbShmBlockDesc *)smb_shm_offset2addr(scanner_p->next);
   }
   
   /* at this point scanner point to a block header or to the end of the list */
   if ( scanner_p == EOList_Addr )	
   {
      DEBUG(0,("ERROR smb_shm_alloc : alloc of %d bytes failed, no free space found\n",size));
      return (NULL_OFFSET);
   }
   
   /* going to modify shared mem */
   smb_shm_header_p->consistent = False;
   
   /* if we found a good one : scanner == the good one */
   if ( scanner_p->size <= num_cells + 2 )
   {
      /* there is no use in making a new one, it will be too small anyway 
      *	 we will link out scanner
      */
      if ( prev_p == scanner_p )
      {
	 smb_shm_header_p->first_free_off = scanner_p->next ;
      }
      else
      {
	 prev_p->next = scanner_p->next ;
      }
      smb_shm_header_p->statistics.cells_free -= scanner_p->size;
      smb_shm_header_p->statistics.cells_used += scanner_p->size;
   }
   else
   {
      /* Make a new one */
      new_p = scanner_p + 1 + num_cells;
      new_p->size = scanner_p->size - num_cells - 1;
      new_p->next = scanner_p->next;
      scanner_p->size = num_cells;
      scanner_p->next = smb_shm_addr2offset(new_p);
      
      if ( prev_p	!= scanner_p )
      {
	 prev_p->next	   = smb_shm_addr2offset(new_p)  ;
      }
      else
      {
	 smb_shm_header_p->first_free_off = smb_shm_addr2offset(new_p)  ;
      }
      smb_shm_header_p->statistics.cells_free -= num_cells+1;
      smb_shm_header_p->statistics.cells_used += num_cells;
      smb_shm_header_p->statistics.cells_system += 1;
   }

   result_offset = smb_shm_addr2offset( &(scanner_p[1]) );
   scanner_p->next =	SMB_SHM_NOT_FREE_OFF ;

   /* end modification of shared mem */
   smb_shm_header_p->consistent = True;

   DEBUG(2,("smb_shm_alloc : request for %d bytes, allocated %d bytes at offset %d\n",size,scanner_p->size*CellSize,result_offset ));

   return ( result_offset );
}   



BOOL smb_shm_free(smb_shm_offset_t offset)
{
   struct SmbShmBlockDesc *header_p  ; /*	pointer	to header of block to free */
   struct SmbShmBlockDesc *scanner_p ; /*	used to	scan the list			   */
   struct SmbShmBlockDesc *prev_p	   ; /*	holds previous in the list		   */
   
   if( !smb_shm_header_p )
   {
      /* not mapped yet */
      DEBUG(0,("ERROR smb_shm_free : shmem not mapped\n"));
      return False;
   }
   
   if( !smb_shm_header_p->consistent)
   {
      DEBUG(0,("ERROR smb_shm_free : shmem not consistent\n"));
      return False;
   }
   
   header_p = (	(struct SmbShmBlockDesc *)smb_shm_offset2addr(offset) - 1); /* make pointer to header of block */

   if (header_p->next != SMB_SHM_NOT_FREE_OFF)
   {
      DEBUG(0,("ERROR smb_shm_free : bad offset (%d)\n",offset));
      return False;
   }
   
   /* find a place in the free_list to put the header in */
   
   /* set scanner and previous pointer to start of list */
   prev_p = (struct SmbShmBlockDesc *)smb_shm_offset2addr(smb_shm_header_p->first_free_off);
   scanner_p = prev_p ;
   
   while ( ( scanner_p != EOList_Addr) && (scanner_p < header_p) ) /* while we didn't scan past its position */
   {
      prev_p = scanner_p ;
      scanner_p = (struct SmbShmBlockDesc *)smb_shm_offset2addr(scanner_p->next);
   }
   
   smb_shm_header_p->consistent = False;
   
   DEBUG(2,("smb_shm_free : freeing %d bytes at offset %d\n",header_p->size*CellSize,offset));

   if ( scanner_p == prev_p )
   {
      smb_shm_header_p->statistics.cells_free += header_p->size;
      smb_shm_header_p->statistics.cells_used -= header_p->size;

      /* we must free it at the beginning of the list */
      smb_shm_header_p->first_free_off = smb_shm_addr2offset(header_p);						 /*	set	the free_list_pointer to this block_header */

      /* scanner is the one that was first in the list */
      header_p->next = smb_shm_addr2offset(scanner_p);
      smb_shm_solve_neighbors( header_p ); /* if neighbors then link them */
      
      smb_shm_header_p->consistent = True;
      return True;
   } 
   else
   {
      smb_shm_header_p->statistics.cells_free += header_p->size;
      smb_shm_header_p->statistics.cells_used -= header_p->size;

      prev_p->next = smb_shm_addr2offset(header_p);
      header_p->next = smb_shm_addr2offset(scanner_p);
      smb_shm_solve_neighbors(header_p) ;
      smb_shm_solve_neighbors(prev_p) ;

      smb_shm_header_p->consistent = True;
      return True;
   }
}

smb_shm_offset_t smb_shm_get_userdef_off(void)
{
   if (!smb_shm_header_p)
      return NULL_OFFSET;
   else
      return smb_shm_header_p->userdef_off;
}

BOOL smb_shm_set_userdef_off(smb_shm_offset_t userdef_off)
{
   if (!smb_shm_header_p)
      return False;
   else
      smb_shm_header_p->userdef_off = userdef_off;
   return True;
}

void * smb_shm_offset2addr(smb_shm_offset_t offset)
{
   if (offset == NULL_OFFSET )
      return (void *)(0);
   
   if (!smb_shm_header_p)
      return (void *)(0);
   
   return (void *)((char *)smb_shm_header_p + offset );
}

smb_shm_offset_t smb_shm_addr2offset(void *addr)
{
   if (!addr)
      return NULL_OFFSET;
   
   if (!smb_shm_header_p)
      return NULL_OFFSET;
   
   return (smb_shm_offset_t)((char *)addr - (char *)smb_shm_header_p);
}

BOOL smb_shm_lock(void)
{
   if (smb_shm_fd < 0)
   {
      DEBUG(0,("ERROR smb_shm_lock : bad smb_shm_fd (%d)\n",smb_shm_fd));
      return False;
   }
   
   smb_shm_times_locked++;
   
   if(smb_shm_times_locked > 1)
   {
      DEBUG(2,("smb_shm_lock : locked %d times\n",smb_shm_times_locked));
      return True;
   }
   
   if (lockf(smb_shm_fd, F_LOCK, 0) < 0)
   {
      DEBUG(0,("ERROR smb_shm_lock : lockf failed with code %d\n",errno));
      smb_shm_times_locked--;
      return False;
   }
   
   return True;
   
}



BOOL smb_shm_unlock(void)
{
   if (smb_shm_fd < 0)
   {
      DEBUG(0,("ERROR smb_shm_unlock : bad smb_shm_fd (%d)\n",smb_shm_fd));
      return False;
   }
   
   if(smb_shm_times_locked == 0)
   {
      DEBUG(0,("ERROR smb_shm_unlock : shmem not locked\n",smb_shm_fd));
      return False;
   }
   
   smb_shm_times_locked--;
   
   if(smb_shm_times_locked > 0)
   {
      DEBUG(2,("smb_shm_unlock : still locked %d times\n",smb_shm_times_locked));
      return True;
   }
   
   if (lockf(smb_shm_fd, F_ULOCK, 0) < 0)
   {
      DEBUG(0,("ERROR smb_shm_unlock : lockf failed with code %d\n",errno));
      smb_shm_times_locked++;
      return False;
   }
   
   return True;
   
}


BOOL smb_shm_get_usage(int *bytes_free,
		   int *bytes_used,
		   int *bytes_overhead)
{
   if( !smb_shm_header_p )
   {
      /* not mapped yet */
      DEBUG(0,("ERROR smb_shm_free : shmem not mapped\n"));
      return False;
   }
   *bytes_free = smb_shm_header_p->statistics.cells_free * CellSize;
   *bytes_used = smb_shm_header_p->statistics.cells_used * CellSize;
   *bytes_overhead = smb_shm_header_p->statistics.cells_system * CellSize + AlignedHeaderSize;
   
   return True;
}

#else /* FAST_SHARE_MODES */
 int shmem_dummy_procedure(void)
{return 0;}
#endif
