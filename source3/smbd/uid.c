/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   uid/user handling
   Copyright (C) Andrew Tridgell 1992-1995
   
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

extern connection_struct Connections[];

static int initial_uid;
static int initial_gid;

/* what user is current? */
struct current_user current_user;

extern pstring OriginalDir;

/****************************************************************************
initialise the uid routines
****************************************************************************/
void init_uid(void)
{
  initial_uid = current_user.uid = geteuid();
  initial_gid = current_user.gid = getegid();

  if (initial_gid != 0 && initial_uid == 0)
    {
#ifdef HPUX
      setresgid(0,0,0);
#else
      setgid(0);
      setegid(0);
#endif
    }

  initial_uid = geteuid();
  initial_gid = getegid();

  current_user.cnum = -1;

  ChDir(OriginalDir);
}


/****************************************************************************
  become the specified uid 
****************************************************************************/
static BOOL become_uid(int uid)
{
  if (initial_uid != 0)
    return(True);

  if (uid == -1 || uid == 65535) {
    DEBUG(1,("WARNING: using uid %d is a security risk\n",uid));    
  }

#ifdef AIX
  {
    /* AIX 3 stuff - inspired by a code fragment in wu-ftpd */
    priv_t priv;

    priv.pv_priv[0] = 0;
    priv.pv_priv[1] = 0;
    if (setpriv(PRIV_SET|PRIV_INHERITED|PRIV_EFFECTIVE|PRIV_BEQUEATH,
		&priv, sizeof(priv_t)) < 0 ||
	setuidx(ID_REAL|ID_EFFECTIVE, (uid_t)uid) < 0 ||
	seteuid((uid_t)uid) < 0) 
      DEBUG(1,("Can't set uid (AIX3)"));
  }
#endif

#ifdef USE_SETRES
  if (setresuid(-1,uid,-1) != 0)
#elif defined(USE_SETFS)
    if (setfsuid(uid) != 0)
#else
    if ((seteuid(uid) != 0) && 
	(setuid(uid) != 0))
#endif
      {
	DEBUG(0,("Couldn't set uid %d currently set to (%d,%d)\n",
		 uid,getuid(), geteuid()));
	if (uid > 32000)
	  DEBUG(0,("Looks like your OS doesn't like high uid values - try using a different account\n"));
	return(False);
      }

  if (((uid == -1) || (uid == 65535)) && geteuid() != uid) {
    DEBUG(0,("Invalid uid -1. perhaps you have a account with uid 65535?\n"));
    return(False);
  }

  current_user.uid = uid;

  return(True);
}


/****************************************************************************
  become the specified gid
****************************************************************************/
static BOOL become_gid(int gid)
{
  if (initial_uid != 0)
    return(True);

  if (gid == -1 || gid == 65535) {
    DEBUG(1,("WARNING: using gid %d is a security risk\n",gid));    
  }
  
#ifdef USE_SETRES 
  if (setresgid(-1,gid,-1) != 0)
#elif defined(USE_SETFS)
  if (setfsgid(gid) != 0)
#else
  if (setgid(gid) != 0)
#endif
      {
	DEBUG(0,("Couldn't set gid %d currently set to (%d,%d)\n",
		 gid,getgid(),getegid()));
	if (gid > 32000)
	  DEBUG(0,("Looks like your OS doesn't like high gid values - try using a different account\n"));
	return(False);
      }

  current_user.gid = gid;

  return(True);
}


/****************************************************************************
  become the specified uid and gid
****************************************************************************/
static BOOL become_id(int uid,int gid)
{
  return(become_gid(gid) && become_uid(uid));
}

/****************************************************************************
become the guest user
****************************************************************************/
BOOL become_guest(void)
{
  BOOL ret;
  static struct passwd *pass=NULL;

  if (initial_uid != 0) 
    return(True);

  if (!pass)
    pass = Get_Pwnam(lp_guestaccount(-1),True);
  if (!pass) return(False);

  ret = become_id(pass->pw_uid,pass->pw_gid);

  if (!ret)
    DEBUG(1,("Failed to become guest. Invalid guest account?\n"));

  current_user.cnum = -2;

  return(ret);
}

/*******************************************************************
check if a username is OK
********************************************************************/
static BOOL check_user_ok(int cnum,user_struct *vuser,int snum)
{
  int i;
  for (i=0;i<Connections[cnum].uid_cache.entries;i++)
    if (Connections[cnum].uid_cache.list[i] == vuser->uid) return(True);

  if (!user_ok(vuser->name,snum)) return(False);

  i = Connections[cnum].uid_cache.entries % UID_CACHE_SIZE;
  Connections[cnum].uid_cache.list[i] = vuser->uid;

  if (Connections[cnum].uid_cache.entries < UID_CACHE_SIZE)
    Connections[cnum].uid_cache.entries++;

  return(True);
}


