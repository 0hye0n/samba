#define OLD_NTDOMAIN 1
/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   read/write to a files_struct
   Copyright (C) Andrew Tridgell 1992-1998
   
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

extern int DEBUGLEVEL;

static BOOL setup_write_cache(files_struct *, SMB_OFF_T);

/****************************************************************************
seek a file. Try to avoid the seek if possible
****************************************************************************/

SMB_OFF_T seek_file(files_struct *fsp,SMB_OFF_T pos)
{
  SMB_OFF_T offset = 0;
  SMB_OFF_T seek_ret;

  if (fsp->print_file && lp_postscript(fsp->conn->service))
    offset = 3;

  seek_ret = fsp->conn->vfs_ops.lseek(fsp,fsp->fd,pos+offset,SEEK_SET);

  /*
   * We want to maintain the fiction that we can seek
   * on a fifo for file system purposes. This allows 
   * people to set up UNIX fifo's that feed data to Windows
   * applications. JRA.
   */

  if((seek_ret == -1) && (errno == ESPIPE)) {
    seek_ret = pos+offset;
    errno = 0;
  }

  if((seek_ret == -1) || (seek_ret != pos+offset)) {
    DEBUG(0,("seek_file: sys_lseek failed. Error was %s\n", strerror(errno) ));
    fsp->pos = -1;
    return -1;
  }

  fsp->pos = seek_ret - offset;

  DEBUG(10,("seek_file: requested pos = %.0f, new pos = %.0f\n",
        (double)(pos+offset), (double)fsp->pos ));

  return(fsp->pos);
}

/****************************************************************************
 Read from write cache if we can.
****************************************************************************/


BOOL read_from_write_cache(files_struct *fsp,char *data,SMB_OFF_T pos,size_t n)
{
  write_cache *wcp = fsp->wcp;

  if(!wcp)
    return False;

  if(n > wcp->data_size || pos < wcp->offset || pos + n > wcp->offset + wcp->data_size)
    return False;

  memcpy(data, wcp->data + (pos - wcp->offset), n);

#ifdef WITH_PROFILE
  INC_PROFILE_COUNT(writecache_read_hits);
#endif

  return True;
}

/****************************************************************************
read from a file
****************************************************************************/

ssize_t read_file(files_struct *fsp,char *data,SMB_OFF_T pos,size_t n)
{
  ssize_t ret=0,readret;

  /* you can't read from print files */
  if (fsp->print_file) {
	  return -1;
  }

  /*
   * Serve from write cache if we can.
   */
  if(read_from_write_cache(fsp, data, pos, n))
    return n;

  flush_write_cache(fsp, READ_FLUSH);

  if (seek_file(fsp,pos) == -1) {
    DEBUG(3,("read_file: Failed to seek to %.0f\n",(double)pos));
    return(ret);
  }
  
  if (n > 0) {
    readret = fsp->conn->vfs_ops.read(fsp,fsp->fd,data,n);
    if (readret == -1)
      return -1;
    if (readret > 0) ret += readret;
  }

  return(ret);
}

/* how many write cache buffers have been allocated */
static unsigned int allocated_write_caches;

/****************************************************************************
 *Really* write to a file.
****************************************************************************/

static ssize_t real_write_file(files_struct *fsp,char *data,SMB_OFF_T pos, size_t n)
{
  if ((pos != -1) && (seek_file(fsp,pos) == -1))
    return -1;

  return vfs_write_data(fsp,data,n);
}

/****************************************************************************
write to a file
****************************************************************************/

