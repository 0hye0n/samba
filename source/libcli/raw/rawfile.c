/* 
   Unix SMB/CIFS implementation.
   client file operations
   Copyright (C) Andrew Tridgell 1994-1998
   Copyright (C) Jeremy Allison 2001-2002
   Copyright (C) James Myers 2003
   
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

#define SETUP_REQUEST(cmd, wct, buflen) do { \
	req = cli_request_setup(tree, cmd, wct, buflen); \
	if (!req) return NULL; \
} while (0)


/****************************************************************************
 Rename a file - async interface
****************************************************************************/
struct cli_request *smb_raw_rename_send(struct cli_tree *tree,
					struct smb_rename *parms)
{
	struct cli_request *req; 

	SETUP_REQUEST(SMBmv, 1, 0);
	
	SSVAL(req->out.vwv, VWV(0), parms->in.attrib);
	
	cli_req_append_ascii4(req, parms->in.pattern1, STR_TERMINATE);
	cli_req_append_ascii4(req, parms->in.pattern2, STR_TERMINATE);

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}

	return req;
}

/****************************************************************************
 Rename a file - sync interface
****************************************************************************/
NTSTATUS smb_raw_rename(struct cli_tree *tree,
			struct smb_rename *parms)
{
	struct cli_request *req = smb_raw_rename_send(tree, parms);
	return cli_request_simple_recv(req);
}


/****************************************************************************
 Delete a file - async interface
****************************************************************************/
struct cli_request *smb_raw_unlink_send(struct cli_tree *tree,
					struct smb_unlink *parms)
{
	struct cli_request *req; 

	SETUP_REQUEST(SMBunlink, 1, 0);

	SSVAL(req->out.vwv, VWV(0), parms->in.attrib);
	cli_req_append_ascii4(req, parms->in.pattern, STR_TERMINATE);

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}
	return req;
}

/*
  delete a file - sync interface
*/
NTSTATUS smb_raw_unlink(struct cli_tree *tree,
			struct smb_unlink *parms)
{
	struct cli_request *req = smb_raw_unlink_send(tree, parms);
	return cli_request_simple_recv(req);
}


/****************************************************************************
 create a directory  using TRANSACT2_MKDIR - async interface
****************************************************************************/
static struct cli_request *smb_raw_t2mkdir_send(struct cli_tree *tree, 
						union smb_mkdir *parms)
{
	struct smb_trans2 t2;
	uint16 setup = TRANSACT2_MKDIR;
	TALLOC_CTX *mem_ctx;
	struct cli_request *req;
	uint16 data_total;

	mem_ctx = talloc_init("t2mkdir");

	data_total = ea_list_size(parms->t2mkdir.in.num_eas, parms->t2mkdir.in.eas);

	t2.in.max_param = 0;
	t2.in.max_data = 0;
	t2.in.max_setup = 0;
	t2.in.flags = 0;
	t2.in.timeout = 0;
	t2.in.setup_count = 1;
	t2.in.setup = &setup;
	t2.in.params = data_blob_talloc(mem_ctx, NULL, 4);
	t2.in.data = data_blob_talloc(mem_ctx, NULL, data_total);

	SIVAL(t2.in.params.data, VWV(0), 0); /* reserved */

	cli_blob_append_string(tree->session, mem_ctx, 
			       &t2.in.params, parms->t2mkdir.in.path, 0);

	ea_put_list(t2.in.data.data, parms->t2mkdir.in.num_eas, parms->t2mkdir.in.eas);

	req = smb_raw_trans2_send(tree, &t2);

	talloc_destroy(mem_ctx);

	return req;
}

/****************************************************************************
 Create a directory - async interface
****************************************************************************/
struct cli_request *smb_raw_mkdir_send(struct cli_tree *tree,
				       union smb_mkdir *parms)
{
	struct cli_request *req; 

	if (parms->generic.level == RAW_MKDIR_T2MKDIR) {
		return smb_raw_t2mkdir_send(tree, parms);
	}

	if (parms->generic.level != RAW_MKDIR_MKDIR) {
		return NULL;
	}

	SETUP_REQUEST(SMBmkdir, 0, 0);
	
	cli_req_append_ascii4(req, parms->mkdir.in.path, STR_TERMINATE);

