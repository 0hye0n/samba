/* 
   Unix SMB/Netbios implementation.
   Version 2.0
   SMB wrapper directory functions
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
#include "wrapper.h"

extern pstring smb_cwd;

static struct smbw_dir *smbw_dirs;

extern struct bitmap *smbw_file_bmap;
extern int DEBUGLEVEL;

extern int smbw_busy;

/***************************************************** 
map a fd to a smbw_dir structure
*******************************************************/
struct smbw_dir *smbw_dir(int fd)
{
	struct smbw_dir *dir;

	for (dir=smbw_dirs;dir;dir=dir->next) {
		if (dir->fd == fd) return dir;
	}
	return NULL;
}

/***************************************************** 
check if a DIR* is one of ours
*******************************************************/
int smbw_dirp(DIR *dirp)
{
	struct smbw_dir *d = (struct smbw_dir *)dirp;
	struct smbw_dir *dir;

	for (dir=smbw_dirs;dir;dir=dir->next) {
		if (dir == d) return 1;
	}
	return 0;
}

/***************************************************** 
free a smbw_dir structure and all entries
*******************************************************/
static void free_dir(struct smbw_dir *dir)
{
	if (dir->list) {
		free(dir->list);
	}
	if (dir->path) free(dir->path);
	ZERO_STRUCTP(dir);
	free(dir);
}


static struct smbw_dir *cur_dir;

/***************************************************** 
add a entry to a directory listing
*******************************************************/
static void smbw_dir_add(struct file_info *finfo)
{
	DEBUG(5,("%s\n", finfo->name));

	if (cur_dir->malloced == cur_dir->count) {
		cur_dir->list = (struct file_info *)Realloc(cur_dir->list, 
							    sizeof(cur_dir->list[0])*
							    (cur_dir->count+100));
		if (!cur_dir->list) {
			/* oops */
			return;
		}
		cur_dir->malloced += 100;
	}

	cur_dir->list[cur_dir->count] = *finfo;
	cur_dir->count++;
}

/***************************************************** 
add a entry to a directory listing
*******************************************************/
static void smbw_share_add(const char *share, uint32 type, const char *comment)
{
	struct file_info finfo;

	ZERO_STRUCT(finfo);

	pstrcpy(finfo.name, share);
	finfo.mode = aRONLY | aDIR;	

	smbw_dir_add(&finfo);
}


/***************************************************** 
add a server to a directory listing
*******************************************************/
static void smbw_server_add(const char *name, uint32 type, 
			    const char *comment)
{
	struct file_info finfo;

	ZERO_STRUCT(finfo);

	pstrcpy(finfo.name, name);
	finfo.mode = aRONLY | aDIR;	

	smbw_dir_add(&finfo);
}


/***************************************************** 
add a entry to a directory listing
*******************************************************/
static void smbw_printjob_add(struct print_job_info *job)
{
	struct file_info finfo;

	ZERO_STRUCT(finfo);

	pstrcpy(finfo.name, job->name);
	finfo.mode = aRONLY | aDIR;	
	finfo.mtime = job->t;
	finfo.atime = job->t;
	finfo.ctime = job->t;
	finfo.uid = nametouid(job->user);
	finfo.mode = aRONLY;
	finfo.size = job->size;

	smbw_dir_add(&finfo);
}