ssize_t write_file(files_struct *fsp, char *data, SMB_OFF_T pos, size_t n)
{
  write_cache *wcp = fsp->wcp;
  ssize_t total_written = 0;
  int write_path = -1; 

  if (fsp->print_file) {
	  return print_job_write(fsp->print_jobid, data, n);
  }

  if (!fsp->can_write) {
    errno = EPERM;
    return(0);
  }

  if (!fsp->modified) {
    SMB_STRUCT_STAT st;
    fsp->modified = True;

    if (fsp->conn->vfs_ops.fstat(fsp,fsp->fd,&st) == 0) {
      int dosmode = dos_mode(fsp->conn,fsp->fsp_name,&st);
      if (MAP_ARCHIVE(fsp->conn) && !IS_DOS_ARCHIVE(dosmode)) {	
        file_chmod(fsp->conn,fsp->fsp_name,dosmode | aARCH,&st);
      }

      /*
       * If this is the first write and we have an exclusive oplock then setup
       * the write cache.
       */

      if (EXCLUSIVE_OPLOCK_TYPE(fsp->oplock_type) && !wcp) {
        setup_write_cache(fsp, st.st_size);
        wcp = fsp->wcp;
      } 
    }  
  }

#ifdef WITH_PROFILE
  INC_PROFILE_COUNT(writecache_total_writes);
  if (!fsp->oplock_type) {
    INC_PROFILE_COUNT(writecache_non_oplock_writes);
  }
#endif

  /*
   * If this file is level II oplocked then we need
   * to grab the shared memory lock and inform all
   * other files with a level II lock that they need
   * to flush their read caches. We keep the lock over
   * the shared memory area whilst doing this.
   */

  if (LEVEL_II_OPLOCK_TYPE(fsp->oplock_type)) {
    share_mode_entry *share_list = NULL;
    pid_t pid = sys_getpid();
    int token = -1;
    int num_share_modes = 0;
    int i;

    if (lock_share_entry_fsp(fsp) == False) {
      DEBUG(0,("write_file: failed to lock share mode entry for file %s.\n", fsp->fsp_name ));
    }

    num_share_modes = get_share_modes(fsp->conn, fsp->dev, fsp->inode, &share_list);

    for(i = 0; i < num_share_modes; i++) {
      share_mode_entry *share_entry = &share_list[i];

      /*
       * As there could have been multiple writes waiting at the lock_share_entry
       * gate we may not be the first to enter. Hence the state of the op_types
       * in the share mode entries may be partly NO_OPLOCK and partly LEVEL_II
       * oplock. It will do no harm to re-send break messages to those smbd's
       * that are still waiting their turn to remove their LEVEL_II state, and
       * also no harm to ignore existing NO_OPLOCK states. JRA.
       */

      if (share_entry->op_type == NO_OPLOCK)
        continue;

      /* Paranoia .... */
      if (EXCLUSIVE_OPLOCK_TYPE(share_entry->op_type)) {
        DEBUG(0,("write_file: PANIC. share mode entry %d is an exlusive oplock !\n", i ));
        unlock_share_entry(fsp->conn, fsp->dev, fsp->inode);
        abort();
      }

      /*
       * Check if this is a file we have open (including the
       * file we've been called to do write_file on. If so
       * then break it directly without releasing the lock.
       */

      if (pid == share_entry->pid) {
        files_struct *new_fsp = file_find_dit(fsp->dev, fsp->inode, &share_entry->time);

        /* Paranoia check... */
        if(new_fsp == NULL) {
          DEBUG(0,("write_file: PANIC. share mode entry %d is not a local file !\n", i ));
          unlock_share_entry(fsp->conn, fsp->dev, fsp->inode);
          abort();
        }
        oplock_break_level2(new_fsp, True, token);

      } else {

        /*
         * This is a remote file and so we send an asynchronous
         * message.
         */

        request_oplock_break(share_entry, fsp->dev, fsp->inode);
      }
    }
 
    free((char *)share_list);
    unlock_share_entry_fsp(fsp);
  }

  /* Paranoia check... */
  if (LEVEL_II_OPLOCK_TYPE(fsp->oplock_type)) {
    DEBUG(0,("write_file: PANIC. File %s still has a level II oplock.\n", fsp->fsp_name));
    abort();
  }

#ifdef WITH_PROFILE
  if (profile_p && profile_p->writecache_total_writes % 500 == 0) {
    DEBUG(3,("WRITECACHE: initwrites=%u abutted=%u total=%u \
nonop=%u allocated=%u active=%u direct=%u perfect=%u readhits=%u\n",
	profile_p->writecache_init_writes,
	profile_p->writecache_abutted_writes,
	profile_p->writecache_total_writes,
	profile_p->writecache_non_oplock_writes,
	profile_p->writecache_allocated_write_caches,
	profile_p->writecache_num_write_caches,
	profile_p->writecache_direct_writes,
	profile_p->writecache_num_perfect_writes,
	profile_p->writecache_read_hits ));

    DEBUG(3,("WRITECACHE: Flushes SEEK=%d, READ=%d, WRITE=%d, READRAW=%d, OPLOCK=%d, CLOSE=%d, SYNC=%d\n",
	profile_p->writecache_flushed_writes[SEEK_FLUSH],
	profile_p->writecache_flushed_writes[READ_FLUSH],
	profile_p->writecache_flushed_writes[WRITE_FLUSH],
	profile_p->writecache_flushed_writes[READRAW_FLUSH],
	profile_p->writecache_flushed_writes[OPLOCK_RELEASE_FLUSH],
	profile_p->writecache_flushed_writes[CLOSE_FLUSH],
	profile_p->writecache_flushed_writes[SYNC_FLUSH] ));
  }
#endif

  if(!wcp) {
#ifdef WITH_PROFILE
    INC_PROFILE_COUNT(writecache_direct_writes);
#endif
    return real_write_file(fsp, data, pos, n);
  }

  DEBUG(9,("write_file(fd=%d pos=%d size=%d) wofs=%d wsize=%d\n",
	   fsp->fd, (int)pos, (int)n, (int)wcp->offset, (int)wcp->data_size));

  /* 
   * If we have active cache and it isn't contiguous then we flush.
   * NOTE: There is a small problem with running out of disk ....
   */

  if (wcp->data_size) {

    BOOL cache_flush_needed = False;

    if ((pos >= wcp->offset) && (pos <= wcp->offset + wcp->data_size)) {
      
      /*
       * Start of write overlaps or abutts the existing data.
       */

      size_t data_used = MIN((wcp->alloc_size - (pos - wcp->offset)), n);

      memcpy(wcp->data + (pos - wcp->offset), data, data_used);

      /*
       * Update the current buffer size with the new data.
       */

      if(pos + data_used > wcp->offset + wcp->data_size)
        wcp->data_size = pos + data_used - wcp->offset;

      /*
       * If we used all the data then
       * return here.
       */

      if(n == data_used)
        return n;
      else
        cache_flush_needed = True;

      /*
       * Move the start of data forward by the amount used,
       * cut down the amount left by the same amount.
       */

      data += data_used;
      pos += data_used;
      n -= data_used;

#ifdef WITH_PROFILE
      INC_PROFILE_COUNT(writecache_abutted_writes);
#endif
      total_written = data_used;

      write_path = 1;

    } else if ((pos < wcp->offset) && (pos + n > wcp->offset) && 
               (pos + n <= wcp->offset + wcp->alloc_size)) {

      /*
       * End of write overlaps the existing data.
       */

      size_t data_used = pos + n - wcp->offset;

      memcpy(wcp->data, data + n - data_used, data_used);

      /*
       * Update the current buffer size with the new data.
       */

      if(pos + n > wcp->offset + wcp->data_size)
        wcp->data_size = pos + n - wcp->offset;

      /*
       * We don't need to move the start of data, but we
       * cut down the amount left by the amount used.
       */

      n -= data_used;

      /*
       * We cannot have used all the data here.
       */

      cache_flush_needed = True;

#ifdef WITH_PROFILE
      INC_PROFILE_COUNT(writecache_abutted_writes);
#endif
      total_written = data_used;

      write_path = 2;

    } else if ( (pos >= wcp->file_size) && 
                (pos > wcp->offset + wcp->data_size) && 
                (pos < wcp->offset + wcp->alloc_size) ) {

      /*
       * Non-contiguous write part of which fits within
       * the cache buffer and is extending the file.
       */

      size_t data_used;

      if(pos + n <= wcp->offset + wcp->alloc_size)
        data_used = n;
      else
        data_used = wcp->offset + wcp->alloc_size - pos;

      /*
       * Fill in the non-continuous area with zeros.
       */

      memset(wcp->data + wcp->data_size, '\0',
             pos - (wcp->offset + wcp->data_size) );

      memcpy(wcp->data + (pos - wcp->offset), data, data_used);

      /*
       * Update the current buffer size with the new data.
       */

      if(pos + data_used > wcp->offset + wcp->data_size)
        wcp->data_size = pos + data_used - wcp->offset;

      /*
       * Update the known file length.
       */

      wcp->file_size = wcp->offset + wcp->data_size;

#if 0
      if (set_filelen(fsp->fd, wcp->file_size) == -1) {
        DEBUG(0,("write_file: error %s in setting file to length %.0f\n",
          strerror(errno), (double)wcp->file_size ));
        return -1;
      }
#endif

      /*
       * If we used all the data then
       * return here.
       */

      if(n == data_used)
        return n;
      else
        cache_flush_needed = True;

      /*
       * Move the start of data forward by the amount used,
       * cut down the amount left by the same amount.
       */

      data += data_used;
      pos += data_used;
      n -= data_used;

#ifdef WITH_PROFILE
      INC_PROFILE_COUNT(writecache_abutted_writes);
#endif
      total_written = data_used;

      write_path = 3;

    } else {

      /*
       * Write is bigger than buffer, or there is no overlap on the
       * low or high ends.
       */

      DEBUG(9,("write_file: non cacheable write : fd = %d, pos = %.0f, len = %u, current cache pos = %.0f \
len = %u\n",fsp->fd, (double)pos, (unsigned int)n, (double)wcp->offset, (unsigned int)wcp->data_size ));

      /*
       * Update the file size if needed.
       */

      if(pos + n > wcp->file_size)
        wcp->file_size = pos + n;

      /*
       * If write would fit in the cache, and is larger than
       * the data already in the cache, flush the cache and
       * preferentially copy the data new data into it. Otherwise
       * just write the data directly.
       */

      if ( n <= wcp->alloc_size && n > wcp->data_size) {
        cache_flush_needed = True;
      } else {
#ifdef WITH_PROFILE
	INC_PROFILE_COUNT(writecache_direct_writes);
#endif
        return real_write_file(fsp, data, pos, n);
      }

      write_path = 4;

    }

    if(wcp->data_size > wcp->file_size)
      wcp->file_size = wcp->data_size;

    if (cache_flush_needed) {
      DEBUG(3,("WRITE_FLUSH:%d: due to noncontinuous write: fd = %d, size = %.0f, pos = %.0f, \
n = %u, wcp->offset=%.0f, wcp->data_size=%u\n",
             write_path, fsp->fd, (double)wcp->file_size, (double)pos, (unsigned int)n,
             (double)wcp->offset, (unsigned int)wcp->data_size ));

      flush_write_cache(fsp, WRITE_FLUSH);
    }
  }

  /*
   * If the write request is bigger than the cache
   * size, write it all out.
   */

  if (n > wcp->alloc_size ) {
    if(real_write_file(fsp, data, pos, n) == -1)
      return -1;
#ifdef WITH_PROFILE
    INC_PROFILE_COUNT(writecache_direct_writes);
#endif
    return total_written + n;
  }

  /*
   * If there's any data left, cache it.
   */

  if (n) {
#ifdef WITH_PROFILE
    if (wcp->data_size) {
      INC_PROFILE_COUNT(writecache_abutted_writes);
    } else {
      INC_PROFILE_COUNT(writecache_init_writes);
    }
#endif
    memcpy(wcp->data+wcp->data_size, data, n);
    if (wcp->data_size == 0) {
      wcp->offset = pos;
#ifdef WITH_PROFILE
      INC_PROFILE_COUNT(writecache_num_write_caches);
#endif
    }
    wcp->data_size += n;
    DEBUG(9,("cache return %u\n", (unsigned int)n));
    total_written += n;
    return total_written; /* .... that's a write :) */
  }
  
  return total_written;
}