	if (!cli_request_send(req)) {
		return NULL;
	}

	return req;
}

/****************************************************************************
 Create a directory - sync interface
****************************************************************************/
NTSTATUS smb_raw_mkdir(struct cli_tree *tree,
		       union smb_mkdir *parms)
{
	struct cli_request *req = smb_raw_mkdir_send(tree, parms);
	return cli_request_simple_recv(req);
}

/****************************************************************************
 Remove a directory - async interface
****************************************************************************/
struct cli_request *smb_raw_rmdir_send(struct cli_tree *tree,
				       struct smb_rmdir *parms)
{
	struct cli_request *req; 

	SETUP_REQUEST(SMBrmdir, 0, 0);
	
	cli_req_append_ascii4(req, parms->in.path, STR_TERMINATE);

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}

	return req;
}

/****************************************************************************
 Remove a directory - sync interface
****************************************************************************/
NTSTATUS smb_raw_rmdir(struct cli_tree *tree,
		       struct smb_rmdir *parms)
{
	struct cli_request *req = smb_raw_rmdir_send(tree, parms);
	return cli_request_simple_recv(req);
}


/****************************************************************************
 Open a file using TRANSACT2_OPEN - async send 
****************************************************************************/
static struct cli_request *smb_raw_t2open_send(struct cli_tree *tree, 
					       union smb_open *parms)
{
	struct smb_trans2 t2;
	uint16 setup = TRANSACT2_OPEN;
	TALLOC_CTX *mem_ctx = talloc_init("smb_raw_t2open");
	struct cli_request *req;
	uint16 list_size;

	list_size = ea_list_size(parms->t2open.in.num_eas, parms->t2open.in.eas);

	t2.in.max_param = 30;
	t2.in.max_data = 0;
	t2.in.max_setup = 0;
	t2.in.flags = 0;
	t2.in.timeout = 0;
	t2.in.setup_count = 1;
	t2.in.setup = &setup;
	t2.in.params = data_blob_talloc(mem_ctx, NULL, 28);
	t2.in.data = data_blob_talloc(mem_ctx, NULL, list_size);

	SSVAL(t2.in.params.data, VWV(0), parms->t2open.in.flags);
	SSVAL(t2.in.params.data, VWV(1), parms->t2open.in.open_mode);
	SSVAL(t2.in.params.data, VWV(2), 0); /* reserved */
	SSVAL(t2.in.params.data, VWV(3), parms->t2open.in.file_attrs);
	put_dos_date(t2.in.params.data, VWV(4), parms->t2open.in.write_time);
	SSVAL(t2.in.params.data, VWV(6), parms->t2open.in.open_func);
	SIVAL(t2.in.params.data, VWV(7), parms->t2open.in.size);
	SIVAL(t2.in.params.data, VWV(9), parms->t2open.in.timeout);
	SIVAL(t2.in.params.data, VWV(11), 0);
	SSVAL(t2.in.params.data, VWV(13), 0);

	cli_blob_append_string(tree->session, mem_ctx, 
			       &t2.in.params, parms->t2open.in.fname, 
			       STR_TERMINATE);

	ea_put_list(t2.in.data.data, parms->t2open.in.num_eas, parms->t2open.in.eas);

	req = smb_raw_trans2_send(tree, &t2);

	talloc_destroy(mem_ctx);

	return req;
}


/****************************************************************************
 Open a file using TRANSACT2_OPEN - async recv
****************************************************************************/
static NTSTATUS smb_raw_t2open_recv(struct cli_request *req, TALLOC_CTX *mem_ctx, union smb_open *parms)
{
	struct smb_trans2 t2;
	NTSTATUS status;

	status = smb_raw_trans2_recv(req, mem_ctx, &t2);
	if (!NT_STATUS_IS_OK(status)) return status;

	if (t2.out.params.length < 30) {
		return NT_STATUS_INFO_LENGTH_MISMATCH;
	}

	parms->t2open.out.fnum =        SVAL(t2.out.params.data, VWV(0));
	parms->t2open.out.attrib =      SVAL(t2.out.params.data, VWV(1));
	parms->t2open.out.write_time = make_unix_date3(t2.out.params.data + VWV(2));
	parms->t2open.out.size =        IVAL(t2.out.params.data, VWV(4));
	parms->t2open.out.access =      SVAL(t2.out.params.data, VWV(6));
	parms->t2open.out.ftype =       SVAL(t2.out.params.data, VWV(7));
	parms->t2open.out.devstate =    SVAL(t2.out.params.data, VWV(8));
	parms->t2open.out.action =      SVAL(t2.out.params.data, VWV(9));
	parms->t2open.out.unknown =     SVAL(t2.out.params.data, VWV(10));

	return NT_STATUS_OK;
}