/***************************************************** 
open a directory on the server
*******************************************************/
int smbw_dir_open(const char *fname)
{
	fstring server, share;
	pstring path;
	struct smbw_server *srv=NULL;
	struct smbw_dir *dir=NULL;
	pstring mask;
	int fd;
	char *s, *p;

	DEBUG(4,("%s\n", __FUNCTION__));

	if (!fname) {
		errno = EINVAL;
		return -1;
	}

	smbw_init();

	/* work out what server they are after */
	s = smbw_parse_path(fname, server, share, path);

	DEBUG(4,("dir_open share=%s\n", share));

	/* get a connection to the server */
	srv = smbw_server(server, share);
	if (!srv) {
		/* smbw_server sets errno */
		goto failed;
	}

	dir = (struct smbw_dir *)malloc(sizeof(*dir));
	if (!dir) {
		errno = ENOMEM;
		goto failed;
	}

	ZERO_STRUCTP(dir);

	cur_dir = dir;

	slprintf(mask, sizeof(mask)-1, "%s\\*", path);
	string_sub(mask,"\\\\","\\");

	if ((p=strstr(srv->server_name,"#1D"))) {
		DEBUG(4,("doing NetServerEnum\n"));
		*p = 0;
		cli_NetServerEnum(&srv->cli, srv->server_name, SV_TYPE_ALL,
				  smbw_server_add);
		*p = '#';
	} else if (strcmp(srv->cli.dev,"IPC") == 0) {
		DEBUG(4,("doing NetShareEnum\n"));
		if (cli_RNetShareEnum(&srv->cli, smbw_share_add) < 0) {
			errno = smbw_errno(&srv->cli);
			goto failed;
		}
	} else if (strncmp(srv->cli.dev,"LPT",3) == 0) {
		if (cli_print_queue(&srv->cli, smbw_printjob_add) < 0) {
			errno = smbw_errno(&srv->cli);
			goto failed;
		}
	} else {
		if (cli_list(&srv->cli, mask, aHIDDEN|aSYSTEM|aDIR, 
			     smbw_dir_add) < 0) {
			errno = smbw_errno(&srv->cli);
			goto failed;
		}
	}

	cur_dir = NULL;
	
	fd = open(SMBW_DUMMY, O_WRONLY);
	if (fd == -1) {
		errno = EMFILE;
		goto failed;
	}

	if (bitmap_query(smbw_file_bmap, fd)) {
		DEBUG(0,("ERROR: fd used in smbw_dir_open\n"));
		errno = EIO;
		goto failed;
	}

	DLIST_ADD(smbw_dirs, dir);
	
	bitmap_set(smbw_file_bmap, fd);

	dir->fd = fd;
	dir->srv = srv;
	dir->path = strdup(s);

	DEBUG(4,("  -> %d\n", dir->count));

	return dir->fd;

 failed:
	if (dir) {
		free_dir(dir);
	}

	return -1;
}

/***************************************************** 
a wrapper for fstat() on a directory
*******************************************************/
int smbw_dir_fstat(int fd, struct stat *st)
{
	struct smbw_dir *dir;

	DEBUG(4,("%s\n", __FUNCTION__));

	dir = smbw_dir(fd);
	if (!dir) {
		errno = EBADF;
		return -1;
	}

	ZERO_STRUCTP(st);

	smbw_setup_stat(st, "", dir->count*sizeof(struct dirent), aDIR);

	st->st_dev = dir->srv->dev;

	return 0;
}

/***************************************************** 
close a directory handle
*******************************************************/
int smbw_dir_close(int fd)
{
	struct smbw_dir *dir;

	DEBUG(4,("%s\n", __FUNCTION__));

	dir = smbw_dir(fd);
	if (!dir) {
		DEBUG(4,("%s(%d)\n", __FUNCTION__, __LINE__));
		errno = EBADF;
		return -1;
	}

	bitmap_clear(smbw_file_bmap, dir->fd);
	close(dir->fd);
	
	DLIST_REMOVE(smbw_dirs, dir);

	free_dir(dir);

	return 0;
}


/***************************************************** 
a wrapper for getdents()
*******************************************************/
int smbw_getdents(unsigned int fd, struct dirent *dirp, int count)
{
	struct smbw_dir *dir;
	int n=0;

	DEBUG(4,("%s\n", __FUNCTION__));

	smbw_busy++;

	dir = smbw_dir(fd);
	if (!dir) {
		errno = EBADF;
		smbw_busy--;
		return -1;
	}
	
	while (count>=sizeof(*dirp) && (dir->offset < dir->count)) {
		dirp->d_off = (dir->offset+1)*sizeof(*dirp);
		dirp->d_reclen = sizeof(*dirp);
		safe_strcpy(&dirp->d_name[0], dir->list[dir->offset].name, 
			    sizeof(dirp->d_name)-1);
		dirp->d_ino = smbw_inode(dir->list[dir->offset].name);
		dir->offset++;
		count -= dirp->d_reclen;
		if (dir->offset == dir->count) {
			dirp->d_off = -1;
		}
		dirp++;
		n++;
	}

	smbw_busy--;
	return n*sizeof(*dirp);
}