/****************************************************************************
 Delete the write cache structure.
****************************************************************************/

void delete_write_cache(files_struct *fsp)
{
  write_cache *wcp;

  if(!fsp)
    return;

  if(!(wcp = fsp->wcp))
    return;

#ifdef WITH_PROFILE
  DEC_PROFILE_COUNT(writecache_allocated_write_caches);
#endif
  allocated_write_caches--;

  SMB_ASSERT(wcp->data_size == 0);

  free(wcp->data);
  free(wcp);

  fsp->wcp = NULL;

  DEBUG(10,("delete_write_cache: File %s deleted write cache\n", fsp->fsp_name ));

}

/****************************************************************************
 Setup the write cache structure.
****************************************************************************/

static BOOL setup_write_cache(files_struct *fsp, SMB_OFF_T file_size)
{
  ssize_t alloc_size = lp_write_cache_size(SNUM(fsp->conn));
  write_cache *wcp;

  if (allocated_write_caches >= MAX_WRITE_CACHES) 
	return False;

  if(alloc_size == 0 || fsp->wcp)
    return False;

  if((wcp = (write_cache *)malloc(sizeof(write_cache))) == NULL) {
    DEBUG(0,("setup_write_cache: malloc fail.\n"));
    return False;
  }

  wcp->file_size = file_size;
  wcp->offset = 0;
  wcp->alloc_size = alloc_size;
  wcp->data_size = 0;
  if((wcp->data = malloc(wcp->alloc_size)) == NULL) {
    DEBUG(0,("setup_write_cache: malloc fail for buffer size %u.\n",
          (unsigned int)wcp->alloc_size ));
    free(wcp);
    return False;
  }

  fsp->wcp = wcp;
#ifdef WITH_PROFILE
  INC_PROFILE_COUNT(writecache_allocated_write_caches);
#endif
  allocated_write_caches++;

  DEBUG(10,("setup_write_cache: File %s allocated write cache size %u\n",
		fsp->fsp_name, wcp->alloc_size ));

  return True;
}

