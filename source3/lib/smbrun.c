/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   run a command as a specified user
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

/* need to move this from here!! need some sleep ... */
struct current_user current_user;

extern int DEBUGLEVEL;

/****************************************************************************
This is a utility function of smbrun(). It must be called only from
the child as it may leave the caller in a privilaged state.
****************************************************************************/
static BOOL setup_stdout_file(char *outfile,BOOL shared)
{  
  int fd;
  SMB_STRUCT_STAT st;
  mode_t mode = S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH;
  int flags = O_RDWR|O_CREAT|O_TRUNC|O_EXCL;

  close(1);

  if (shared) {
	  /* become root - unprivilaged users can't delete these files */
#ifdef HAVE_SETRESUID
	  setresgid(0,0,0);
	  setresuid(0,0,0);
#else      
	  setuid(0);
	  seteuid(0);
#endif
  }

  if(sys_stat(outfile, &st) == 0) {
    /* Check we're not deleting a device file. */ 
    if(st.st_mode & S_IFREG)
      unlink(outfile);
    else
      flags = O_RDWR;
  }
  /* now create the file */
  fd = open(outfile,flags,mode);

  if (fd == -1) return False;

  if (fd != 1) {
    if (dup2(fd,1) != 0) {
      DEBUG(2,("Failed to create stdout file descriptor\n"));
      close(fd);
      return False;
    }
    close(fd);
  }
  return True;
}


/****************************************************************************
run a command being careful about uid/gid handling and putting the output in
outfile (or discard it if outfile is NULL).

if shared is True then ensure the file will be writeable by all users
but created such that its owned by root. This overcomes a security hole.

if shared is not set then open the file with O_EXCL set
****************************************************************************/
int smbrun(char *cmd,char *outfile,BOOL shared)
{
	int fd,pid;
	int uid = current_user.uid;
	int gid = current_user.gid;

#ifndef HAVE_EXECL
	int ret;
	pstring syscmd;  
	char *path = lp_smbrun();
	
	/* in the old method we use system() to execute smbrun which then
	   executes the command (using system() again!). This involves lots
	   of shell launches and is very slow. It also suffers from a
	   potential security hole */
	if (!file_exist(path,NULL)) {
		DEBUG(0,("SMBRUN ERROR: Can't find %s. Installation problem?\n",path));
		return(1);
	}

	slprintf(syscmd,sizeof(syscmd)-1,"%s %d %d \"(%s 2>&1) > %s\"",
		 path,uid,gid,cmd,
		 outfile?outfile:"/dev/null");
	
	DEBUG(5,("smbrun - running %s ",syscmd));
	ret = system(syscmd);
	DEBUG(5,("gave %d\n",ret));
	return(ret);
#else
	/* in this newer method we will exec /bin/sh with the correct
	   arguments, after first setting stdout to point at the file */
	
	if ((pid=fork())) {
		int status=0;
		/* the parent just waits for the child to exit */
		if (sys_waitpid(pid,&status,0) != pid) {
			DEBUG(2,("waitpid(%d) : %s\n",pid,strerror(errno)));
			return -1;
		}
		return status;
	}
	
	
	/* we are in the child. we exec /bin/sh to do the work for us. we
	   don't directly exec the command we want because it may be a
	   pipeline or anything else the config file specifies */
	
	/* point our stdout at the file we want output to go into */
	if (outfile && !setup_stdout_file(outfile,shared)) {
		exit(80);
	}
	
	/* now completely lose our privilages. This is a fairly paranoid
	   way of doing it, but it does work on all systems that I know of */
#ifdef HAVE_SETRESUID
	setresgid(0,0,0);
	setresuid(0,0,0);
	setresgid(gid,gid,gid);
	setresuid(uid,uid,uid);      
#else      
	setuid(0);
	seteuid(0);
	setgid(gid);
	setegid(gid);
	setuid(uid);
	seteuid(uid);
#endif
	
	if (getuid() != uid || geteuid() != uid ||
	    getgid() != gid || getegid() != gid) {
		/* we failed to lose our privilages - do not execute
                   the command */
		exit(81); /* we can't print stuff at this stage,
			     instead use exit codes for debugging */
	}
	
	/* close all other file descriptors, leaving only 0, 1 and 2. 0 and
	   2 point to /dev/null from the startup code */
	for (fd=3;fd<256;fd++) close(fd);
	
	execl("/bin/sh","sh","-c",cmd,NULL);  
	
	/* not reached */
	exit(82);
#endif
	return 1;
}
