/* 
   Unix SMB/CIFS implementation.
   test suite for session setup operations
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

#define BASEDIR "\\rawcontext"

#define CHECK_STATUS(status, correct) do { \
	if (!NT_STATUS_EQUAL(status, correct)) { \
		printf("(%d) Incorrect status %s - should be %s\n", \
		       __LINE__, nt_errstr(status), nt_errstr(correct)); \
		ret = False; \
		goto done; \
	}} while (0)

#define CHECK_VALUE(v, correct) do { \
	if ((v) != (correct)) { \
		printf("(%d) Incorrect value %s=%d - should be %d\n", \
		       __LINE__, #v, v, correct); \
		ret = False; \
		goto done; \
	}} while (0)


/*
  test session ops
*/
static BOOL test_session(struct cli_state *cli, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	BOOL ret = True;
	char *username, *domain, *password;
	struct cli_session *session;
	struct cli_tree *tree;
	union smb_sesssetup setup;
	union smb_open io;
	union smb_write wr;
	union smb_close cl;
	int fnum;
	const char *fname = BASEDIR "\\test.txt";
	char c = 1;

	printf("TESTING SESSION HANDLING\n");

	if (cli_deltree(cli, BASEDIR) == -1 ||
	    !cli_mkdir(cli, BASEDIR)) {
		printf("Unable to setup %s - %s\n", BASEDIR, cli_errstr(cli));
		return False;
	}

	username = lp_parm_string(-1, "torture", "username");
	password = lp_parm_string(-1, "torture", "password");
	domain = lp_workgroup();

	printf("create a second security context on the same transport\n");
	session = cli_session_init(cli->transport);
	setup.generic.level = RAW_SESSSETUP_GENERIC;
	setup.generic.in.sesskey = cli->transport->negotiate.sesskey;
	setup.generic.in.capabilities = 0; /* ignored in secondary session setup */
	setup.generic.in.password = password;
	setup.generic.in.user = username;
	setup.generic.in.domain = domain;

	status = smb_raw_session_setup(session, mem_ctx, &setup);
	CHECK_STATUS(status, NT_STATUS_OK);

	session->vuid = setup.generic.out.vuid;

	printf("use the same tree as the existing connection\n");
	tree = cli_tree_init(session);
	tree->tid = cli->tree->tid;
	cli->tree->reference_count++;

	printf("vuid1=%d vuid2=%d\n", cli->session->vuid, session->vuid);

	printf("create a file using the new vuid\n");
	io.generic.level = RAW_OPEN_NTCREATEX;
	io.ntcreatex.in.root_fid = 0;
	io.ntcreatex.in.flags = 0;
	io.ntcreatex.in.access_mask = SEC_RIGHT_MAXIMUM_ALLOWED;
	io.ntcreatex.in.create_options = 0;
	io.ntcreatex.in.file_attr = FILE_ATTRIBUTE_NORMAL;
	io.ntcreatex.in.share_access = NTCREATEX_SHARE_ACCESS_READ | NTCREATEX_SHARE_ACCESS_WRITE;
	io.ntcreatex.in.alloc_size = 0;
	io.ntcreatex.in.open_disposition = NTCREATEX_DISP_CREATE;
	io.ntcreatex.in.impersonation = NTCREATEX_IMPERSONATION_ANONYMOUS;
	io.ntcreatex.in.security_flags = 0;
	io.ntcreatex.in.fname = fname;
	status = smb_raw_open(tree, mem_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);
	fnum = io.ntcreatex.out.fnum;

	printf("write using the old vuid\n");
	wr.generic.level = RAW_WRITE_WRITEX;
	wr.writex.in.fnum = fnum;
	wr.writex.in.offset = 0;
	wr.writex.in.wmode = 0;
	wr.writex.in.remaining = 0;
	wr.writex.in.count = 1;
	wr.writex.in.data = &c;

	status = smb_raw_write(cli->tree, &wr);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

	printf("write with the new vuid\n");
	status = smb_raw_write(tree, &wr);
	CHECK_STATUS(status, NT_STATUS_OK);
	CHECK_VALUE(wr.writex.out.nwritten, 1);

	printf("logoff the new vuid\n");
	status = smb_raw_ulogoff(session);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("the new vuid should not now be accessible\n");
	status = smb_raw_write(tree, &wr);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

	printf("the fnum should have been auto-closed\n");
	cl.close.level = RAW_CLOSE_CLOSE;
	cl.close.in.fnum = fnum;
	cl.close.in.write_time = 0;
	status = smb_raw_close(cli->tree, &cl);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

	/* close down the new tree, which will also close the session
	   as the reference count will be 0 */
	cli_tree_close(tree);
	
done:
	return ret;
}