/****************************************************************************
  become the user of a connection number
****************************************************************************/
BOOL become_user(int cnum, int uid)
{
  user_struct *vuser;
  int snum,gid;
  int id = uid;

  if (current_user.cnum == cnum && current_user.id == id) {
    DEBUG(4,("Skipping become_user - already user\n"));
    return(True);
  }

  unbecome_user();

  if (!OPEN_CNUM(cnum)) {
    DEBUG(2,("Connection %d not open\n",cnum));
    return(False);
  }

  snum = Connections[cnum].service;

  if (Connections[cnum].force_user || 
      lp_security() == SEC_SHARE ||
      !(vuser = get_valid_user_struct(uid)) ||
      !check_user_ok(cnum,vuser,snum)) {
    uid = Connections[cnum].uid;
    gid = Connections[cnum].gid;
    current_user.groups = Connections[cnum].groups;
    current_user.igroups = Connections[cnum].igroups;
    current_user.ngroups = Connections[cnum].ngroups;
  } else {
    if (!vuser) {
      DEBUG(2,("Invalid vuid used %d\n",uid));
      return(False);
    }
    uid = vuser->uid;
    if(!*lp_force_group(snum))
      gid = vuser->gid;
    else
      gid = Connections[cnum].gid;
    current_user.groups = vuser->user_groups;
    current_user.igroups = vuser->user_igroups;
    current_user.ngroups = vuser->user_ngroups;
  }

  if (initial_uid == 0)
    {
      if (!become_gid(gid)) return(False);

#ifndef NO_SETGROUPS      
      if (!IS_IPC(cnum)) {
	/* groups stuff added by ih/wreu */
	if (current_user.ngroups > 0)
	  if (setgroups(current_user.ngroups,current_user.groups)<0)
	    DEBUG(0,("setgroups call failed!\n"));
      }
#endif

      if (!Connections[cnum].admin_user && !become_uid(uid))
	return(False);
    }

  current_user.cnum = cnum;
  current_user.id = id;
  
  DEBUG(5,("become_user uid=(%d,%d) gid=(%d,%d)\n",
	   getuid(),geteuid(),getgid(),getegid()));
  
  return(True);
}

/****************************************************************************
  unbecome the user of a connection number
****************************************************************************/
BOOL unbecome_user(void )
{
  if (current_user.cnum == -1)
    return(False);

  ChDir(OriginalDir);

  if (initial_uid == 0)
    {
#ifdef USE_SETRES
      setresuid(-1,getuid(),-1);
      setresgid(-1,getgid(),-1);
#elif defined(USE_SETFS)
      setfsuid(initial_uid);
      setfsgid(initial_gid);
#else
      if (seteuid(initial_uid) != 0) 
	setuid(initial_uid);
      setgid(initial_gid);
#endif
    }
#ifdef NO_EID
  if (initial_uid == 0)
    DEBUG(2,("Running with no EID\n"));
  initial_uid = getuid();
  initial_gid = getgid();
#else
  if (geteuid() != initial_uid)
    {
      DEBUG(0,("Warning: You appear to have a trapdoor uid system\n"));
      initial_uid = geteuid();
    }
  if (getegid() != initial_gid)
    {
      DEBUG(0,("Warning: You appear to have a trapdoor gid system\n"));
      initial_gid = getegid();
    }
#endif

  current_user.uid = initial_uid;
  current_user.gid = initial_gid;
  
  if (ChDir(OriginalDir) != 0)
    DEBUG(0,("%s chdir(%s) failed in unbecome_user\n",
	     timestring(),OriginalDir));

  DEBUG(5,("unbecome_user now uid=(%d,%d) gid=(%d,%d)\n",
	getuid(),geteuid(),getgid(),getegid()));

  current_user.cnum = -1;

  return(True);
}


/****************************************************************************
This is a utility function of smbrun(). It must be called only from
the child as it may leave the caller in a privilaged state.
****************************************************************************/
static BOOL setup_stdout_file(char *outfile,BOOL shared)
{  
  int fd;
  mode_t mode = S_IWUSR|S_IRUSR|S_IRGRP|S_IROTH;

  close(1);

  if (shared) {
    /* become root - unprivilaged users can't delete these files */
#ifdef USE_SETRES
    setresgid(0,0,0);
    setresuid(0,0,0);
#else      
    setuid(0);
    seteuid(0);
#endif
  }

  /* now create the file with O_EXCL set */
  unlink(outfile);
  fd = open(outfile,O_RDWR|O_CREAT|O_TRUNC|O_EXCL,mode);

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

#if USE_SYSTEM
  int ret;
  pstring syscmd;  
  char *path = lp_smbrun();

  /* in the old method we use system() to execute smbrun which then
     executes the command (using system() again!). This involves lots
     of shell launches and is very slow. It also suffers from a
     potential security hole */
  if (!file_exist(path,NULL))
    {
      DEBUG(0,("SMBRUN ERROR: Can't find %s. Installation problem?\n",path));
      return(1);
    }

  sprintf(syscmd,"%s %d %d \"(%s 2>&1) > %s\"",
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
#ifdef USE_SETRES
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
    /* we failed to lose our privilages - do not execute the command */
    exit(81); /* we can't print stuff at this stage, instead use exit codes
		 for debugging */
  }

  /* close all other file descriptors, leaving only 0, 1 and 2. 0 and
     2 point to /dev/null from the startup code */
  for (fd=3;fd<256;fd++) close(fd);

  execl("/bin/sh","sh","-c",cmd,NULL);  

  /* not reached */
  exit(82);
#endif
}