/****************************************************************************
 Cope with a size change.
****************************************************************************/

void set_filelen_write_cache(files_struct *fsp, SMB_OFF_T file_size)
{
  if(fsp->wcp) {
    flush_write_cache(fsp, SIZECHANGE_FLUSH);
    fsp->wcp->file_size = file_size;
  }
}

/*******************************************************************
 Flush a write cache struct to disk.
********************************************************************/

ssize_t flush_write_cache(files_struct *fsp, enum flush_reason_enum reason)
{
  write_cache *wcp = fsp->wcp;
  size_t data_size;

  if(!wcp || !wcp->data_size)
    return 0;

  data_size = wcp->data_size;
  wcp->data_size = 0;

#ifdef WITH_PROFILE
  DEC_PROFILE_COUNT(writecache_num_write_caches);
  INC_PROFILE_COUNT(writecache_flushed_writes[reason]);
#endif

  DEBUG(9,("flushing write cache: fd = %d, off=%.0f, size=%u\n",
	   fsp->fd, (double)wcp->offset, (unsigned int)data_size));

#ifdef WITH_PROFILE
  if(data_size == wcp->alloc_size)
    INC_PROFILE_COUNT(writecache_num_perfect_writes);
#endif

  return real_write_file(fsp, wcp->data, wcp->offset, data_size);
}

/*******************************************************************
sync a file
********************************************************************/

void sync_file(connection_struct *conn, files_struct *fsp)
{
    if(lp_strict_sync(SNUM(conn)) && fsp->fd != -1) {
      flush_write_cache(fsp, SYNC_FLUSH);
      conn->vfs_ops.fsync(fsp,fsp->fd);
    }
}
#undef OLD_NTDOMAIN