/*
  test tree ops
*/
static BOOL test_tree(struct cli_state *cli, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	BOOL ret = True;
	char *share;
	struct cli_tree *tree;
	union smb_tcon tcon;
	union smb_open io;
	union smb_write wr;
	union smb_close cl;
	int fnum;
	const char *fname = BASEDIR "\\test.txt";
	char c = 1;

	printf("TESTING TREE HANDLING\n");

	if (cli_deltree(cli, BASEDIR) == -1 ||
	    !cli_mkdir(cli, BASEDIR)) {
		printf("Unable to setup %s - %s\n", BASEDIR, cli_errstr(cli));
		return False;
	}

	share = lp_parm_string(-1, "torture", "share");

	printf("create a second tree context on the same session\n");
	tree = cli_tree_init(cli->session);

	tcon.generic.level = RAW_TCON_TCONX;
	tcon.tconx.in.flags = 0;
	tcon.tconx.in.password = data_blob(NULL, 0);
	tcon.tconx.in.path = share;
	tcon.tconx.in.device = "A:";	
	status = smb_tree_connect(tree, mem_ctx, &tcon);
	CHECK_STATUS(status, NT_STATUS_OK);

	tree->tid = tcon.tconx.out.cnum;
	printf("tid1=%d tid2=%d\n", cli->tree->tid, tree->tid);

	printf("try a tconx with a bad device type\n");
	tcon.tconx.in.device = "FOO";	
	status = smb_tree_connect(tree, mem_ctx, &tcon);
	CHECK_STATUS(status, NT_STATUS_BAD_DEVICE_TYPE);


	printf("create a file using the new tid\n");
	io.generic.level = RAW_OPEN_NTCREATEX;
	io.ntcreatex.in.root_fid = 0;
	io.ntcreatex.in.flags = 0;
	io.ntcreatex.in.access_mask = SEC_RIGHT_MAXIMUM_ALLOWED;
	io.ntcreatex.in.create_options = 0;
	io.ntcreatex.in.file_attr = FILE_ATTRIBUTE_NORMAL;
	io.ntcreatex.in.share_access = NTCREATEX_SHARE_ACCESS_READ | NTCREATEX_SHARE_ACCESS_WRITE;
	io.ntcreatex.in.alloc_size = 0;
	io.ntcreatex.in.open_disposition = NTCREATEX_DISP_CREATE;
	io.ntcreatex.in.impersonation = NTCREATEX_IMPERSONATION_ANONYMOUS;
	io.ntcreatex.in.security_flags = 0;
	io.ntcreatex.in.fname = fname;
	status = smb_raw_open(tree, mem_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);
	fnum = io.ntcreatex.out.fnum;

	printf("write using the old tid\n");
	wr.generic.level = RAW_WRITE_WRITEX;
	wr.writex.in.fnum = fnum;
	wr.writex.in.offset = 0;
	wr.writex.in.wmode = 0;
	wr.writex.in.remaining = 0;
	wr.writex.in.count = 1;
	wr.writex.in.data = &c;

	status = smb_raw_write(cli->tree, &wr);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

	printf("write with the new tid\n");
	status = smb_raw_write(tree, &wr);
	CHECK_STATUS(status, NT_STATUS_OK);
	CHECK_VALUE(wr.writex.out.nwritten, 1);

	printf("disconnect the new tid\n");
	status = smb_tree_disconnect(tree);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("the new tid should not now be accessible\n");
	status = smb_raw_write(tree, &wr);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

	printf("the fnum should have been auto-closed\n");
	cl.close.level = RAW_CLOSE_CLOSE;
	cl.close.in.fnum = fnum;
	cl.close.in.write_time = 0;
	status = smb_raw_close(cli->tree, &cl);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

	/* close down the new tree */
	cli_tree_close(tree);
	
done:
	return ret;
}


