/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   pidfile handling
   Copyright (C) Andrew Tridgell 1998
   
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

#ifndef O_NONBLOCK
#define O_NONBLOCK
#endif


/* create a pid file in the lock directory. open it and leave it locked */
void pidfile_create(char *name)
{
	int     fd;
	char    buf[20];
	pstring pidFile;
	int pid;

	slprintf(pidFile, sizeof(pidFile)-1, "%s/%s.pid", lp_lockdir(), name);

	pid = pidfile_pid(name);
	if (pid > 0 && process_exists(pid)) {
		DEBUG(0,("ERROR: %s is already running\n", name));
		exit(1);
	}

	fd = open(pidFile, O_NONBLOCK | O_CREAT | O_WRONLY, 0644);
	if (fd < 0) {
		DEBUG(0,("ERROR: can't open %s: %s\n", pidFile, 
			 strerror(errno)));
		exit(1);
	}

	if (fcntl_lock(fd,F_SETLK,0,1,F_WRLCK)==False) {
		DEBUG(0,("ERROR: %s is already running\n", name));
		exit(1);
	}

	memset(buf, 0, sizeof(buf));
	slprintf(buf, sizeof(buf) - 1, "%u\n", (unsigned int) getpid());
	if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
		DEBUG(0,("ERROR: can't write to %s: %s\n", 
			 pidFile, strerror(errno)));
		exit(1);
	}
	/* Leave pid file open & locked for the duration... */
}


/* return the pid in a pidfile. return 0 if the process (or pidfile)
   does not exist */
int pidfile_pid(char *name)
{
	FILE *f;
	pstring pidFile;
	unsigned ret;

	slprintf(pidFile, sizeof(pidFile)-1, "%s/%s.pid", lp_lockdir(), name);

	f = fopen(pidFile, "r");
	if (!f) {
		return 0;
	}

	if (fscanf(f,"%u", &ret) != 1) {
		fclose(f);
		return 0;
	}
	fclose(f);
	
	if (!process_exists(ret)) return 0;

	return ret;
}