/****************************************************************************
 Open a file - async send
****************************************************************************/
struct cli_request *smb_raw_open_send(struct cli_tree *tree, union smb_open *parms)
{
	int len;
	struct cli_request *req = NULL; 

	switch (parms->open.level) {
	case RAW_OPEN_T2OPEN:
		return smb_raw_t2open_send(tree, parms);

	case RAW_OPEN_OPEN:
		SETUP_REQUEST(SMBopen, 2, 0);
		SSVAL(req->out.vwv, VWV(0), parms->open.in.flags);
		SSVAL(req->out.vwv, VWV(1), parms->open.in.search_attrs);
		cli_req_append_ascii4(req, parms->open.in.fname, STR_TERMINATE);
		break;
		
	case RAW_OPEN_OPENX:
		SETUP_REQUEST(SMBopenX, 15, 0);
		SSVAL(req->out.vwv, VWV(0), SMB_CHAIN_NONE);
		SSVAL(req->out.vwv, VWV(1), 0);
		SSVAL(req->out.vwv, VWV(2), parms->openx.in.flags);
		SSVAL(req->out.vwv, VWV(3), parms->openx.in.open_mode);
		SSVAL(req->out.vwv, VWV(4), parms->openx.in.search_attrs);
		SSVAL(req->out.vwv, VWV(5), parms->openx.in.file_attrs);
		put_dos_date3(req->out.vwv, VWV(6), parms->openx.in.write_time);
		SSVAL(req->out.vwv, VWV(8), parms->openx.in.open_func);
		SIVAL(req->out.vwv, VWV(9), parms->openx.in.size);
		SIVAL(req->out.vwv, VWV(11),parms->openx.in.timeout);
		SIVAL(req->out.vwv, VWV(13),0); /* reserved */
		cli_req_append_string(req, parms->openx.in.fname, STR_TERMINATE);
		break;
		
	case RAW_OPEN_MKNEW:
		SETUP_REQUEST(SMBmknew, 3, 0);
		SSVAL(req->out.vwv, VWV(0), parms->mknew.in.attrib);
		put_dos_date3(req->out.vwv, VWV(1), parms->mknew.in.write_time);
		cli_req_append_ascii4(req, parms->mknew.in.fname, STR_TERMINATE);
		break;
		
	case RAW_OPEN_CTEMP:
		SETUP_REQUEST(SMBctemp, 3, 0);
		SSVAL(req->out.vwv, VWV(0), parms->ctemp.in.attrib);
		put_dos_date3(req->out.vwv, VWV(1), parms->ctemp.in.write_time);
		cli_req_append_ascii4(req, parms->ctemp.in.directory, STR_TERMINATE);
		break;
		
	case RAW_OPEN_SPLOPEN:
		SETUP_REQUEST(SMBsplopen, 2, 0);
		SSVAL(req->out.vwv, VWV(0), parms->splopen.in.setup_length);
		SSVAL(req->out.vwv, VWV(1), parms->splopen.in.mode);
		break;
		
	case RAW_OPEN_NTCREATEX:
		SETUP_REQUEST(SMBntcreateX, 24, 0);
		SSVAL(req->out.vwv, VWV(0),SMB_CHAIN_NONE);
		SSVAL(req->out.vwv, VWV(1),0);
		SCVAL(req->out.vwv, VWV(2),0); /* padding */
		SIVAL(req->out.vwv,  7, parms->ntcreatex.in.flags);
		SIVAL(req->out.vwv, 11, parms->ntcreatex.in.root_fid);
		SIVAL(req->out.vwv, 15, parms->ntcreatex.in.access_mask);
		SBVAL(req->out.vwv, 19, parms->ntcreatex.in.alloc_size);
		SIVAL(req->out.vwv, 27, parms->ntcreatex.in.file_attr);
		SIVAL(req->out.vwv, 31, parms->ntcreatex.in.share_access);
		SIVAL(req->out.vwv, 35, parms->ntcreatex.in.open_disposition);
		SIVAL(req->out.vwv, 39, parms->ntcreatex.in.create_options);
		SIVAL(req->out.vwv, 43, parms->ntcreatex.in.impersonation);
		SCVAL(req->out.vwv, 47, parms->ntcreatex.in.security_flags);
		
		cli_req_append_string_len(req, parms->ntcreatex.in.fname, STR_TERMINATE, &len);
		SSVAL(req->out.vwv, 5, len);
		break;
	}

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}

	return req;
}