/***************************************************** 
a wrapper for chdir()
*******************************************************/
int smbw_chdir(const char *name)
{
	struct smbw_server *srv;
	fstring server, share;
	pstring path;
	uint32 mode = aDIR;
	char *cwd;

	smbw_init();

	if (smbw_busy) return real_chdir(cwd);

	smbw_busy++;

	if (!name) {
		errno = EINVAL;
		goto failed;
	}

	DEBUG(4,("%s (%s)\n", __FUNCTION__, name));

	/* work out what server they are after */
	cwd = smbw_parse_path(name, server, share, path);

	if (strncmp(cwd,SMBW_PREFIX,strlen(SMBW_PREFIX))) {
		if (real_chdir(cwd) == 0) {
			DEBUG(4,("set SMBW_CWD to %s\n", cwd));
			pstrcpy(smb_cwd, cwd);
			if (setenv(SMBW_PWD_ENV, smb_cwd, 1)) {
				DEBUG(4,("setenv failed\n"));
			}
			goto success;
		}
		errno = ENOENT;
		goto failed;
	}

	/* get a connection to the server */
	srv = smbw_server(server, share);
	if (!srv) {
		/* smbw_server sets errno */
		goto failed;
	}

	if (strncmp(srv->cli.dev,"IPC",3) &&
	    strncmp(srv->cli.dev,"LPT",3) &&
	    !smbw_getatr(srv, path, 
			 &mode, NULL, NULL, NULL, NULL)) {
		errno = smbw_errno(&srv->cli);
		goto failed;
	}

	if (!(mode & aDIR)) {
		errno = ENOTDIR;
		goto failed;
	}

	DEBUG(4,("set SMBW_CWD2 to %s\n", cwd));
	pstrcpy(smb_cwd, cwd);
	if (setenv(SMBW_PWD_ENV, smb_cwd, 1)) {
		DEBUG(4,("setenv failed\n"));
	}

	/* we don't want the old directory to be busy */
	real_chdir("/");

 success:
	smbw_busy--;
	return 0;

 failed:
	smbw_busy--;
	return -1;
}


/***************************************************** 
a wrapper for lseek() on directories
*******************************************************/
off_t smbw_dir_lseek(int fd, off_t offset, int whence)
{
	struct smbw_dir *dir;
	off_t ret;

	DEBUG(4,("%s offset=%d whence=%d\n", __FUNCTION__, 
		 (int)offset, whence));

	dir = smbw_dir(fd);
	if (!dir) {
		errno = EBADF;
		return -1;
	}

	switch (whence) {
	case SEEK_SET:
		dir->offset = offset/sizeof(struct dirent);
		break;
	case SEEK_CUR:
		dir->offset += offset/sizeof(struct dirent);
		break;
	case SEEK_END:
		dir->offset = (dir->count * sizeof(struct dirent)) + offset;
		dir->offset /= sizeof(struct dirent);
		break;
	}

	ret = dir->offset * sizeof(struct dirent);

	DEBUG(4,("   -> %d\n", (int)ret));

	return ret;
}


