/* 
   Unix SMB/CIFS implementation.

   a pass-thru NTVFS module to record a NBENCH load file

   Copyright (C) Andrew Tridgell 2004

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

/*
  "passthru" in this module refers to the next level of NTVFS being used
*/

#include "includes.h"

/* this is stored in ntvfs_private */
struct nbench_private {
	const struct ntvfs_ops *passthru_ops;
	void *passthru_private;
	const struct ntvfs_ops *nbench_ops;
	int log_fd;
};


/*
  log one request to the nbench log
*/
static void nbench_log(struct nbench_private *private, 
		       const char *format, ...)
{
	va_list ap;
	char *s = NULL;

	va_start(ap, format);
	vasprintf(&s, format, ap);
	va_end(ap);

	write(private->log_fd, s, strlen(s));
	free(s);
}


/*
  when we call the next stacked level of NTVFS module we need
  to give it its own private pointer, plus its own NTVFS operations structure.
  Then we need to restore both of these after the call, as the next level could
  modify either of these
*/
#define PASS_THRU(conn, op, args) do { \
	conn->ntvfs_private = private->passthru_private; \
	conn->ntvfs_ops = private->passthru_ops; \
\
	status = private->passthru_ops->op args; \
\
	private->passthru_private = conn->ntvfs_private; \
	private->passthru_ops = conn->ntvfs_ops; \
\
	conn->ntvfs_private = private; \
	conn->ntvfs_ops = private->nbench_ops; \
} while (0)

/*
  this pass through macro operates on request contexts, and disables
  async calls. 

  async calls are a pain for the nbench module as it makes pulling the
  status code and any result parameters much harder.
*/
#define PASS_THRU_REQ(req, op, args) do { \
	void *send_fn_saved = req->async.send_fn; \
	req->async.send_fn = NULL; \
	PASS_THRU(req->conn, op, args); \
	req->async.send_fn = send_fn_saved; \
} while (0)


/*
  connect to a share - used when a tree_connect operation comes in.
*/
static NTSTATUS nbench_connect(struct request_context *req, const char *sharename)
{
	struct nbench_private *private;
	const char *passthru;
	NTSTATUS status;
	char *logname = NULL;

	private = talloc_p(req->conn->mem_ctx, struct nbench_private);
	if (!private) {
		return NT_STATUS_NO_MEMORY;
	}

	asprintf(&logname, "/tmp/nbenchlog.%u", getpid());
	private->log_fd = open(logname, O_WRONLY|O_CREAT|O_APPEND, 0644);
	free(logname);

	if (private->log_fd == -1) {
		DEBUG(0,("Failed to open nbench log\n"));
		return NT_STATUS_UNSUCCESSFUL;
	}

	passthru = lp_parm_string(req->conn->service, "nbench", "passthru");

	private->passthru_private = NULL;
	private->nbench_ops = req->conn->ntvfs_ops;
	private->passthru_ops = ntvfs_backend_byname(passthru, NTVFS_DISK);

	if (!private->passthru_ops) {
		DEBUG(0,("Unable to connect to '%s' pass through backend\n", passthru));
		return NT_STATUS_UNSUCCESSFUL;
	}
	
	PASS_THRU(req->conn, connect, (req, sharename));

	return status;
}

/*
  disconnect from a share
*/
static NTSTATUS nbench_disconnect(struct tcon_context *conn)
{
	struct nbench_private *private = conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU(conn, disconnect, (conn));

	close(private->log_fd);

	return status;
}

/*
  delete a file - the dirtype specifies the file types to include in the search. 
  The name can contain CIFS wildcards, but rarely does (except with OS/2 clients)
*/
static NTSTATUS nbench_unlink(struct request_context *req, struct smb_unlink *unl)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, unlink, (req, unl));

	nbench_log(private, "Unlink \"%s\" 0x%x %s\n", 
		   unl->in.pattern, unl->in.attrib, 
		   get_nt_error_c_code(status));

	return status;
}

/*
  ioctl interface
*/
static NTSTATUS nbench_ioctl(struct request_context *req, union smb_ioctl *io)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, ioctl, (req, io));

	nbench_log(private, "Ioctl - NOT HANDLED\n");

	return status;
}