/****************************************************************************
 Open a file - async recv
****************************************************************************/
NTSTATUS smb_raw_open_recv(struct cli_request *req, TALLOC_CTX *mem_ctx, union smb_open *parms)
{
	if (!cli_request_receive(req) ||
	    cli_request_is_error(req)) {
		goto failed;
	}

	switch (parms->open.level) {
	case RAW_OPEN_T2OPEN:
		return smb_raw_t2open_recv(req, mem_ctx, parms);

	case RAW_OPEN_OPEN:
		CLI_CHECK_WCT(req, 7);
		parms->open.out.fnum = SVAL(req->in.vwv, VWV(0));
		parms->open.out.attrib = SVAL(req->in.vwv, VWV(1));
		parms->open.out.write_time = make_unix_date3(req->in.vwv + VWV(2));
		parms->open.out.size = IVAL(req->in.vwv, VWV(4));
		parms->open.out.rmode = SVAL(req->in.vwv, VWV(6));
		break;

	case RAW_OPEN_OPENX:
		CLI_CHECK_MIN_WCT(req, 15);
		parms->openx.out.fnum = SVAL(req->in.vwv, VWV(2));
		parms->openx.out.attrib = SVAL(req->in.vwv, VWV(3));
		parms->openx.out.write_time = make_unix_date3(req->in.vwv + VWV(4));
		parms->openx.out.size = IVAL(req->in.vwv, VWV(6));
		parms->openx.out.access = SVAL(req->in.vwv, VWV(8));
		parms->openx.out.ftype = SVAL(req->in.vwv, VWV(9));
		parms->openx.out.devstate = SVAL(req->in.vwv, VWV(10));
		parms->openx.out.action = SVAL(req->in.vwv, VWV(11));
		parms->openx.out.unique_fid = IVAL(req->in.vwv, VWV(12));
		if (req->in.wct >= 19) {
			parms->openx.out.access_mask = IVAL(req->in.vwv, VWV(15));
			parms->openx.out.unknown =     IVAL(req->in.vwv, VWV(17));
		} else {
			parms->openx.out.access_mask = 0;
			parms->openx.out.unknown = 0;
		}
		break;

	case RAW_OPEN_MKNEW:
		CLI_CHECK_WCT(req, 1);
		parms->mknew.out.fnum = SVAL(req->in.vwv, VWV(0));
		break;

	case RAW_OPEN_CTEMP:
		CLI_CHECK_WCT(req, 1);
		parms->ctemp.out.fnum = SVAL(req->in.vwv, VWV(0));
		cli_req_pull_string(req, mem_ctx, &parms->ctemp.out.name, req->in.data, -1, STR_TERMINATE | STR_ASCII);
		break;

	case RAW_OPEN_SPLOPEN:
		CLI_CHECK_WCT(req, 1);
		parms->splopen.out.fnum = SVAL(req->in.vwv, VWV(0));
		break;

	case RAW_OPEN_NTCREATEX:
		CLI_CHECK_MIN_WCT(req, 34);
		parms->ntcreatex.out.oplock_level =              CVAL(req->in.vwv, 4);
		parms->ntcreatex.out.fnum =                      SVAL(req->in.vwv, 5);
		parms->ntcreatex.out.create_action =             IVAL(req->in.vwv, 7);
		parms->ntcreatex.out.create_time =   cli_pull_nttime(req->in.vwv, 11);
		parms->ntcreatex.out.access_time =   cli_pull_nttime(req->in.vwv, 19);
		parms->ntcreatex.out.write_time =    cli_pull_nttime(req->in.vwv, 27);
		parms->ntcreatex.out.change_time =   cli_pull_nttime(req->in.vwv, 35);
		parms->ntcreatex.out.attrib =                   IVAL(req->in.vwv, 43);
		parms->ntcreatex.out.alloc_size =               BVAL(req->in.vwv, 47);
		parms->ntcreatex.out.size =                     BVAL(req->in.vwv, 55);
		parms->ntcreatex.out.file_type =                SVAL(req->in.vwv, 63);
		parms->ntcreatex.out.ipc_state =                SVAL(req->in.vwv, 65);
		parms->ntcreatex.out.is_directory =             CVAL(req->in.vwv, 67);
		break;
	}

failed:
	return cli_request_destroy(req);
}