/***************************************************** 
a wrapper for mkdir()
*******************************************************/
int smbw_mkdir(const char *fname, mode_t mode)
{
	struct smbw_server *srv;
	fstring server, share;
	pstring path;

	DEBUG(4,("%s (%s)\n", __FUNCTION__, fname));

	if (!fname) {
		errno = EINVAL;
		return -1;
	}

	smbw_init();

	smbw_busy++;

	/* work out what server they are after */
	smbw_parse_path(fname, server, share, path);

	/* get a connection to the server */
	srv = smbw_server(server, share);
	if (!srv) {
		/* smbw_server sets errno */
		goto failed;
	}

	if (!cli_mkdir(&srv->cli, path)) {
		errno = smbw_errno(&srv->cli);
		goto failed;
	}

	smbw_busy--;
	return 0;

 failed:
	smbw_busy--;
	return -1;
}

/***************************************************** 
a wrapper for rmdir()
*******************************************************/
int smbw_rmdir(const char *fname)
{
	struct smbw_server *srv;
	fstring server, share;
	pstring path;

	DEBUG(4,("%s (%s)\n", __FUNCTION__, fname));

	if (!fname) {
		errno = EINVAL;
		return -1;
	}

	smbw_init();

	smbw_busy++;

	/* work out what server they are after */
	smbw_parse_path(fname, server, share, path);

	/* get a connection to the server */
	srv = smbw_server(server, share);
	if (!srv) {
		/* smbw_server sets errno */
		goto failed;
	}

	if (!cli_rmdir(&srv->cli, path)) {
		errno = smbw_errno(&srv->cli);
		goto failed;
	}

	smbw_busy--;
	return 0;

 failed:
	smbw_busy--;
	return -1;
}


/***************************************************** 
a wrapper for getcwd()
*******************************************************/
char *smbw_getcwd(char *buf, size_t size)
{
	smbw_init();

	if (smbw_busy) {
		return __getcwd(buf, size);
	}

	smbw_busy++;

	if (!buf) {
		if (size <= 0) size = strlen(smb_cwd)+1;
		buf = (char *)malloc(size);
		if (!buf) {
			errno = ENOMEM;
			smbw_busy--;
			return NULL;
		}
	}

	if (strlen(smb_cwd) > size-1) {
		errno = ERANGE;
		smbw_busy--;
		return NULL;
	}

	safe_strcpy(buf, smb_cwd, size);

	smbw_busy--;
	return buf;
}

/***************************************************** 
a wrapper for fchdir()
*******************************************************/
int smbw_fchdir(unsigned int fd)
{
	struct smbw_dir *dir;

	DEBUG(4,("%s\n", __FUNCTION__));

	smbw_busy++;

	dir = smbw_dir(fd);
	if (!dir) {
		errno = EBADF;
		smbw_busy--;
		return -1;
	}	

	smbw_busy--;
	
	return chdir(dir->path);
}

/***************************************************** 
open a directory on the server
*******************************************************/
DIR *smbw_opendir(const char *fname)
{
	int fd;

	smbw_busy++;

	fd = smbw_dir_open(fname);

	if (fd == -1) {
		smbw_busy--;
		return NULL;
	}

	smbw_busy--;

	return (DIR *)smbw_dir(fd);
}

/***************************************************** 
read one entry from a directory
*******************************************************/
struct dirent *smbw_readdir(DIR *dirp)
{
	struct smbw_dir *d = (struct smbw_dir *)dirp;
	static struct dirent de;

	if (smbw_getdents(d->fd, &de, sizeof(struct dirent)) > 0) 
		return &de;

	return NULL;
}

/***************************************************** 
close a DIR*
*******************************************************/
int smbw_closedir(DIR *dirp)
{
	struct smbw_dir *d = (struct smbw_dir *)dirp;
	return smbw_close(d->fd);
}

/***************************************************** 
seek in a directory
*******************************************************/
void smbw_seekdir(DIR *dirp, off_t offset)
{
	struct smbw_dir *d = (struct smbw_dir *)dirp;
	smbw_dir_lseek(d->fd,offset, SEEK_SET);
}

/***************************************************** 
current loc in a directory
*******************************************************/
off_t smbw_telldir(DIR *dirp)
{
	struct smbw_dir *d = (struct smbw_dir *)dirp;
	return smbw_dir_lseek(d->fd,0,SEEK_CUR);
}
