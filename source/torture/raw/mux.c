/* 
   Unix SMB/CIFS implementation.
   basic raw test suite for multiplexing
   Copyright (C) Andrew Tridgell 2003
   
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

#define BASEDIR "\\test_mux"

#define CHECK_STATUS(status, correct) do { \
	if (!NT_STATUS_EQUAL(status, correct)) { \
		printf("(%d) Incorrect status %s - should be %s\n", \
		       __LINE__, nt_errstr(status), nt_errstr(correct)); \
		ret = False; \
		goto done; \
	}} while (0)


/*
  test the delayed reply to a open that leads to a sharing violation
*/
static BOOL test_mux_open(struct cli_state *cli, TALLOC_CTX *mem_ctx)
{
	union smb_open io;
	NTSTATUS status;
	int fnum;
	BOOL ret = True;
	struct cli_request *req;

	printf("testing multiplexed open/open/close\n");

	/*
	  file open with no share access
	*/
	io.generic.level = RAW_OPEN_NTCREATEX;
	io.ntcreatex.in.root_fid = 0;
	io.ntcreatex.in.flags = 0;
	io.ntcreatex.in.access_mask = SEC_RIGHT_MAXIMUM_ALLOWED;
	io.ntcreatex.in.create_options = 0;
	io.ntcreatex.in.file_attr = FILE_ATTRIBUTE_NORMAL;
	io.ntcreatex.in.share_access = 0;
	io.ntcreatex.in.alloc_size = 0;
	io.ntcreatex.in.open_disposition = NTCREATEX_DISP_CREATE;
	io.ntcreatex.in.impersonation = NTCREATEX_IMPERSONATION_ANONYMOUS;
	io.ntcreatex.in.security_flags = 0;
	io.ntcreatex.in.fname = BASEDIR "\\open.dat";
	status = smb_raw_open(cli->tree, mem_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);
	fnum = io.ntcreatex.out.fnum;

	/* send an open that will conflict */
	io.ntcreatex.in.open_disposition = NTCREATEX_DISP_OPEN;
	status = smb_raw_open(cli->tree, mem_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_SHARING_VIOLATION);

	/*
	  same request, but async
	*/
	req = smb_raw_open_send(cli->tree, &io);
	
	/* and close the file */
	cli_close(cli->tree, fnum);

	/* see if the async open succeeded */
	status = smb_raw_open_recv(req, mem_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	cli_close(cli->tree, io.ntcreatex.out.fnum);

done:
	return ret;
}


/*
  test a write that hits a byte range lock and send the close after the write
*/
static BOOL test_mux_write(struct cli_state *cli, TALLOC_CTX *mem_ctx)
{
	union smb_write io;
	NTSTATUS status;
	int fnum;
	BOOL ret = True;
	struct cli_request *req;

	printf("testing multiplexed lock/write/close\n");

	fnum = cli_open(cli->tree, BASEDIR "\\write.dat", O_RDWR | O_CREAT, DENY_NONE);
	if (fnum == -1) {
		printf("open failed in mux_write - %s\n", cli_errstr(cli->tree));
		ret = False;
		goto done;
	}

	cli->session->pid = 1;

	/* lock a range */
	if (!cli_lock(cli->tree, fnum, 0, 4, 0, WRITE_LOCK)) {
		printf("lock failed in mux_write - %s\n", cli_errstr(cli->tree));
		ret = False;
		goto done;
	}

	cli->session->pid = 2;

	/* send an async write */
	io.generic.level = RAW_WRITE_WRITEX;
	io.writex.in.fnum = fnum;
	io.writex.in.offset = 0;
	io.writex.in.wmode = 0;
	io.writex.in.remaining = 0;
	io.writex.in.count = 4;
	io.writex.in.data = (void *)&fnum;	
	req = smb_raw_write_send(cli->tree, &io);

	/* unlock the range */
	cli->session->pid = 1;
	cli_unlock(cli->tree, fnum, 0, 4);

	/* and recv the async write reply */
	status = smb_raw_write_recv(req, &io);
	CHECK_STATUS(status, NT_STATUS_FILE_LOCK_CONFLICT);

	cli_close(cli->tree, fnum);

done:
	return ret;
}


/*
  test a lock that conflicts with an existing lock
*/
static BOOL test_mux_lock(struct cli_state *cli, TALLOC_CTX *mem_ctx)
{
	union smb_lock io;
	NTSTATUS status;
	int fnum;
	BOOL ret = True;
	struct cli_request *req;
	struct smb_lock_entry lock[1];

	printf("TESTING MULTIPLEXED LOCK/LOCK/UNLOCK\n");

	fnum = cli_open(cli->tree, BASEDIR "\\write.dat", O_RDWR | O_CREAT, DENY_NONE);
	if (fnum == -1) {
		printf("open failed in mux_write - %s\n", cli_errstr(cli->tree));
		ret = False;
		goto done;
	}

	printf("establishing a lock\n");
	io.lockx.level = RAW_LOCK_LOCKX;
	io.lockx.in.fnum = fnum;
	io.lockx.in.mode = 0;
	io.lockx.in.timeout = 0;
	io.lockx.in.lock_cnt = 1;
	io.lockx.in.ulock_cnt = 0;
	lock[0].pid = 1;
	lock[0].offset = 0;
	lock[0].count = 4;
	io.lockx.in.locks = &lock[0];

	status = smb_raw_lock(cli->tree, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("the second lock will conflict with the first\n");
	lock[0].pid = 2;
	io.lockx.in.timeout = 1000;
	status = smb_raw_lock(cli->tree, &io);
	CHECK_STATUS(status, NT_STATUS_FILE_LOCK_CONFLICT);

	printf("this will too, but we'll unlock while waiting\n");
	req = smb_raw_lock_send(cli->tree, &io);

	printf("unlock the first range\n");
	lock[0].pid = 1;
	io.lockx.in.ulock_cnt = 1;
	io.lockx.in.lock_cnt = 0;
	io.lockx.in.timeout = 0;
	status = smb_raw_lock(cli->tree, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("recv the async reply\n");
	status = cli_request_simple_recv(req);
	CHECK_STATUS(status, NT_STATUS_OK);	

	printf("reopening with an exit\n");
	smb_raw_exit(cli->session);
	fnum = cli_open(cli->tree, BASEDIR "\\write.dat", O_RDWR | O_CREAT, DENY_NONE);

	printf("Now trying with a cancel\n");

	io.lockx.level = RAW_LOCK_LOCKX;
	io.lockx.in.fnum = fnum;
	io.lockx.in.mode = 0;
	io.lockx.in.timeout = 0;
	io.lockx.in.lock_cnt = 1;
	io.lockx.in.ulock_cnt = 0;
	lock[0].pid = 1;
	lock[0].offset = 0;
	lock[0].count = 4;
	io.lockx.in.locks = &lock[0];

	status = smb_raw_lock(cli->tree, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	lock[0].pid = 2;
	io.lockx.in.timeout = 1000;
	status = smb_raw_lock(cli->tree, &io);
	CHECK_STATUS(status, NT_STATUS_FILE_LOCK_CONFLICT);

	req = smb_raw_lock_send(cli->tree, &io);

	/* cancel the blocking lock */
	smb_raw_ntcancel(req);

	lock[0].pid = 1;
	io.lockx.in.ulock_cnt = 1;
	io.lockx.in.lock_cnt = 0;
	io.lockx.in.timeout = 0;
	status = smb_raw_lock(cli->tree, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = cli_request_simple_recv(req);
	CHECK_STATUS(status, NT_STATUS_FILE_LOCK_CONFLICT);	

	cli_close(cli->tree, fnum);

done:
	return ret;
}



/* 
   basic testing of multiplexing notify
*/
BOOL torture_raw_mux(int dummy)
{
	struct cli_state *cli;
	BOOL ret = True;
	TALLOC_CTX *mem_ctx;
		
	if (!torture_open_connection(&cli)) {
		return False;
	}

	mem_ctx = talloc_init("torture_raw_mux");

	/* cleanup */
	if (cli_deltree(cli->tree, BASEDIR) == -1) {
		printf("Failed to cleanup " BASEDIR "\n");
		ret = False;
		goto done;
	}


	if (!cli_mkdir(cli->tree, BASEDIR)) {
		printf("Failed to create %s\n", BASEDIR);
		ret = False;
		goto done;
	}

	if (!test_mux_open(cli, mem_ctx)) {
		ret = False;
	}

	if (!test_mux_write(cli, mem_ctx)) {
		ret = False;
	}

	if (!test_mux_lock(cli, mem_ctx)) {
		ret = False;
	}

done:
	smb_raw_exit(cli->session);
	cli_deltree(cli->tree, BASEDIR);
	torture_close_connection(cli);
	talloc_destroy(mem_ctx);
	return ret;
}