/****************************************************************************
 Open a file - sync interface
****************************************************************************/
NTSTATUS smb_raw_open(struct cli_tree *tree, TALLOC_CTX *mem_ctx, union smb_open *parms)
{
	struct cli_request *req = smb_raw_open_send(tree, parms);
	return smb_raw_open_recv(req, mem_ctx, parms);
}


/****************************************************************************
 Close a file - async send
****************************************************************************/
struct cli_request *smb_raw_close_send(struct cli_tree *tree, union smb_close *parms)
{
	struct cli_request *req; 

	switch (parms->generic.level) {
	case RAW_CLOSE_GENERIC:
		return NULL;

	case RAW_CLOSE_CLOSE:
		SETUP_REQUEST(SMBclose, 3, 0);
		SSVAL(req->out.vwv, VWV(0), parms->close.in.fnum);
		put_dos_date3(req->out.vwv, VWV(1), parms->close.in.write_time);
		break;

	case RAW_CLOSE_SPLCLOSE:
		SETUP_REQUEST(SMBsplclose, 3, 0);
		SSVAL(req->out.vwv, VWV(0), parms->splclose.in.fnum);
		SIVAL(req->out.vwv, VWV(1), 0); /* reserved */
		break;
	}

	if (!req) return NULL;

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}

	return req;
}


/****************************************************************************
 Close a file - sync interface
****************************************************************************/
NTSTATUS smb_raw_close(struct cli_tree *tree, union smb_close *parms)
{
	struct cli_request *req = smb_raw_close_send(tree, parms);
	return cli_request_simple_recv(req);
}


/****************************************************************************
 Locking calls - async interface
****************************************************************************/
struct cli_request *smb_raw_lock_send(struct cli_tree *tree, union smb_lock *parms)
{
	struct cli_request *req; 

	switch (parms->generic.level) {
	case RAW_LOCK_GENERIC:
		return NULL;

	case RAW_LOCK_LOCK:
		SETUP_REQUEST(SMBlock, 5, 0);
		SSVAL(req->out.vwv, VWV(0), parms->lock.in.fnum);
		SIVAL(req->out.vwv, VWV(1), parms->lock.in.count);
		SIVAL(req->out.vwv, VWV(3), parms->lock.in.offset);
		break;
		
	case RAW_LOCK_UNLOCK:
		SETUP_REQUEST(SMBunlock, 5, 0);
		SSVAL(req->out.vwv, VWV(0), parms->unlock.in.fnum);
		SIVAL(req->out.vwv, VWV(1), parms->unlock.in.count);
		SIVAL(req->out.vwv, VWV(3), parms->unlock.in.offset);
		break;
		
	case RAW_LOCK_LOCKX: {
		struct smb_lock_entry *lockp;
		uint_t lck_size = (parms->lockx.in.mode & LOCKING_ANDX_LARGE_FILES)? 20 : 10;
		uint_t lock_count = parms->lockx.in.ulock_cnt + parms->lockx.in.lock_cnt;
		int i;

		SETUP_REQUEST(SMBlockingX, 8, lck_size * lock_count);
		SSVAL(req->out.vwv, VWV(0), SMB_CHAIN_NONE);
		SSVAL(req->out.vwv, VWV(1), 0);
		SSVAL(req->out.vwv, VWV(2), parms->lockx.in.fnum);
		SSVAL(req->out.vwv, VWV(3), parms->lockx.in.mode);
		SIVAL(req->out.vwv, VWV(4), parms->lockx.in.timeout);
		SSVAL(req->out.vwv, VWV(6), parms->lockx.in.ulock_cnt);
		SSVAL(req->out.vwv, VWV(7), parms->lockx.in.lock_cnt);
		
		/* copy in all the locks */
		lockp = &parms->lockx.in.locks[0];
		for (i = 0; i < lock_count; i++) {
			char *p = req->out.data + lck_size * i;
			SSVAL(p, 0, lockp[i].pid);
			if (parms->lockx.in.mode & LOCKING_ANDX_LARGE_FILES) {
				SSVAL(p,  2, 0); /* reserved */
				SIVAL(p,  4, lockp[i].offset>>32);
				SIVAL(p,  8, lockp[i].offset);
				SIVAL(p, 12, lockp[i].count>>32);
				SIVAL(p, 16, lockp[i].count);
			} else {
				SIVAL(p, 2, lockp[i].offset);
				SIVAL(p, 6, lockp[i].count);
			}
		}	
	}
	}

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}

	return req;
}