/*
  check if a directory exists
*/
static NTSTATUS nbench_chkpath(struct request_context *req, struct smb_chkpath *cp)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, chkpath, (req, cp));

	nbench_log(private, "Chkpath \"%s\" %s\n", 
		   cp->in.path, 
		   get_nt_error_c_code(status));

	return status;
}

/*
  return info on a pathname
*/
static NTSTATUS nbench_qpathinfo(struct request_context *req, union smb_fileinfo *info)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, qpathinfo, (req, info));

	nbench_log(private, "QUERY_PATH_INFORMATION \"%s\" %d %s\n", 
		   info->generic.in.fname, 
		   info->generic.level,
		   get_nt_error_c_code(status));

	return status;
}

/*
  query info on a open file
*/
static NTSTATUS nbench_qfileinfo(struct request_context *req, union smb_fileinfo *info)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, qfileinfo, (req, info));

	nbench_log(private, "QUERY_FILE_INFORMATION %d %d %s\n", 
		   info->generic.in.fnum, 
		   info->generic.level,
		   get_nt_error_c_code(status));

	return status;
}


/*
  set info on a pathname
*/
static NTSTATUS nbench_setpathinfo(struct request_context *req, union smb_setfileinfo *st)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, setpathinfo, (req, st));

	nbench_log(private, "SET_PATH_INFORMATION \"%s\" %d %s\n", 
		   st->generic.file.fname, 
		   st->generic.level,
		   get_nt_error_c_code(status));

	return status;
}

/*
  open a file
*/
static NTSTATUS nbench_open(struct request_context *req, union smb_open *io)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, open, (req, io));

	switch (io->generic.level) {
	case RAW_OPEN_NTCREATEX:
		nbench_log(private, "NTCreateX \"%s\" 0x%x 0x%x %d %s\n", 
			   io->ntcreatex.in.fname, 
			   io->ntcreatex.in.create_options, 
			   io->ntcreatex.in.open_disposition, 
			   io->ntcreatex.out.fnum,
			   get_nt_error_c_code(status));
		break;

	default:
		nbench_log(private, "Open-%d - NOT HANDLED\n",
			   io->generic.level);
		break;
	}

	return status;
}

/*
  create a directory
*/
static NTSTATUS nbench_mkdir(struct request_context *req, union smb_mkdir *md)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, mkdir, (req, md));

	nbench_log(private, "Mkdir - NOT HANDLED\n");

	return status;
}

/*
  remove a directory
*/
static NTSTATUS nbench_rmdir(struct request_context *req, struct smb_rmdir *rd)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, rmdir, (req, rd));

	nbench_log(private, "Rmdir \"%s\" %s\n", 
		   rd->in.path, 
		   get_nt_error_c_code(status));

	return status;
}

/*
  rename a set of files
*/
static NTSTATUS nbench_rename(struct request_context *req, union smb_rename *ren)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, rename, (req, ren));

	switch (ren->generic.level) {
	case RAW_RENAME_RENAME:
		nbench_log(private, "Rename \"%s\" \"%s\" %s\n", 
			   ren->rename.in.pattern1, 
			   ren->rename.in.pattern2, 
			   get_nt_error_c_code(status));
		break;

	default:
		nbench_log(private, "Rename-%d - NOT HANDLED\n",
			   ren->generic.level);
		break;
	}

	return status;
}

/*
  copy a set of files
*/
static NTSTATUS nbench_copy(struct request_context *req, struct smb_copy *cp)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, copy, (req, cp));

	nbench_log(private, "Copy - NOT HANDLED\n");

	return status;
}

/*
  read from a file
*/
static NTSTATUS nbench_read(struct request_context *req, union smb_read *rd)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, read, (req, rd));

	switch (rd->generic.level) {
	case RAW_READ_READX:
		nbench_log(private, "ReadX %d %d %d %d %s\n", 
			   rd->readx.in.fnum, 
			   (int)rd->readx.in.offset,
			   rd->readx.in.maxcnt,
			   rd->readx.out.nread,
			   get_nt_error_c_code(status));
		break;
	default:
		nbench_log(private, "Read-%d - NOT HANDLED\n",
			   rd->generic.level);
		break;
	}

	return status;
}