/*
  test pid ops
*/
static BOOL test_pid(struct cli_state *cli, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	BOOL ret = True;
	union smb_open io;
	union smb_write wr;
	union smb_close cl;
	int fnum;
	const char *fname = BASEDIR "\\test.txt";
	char c = 1;
	uint16 pid1, pid2;

	printf("TESTING PID HANDLING\n");

	if (cli_deltree(cli, BASEDIR) == -1 ||
	    !cli_mkdir(cli, BASEDIR)) {
		printf("Unable to setup %s - %s\n", BASEDIR, cli_errstr(cli));
		return False;
	}

	printf("create a second pid\n");
	pid1 = cli->session->pid;
	pid2 = pid1+1;

	printf("pid1=%d pid2=%d\n", pid1, pid2);

	printf("create a file using the new pid\n");
	cli->session->pid = pid2;
	io.generic.level = RAW_OPEN_NTCREATEX;
	io.ntcreatex.in.root_fid = 0;
	io.ntcreatex.in.flags = 0;
	io.ntcreatex.in.access_mask = SEC_RIGHT_MAXIMUM_ALLOWED;
	io.ntcreatex.in.create_options = 0;
	io.ntcreatex.in.file_attr = FILE_ATTRIBUTE_NORMAL;
	io.ntcreatex.in.share_access = NTCREATEX_SHARE_ACCESS_READ | NTCREATEX_SHARE_ACCESS_WRITE;
	io.ntcreatex.in.alloc_size = 0;
	io.ntcreatex.in.open_disposition = NTCREATEX_DISP_CREATE;
	io.ntcreatex.in.impersonation = NTCREATEX_IMPERSONATION_ANONYMOUS;
	io.ntcreatex.in.security_flags = 0;
	io.ntcreatex.in.fname = fname;
	status = smb_raw_open(cli->tree, mem_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);
	fnum = io.ntcreatex.out.fnum;

	printf("write using the old pid\n");
	cli->session->pid = pid1;
	wr.generic.level = RAW_WRITE_WRITEX;
	wr.writex.in.fnum = fnum;
	wr.writex.in.offset = 0;
	wr.writex.in.wmode = 0;
	wr.writex.in.remaining = 0;
	wr.writex.in.count = 1;
	wr.writex.in.data = &c;

	status = smb_raw_write(cli->tree, &wr);
	CHECK_STATUS(status, NT_STATUS_OK);
	CHECK_VALUE(wr.writex.out.nwritten, 1);

	printf("write with the new pid\n");
	cli->session->pid = pid2;
	status = smb_raw_write(cli->tree, &wr);
	CHECK_STATUS(status, NT_STATUS_OK);
	CHECK_VALUE(wr.writex.out.nwritten, 1);

	printf("exit the old pid\n");
	cli->session->pid = pid1;
	status = smb_raw_exit(cli->session);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("the fnum should still be accessible\n");
	cli->session->pid = pid1;
	status = smb_raw_write(cli->tree, &wr);
	CHECK_STATUS(status, NT_STATUS_OK);
	CHECK_VALUE(wr.writex.out.nwritten, 1);

	printf("exit the new pid\n");
	cli->session->pid = pid2;
	status = smb_raw_exit(cli->session);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("the fnum should not now be accessible\n");
	cli->session->pid = pid1;
	status = smb_raw_write(cli->tree, &wr);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

	printf("the fnum should have been auto-closed\n");
	cl.close.level = RAW_CLOSE_CLOSE;
	cl.close.in.fnum = fnum;
	cl.close.in.write_time = 0;
	status = smb_raw_close(cli->tree, &cl);
	CHECK_STATUS(status, NT_STATUS_INVALID_HANDLE);

done:
	return ret;
}


/* 
   basic testing of session/tree context calls
*/
BOOL torture_raw_context(int dummy)
{
	struct cli_state *cli;
	BOOL ret = True;
	TALLOC_CTX *mem_ctx;

	if (!torture_open_connection(&cli)) {
		return False;
	}

	mem_ctx = talloc_init("torture_raw_context");

	if (!test_session(cli, mem_ctx)) {
		ret = False;
	}

	if (!test_tree(cli, mem_ctx)) {
		ret = False;
	}

	if (!test_pid(cli, mem_ctx)) {
		ret = False;
	}

	smb_raw_exit(cli->session);
	cli_deltree(cli, BASEDIR);

	torture_close_connection(cli);
	talloc_destroy(mem_ctx);
	return ret;
}