/****************************************************************************
 Locking calls - sync interface
****************************************************************************/
NTSTATUS smb_raw_lock(struct cli_tree *tree, union smb_lock *parms)
{
	struct cli_request *req = smb_raw_lock_send(tree, parms);
	return cli_request_simple_recv(req);
}
	

/****************************************************************************
 Check for existence of a dir - async send
****************************************************************************/
struct cli_request *smb_raw_chkpath_send(struct cli_tree *tree, struct smb_chkpath *parms)
{
	struct cli_request *req; 

	SETUP_REQUEST(SMBchkpth, 0, 0);

	cli_req_append_ascii4(req, parms->in.path, STR_TERMINATE);

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}

	return req;
}

/****************************************************************************
 Check for existence of a dir - sync interface
****************************************************************************/
NTSTATUS smb_raw_chkpath(struct cli_tree *tree, struct smb_chkpath *parms)
{
	struct cli_request *req = smb_raw_chkpath_send(tree, parms);
	return cli_request_simple_recv(req);
}




/****************************************************************************
 flush a file - async send
 a flush to fnum 0xFFFF will flush all files
****************************************************************************/
struct cli_request *smb_raw_flush_send(struct cli_tree *tree, struct smb_flush *parms)
{
	struct cli_request *req; 

	SETUP_REQUEST(SMBflush, 1, 0);
	SSVAL(req->out.vwv, VWV(0), parms->in.fnum);

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}

	return req;
}


/****************************************************************************
 flush a file - sync interface
****************************************************************************/
NTSTATUS smb_raw_flush(struct cli_tree *tree, struct smb_flush *parms)
{
	struct cli_request *req = smb_raw_flush_send(tree, parms);
	return cli_request_simple_recv(req);
}


/****************************************************************************
 seek a file - async send
****************************************************************************/
struct cli_request *smb_raw_seek_send(struct cli_tree *tree,
				      struct smb_seek *parms)
{
	struct cli_request *req; 

	SETUP_REQUEST(SMBlseek, 4, 0);

	SSVAL(req->out.vwv, VWV(0), parms->in.fnum);
	SSVAL(req->out.vwv, VWV(1), parms->in.mode);
	SIVALS(req->out.vwv, VWV(2), parms->in.offset);

	if (!cli_request_send(req)) {
		cli_request_destroy(req);
		return NULL;
	}
	return req;
}

/****************************************************************************
 seek a file - async receive
****************************************************************************/
NTSTATUS smb_raw_seek_recv(struct cli_request *req,
				      struct smb_seek *parms)
{
	if (!cli_request_receive(req) ||
	    cli_request_is_error(req)) {
		return cli_request_destroy(req);
	}

	CLI_CHECK_WCT(req, 2);	
	parms->out.offset = IVAL(req->in.vwv, VWV(0));

failed:	
	return cli_request_destroy(req);
}

/*
  seek a file - sync interface
*/
NTSTATUS smb_raw_seek(struct cli_tree *tree,
		      struct smb_seek *parms)
{
	struct cli_request *req = smb_raw_seek_send(tree, parms);
	return smb_raw_seek_recv(req, parms);
}