/*
  write to a file
*/
static NTSTATUS nbench_write(struct request_context *req, union smb_write *wr)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, write, (req, wr));

	switch (wr->generic.level) {
	case RAW_WRITE_WRITEX:
		nbench_log(private, "WriteX %d %d %d %d %s\n", 
			   wr->writex.in.fnum, 
			   (int)wr->writex.in.offset,
			   wr->writex.in.count,
			   wr->writex.out.nwritten,
			   get_nt_error_c_code(status));
		break;

	case RAW_WRITE_WRITE:
		nbench_log(private, "Write %d %d %d %d %s\n", 
			   wr->write.in.fnum, 
			   wr->write.in.offset,
			   wr->write.in.count,
			   wr->write.out.nwritten,
			   get_nt_error_c_code(status));
		break;

	default:
		nbench_log(private, "Write-%d - NOT HANDLED\n",
			   wr->generic.level);
		break;
	}

	return status;
}

/*
  seek in a file
*/
static NTSTATUS nbench_seek(struct request_context *req, struct smb_seek *io)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, seek, (req, io));

	nbench_log(private, "Seek - NOT HANDLED\n");

	return status;
}

/*
  flush a file
*/
static NTSTATUS nbench_flush(struct request_context *req, struct smb_flush *io)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, flush, (req, io));

	nbench_log(private, "Flush %d %s\n",
		   io->in.fnum,
		   get_nt_error_c_code(status));

	return status;
}

/*
  close a file
*/
static NTSTATUS nbench_close(struct request_context *req, union smb_close *io)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, close, (req, io));

	switch (io->generic.level) {
	case RAW_CLOSE_CLOSE:
		nbench_log(private, "Close %d %s\n",
			   io->close.in.fnum,
			   get_nt_error_c_code(status));
		break;

	default:
		nbench_log(private, "Close-%d - NOT HANDLED\n",
			   io->generic.level);
		break;
	}		

	return status;
}

/*
  exit - closing files
*/
static NTSTATUS nbench_exit(struct request_context *req)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, exit, (req));

	return status;
}

/*
  lock a byte range
*/
static NTSTATUS nbench_lock(struct request_context *req, union smb_lock *lck)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, lock, (req, lck));

	if (lck->generic.level == RAW_LOCK_LOCKX &&
	    lck->lockx.in.lock_cnt == 1 &&
	    lck->lockx.in.ulock_cnt == 0) {
		nbench_log(private, "LockX %d %d %d %s\n", 
			   lck->lockx.in.fnum,
			   (int)lck->lockx.in.locks[0].offset,
			   (int)lck->lockx.in.locks[0].count,
			   get_nt_error_c_code(status));
	} else if (lck->generic.level == RAW_LOCK_LOCKX &&
		   lck->lockx.in.ulock_cnt == 1) {
		nbench_log(private, "UnlockX %d %d %d %s\n", 
			   lck->lockx.in.fnum,
			   (int)lck->lockx.in.locks[0].offset,
			   (int)lck->lockx.in.locks[0].count,
			   get_nt_error_c_code(status));
	} else {
		nbench_log(private, "Lock-%d - NOT HANDLED\n", lck->generic.level);
	}

	return status;
}

/*
  set info on a open file
*/
static NTSTATUS nbench_setfileinfo(struct request_context *req, 
				 union smb_setfileinfo *info)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, setfileinfo, (req, info));

	nbench_log(private, "Setfileinfo %d %d %s\n", 
		   info->generic.file.fnum,
		   info->generic.level,
		   get_nt_error_c_code(status));

	return status;
}


/*
  return filesystem space info
*/
static NTSTATUS nbench_fsinfo(struct request_context *req, union smb_fsinfo *fs)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, fsinfo, (req, fs));

	nbench_log(private, "Fsinfo %d %s", 
		   fs->generic.level, 
		   get_nt_error_c_code(status));

	return status;
}

/*
  return print queue info
*/
static NTSTATUS nbench_lpq(struct request_context *req, union smb_lpq *lpq)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, lpq, (req, lpq));

	nbench_log(private, "Lpq-%d - NOT HANDLED\n", lpq->generic.level);

	return status;
}

/* 
   list files in a directory matching a wildcard pattern
*/
static NTSTATUS nbench_search_first(struct request_context *req, union smb_search_first *io, 
				  void *search_private, 
				  BOOL (*callback)(void *, union smb_search_data *))
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, search_first, (req, io, search_private, callback));

	switch (io->generic.level) {
	case RAW_SEARCH_BOTH_DIRECTORY_INFO:
		nbench_log(private, "Search \"%s\" %d %d %d %s\n", 
			   io->t2ffirst.in.pattern,
			   io->generic.level,
			   io->t2ffirst.in.max_count,
			   io->t2ffirst.out.count,
			   get_nt_error_c_code(status));
		break;
		
	default:
		nbench_log(private, "Search-%d - NOT HANDLED\n", io->generic.level);
		break;
	}

	return status;
}

/* continue a search */
static NTSTATUS nbench_search_next(struct request_context *req, union smb_search_next *io, 
				 void *search_private, 
				 BOOL (*callback)(void *, union smb_search_data *))
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, search_next, (req, io, search_private, callback));

	nbench_log(private, "Searchnext-%d - NOT HANDLED\n", io->generic.level);

	return status;
}

/* close a search */
static NTSTATUS nbench_search_close(struct request_context *req, union smb_search_close *io)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, search_close, (req, io));

	nbench_log(private, "Searchclose-%d - NOT HANDLED\n", io->generic.level);

	return status;
}

/* SMBtrans - not used on file shares */
static NTSTATUS nbench_trans(struct request_context *req, struct smb_trans2 *trans2)
{
	struct nbench_private *private = req->conn->ntvfs_private;
	NTSTATUS status;

	PASS_THRU_REQ(req, trans, (req,trans2));

	nbench_log(private, "Trans - NOT HANDLED\n");

	return status;
}

/*
  initialise the nbench backend, registering ourselves with the ntvfs subsystem
 */
NTSTATUS ntvfs_nbench_init(void)
{
	NTSTATUS ret;
	struct ntvfs_ops ops;

	ZERO_STRUCT(ops);

	/* fill in the name and type */
	ops.name = "nbench";
	ops.type = NTVFS_DISK;
	
	/* fill in all the operations */
	ops.connect = nbench_connect;
	ops.disconnect = nbench_disconnect;
	ops.unlink = nbench_unlink;
	ops.chkpath = nbench_chkpath;
	ops.qpathinfo = nbench_qpathinfo;
	ops.setpathinfo = nbench_setpathinfo;
	ops.open = nbench_open;
	ops.mkdir = nbench_mkdir;
	ops.rmdir = nbench_rmdir;
	ops.rename = nbench_rename;
	ops.copy = nbench_copy;
	ops.ioctl = nbench_ioctl;
	ops.read = nbench_read;
	ops.write = nbench_write;
	ops.seek = nbench_seek;
	ops.flush = nbench_flush;	
	ops.close = nbench_close;
	ops.exit = nbench_exit;
	ops.lock = nbench_lock;
	ops.setfileinfo = nbench_setfileinfo;
	ops.qfileinfo = nbench_qfileinfo;
	ops.fsinfo = nbench_fsinfo;
	ops.lpq = nbench_lpq;
	ops.search_first = nbench_search_first;
	ops.search_next = nbench_search_next;
	ops.search_close = nbench_search_close;
	ops.trans = nbench_trans;

	/* we don't register a trans2 handler as we want to be able to
	   log individual trans2 requests */
	ops.trans2 = NULL;

	/* register ourselves with the NTVFS subsystem. */
	ret = register_backend("ntvfs", &ops);

	if (!NT_STATUS_IS_OK(ret)) {
		DEBUG(0,("Failed to register nbench backend!\n"));
	}
	
	return ret;
}
