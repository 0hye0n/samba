/* 
   Unix SMB/CIFS implementation.
   SMB torture tester
   Copyright (C) Andrew Tridgell 1997-2003
   
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
#include "dynconfig.h"
#include "clilist.h"
#include "lib/cmdline/popt_common.h"
#include "libcli/raw/libcliraw.h"
#include "system/time.h"
#include "system/wait.h"
#include "system/filesys.h"
#include "ioctl.h"
#include "librpc/gen_ndr/ndr_security.h"

int torture_nprocs=4;
int torture_numops=100;
int torture_entries=1000;
int torture_failures=1;
int torture_seed=0;
static int procnum; /* records process count number when forking */
static struct smbcli_state *current_cli;
static BOOL use_oplocks;
static BOOL use_level_II_oplocks;

BOOL torture_showall = False;

#define CHECK_MAX_FAILURES(label) do { if (++failures >= torture_failures) goto label; } while (0)

static struct smbcli_state *open_nbt_connection(void)
{
	struct nbt_name called, calling;
	struct smbcli_state *cli;
	const char *host = lp_parm_string(-1, "torture", "host");

	make_nbt_name_client(&calling, lp_netbios_name());

	nbt_choose_called_name(NULL, &called, host, NBT_NAME_SERVER);

	cli = smbcli_state_init(NULL);
	if (!cli) {
		printf("Failed initialize smbcli_struct to connect with %s\n", host);
		goto failed;
	}

	if (!smbcli_socket_connect(cli, host)) {
		printf("Failed to connect with %s\n", host);
		goto failed;
	}

	if (!smbcli_transport_establish(cli, &calling, &called)) {
		printf("%s rejected the session\n",host);
		goto failed;
	}

	return cli;

failed:
	talloc_free(cli);
	return NULL;
}

BOOL torture_open_connection_share(struct smbcli_state **c, 
				   const char *hostname, 
				   const char *sharename)
{
	NTSTATUS status;

	status = smbcli_full_connection(NULL,
					c, hostname, 
					sharename, NULL,
					cmdline_credentials, NULL);
	if (!NT_STATUS_IS_OK(status)) {
		printf("Failed to open connection - %s\n", nt_errstr(status));
		return False;
	}

	(*c)->transport->options.use_oplocks = use_oplocks;
	(*c)->transport->options.use_level2_oplocks = use_level_II_oplocks;

	return True;
}

BOOL torture_open_connection(struct smbcli_state **c)
{
	const char *host = lp_parm_string(-1, "torture", "host");
	const char *share = lp_parm_string(-1, "torture", "share");

	return torture_open_connection_share(c, host, share);
}



BOOL torture_close_connection(struct smbcli_state *c)
{
	BOOL ret = True;
	if (!c) return True;
	if (NT_STATUS_IS_ERR(smbcli_tdis(c))) {
		printf("tdis failed (%s)\n", smbcli_errstr(c->tree));
		ret = False;
	}
	talloc_free(c);
	return ret;
}

/* open a rpc connection to the chosen binding string */
NTSTATUS torture_rpc_connection(TALLOC_CTX *parent_ctx, 
				struct dcerpc_pipe **p, 
				const char *pipe_name,
				const char *pipe_uuid, 
				uint32_t pipe_version)
{
        NTSTATUS status;
	const char *binding = lp_parm_string(-1, "torture", "binding");

	if (!binding) {
		printf("You must specify a ncacn binding string\n");
		return NT_STATUS_INVALID_PARAMETER;
	}

	status = dcerpc_pipe_connect(parent_ctx, 
				     p, binding, pipe_uuid, pipe_version,
				     cmdline_credentials, NULL);
 
        return status;
}

/* open a rpc connection to a specific transport */
NTSTATUS torture_rpc_connection_transport(TALLOC_CTX *parent_ctx, 
					  struct dcerpc_pipe **p, 
					  const char *pipe_name,
					  const char *pipe_uuid, 
					  uint32_t pipe_version,
					  enum dcerpc_transport_t transport)
{
        NTSTATUS status;
	const char *binding = lp_parm_string(-1, "torture", "binding");
	struct dcerpc_binding *b;
	TALLOC_CTX *mem_ctx = talloc_named(parent_ctx, 0, "torture_rpc_connection_smb");

	if (!binding) {
		printf("You must specify a ncacn binding string\n");
		talloc_free(mem_ctx);
		return NT_STATUS_INVALID_PARAMETER;
	}

	status = dcerpc_parse_binding(mem_ctx, binding, &b);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0,("Failed to parse dcerpc binding '%s'\n", binding));
		talloc_free(mem_ctx);
		return status;
	}

	b->transport = transport;

	status = dcerpc_pipe_connect_b(mem_ctx, p, b, pipe_uuid, pipe_version,
				       cmdline_credentials, NULL);
					   
	if (NT_STATUS_IS_OK(status)) {
		*p = talloc_reference(parent_ctx, *p);
	} else {
		*p = NULL;
	}
	talloc_free(mem_ctx);
        return status;
}

/* check if the server produced the expected error code */
BOOL check_error(const char *location, struct smbcli_state *c, 
		 uint8_t eclass, uint32_t ecode, NTSTATUS nterr)
{
	NTSTATUS status;
	
	status = smbcli_nt_error(c->tree);
	if (NT_STATUS_IS_DOS(status)) {
		int class, num;
		class = NT_STATUS_DOS_CLASS(status);
		num = NT_STATUS_DOS_CODE(status);
                if (eclass != class || ecode != num) {
                        printf("unexpected error code %s\n", nt_errstr(status));
                        printf(" expected %s or %s (at %s)\n", 
			       nt_errstr(NT_STATUS_DOS(eclass, ecode)), 
                               nt_errstr(nterr), location);
                        return False;
                }
        } else {
                if (!NT_STATUS_EQUAL(nterr, status)) {
                        printf("unexpected error code %s\n", nt_errstr(status));
                        printf(" expected %s (at %s)\n", nt_errstr(nterr), location);
                        return False;
                }
        }

	return True;
}


static BOOL wait_lock(struct smbcli_state *c, int fnum, uint32_t offset, uint32_t len)
{
	while (NT_STATUS_IS_ERR(smbcli_lock(c->tree, fnum, offset, len, -1, WRITE_LOCK))) {
		if (!check_error(__location__, c, ERRDOS, ERRlock, NT_STATUS_LOCK_NOT_GRANTED)) return False;
	}
	return True;
}


static BOOL rw_torture(struct smbcli_state *c)
{
	const char *lockfname = "\\torture.lck";
	char *fname;
	int fnum;
	int fnum2;
	pid_t pid2, pid = getpid();
	int i, j;
	uint8_t buf[1024];
	BOOL correct = True;

	fnum2 = smbcli_open(c->tree, lockfname, O_RDWR | O_CREAT | O_EXCL, 
			 DENY_NONE);
	if (fnum2 == -1)
		fnum2 = smbcli_open(c->tree, lockfname, O_RDWR, DENY_NONE);
	if (fnum2 == -1) {
		printf("open of %s failed (%s)\n", lockfname, smbcli_errstr(c->tree));
		return False;
	}


	for (i=0;i<torture_numops;i++) {
		uint_t n = (uint_t)random()%10;
		if (i % 10 == 0) {
			printf("%d\r", i); fflush(stdout);
		}
		asprintf(&fname, "\\torture.%u", n);

		if (!wait_lock(c, fnum2, n*sizeof(int), sizeof(int))) {
			return False;
		}

		fnum = smbcli_open(c->tree, fname, O_RDWR | O_CREAT | O_TRUNC, DENY_ALL);
		if (fnum == -1) {
			printf("open failed (%s)\n", smbcli_errstr(c->tree));
			correct = False;
			break;
		}

		if (smbcli_write(c->tree, fnum, 0, &pid, 0, sizeof(pid)) != sizeof(pid)) {
			printf("write failed (%s)\n", smbcli_errstr(c->tree));
			correct = False;
		}

		for (j=0;j<50;j++) {
			if (smbcli_write(c->tree, fnum, 0, buf, 
				      sizeof(pid)+(j*sizeof(buf)), 
				      sizeof(buf)) != sizeof(buf)) {
				printf("write failed (%s)\n", smbcli_errstr(c->tree));
				correct = False;
			}
		}

		pid2 = 0;

		if (smbcli_read(c->tree, fnum, &pid2, 0, sizeof(pid)) != sizeof(pid)) {
			printf("read failed (%s)\n", smbcli_errstr(c->tree));
			correct = False;
		}

		if (pid2 != pid) {
			printf("data corruption!\n");
			correct = False;
		}

		if (NT_STATUS_IS_ERR(smbcli_close(c->tree, fnum))) {
			printf("close failed (%s)\n", smbcli_errstr(c->tree));
			correct = False;
		}

		if (NT_STATUS_IS_ERR(smbcli_unlink(c->tree, fname))) {
			printf("unlink failed (%s)\n", smbcli_errstr(c->tree));
			correct = False;
		}

		if (NT_STATUS_IS_ERR(smbcli_unlock(c->tree, fnum2, n*sizeof(int), sizeof(int)))) {
			printf("unlock failed (%s)\n", smbcli_errstr(c->tree));
			correct = False;
		}
		free(fname);
	}

	smbcli_close(c->tree, fnum2);
	smbcli_unlink(c->tree, lockfname);

	printf("%d\n", i);

	return correct;
}

static BOOL run_torture(struct smbcli_state *cli, int dummy)
{
        BOOL ret;

	ret = rw_torture(cli);
	
	if (!torture_close_connection(cli)) {
		ret = False;
	}

	return ret;
}


static BOOL rw_torture2(struct smbcli_state *c1, struct smbcli_state *c2)
{
	const char *lockfname = "\\torture2.lck";
	int fnum1;
	int fnum2;
	int i;
	uint8_t buf[131072];
	uint8_t buf_rd[131072];
	BOOL correct = True;
	ssize_t bytes_read, bytes_written;

	if (smbcli_deltree(c1->tree, lockfname) == -1) {
		printf("unlink failed (%s)\n", smbcli_errstr(c1->tree));
	}

	fnum1 = smbcli_open(c1->tree, lockfname, O_RDWR | O_CREAT | O_EXCL, 
			 DENY_NONE);
	if (fnum1 == -1) {
		printf("first open read/write of %s failed (%s)\n",
				lockfname, smbcli_errstr(c1->tree));
		return False;
	}
	fnum2 = smbcli_open(c2->tree, lockfname, O_RDONLY, 
			 DENY_NONE);
	if (fnum2 == -1) {
		printf("second open read-only of %s failed (%s)\n",
				lockfname, smbcli_errstr(c2->tree));
		smbcli_close(c1->tree, fnum1);
		return False;
	}

	printf("Checking data integrity over %d ops\n", torture_numops);

	for (i=0;i<torture_numops;i++)
	{
		size_t buf_size = ((uint_t)random()%(sizeof(buf)-1))+ 1;
		if (i % 10 == 0) {
			printf("%d\r", i); fflush(stdout);
		}

		generate_random_buffer(buf, buf_size);

		if ((bytes_written = smbcli_write(c1->tree, fnum1, 0, buf, 0, buf_size)) != buf_size) {
			printf("write failed (%s)\n", smbcli_errstr(c1->tree));
			printf("wrote %d, expected %d\n", (int)bytes_written, (int)buf_size); 
			correct = False;
			break;
		}

		if ((bytes_read = smbcli_read(c2->tree, fnum2, buf_rd, 0, buf_size)) != buf_size) {
			printf("read failed (%s)\n", smbcli_errstr(c2->tree));
			printf("read %d, expected %d\n", (int)bytes_read, (int)buf_size); 
			correct = False;
			break;
		}

		if (memcmp(buf_rd, buf, buf_size) != 0)
		{
			printf("read/write compare failed\n");
			correct = False;
			break;
		}
	}

	if (NT_STATUS_IS_ERR(smbcli_close(c2->tree, fnum2))) {
		printf("close failed (%s)\n", smbcli_errstr(c2->tree));
		correct = False;
	}
	if (NT_STATUS_IS_ERR(smbcli_close(c1->tree, fnum1))) {
		printf("close failed (%s)\n", smbcli_errstr(c1->tree));
		correct = False;
	}

	if (NT_STATUS_IS_ERR(smbcli_unlink(c1->tree, lockfname))) {
		printf("unlink failed (%s)\n", smbcli_errstr(c1->tree));
		correct = False;
	}

	return correct;
}

#define BOOLSTR(b) ((b) ? "Yes" : "No")

static BOOL run_readwritetest(void)
{
	struct smbcli_state *cli1, *cli2;
	BOOL test1, test2 = True;

	if (!torture_open_connection(&cli1) || !torture_open_connection(&cli2)) {
		return False;
	}

	printf("starting readwritetest\n");

	test1 = rw_torture2(cli1, cli2);
	printf("Passed readwritetest v1: %s\n", BOOLSTR(test1));

	if (test1) {
		test2 = rw_torture2(cli1, cli1);
		printf("Passed readwritetest v2: %s\n", BOOLSTR(test2));
	}

	if (!torture_close_connection(cli1)) {
		test1 = False;
	}

	if (!torture_close_connection(cli2)) {
		test2 = False;
	}

	return (test1 && test2);
}

/*
  this checks to see if a secondary tconx can use open files from an
  earlier tconx
 */
static BOOL run_tcon_test(void)
{
	struct smbcli_state *cli;
	const char *fname = "\\tcontest.tmp";
	int fnum1;
	uint16_t cnum1, cnum2, cnum3;
	uint16_t vuid1, vuid2;
	uint8_t buf[4];
	BOOL ret = True;
	struct smbcli_tree *tree1;
	const char *host = lp_parm_string(-1, "torture", "host");
	const char *share = lp_parm_string(-1, "torture", "share");
	const char *password = lp_parm_string(-1, "torture", "password");

	if (!torture_open_connection(&cli)) {
		return False;
	}

	printf("starting tcontest\n");

	if (smbcli_deltree(cli->tree, fname) == -1) {
		printf("unlink of %s failed (%s)\n", fname, smbcli_errstr(cli->tree));
	}

	fnum1 = smbcli_open(cli->tree, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, smbcli_errstr(cli->tree));
		return False;
	}

	cnum1 = cli->tree->tid;
	vuid1 = cli->session->vuid;

	memset(&buf, 0, 4); /* init buf so valgrind won't complain */
	if (smbcli_write(cli->tree, fnum1, 0, buf, 130, 4) != 4) {
		printf("initial write failed (%s)\n", smbcli_errstr(cli->tree));
		return False;
	}

	tree1 = cli->tree;	/* save old tree connection */
	if (NT_STATUS_IS_ERR(smbcli_tconX(cli, share, "?????", password))) {
		printf("%s refused 2nd tree connect (%s)\n", host,
		           smbcli_errstr(cli->tree));
		talloc_free(cli);
		return False;
	}

	cnum2 = cli->tree->tid;
	cnum3 = MAX(cnum1, cnum2) + 1; /* any invalid number */
	vuid2 = cli->session->vuid + 1;

	/* try a write with the wrong tid */
	cli->tree->tid = cnum2;

	if (smbcli_write(cli->tree, fnum1, 0, buf, 130, 4) == 4) {
		printf("* server allows write with wrong TID\n");
		ret = False;
	} else {
		printf("server fails write with wrong TID : %s\n", smbcli_errstr(cli->tree));
	}


	/* try a write with an invalid tid */
	cli->tree->tid = cnum3;

	if (smbcli_write(cli->tree, fnum1, 0, buf, 130, 4) == 4) {
		printf("* server allows write with invalid TID\n");
		ret = False;
	} else {
		printf("server fails write with invalid TID : %s\n", smbcli_errstr(cli->tree));
	}

	/* try a write with an invalid vuid */
	cli->session->vuid = vuid2;
	cli->tree->tid = cnum1;

	if (smbcli_write(cli->tree, fnum1, 0, buf, 130, 4) == 4) {
		printf("* server allows write with invalid VUID\n");
		ret = False;
	} else {
		printf("server fails write with invalid VUID : %s\n", smbcli_errstr(cli->tree));
	}

	cli->session->vuid = vuid1;
	cli->tree->tid = cnum1;

	if (NT_STATUS_IS_ERR(smbcli_close(cli->tree, fnum1))) {
		printf("close failed (%s)\n", smbcli_errstr(cli->tree));
		return False;
	}

	cli->tree->tid = cnum2;

	if (NT_STATUS_IS_ERR(smbcli_tdis(cli))) {
		printf("secondary tdis failed (%s)\n", smbcli_errstr(cli->tree));
		return False;
	}

	cli->tree = tree1;  /* restore initial tree */
	cli->tree->tid = cnum1;

	smbcli_unlink(tree1, fname);

	if (!torture_close_connection(cli)) {
		return False;
	}

	return ret;
}



static BOOL tcon_devtest(struct smbcli_state *cli,
			 const char *myshare, const char *devtype,
			 NTSTATUS expected_error)
{
	BOOL status;
	BOOL ret;
	const char *password = lp_parm_string(-1, "torture", "password");

	status = NT_STATUS_IS_OK(smbcli_tconX(cli, myshare, devtype, 
						password));

	printf("Trying share %s with devtype %s\n", myshare, devtype);

	if (NT_STATUS_IS_OK(expected_error)) {
		if (status) {
			ret = True;
		} else {
			printf("tconX to share %s with type %s "
			       "should have succeeded but failed\n",
			       myshare, devtype);
			ret = False;
		}
		smbcli_tdis(cli);
	} else {
		if (status) {
			printf("tconx to share %s with type %s "
			       "should have failed but succeeded\n",
			       myshare, devtype);
			ret = False;
		} else {
			if (NT_STATUS_EQUAL(smbcli_nt_error(cli->tree),
					    expected_error)) {
				ret = True;
			} else {
				printf("Returned unexpected error\n");
				ret = False;
			}
		}
	}
	return ret;
}

/*
 checks for correct tconX support
 */
static BOOL run_tcon_devtype_test(void)
{
	struct smbcli_state *cli1 = NULL;
	NTSTATUS status;
	BOOL ret = True;
	const char *host = lp_parm_string(-1, "torture", "host");
	const char *share = lp_parm_string(-1, "torture", "share");
	
	status = smbcli_full_connection(NULL,
					&cli1, host, 
					share, NULL,
					cmdline_credentials, NULL);

	if (!NT_STATUS_IS_OK(status)) {
		printf("could not open connection\n");
		return False;
	}

	if (!tcon_devtest(cli1, "IPC$", "A:", NT_STATUS_BAD_DEVICE_TYPE))
		ret = False;

	if (!tcon_devtest(cli1, "IPC$", "?????", NT_STATUS_OK))
		ret = False;

	if (!tcon_devtest(cli1, "IPC$", "LPT:", NT_STATUS_BAD_DEVICE_TYPE))
		ret = False;

	if (!tcon_devtest(cli1, "IPC$", "IPC", NT_STATUS_OK))
		ret = False;
			
	if (!tcon_devtest(cli1, "IPC$", "FOOBA", NT_STATUS_BAD_DEVICE_TYPE))
		ret = False;

	if (!tcon_devtest(cli1, share, "A:", NT_STATUS_OK))
		ret = False;

	if (!tcon_devtest(cli1, share, "?????", NT_STATUS_OK))
		ret = False;

	if (!tcon_devtest(cli1, share, "LPT:", NT_STATUS_BAD_DEVICE_TYPE))
		ret = False;

	if (!tcon_devtest(cli1, share, "IPC", NT_STATUS_BAD_DEVICE_TYPE))
		ret = False;
			
	if (!tcon_devtest(cli1, share, "FOOBA", NT_STATUS_BAD_DEVICE_TYPE))
		ret = False;

	talloc_free(cli1);

	if (ret)
		printf("Passed tcondevtest\n");

	return ret;
}


/*
test whether fnums and tids open on one VC are available on another (a major
security hole)
*/
static BOOL run_fdpasstest(void)
{
	struct smbcli_state *cli1, *cli2;
	const char *fname = "\\fdpass.tst";
	int fnum1, oldtid;
	uint8_t buf[1024];

	if (!torture_open_connection(&cli1) || !torture_open_connection(&cli2)) {
		return False;
	}

	printf("starting fdpasstest\n");

	smbcli_unlink(cli1->tree, fname);

	printf("Opening a file on connection 1\n");

	fnum1 = smbcli_open(cli1->tree, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	printf("writing to file on connection 1\n");

	if (smbcli_write(cli1->tree, fnum1, 0, "hello world\n", 0, 13) != 13) {
		printf("write failed (%s)\n", smbcli_errstr(cli1->tree));
		return False;
	}

	oldtid = cli2->tree->tid;
	cli2->session->vuid = cli1->session->vuid;
	cli2->tree->tid = cli1->tree->tid;
	cli2->session->pid = cli1->session->pid;

	printf("reading from file on connection 2\n");

	if (smbcli_read(cli2->tree, fnum1, buf, 0, 13) == 13) {
		printf("read succeeded! nasty security hole [%s]\n",
		       buf);
		return False;
	}

	smbcli_close(cli1->tree, fnum1);
	smbcli_unlink(cli1->tree, fname);

	cli2->tree->tid = oldtid;

	torture_close_connection(cli1);
	torture_close_connection(cli2);

	printf("finished fdpasstest\n");
	return True;
}


/*
test the timing of deferred open requests
*/
static BOOL run_deferopen(struct smbcli_state *cli, int dummy)
{
	const char *fname = "\\defer_open_test.dat";
	int retries=4;
	int i = 0;
	BOOL correct = True;

	if (retries <= 0) {
		printf("failed to connect\n");
		return False;
	}

	printf("Testing deferred open requests.\n");

	while (i < 4) {
		int fnum = -1;

		do {
			struct timeval tv;
			tv = timeval_current();
			fnum = smbcli_nt_create_full(cli->tree, fname, 0, 
						     SEC_RIGHTS_FILE_ALL,
						     FILE_ATTRIBUTE_NORMAL, 
						     NTCREATEX_SHARE_ACCESS_NONE,
						     NTCREATEX_DISP_OPEN_IF, 0, 0);
			if (fnum != -1) {
				break;
			}
			if (NT_STATUS_EQUAL(smbcli_nt_error(cli->tree),NT_STATUS_SHARING_VIOLATION)) {
				double e = timeval_elapsed(&tv);
				if (e < 0.5 || e > 1.5) {
					fprintf(stderr,"Timing incorrect %.2f violation\n",
						e);
				}
			}
		} while (NT_STATUS_EQUAL(smbcli_nt_error(cli->tree),NT_STATUS_SHARING_VIOLATION));

		if (fnum == -1) {
			fprintf(stderr,"Failed to open %s, error=%s\n", fname, smbcli_errstr(cli->tree));
			return False;
		}

		printf("pid %u open %d\n", getpid(), i);

		sleep(10);
		i++;
		if (NT_STATUS_IS_ERR(smbcli_close(cli->tree, fnum))) {
			fprintf(stderr,"Failed to close %s, error=%s\n", fname, smbcli_errstr(cli->tree));
			return False;
		}
		sleep(2);
	}

	if (NT_STATUS_IS_ERR(smbcli_unlink(cli->tree, fname))) {
		/* All until the last unlink will fail with sharing violation. */
		if (!NT_STATUS_EQUAL(smbcli_nt_error(cli->tree),NT_STATUS_SHARING_VIOLATION)) {
			printf("unlink of %s failed (%s)\n", fname, smbcli_errstr(cli->tree));
			correct = False;
		}
	}

	printf("deferred test finished\n");
	if (!torture_close_connection(cli)) {
		correct = False;
	}
	return correct;
}

/*
test how many open files this server supports on the one socket
*/
static BOOL run_maxfidtest(struct smbcli_state *cli, int dummy)
{
#define MAXFID_TEMPLATE "\\maxfid\\fid%d\\maxfid.%d.%d"
	char *fname;
	int fnums[0x11000], i;
	int retries=4, maxfid;
	BOOL correct = True;

	if (retries <= 0) {
		printf("failed to connect\n");
		return False;
	}

	if (smbcli_deltree(cli->tree, "\\maxfid") == -1) {
		printf("Failed to deltree \\maxfid - %s\n",
		       smbcli_errstr(cli->tree));
		return False;
	}
	if (NT_STATUS_IS_ERR(smbcli_mkdir(cli->tree, "\\maxfid"))) {
		printf("Failed to mkdir \\maxfid, error=%s\n", 
		       smbcli_errstr(cli->tree));
		return False;
	}

	printf("Testing maximum number of open files\n");

	for (i=0; i<0x11000; i++) {
		if (i % 1000 == 0) {
			asprintf(&fname, "\\maxfid\\fid%d", i/1000);
			if (NT_STATUS_IS_ERR(smbcli_mkdir(cli->tree, fname))) {
				printf("Failed to mkdir %s, error=%s\n", 
				       fname, smbcli_errstr(cli->tree));
				return False;
			}
			free(fname);
		}
		asprintf(&fname, MAXFID_TEMPLATE, i/1000, i,(int)getpid());
		if ((fnums[i] = smbcli_open(cli->tree, fname, 
					O_RDWR|O_CREAT|O_TRUNC, DENY_NONE)) ==
		    -1) {
			printf("open of %s failed (%s)\n", 
			       fname, smbcli_errstr(cli->tree));
			printf("maximum fnum is %d\n", i);
			break;
		}
		free(fname);
		printf("%6d\r", i);
	}
	printf("%6d\n", i);
	i--;

	maxfid = i;

	printf("cleaning up\n");
	for (i=0;i<maxfid/2;i++) {
		asprintf(&fname, MAXFID_TEMPLATE, i/1000, i,(int)getpid());
		if (NT_STATUS_IS_ERR(smbcli_close(cli->tree, fnums[i]))) {
			printf("Close of fnum %d failed - %s\n", fnums[i], smbcli_errstr(cli->tree));
		}
		if (NT_STATUS_IS_ERR(smbcli_unlink(cli->tree, fname))) {
			printf("unlink of %s failed (%s)\n", 
			       fname, smbcli_errstr(cli->tree));
			correct = False;
		}
		free(fname);

		asprintf(&fname, MAXFID_TEMPLATE, (maxfid-i)/1000, maxfid-i,(int)getpid());
		if (NT_STATUS_IS_ERR(smbcli_close(cli->tree, fnums[maxfid-i]))) {
			printf("Close of fnum %d failed - %s\n", fnums[maxfid-i], smbcli_errstr(cli->tree));
		}
		if (NT_STATUS_IS_ERR(smbcli_unlink(cli->tree, fname))) {
			printf("unlink of %s failed (%s)\n", 
			       fname, smbcli_errstr(cli->tree));
			correct = False;
		}
		free(fname);

		printf("%6d %6d\r", i, maxfid-i);
	}
	printf("%6d\n", 0);

	if (smbcli_deltree(cli->tree, "\\maxfid") == -1) {
		printf("Failed to deltree \\maxfid - %s\n",
		       smbcli_errstr(cli->tree));
		return False;
	}

	printf("maxfid test finished\n");
	if (!torture_close_connection(cli)) {
		correct = False;
	}
	return correct;
#undef MAXFID_TEMPLATE
}

/* send smb negprot commands, not reading the response */
static BOOL run_negprot_nowait(void)
{
	int i;
	struct smbcli_state *cli, *cli2;
	BOOL correct = True;

	printf("starting negprot nowait test\n");

	cli = open_nbt_connection();
	if (!cli) {
		return False;
	}

	printf("Filling send buffer\n");

	for (i=0;i<10000;i++) {
		struct smbcli_request *req;
		time_t t1 = time(NULL);
		req = smb_raw_negotiate_send(cli->transport, PROTOCOL_NT1);
		while (req->state == SMBCLI_REQUEST_SEND && time(NULL) < t1+5) {
			smbcli_transport_process(cli->transport);
		}
		if (req->state == SMBCLI_REQUEST_ERROR) {
			printf("Failed to fill pipe - %s\n", nt_errstr(req->status));
			torture_close_connection(cli);
			return correct;
		}
		if (req->state == SMBCLI_REQUEST_SEND) {
			break;
		}
	}

	if (i == 10000) {
		printf("send buffer failed to fill\n");
		if (!torture_close_connection(cli)) {
			correct = False;
		}
		return correct;
	}

	printf("send buffer filled after %d requests\n", i);

	printf("Opening secondary connection\n");
	if (!torture_open_connection(&cli2)) {
		return False;
	}

	if (!torture_close_connection(cli)) {
		correct = False;
	}

	if (!torture_close_connection(cli2)) {
		correct = False;
	}

	printf("finished negprot nowait test\n");

	return correct;
}


/*
  This checks how the getatr calls works
*/
static BOOL run_attrtest(void)
{
	struct smbcli_state *cli;
	int fnum;
	time_t t, t2;
	const char *fname = "\\attrib123456789.tst";
	BOOL correct = True;

	printf("starting attrib test\n");

	if (!torture_open_connection(&cli)) {
		return False;
	}

	smbcli_unlink(cli->tree, fname);
	fnum = smbcli_open(cli->tree, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	smbcli_close(cli->tree, fnum);

	if (NT_STATUS_IS_ERR(smbcli_getatr(cli->tree, fname, NULL, NULL, &t))) {
		printf("getatr failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	}

	printf("New file time is %s", ctime(&t));

	if (abs(t - time(NULL)) > 60*60*24*10) {
		printf("ERROR: SMBgetatr bug. time is %s",
		       ctime(&t));
		t = time(NULL);
		correct = False;
	}

	t2 = t-60*60*24; /* 1 day ago */

	printf("Setting file time to %s", ctime(&t2));

	if (NT_STATUS_IS_ERR(smbcli_setatr(cli->tree, fname, 0, t2))) {
		printf("setatr failed (%s)\n", smbcli_errstr(cli->tree));
		correct = True;
	}

	if (NT_STATUS_IS_ERR(smbcli_getatr(cli->tree, fname, NULL, NULL, &t))) {
		printf("getatr failed (%s)\n", smbcli_errstr(cli->tree));
		correct = True;
	}

	printf("Retrieved file time as %s", ctime(&t));

	if (t != t2) {
		printf("ERROR: getatr/setatr bug. times are\n%s",
		       ctime(&t));
		printf("%s", ctime(&t2));
		correct = True;
	}

	smbcli_unlink(cli->tree, fname);

	if (!torture_close_connection(cli)) {
		correct = False;
	}

	printf("attrib test finished\n");

	return correct;
}


/*
  This checks a couple of trans2 calls
*/
static BOOL run_trans2test(void)
{
	struct smbcli_state *cli;
	int fnum;
	size_t size;
	time_t c_time, a_time, m_time, w_time, m_time2;
	const char *fname = "\\trans2.tst";
	const char *dname = "\\trans2";
	const char *fname2 = "\\trans2\\trans2.tst";
	const char *pname;
	BOOL correct = True;

	printf("starting trans2 test\n");

	if (!torture_open_connection(&cli)) {
		return False;
	}

	smbcli_unlink(cli->tree, fname);

	printf("Testing qfileinfo\n");
	
	fnum = smbcli_open(cli->tree, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	if (NT_STATUS_IS_ERR(smbcli_qfileinfo(cli->tree, fnum, NULL, &size, &c_time, &a_time, &m_time,
			   NULL, NULL))) {
		printf("ERROR: qfileinfo failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	}

	printf("Testing NAME_INFO\n");

	if (NT_STATUS_IS_ERR(smbcli_qfilename(cli->tree, fnum, &pname))) {
		printf("ERROR: qfilename failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	}

	if (!pname || strcmp(pname, fname)) {
		printf("qfilename gave different name? [%s] [%s]\n",
		       fname, pname);
		correct = False;
	}

	smbcli_close(cli->tree, fnum);
	smbcli_unlink(cli->tree, fname);

	fnum = smbcli_open(cli->tree, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	if (fnum == -1) {
		printf("open of %s failed (%s)\n", fname, smbcli_errstr(cli->tree));
		return False;
	}
	smbcli_close(cli->tree, fnum);

	printf("Checking for sticky create times\n");

	if (NT_STATUS_IS_ERR(smbcli_qpathinfo(cli->tree, fname, &c_time, &a_time, &m_time, &size, NULL))) {
		printf("ERROR: qpathinfo failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	} else {
		if (c_time != m_time) {
			printf("create time=%s", ctime(&c_time));
			printf("modify time=%s", ctime(&m_time));
			printf("This system appears to have sticky create times\n");
		}
		if (a_time % (60*60) == 0) {
			printf("access time=%s", ctime(&a_time));
			printf("This system appears to set a midnight access time\n");
			correct = False;
		}

		if (abs(m_time - time(NULL)) > 60*60*24*7) {
			printf("ERROR: totally incorrect times - maybe word reversed? mtime=%s", ctime(&m_time));
			correct = False;
		}
	}


	smbcli_unlink(cli->tree, fname);
	fnum = smbcli_open(cli->tree, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	smbcli_close(cli->tree, fnum);
	if (NT_STATUS_IS_ERR(smbcli_qpathinfo2(cli->tree, fname, &c_time, &a_time, &m_time, &w_time, &size, NULL, NULL))) {
		printf("ERROR: qpathinfo2 failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	} else {
		if (w_time < 60*60*24*2) {
			printf("write time=%s", ctime(&w_time));
			printf("This system appears to set a initial 0 write time\n");
			correct = False;
		}
	}

	smbcli_unlink(cli->tree, fname);


	/* check if the server updates the directory modification time
           when creating a new file */
	if (NT_STATUS_IS_ERR(smbcli_mkdir(cli->tree, dname))) {
		printf("ERROR: mkdir failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	}
	sleep(3);
	if (NT_STATUS_IS_ERR(smbcli_qpathinfo2(cli->tree, "\\trans2\\", &c_time, &a_time, &m_time, &w_time, &size, NULL, NULL))) {
		printf("ERROR: qpathinfo2 failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	}

	fnum = smbcli_open(cli->tree, fname2, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	smbcli_write(cli->tree, fnum,  0, &fnum, 0, sizeof(fnum));
	smbcli_close(cli->tree, fnum);
	if (NT_STATUS_IS_ERR(smbcli_qpathinfo2(cli->tree, "\\trans2\\", &c_time, &a_time, &m_time2, &w_time, &size, NULL, NULL))) {
		printf("ERROR: qpathinfo2 failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	} else {
		if (m_time2 == m_time) {
			printf("This system does not update directory modification times\n");
			correct = False;
		}
	}
	smbcli_unlink(cli->tree, fname2);
	smbcli_rmdir(cli->tree, dname);

	if (!torture_close_connection(cli)) {
		correct = False;
	}

	printf("trans2 test finished\n");

	return correct;
}



/* FIRST_DESIRED_ACCESS   0xf019f */
#define FIRST_DESIRED_ACCESS   SEC_FILE_READ_DATA|SEC_FILE_WRITE_DATA|SEC_FILE_APPEND_DATA|\
                               SEC_FILE_READ_EA|                           /* 0xf */ \
                               SEC_FILE_WRITE_EA|SEC_FILE_READ_ATTRIBUTE|     /* 0x90 */ \
                               SEC_FILE_WRITE_ATTRIBUTE|                  /* 0x100 */ \
                               SEC_STD_DELETE|SEC_STD_READ_CONTROL|\
                               SEC_STD_WRITE_DAC|SEC_STD_WRITE_OWNER     /* 0xf0000 */
/* SECOND_DESIRED_ACCESS  0xe0080 */
#define SECOND_DESIRED_ACCESS  SEC_FILE_READ_ATTRIBUTE|                   /* 0x80 */ \
                               SEC_STD_READ_CONTROL|SEC_STD_WRITE_DAC|\
                               SEC_STD_WRITE_OWNER                      /* 0xe0000 */

#if 0
#define THIRD_DESIRED_ACCESS   FILE_READ_ATTRIBUTE|                   /* 0x80 */ \
                               READ_CONTROL|WRITE_DAC|\
                               SEC_FILE_READ_DATA|\
                               WRITE_OWNER                      /* */
#endif

/*
  Test ntcreate calls made by xcopy
 */
static BOOL run_xcopy(void)
{
	struct smbcli_state *cli1;
	const char *fname = "\\test.txt";
	BOOL correct = True;
	int fnum1, fnum2;

	printf("starting xcopy test\n");
	
	if (!torture_open_connection(&cli1)) {
		return False;
	}
	
	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0,
				      FIRST_DESIRED_ACCESS, 
				      FILE_ATTRIBUTE_ARCHIVE,
				      NTCREATEX_SHARE_ACCESS_NONE, 
				      NTCREATEX_DISP_OVERWRITE_IF, 
				      0x4044, 0);

	if (fnum1 == -1) {
		printf("First open failed - %s\n", smbcli_errstr(cli1->tree));
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli1->tree, fname, 0,
				   SECOND_DESIRED_ACCESS, 0,
				   NTCREATEX_SHARE_ACCESS_READ|NTCREATEX_SHARE_ACCESS_WRITE|NTCREATEX_SHARE_ACCESS_DELETE, NTCREATEX_DISP_OPEN, 
				   0x200000, 0);
	if (fnum2 == -1) {
		printf("second open failed - %s\n", smbcli_errstr(cli1->tree));
		return False;
	}
	
	if (!torture_close_connection(cli1)) {
		correct = False;
	}
	
	return correct;
}


/*
  see how many RPC pipes we can open at once
*/
static BOOL run_pipe_number(void)
{
	struct smbcli_state *cli1;
	const char *pipe_name = "\\WKSSVC";
	int fnum;
	int num_pipes = 0;

	printf("starting pipenumber test\n");
	if (!torture_open_connection(&cli1)) {
		return False;
	}

	while(1) {
		fnum = smbcli_nt_create_full(cli1->tree, pipe_name, 0, SEC_FILE_READ_DATA, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_READ|NTCREATEX_SHARE_ACCESS_WRITE, NTCREATEX_DISP_OPEN_IF, 0, 0);

		if (fnum == -1) {
			printf("Open of pipe %s failed with error (%s)\n", pipe_name, smbcli_errstr(cli1->tree));
			break;
		}
		num_pipes++;
		printf("%d\r", num_pipes);
		fflush(stdout);
	}

	printf("pipe_number test - we can open %d %s pipes.\n", num_pipes, pipe_name );
	torture_close_connection(cli1);
	return True;
}




/*
  open N connections to the server and just hold them open
  used for testing performance when there are N idle users
  already connected
 */
 static BOOL torture_holdcon(void)
{
	int i;
	struct smbcli_state **cli;
	int num_dead = 0;

	printf("Opening %d connections\n", torture_numops);
	
	cli = malloc_array_p(struct smbcli_state *, torture_numops);

	for (i=0;i<torture_numops;i++) {
		if (!torture_open_connection(&cli[i])) {
			return False;
		}
		printf("opened %d connections\r", i);
		fflush(stdout);
	}

	printf("\nStarting pings\n");

	while (1) {
		for (i=0;i<torture_numops;i++) {
			NTSTATUS status;
			if (cli[i]) {
				status = smbcli_chkpath(cli[i]->tree, "\\");
				if (!NT_STATUS_IS_OK(status)) {
					printf("Connection %d is dead\n", i);
					cli[i] = NULL;
					num_dead++;
				}
				usleep(100);
			}
		}

		if (num_dead == torture_numops) {
			printf("All connections dead - finishing\n");
			break;
		}

		printf(".");
		fflush(stdout);
	}

	return True;
}

/*
  Try with a wrong vuid and check error message.
 */

static BOOL run_vuidtest(void)
{
	struct smbcli_state *cli;
	const char *fname = "\\vuid.tst";
	int fnum;
	size_t size;
	time_t c_time, a_time, m_time;
	BOOL correct = True;

	uint16_t orig_vuid;
	NTSTATUS result;

	printf("starting vuid test\n");

	if (!torture_open_connection(&cli)) {
		return False;
	}

	smbcli_unlink(cli->tree, fname);

	fnum = smbcli_open(cli->tree, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);

	orig_vuid = cli->session->vuid;

	cli->session->vuid += 1234;

	printf("Testing qfileinfo with wrong vuid\n");
	
	if (NT_STATUS_IS_OK(result = smbcli_qfileinfo(cli->tree, fnum, NULL,
						   &size, &c_time, &a_time,
						   &m_time, NULL, NULL))) {
		printf("ERROR: qfileinfo passed with wrong vuid\n");
		correct = False;
	}

	if (!NT_STATUS_EQUAL(cli->transport->error.e.nt_status, 
			     NT_STATUS_DOS(ERRSRV, ERRbaduid)) &&
	    !NT_STATUS_EQUAL(cli->transport->error.e.nt_status, 
			     NT_STATUS_INVALID_HANDLE)) {
		printf("ERROR: qfileinfo should have returned DOS error "
		       "ERRSRV:ERRbaduid\n  but returned %s\n",
		       smbcli_errstr(cli->tree));
		correct = False;
	}

	cli->session->vuid -= 1234;

	if (NT_STATUS_IS_ERR(smbcli_close(cli->tree, fnum))) {
		printf("close failed (%s)\n", smbcli_errstr(cli->tree));
		correct = False;
	}

	smbcli_unlink(cli->tree, fname);

	if (!torture_close_connection(cli)) {
		correct = False;
	}

	printf("vuid test finished\n");

	return correct;
}

/*
  Test open mode returns on read-only files.
 */
 static BOOL run_opentest(void)
{
	static struct smbcli_state *cli1;
	static struct smbcli_state *cli2;
	const char *fname = "\\readonly.file";
	char *control_char_fname;
	int fnum1, fnum2;
	uint8_t buf[20];
	size_t fsize;
	BOOL correct = True;
	char *tmp_path;
	int failures = 0;
	int i;

	printf("starting open test\n");
	
	if (!torture_open_connection(&cli1)) {
		return False;
	}
	
	asprintf(&control_char_fname, "\\readonly.afile");
	for (i = 1; i <= 0x1f; i++) {
		control_char_fname[10] = i;
		fnum1 = smbcli_nt_create_full(cli1->tree, control_char_fname, 0, SEC_FILE_WRITE_DATA, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);
		
        	if (!check_error(__location__, cli1, ERRDOS, ERRinvalidname, 
				NT_STATUS_OBJECT_NAME_INVALID)) {
			printf("Error code should be NT_STATUS_OBJECT_NAME_INVALID, was %s for file with %d char\n",
					smbcli_errstr(cli1->tree), i);
			failures++;
		}

		if (fnum1 != -1) {
			smbcli_close(cli1->tree, fnum1);
		}
		smbcli_setatr(cli1->tree, control_char_fname, 0, 0);
		smbcli_unlink(cli1->tree, control_char_fname);
	}
	free(control_char_fname);

	if (!failures)
		printf("Create file with control char names passed.\n");

	smbcli_setatr(cli1->tree, fname, 0, 0);
	smbcli_unlink(cli1->tree, fname);
	
	fnum1 = smbcli_open(cli1->tree, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("close2 failed (%s)\n", smbcli_errstr(cli1->tree));
		return False;
	}
	
	if (NT_STATUS_IS_ERR(smbcli_setatr(cli1->tree, fname, FILE_ATTRIBUTE_READONLY, 0))) {
		printf("smbcli_setatr failed (%s)\n", smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test1);
		return False;
	}
	
	fnum1 = smbcli_open(cli1->tree, fname, O_RDONLY, DENY_WRITE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test1);
		return False;
	}
	
	/* This will fail - but the error should be ERRnoaccess, not ERRbadshare. */
	fnum2 = smbcli_open(cli1->tree, fname, O_RDWR, DENY_ALL);
	
        if (check_error(__location__, cli1, ERRDOS, ERRnoaccess, 
			NT_STATUS_ACCESS_DENIED)) {
		printf("correct error code ERRDOS/ERRnoaccess returned\n");
	}
	
	printf("finished open test 1\n");
error_test1:
	smbcli_close(cli1->tree, fnum1);
	
	/* Now try not readonly and ensure ERRbadshare is returned. */
	
	smbcli_setatr(cli1->tree, fname, 0, 0);
	
	fnum1 = smbcli_open(cli1->tree, fname, O_RDONLY, DENY_WRITE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}
	
	/* This will fail - but the error should be ERRshare. */
	fnum2 = smbcli_open(cli1->tree, fname, O_RDWR, DENY_ALL);
	
	if (check_error(__location__, cli1, ERRDOS, ERRbadshare, 
			NT_STATUS_SHARING_VIOLATION)) {
		printf("correct error code ERRDOS/ERRbadshare returned\n");
	}
	
	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("close2 failed (%s)\n", smbcli_errstr(cli1->tree));
		return False;
	}
	
	smbcli_unlink(cli1->tree, fname);
	
	printf("finished open test 2\n");
	
	/* Test truncate open disposition on file opened for read. */
	
	fnum1 = smbcli_open(cli1->tree, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("(3) open (1) of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}
	
	/* write 20 bytes. */
	
	memset(buf, '\0', 20);

	if (smbcli_write(cli1->tree, fnum1, 0, buf, 0, 20) != 20) {
		printf("write failed (%s)\n", smbcli_errstr(cli1->tree));
		correct = False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("(3) close1 failed (%s)\n", smbcli_errstr(cli1->tree));
		return False;
	}
	
	/* Ensure size == 20. */
	if (NT_STATUS_IS_ERR(smbcli_getatr(cli1->tree, fname, NULL, &fsize, NULL))) {
		printf("(3) getatr failed (%s)\n", smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test3);
		return False;
	}
	
	if (fsize != 20) {
		printf("(3) file size != 20\n");
		CHECK_MAX_FAILURES(error_test3);
		return False;
	}

	/* Now test if we can truncate a file opened for readonly. */
	
	fnum1 = smbcli_open(cli1->tree, fname, O_RDONLY|O_TRUNC, DENY_NONE);
	if (fnum1 == -1) {
		printf("(3) open (2) of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test3);
		return False;
	}
	
	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("close2 failed (%s)\n", smbcli_errstr(cli1->tree));
		return False;
	}

	/* Ensure size == 0. */
	if (NT_STATUS_IS_ERR(smbcli_getatr(cli1->tree, fname, NULL, &fsize, NULL))) {
		printf("(3) getatr failed (%s)\n", smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test3);
		return False;
	}

	if (fsize != 0) {
		printf("(3) file size != 0\n");
		CHECK_MAX_FAILURES(error_test3);
		return False;
	}
	printf("finished open test 3\n");
error_test3:	
	smbcli_unlink(cli1->tree, fname);


	printf("testing ctemp\n");
	fnum1 = smbcli_ctemp(cli1->tree, "\\", &tmp_path);
	if (fnum1 == -1) {
		printf("ctemp failed (%s)\n", smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test4);
		return False;
	}
	printf("ctemp gave path %s\n", tmp_path);
	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("close of temp failed (%s)\n", smbcli_errstr(cli1->tree));
	}
	if (NT_STATUS_IS_ERR(smbcli_unlink(cli1->tree, tmp_path))) {
		printf("unlink of temp failed (%s)\n", smbcli_errstr(cli1->tree));
	}
error_test4:	
	/* Test the non-io opens... */

	if (!torture_open_connection(&cli2)) {
		return False;
	}
	
	smbcli_setatr(cli2->tree, fname, 0, 0);
	smbcli_unlink(cli2->tree, fname);
	
	printf("TEST #1 testing 2 non-io opens (no delete)\n");
	
	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0, SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);

	if (fnum1 == -1) {
		printf("test 1 open 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test10);
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli2->tree, fname, 0, SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OPEN_IF, 0, 0);
	if (fnum2 == -1) {
		printf("test 1 open 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		CHECK_MAX_FAILURES(error_test10);
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("test 1 close 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}
	if (NT_STATUS_IS_ERR(smbcli_close(cli2->tree, fnum2))) {
		printf("test 1 close 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		return False;
	}

	printf("non-io open test #1 passed.\n");
error_test10:
	smbcli_unlink(cli1->tree, fname);

	printf("TEST #2 testing 2 non-io opens (first with delete)\n");
	
	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0, SEC_STD_DELETE|SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);

	if (fnum1 == -1) {
		printf("test 2 open 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test20);
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli2->tree, fname, 0, SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OPEN_IF, 0, 0);

	if (fnum2 == -1) {
		printf("test 2 open 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		CHECK_MAX_FAILURES(error_test20);
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("test 1 close 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}
	if (NT_STATUS_IS_ERR(smbcli_close(cli2->tree, fnum2))) {
		printf("test 1 close 2 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	printf("non-io open test #2 passed.\n");
error_test20:
	smbcli_unlink(cli1->tree, fname);

	printf("TEST #3 testing 2 non-io opens (second with delete)\n");
	
	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0, SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);

	if (fnum1 == -1) {
		printf("test 3 open 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test30);
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli2->tree, fname, 0, SEC_STD_DELETE|SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OPEN_IF, 0, 0);

	if (fnum2 == -1) {
		printf("test 3 open 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		CHECK_MAX_FAILURES(error_test30);
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("test 3 close 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}
	if (NT_STATUS_IS_ERR(smbcli_close(cli2->tree, fnum2))) {
		printf("test 3 close 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		return False;
	}

	printf("non-io open test #3 passed.\n");
error_test30:
	smbcli_unlink(cli1->tree, fname);

	printf("TEST #4 testing 2 non-io opens (both with delete)\n");
	
	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0, SEC_STD_DELETE|SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);

	if (fnum1 == -1) {
		printf("test 4 open 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test40);
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli2->tree, fname, 0, SEC_STD_DELETE|SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OPEN_IF, 0, 0);

	if (fnum2 != -1) {
		printf("test 4 open 2 of %s SUCCEEDED - should have failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		CHECK_MAX_FAILURES(error_test40);
		return False;
	}

	printf("test 4 open 2 of %s gave %s (correct error should be %s)\n", fname, smbcli_errstr(cli2->tree), "sharing violation");

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("test 4 close 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	printf("non-io open test #4 passed.\n");
error_test40:
	smbcli_unlink(cli1->tree, fname);

	printf("TEST #5 testing 2 non-io opens (both with delete - both with file share delete)\n");
	
	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0, SEC_STD_DELETE|SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_DELETE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);

	if (fnum1 == -1) {
		printf("test 5 open 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test50);
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli2->tree, fname, 0, SEC_STD_DELETE|SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_DELETE, NTCREATEX_DISP_OPEN_IF, 0, 0);

	if (fnum2 == -1) {
		printf("test 5 open 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		CHECK_MAX_FAILURES(error_test50);
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("test 5 close 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli2->tree, fnum2))) {
		printf("test 5 close 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		return False;
	}

	printf("non-io open test #5 passed.\n");
error_test50:
	printf("TEST #6 testing 1 non-io open, one io open\n");
	
	smbcli_unlink(cli1->tree, fname);

	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0, SEC_FILE_READ_DATA, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);

	if (fnum1 == -1) {
		printf("test 6 open 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test60);
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli2->tree, fname, 0, SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_READ, NTCREATEX_DISP_OPEN_IF, 0, 0);

	if (fnum2 == -1) {
		printf("test 6 open 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		CHECK_MAX_FAILURES(error_test60);
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("test 6 close 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli2->tree, fnum2))) {
		printf("test 6 close 2 of %s failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		return False;
	}

	printf("non-io open test #6 passed.\n");
error_test60:
	printf("TEST #7 testing 1 non-io open, one io open with delete\n");

	smbcli_unlink(cli1->tree, fname);

	fnum1 = smbcli_nt_create_full(cli1->tree, fname, 0, SEC_FILE_READ_DATA, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_NONE, NTCREATEX_DISP_OVERWRITE_IF, 0, 0);

	if (fnum1 == -1) {
		printf("test 7 open 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test70);
		return False;
	}

	fnum2 = smbcli_nt_create_full(cli2->tree, fname, 0, SEC_STD_DELETE|SEC_FILE_READ_ATTRIBUTE, FILE_ATTRIBUTE_NORMAL,
				   NTCREATEX_SHARE_ACCESS_READ|NTCREATEX_SHARE_ACCESS_DELETE, NTCREATEX_DISP_OPEN_IF, 0, 0);

	if (fnum2 != -1) {
		printf("test 7 open 2 of %s SUCCEEDED - should have failed (%s)\n", fname, smbcli_errstr(cli2->tree));
		CHECK_MAX_FAILURES(error_test70);
		return False;
	}

	printf("test 7 open 2 of %s gave %s (correct error should be %s)\n", fname, smbcli_errstr(cli2->tree), "sharing violation");

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("test 7 close 1 of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	printf("non-io open test #7 passed.\n");

error_test70:

	printf("TEST #8 testing one normal open, followed by lock, followed by open with truncate\n");

	smbcli_unlink(cli1->tree, fname);

	fnum1 = smbcli_open(cli1->tree, fname, O_RDWR|O_CREAT, DENY_NONE);
	if (fnum1 == -1) {
		printf("(8) open (1) of %s failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}
	
	/* write 20 bytes. */
	
	memset(buf, '\0', 20);

	if (smbcli_write(cli1->tree, fnum1, 0, buf, 0, 20) != 20) {
		printf("(8) write failed (%s)\n", smbcli_errstr(cli1->tree));
		correct = False;
	}

	/* Ensure size == 20. */
	if (NT_STATUS_IS_ERR(smbcli_getatr(cli1->tree, fname, NULL, &fsize, NULL))) {
		printf("(8) getatr (1) failed (%s)\n", smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test80);
		return False;
	}
	
	if (fsize != 20) {
		printf("(8) file size != 20\n");
		CHECK_MAX_FAILURES(error_test80);
		return False;
	}

	/* Get an exclusive lock on the open file. */
	if (NT_STATUS_IS_ERR(smbcli_lock(cli1->tree, fnum1, 0, 4, 0, WRITE_LOCK))) {
		printf("(8) lock1 failed (%s)\n", smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test80);
		return False;
	}

	fnum2 = smbcli_open(cli1->tree, fname, O_RDWR|O_TRUNC, DENY_NONE);
	if (fnum1 == -1) {
		printf("(8) open (2) of %s with truncate failed (%s)\n", fname, smbcli_errstr(cli1->tree));
		return False;
	}

	/* Ensure size == 0. */
	if (NT_STATUS_IS_ERR(smbcli_getatr(cli1->tree, fname, NULL, &fsize, NULL))) {
		printf("(8) getatr (2) failed (%s)\n", smbcli_errstr(cli1->tree));
		CHECK_MAX_FAILURES(error_test80);
		return False;
	}
	
	if (fsize != 0) {
		printf("(8) file size != 0\n");
		CHECK_MAX_FAILURES(error_test80);
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum1))) {
		printf("(8) close1 failed (%s)\n", smbcli_errstr(cli1->tree));
		return False;
	}
	
	if (NT_STATUS_IS_ERR(smbcli_close(cli1->tree, fnum2))) {
		printf("(8) close1 failed (%s)\n", smbcli_errstr(cli1->tree));
		return False;
	}
	
error_test80:

	printf("open test #8 passed.\n");

	smbcli_unlink(cli1->tree, fname);

	if (!torture_close_connection(cli1)) {
		correct = False;
	}
	if (!torture_close_connection(cli2)) {
		correct = False;
	}
	
	return correct;
}


/*
  sees what IOCTLs are supported
 */
BOOL torture_ioctl_test(void)
{
	struct smbcli_state *cli;
	uint16_t device, function;
	int fnum;
	const char *fname = "\\ioctl.dat";
	NTSTATUS status;
	union smb_ioctl parms;
	TALLOC_CTX *mem_ctx;

	if (!torture_open_connection(&cli)) {
		return False;
	}

	mem_ctx = talloc_init("ioctl_test");

	printf("starting ioctl test\n");

	smbcli_unlink(cli->tree, fname);

	fnum = smbcli_open(cli->tree, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum == -1) {
		printf("open of %s failed (%s)\n", fname, smbcli_errstr(cli->tree));
		return False;
	}

	parms.ioctl.level = RAW_IOCTL_IOCTL;
	parms.ioctl.in.fnum = fnum;
	parms.ioctl.in.request = IOCTL_QUERY_JOB_INFO;
	status = smb_raw_ioctl(cli->tree, mem_ctx, &parms);
	printf("ioctl job info: %s\n", smbcli_errstr(cli->tree));

	for (device=0;device<0x100;device++) {
		printf("testing device=0x%x\n", device);
		for (function=0;function<0x100;function++) {
			parms.ioctl.in.request = (device << 16) | function;
			status = smb_raw_ioctl(cli->tree, mem_ctx, &parms);

			if (NT_STATUS_IS_OK(status)) {
				printf("ioctl device=0x%x function=0x%x OK : %d bytes\n", 
					device, function, (int)parms.ioctl.out.blob.length);
			}
		}
	}

	if (!torture_close_connection(cli)) {
		return False;
	}

	return True;
}


/*
  tries variants of chkpath
 */
BOOL torture_chkpath_test(void)
{
	struct smbcli_state *cli;
	int fnum;
	BOOL ret;

	if (!torture_open_connection(&cli)) {
		return False;
	}

	printf("starting chkpath test\n");

	printf("Testing valid and invalid paths\n");

	/* cleanup from an old run */
	smbcli_rmdir(cli->tree, "\\chkpath.dir\\dir2");
	smbcli_unlink(cli->tree, "\\chkpath.dir\\*");
	smbcli_rmdir(cli->tree, "\\chkpath.dir");

	if (NT_STATUS_IS_ERR(smbcli_mkdir(cli->tree, "\\chkpath.dir"))) {
		printf("mkdir1 failed : %s\n", smbcli_errstr(cli->tree));
		return False;
	}

	if (NT_STATUS_IS_ERR(smbcli_mkdir(cli->tree, "\\chkpath.dir\\dir2"))) {
		printf("mkdir2 failed : %s\n", smbcli_errstr(cli->tree));
		return False;
	}

	fnum = smbcli_open(cli->tree, "\\chkpath.dir\\foo.txt", O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum == -1) {
		printf("open1 failed (%s)\n", smbcli_errstr(cli->tree));
		return False;
	}
	smbcli_close(cli->tree, fnum);

	if (NT_STATUS_IS_ERR(smbcli_chkpath(cli->tree, "\\chkpath.dir"))) {
		printf("chkpath1 failed: %s\n", smbcli_errstr(cli->tree));
		ret = False;
	}

	if (NT_STATUS_IS_ERR(smbcli_chkpath(cli->tree, "\\chkpath.dir\\dir2"))) {
		printf("chkpath2 failed: %s\n", smbcli_errstr(cli->tree));
		ret = False;
	}

	if (NT_STATUS_IS_ERR(smbcli_chkpath(cli->tree, "\\chkpath.dir\\foo.txt"))) {
		ret = check_error(__location__, cli, ERRDOS, ERRbadpath, 
				  NT_STATUS_NOT_A_DIRECTORY);
	} else {
		printf("* chkpath on a file should fail\n");
		ret = False;
	}

	if (NT_STATUS_IS_ERR(smbcli_chkpath(cli->tree, "\\chkpath.dir\\bar.txt"))) {
		ret = check_error(__location__, cli, ERRDOS, ERRbadpath, 
				  NT_STATUS_OBJECT_NAME_NOT_FOUND);
	} else {
		printf("* chkpath on a non existent file should fail\n");
		ret = False;
	}

	if (NT_STATUS_IS_ERR(smbcli_chkpath(cli->tree, "\\chkpath.dir\\dirxx\\bar.txt"))) {
		ret = check_error(__location__, cli, ERRDOS, ERRbadpath, 
				  NT_STATUS_OBJECT_PATH_NOT_FOUND);
	} else {
		printf("* chkpath on a non existent component should fail\n");
		ret = False;
	}

	smbcli_rmdir(cli->tree, "\\chkpath.dir\\dir2");
	smbcli_unlink(cli->tree, "\\chkpath.dir\\*");
	smbcli_rmdir(cli->tree, "\\chkpath.dir");

	if (!torture_close_connection(cli)) {
		return False;
	}

	return ret;
}


static void sigcont(int sig)
{
}

double torture_create_procs(BOOL (*fn)(struct smbcli_state *, int), BOOL *result)
{
	int i, status;
	volatile pid_t *child_status;
	volatile BOOL *child_status_out;
	int synccount;
	int tries = 8;
	double start_time_limit = 10 + (torture_nprocs * 1.5);
	char **unc_list = NULL;
	const char *p;
	int num_unc_names = 0;
	struct timeval tv;

	*result = True;

	synccount = 0;

	signal(SIGCONT, sigcont);

	child_status = (volatile pid_t *)shm_setup(sizeof(pid_t)*torture_nprocs);
	if (!child_status) {
		printf("Failed to setup shared memory\n");
		return -1;
	}

	child_status_out = (volatile BOOL *)shm_setup(sizeof(BOOL)*torture_nprocs);
	if (!child_status_out) {
		printf("Failed to setup result status shared memory\n");
		return -1;
	}

	p = lp_parm_string(-1, "torture", "unclist");
	if (p) {
		unc_list = file_lines_load(p, &num_unc_names, NULL);
		if (!unc_list || num_unc_names <= 0) {
			printf("Failed to load unc names list from '%s'\n", p);
			exit(1);
		}
	}

	for (i = 0; i < torture_nprocs; i++) {
		child_status[i] = 0;
		child_status_out[i] = True;
	}

	tv = timeval_current();

	for (i=0;i<torture_nprocs;i++) {
		procnum = i;
		if (fork() == 0) {
			char *myname;
			const char *hostname=NULL, *sharename;

			pid_t mypid = getpid();
			srandom(((int)mypid) ^ ((int)time(NULL)));

			asprintf(&myname, "CLIENT%d", i);
			lp_set_cmdline("netbios name", myname);
			free(myname);


			if (unc_list) {
				if (!smbcli_parse_unc(unc_list[i % num_unc_names],
						      NULL, &hostname, &sharename)) {
					printf("Failed to parse UNC name %s\n",
					       unc_list[i % num_unc_names]);
					exit(1);
				}
			}

			while (1) {
				if (hostname) {
					if (torture_open_connection_share(&current_cli,
									  hostname, 
									  sharename)) {
						break;
					}
				} else if (torture_open_connection(&current_cli)) {
						break;
				}
				if (tries-- == 0) {
					printf("pid %d failed to start\n", (int)getpid());
					_exit(1);
				}
				msleep(100);	
			}

			child_status[i] = getpid();

			pause();

			if (child_status[i]) {
				printf("Child %d failed to start!\n", i);
				child_status_out[i] = 1;
				_exit(1);
			}

			child_status_out[i] = fn(current_cli, i);
			_exit(0);
		}
	}

	do {
		synccount = 0;
		for (i=0;i<torture_nprocs;i++) {
			if (child_status[i]) synccount++;
		}
		if (synccount == torture_nprocs) break;
		msleep(100);
	} while (timeval_elapsed(&tv) < start_time_limit);

	if (synccount != torture_nprocs) {
		printf("FAILED TO START %d CLIENTS (started %d)\n", torture_nprocs, synccount);
		*result = False;
		return timeval_elapsed(&tv);
	}

	printf("Starting %d clients\n", torture_nprocs);

	/* start the client load */
	tv = timeval_current();
	for (i=0;i<torture_nprocs;i++) {
		child_status[i] = 0;
	}

	printf("%d clients started\n", torture_nprocs);

	kill(0, SIGCONT);

	for (i=0;i<torture_nprocs;i++) {
		int ret;
		while ((ret=sys_waitpid(0, &status, 0)) == -1 && errno == EINTR) /* noop */ ;
		if (ret == -1 || WEXITSTATUS(status) != 0) {
			*result = False;
		}
	}

	printf("\n");
	
	for (i=0;i<torture_nprocs;i++) {
		if (!child_status_out[i]) {
			*result = False;
		}
	}
	return timeval_elapsed(&tv);
}

#define FLAG_MULTIPROC 1

static struct {
	const char *name;
	BOOL (*fn)(void);
	BOOL (*multi_fn)(struct smbcli_state *, int );
} torture_ops[] = {
	/* base tests */
	{"BASE-FDPASS", run_fdpasstest, 0},
	{"BASE-LOCK1",  torture_locktest1,  0},
	{"BASE-LOCK2",  torture_locktest2,  0},
	{"BASE-LOCK3",  torture_locktest3,  0},
	{"BASE-LOCK4",  torture_locktest4,  0},
	{"BASE-LOCK5",  torture_locktest5,  0},
	{"BASE-LOCK6",  torture_locktest6,  0},
	{"BASE-LOCK7",  torture_locktest7,  0},
	{"BASE-UNLINK", torture_unlinktest, 0},
	{"BASE-ATTR",   run_attrtest,   0},
	{"BASE-TRANS2", run_trans2test, 0},
	{"BASE-NEGNOWAIT", run_negprot_nowait, 0},
	{"BASE-DIR1",  torture_dirtest1, 0},
	{"BASE-DIR2",  torture_dirtest2, 0},
	{"BASE-DENY1",  torture_denytest1, 0},
	{"BASE-DENY2",  torture_denytest2, 0},
	{"BASE-DENY3",  torture_denytest3, 0},
	{"BASE-DENYDOS",  torture_denydos_sharing, 0},
	{"BASE-NTDENY1",  NULL, torture_ntdenytest1},
	{"BASE-NTDENY2",  torture_ntdenytest2, 0},
	{"BASE-TCON",  run_tcon_test, 0},
	{"BASE-TCONDEV",  run_tcon_devtype_test, 0},
	{"BASE-VUID", run_vuidtest, 0},
	{"BASE-RW1",  run_readwritetest, 0},
	{"BASE-OPEN", run_opentest, 0},
	{"BASE-DEFER_OPEN", NULL, run_deferopen},
	{"BASE-XCOPY", run_xcopy, 0},
	{"BASE-RENAME", torture_test_rename, 0},
	{"BASE-DELETE", torture_test_delete, 0},
	{"BASE-PROPERTIES", torture_test_properties, 0},
	{"BASE-MANGLE", torture_mangle, 0},
	{"BASE-OPENATTR", torture_openattrtest, 0},
	{"BASE-CHARSET", torture_charset, 0},
	{"BASE-CHKPATH",  torture_chkpath_test, 0},
	{"BASE-SECLEAK",  torture_sec_leak, 0},
	{"BASE-DISCONNECT",  torture_disconnect, 0},
	{"BASE-DELAYWRITE", torture_delay_write, 0},

	/* benchmarking tests */
	{"BENCH-HOLDCON",  torture_holdcon, 0},
	{"BENCH-NBENCH",  torture_nbench, 0},
	{"BENCH-TORTURE", NULL, run_torture},
	{"BENCH-NBT",     torture_bench_nbt, 0},
	{"BENCH-WINS",    torture_bench_wins, 0},
	{"BENCH-RPC",     torture_bench_rpc, 0},
	{"BENCH-CLDAP",   torture_bench_cldap, 0},

	/* RAW smb tests */
	{"RAW-QFSINFO", torture_raw_qfsinfo, 0},
	{"RAW-QFILEINFO", torture_raw_qfileinfo, 0},
	{"RAW-SFILEINFO", torture_raw_sfileinfo, 0},
	{"RAW-SFILEINFO-BUG", torture_raw_sfileinfo_bug, 0},
	{"RAW-SEARCH", torture_raw_search, 0},
	{"RAW-CLOSE", torture_raw_close, 0},
	{"RAW-OPEN", torture_raw_open, 0},
	{"RAW-MKDIR", torture_raw_mkdir, 0},
	{"RAW-OPLOCK", torture_raw_oplock, 0},
	{"RAW-NOTIFY", torture_raw_notify, 0},
	{"RAW-MUX", torture_raw_mux, 0},
	{"RAW-IOCTL", torture_raw_ioctl, 0},
	{"RAW-CHKPATH", torture_raw_chkpath, 0},
	{"RAW-UNLINK", torture_raw_unlink, 0},
	{"RAW-READ", torture_raw_read, 0},
	{"RAW-WRITE", torture_raw_write, 0},
	{"RAW-LOCK", torture_raw_lock, 0},
	{"RAW-CONTEXT", torture_raw_context, 0},
	{"RAW-RENAME", torture_raw_rename, 0},
	{"RAW-SEEK", torture_raw_seek, 0},
	{"RAW-EAS", torture_raw_eas, 0},
	{"RAW-EAMAX", torture_max_eas, 0},
	{"RAW-STREAMS", torture_raw_streams, 0},
	{"RAW-ACLS", torture_raw_acls, 0},
	{"RAW-RAP", torture_raw_rap, 0},
	{"RAW-COMPOSITE", torture_raw_composite, 0},

	/* protocol scanners */
	{"SCAN-TRANS2", torture_trans2_scan, 0},
	{"SCAN-NTTRANS", torture_nttrans_scan, 0},
	{"SCAN-ALIASES", torture_trans2_aliases, 0},
	{"SCAN-SMB", torture_smb_scan, 0},
	{"SCAN-MAXFID", NULL, run_maxfidtest},
	{"SCAN-UTABLE", torture_utable, 0},
	{"SCAN-CASETABLE", torture_casetable, 0},
	{"SCAN-PIPE_NUMBER", run_pipe_number, 0},
	{"SCAN-IOCTL",  torture_ioctl_test, 0},
	{"SCAN-RAP",  torture_rap_scan, 0},

	/* rpc testers */
        {"RPC-LSA", torture_rpc_lsa, 0},
        {"RPC-SECRETS", torture_rpc_lsa_secrets, 0},
        {"RPC-ECHO", torture_rpc_echo, 0},
        {"RPC-DFS", torture_rpc_dfs, 0},
        {"RPC-SPOOLSS", torture_rpc_spoolss, 0},
        {"RPC-SAMR", torture_rpc_samr, 0},
        {"RPC-UNIXINFO", torture_rpc_unixinfo, 0},
        {"RPC-NETLOGON", torture_rpc_netlogon, 0},
        {"RPC-SAMLOGON", torture_rpc_samlogon, 0},
        {"RPC-SAMSYNC", torture_rpc_samsync, 0},
        {"RPC-SCHANNEL", torture_rpc_schannel, 0},
        {"RPC-WKSSVC", torture_rpc_wkssvc, 0},
        {"RPC-SRVSVC", torture_rpc_srvsvc, 0},
        {"RPC-SVCCTL", torture_rpc_svcctl, 0},
        {"RPC-ATSVC", torture_rpc_atsvc, 0},
        {"RPC-EVENTLOG", torture_rpc_eventlog, 0},
        {"RPC-EPMAPPER", torture_rpc_epmapper, 0},
        {"RPC-WINREG", torture_rpc_winreg, 0},
        {"RPC-INITSHUTDOWN", torture_rpc_initshutdown, 0},
        {"RPC-OXIDRESOLVE", torture_rpc_oxidresolve, 0},
        {"RPC-REMACT", torture_rpc_remact, 0},
        {"RPC-MGMT", torture_rpc_mgmt, 0},
        {"RPC-SCANNER", torture_rpc_scanner, 0},
        {"RPC-AUTOIDL", torture_rpc_autoidl, 0},
        {"RPC-COUNTCALLS", torture_rpc_countcalls, 0},
	{"RPC-MULTIBIND", torture_multi_bind, 0},
	{"RPC-DRSUAPI", torture_rpc_drsuapi, 0},
	{"RPC-LOGIN", torture_rpc_login, 0},
	{"RPC-ROT", torture_rpc_rot, 0},
	{"RPC-DSSETUP", torture_rpc_dssetup, 0},
        {"RPC-ALTERCONTEXT", torture_rpc_alter_context, 0},

	/* local (no server) testers */
	{"LOCAL-NTLMSSP", torture_ntlmssp_self_check, 0},
	{"LOCAL-ICONV", torture_local_iconv, 0},
	{"LOCAL-TALLOC", torture_local_talloc, 0},
	{"LOCAL-MESSAGING", torture_local_messaging, 0},
	{"LOCAL-IRPC",  torture_local_irpc, 0},
	{"LOCAL-BINDING", torture_local_binding_string, 0},
	{"LOCAL-IDTREE", torture_local_idtree, 0},
	{"LOCAL-SOCKET", torture_local_socket, 0},
	{"LOCAL-PAC", torture_pac, 0},

	/* COM (Component Object Model) testers */
	{"COM-SIMPLE", torture_com_simple, 0 },

	/* ldap testers */
	{"LDAP-BASIC", torture_ldap_basic, 0},
	{"LDAP-CLDAP", torture_cldap, 0},

	/* nbt tests */
	{"NBT-REGISTER", torture_nbt_register, 0},
	{"NBT-WINS", torture_nbt_wins, 0},
	{"NBT-WINSREPLICATION", torture_nbt_winsreplication, 0},
	{"NBT-DGRAM", torture_nbt_dgram, 0},
	
	/* libnet tests */
	{"NET-USERINFO", torture_userinfo, 0},
	{"NET-USERADD", torture_useradd, 0},
	{"NET-USERDEL", torture_userdel, 0},
	{"NET-USERMOD", torture_usermod, 0},
	{"NET-DOMOPEN", torture_domainopen, 0},
	{"NET-API-LOOKUP", torture_lookup, 0},
	{"NET-API-LOOKUPHOST", torture_lookup_host, 0},
	{"NET-API-LOOKUPPDC", torture_lookup_pdc, 0},
	{"NET-API-CREATEUSER", torture_createuser, 0},
	{"NET-API-RPCCONNECT", torture_rpc_connect, 0},

	{NULL, NULL, 0}};



/****************************************************************************
run a specified test or "ALL"
****************************************************************************/
static BOOL run_test(const char *name)
{
	BOOL ret = True;
	int i;
	BOOL matched = False;

	if (strequal(name,"ALL")) {
		for (i=0;torture_ops[i].name;i++) {
			if (!run_test(torture_ops[i].name)) {
				ret = False;
			}
		}
		return ret;
	}

	for (i=0;torture_ops[i].name;i++) {
		if (gen_fnmatch(name, torture_ops[i].name) == 0) {
			double t;
			matched = True;
			init_iconv();
			printf("Running %s\n", torture_ops[i].name);
			if (torture_ops[i].multi_fn) {
				BOOL result = False;
				t = torture_create_procs(torture_ops[i].multi_fn, 
							 &result);
				if (!result) { 
					ret = False;
					printf("TEST %s FAILED!\n", torture_ops[i].name);
				}
					 
			} else {
				struct timeval tv = timeval_current();
				if (!torture_ops[i].fn()) {
					ret = False;
					printf("TEST %s FAILED!\n", torture_ops[i].name);
				}
				t = timeval_elapsed(&tv);
			}
			printf("%s took %g secs\n\n", torture_ops[i].name, t);
		}
	}

	if (!matched) {
		printf("Unknown torture operation '%s'\n", name);
	}

	return ret;
}


static void parse_dns(const char *dns)
{
	char *userdn, *basedn, *secret;
	char *p, *d;

	/* retrievieng the userdn */
	p = strchr_m(dns, '#');
	if (!p) {
		lp_set_cmdline("torture:ldap_userdn", "");
		lp_set_cmdline("torture:ldap_basedn", "");
		lp_set_cmdline("torture:ldap_secret", "");
		return;
	}
	userdn = strndup(dns, p - dns);
	lp_set_cmdline("torture:ldap_userdn", userdn);

	/* retrieve the basedn */
	d = p + 1;
	p = strchr_m(d, '#');
	if (!p) {
		lp_set_cmdline("torture:ldap_basedn", "");
		lp_set_cmdline("torture:ldap_secret", "");
		return;
	}
	basedn = strndup(d, p - d);
	lp_set_cmdline("torture:ldap_basedn", basedn);

	/* retrieve the secret */
	p = p + 1;
	if (!p) {
		lp_set_cmdline("torture:ldap_secret", "");
		return;
	}
	secret = strdup(p);
	lp_set_cmdline("torture:ldap_secret", secret);

	printf ("%s - %s - %s\n", userdn, basedn, secret);

}

static void usage(poptContext pc)
{
	int i;
	int perline = 5;

	poptPrintUsage(pc, stdout, 0);
	printf("\n");

	printf("The binding format is:\n\n");

	printf("  TRANSPORT:host[flags]\n\n");

	printf("  where TRANSPORT is either ncacn_np for SMB or ncacn_ip_tcp for RPC/TCP\n\n");

	printf("  'host' is an IP or hostname or netbios name. If the binding string\n");
	printf("  identifies the server side of an endpoint, 'host' may be an empty\n");
	printf("  string.\n\n");

	printf("  'flags' can include a SMB pipe name if using the ncacn_np transport or\n");
	printf("  a TCP port number if using the ncacn_ip_tcp transport, otherwise they\n");
	printf("  will be auto-determined.\n\n");

	printf("  other recognised flags are:\n\n");

	printf("    sign : enable ntlmssp signing\n");
	printf("    seal : enable ntlmssp sealing\n");
	printf("    connect : enable rpc connect level auth (auth, but no sign or seal)\n");
	printf("    validate: enable the NDR validator\n");
	printf("    print: enable debugging of the packets\n");
	printf("    bigendian: use bigendian RPC\n");
	printf("    padcheck: check reply data for non-zero pad bytes\n\n");

	printf("  For example, these all connect to the samr pipe:\n\n");

	printf("    ncacn_np:myserver\n");
	printf("    ncacn_np:myserver[samr]\n");
	printf("    ncacn_np:myserver[\\pipe\\samr]\n");
	printf("    ncacn_np:myserver[/pipe/samr]\n");
	printf("    ncacn_np:myserver[samr,sign,print]\n");
	printf("    ncacn_np:myserver[\\pipe\\samr,sign,seal,bigendian]\n");
	printf("    ncacn_np:myserver[/pipe/samr,seal,validate]\n");
	printf("    ncacn_np:\n");
	printf("    ncacn_np:[/pipe/samr]\n\n");

	printf("    ncacn_ip_tcp:myserver\n");
	printf("    ncacn_ip_tcp:myserver[1024]\n");
	printf("    ncacn_ip_tcp:myserver[1024,sign,seal]\n\n");

	printf("The unc format is:\n\n");

	printf("    //server/share\n\n");

	printf("tests are:");
	for (i=0;torture_ops[i].name;i++) {
		if ((i%perline)==0) {
			printf("\n");
		}
		printf("%s ", torture_ops[i].name);
	}
	printf("\n\n");

	printf("default test is ALL\n");

	exit(1);
}

static BOOL is_binding_string(const char *binding_string)
{
	TALLOC_CTX *mem_ctx = talloc_init("is_binding_string");
	struct dcerpc_binding *binding_struct;
	NTSTATUS status;
	
	status = dcerpc_parse_binding(mem_ctx, binding_string, &binding_struct);

	talloc_free(mem_ctx);
	return NT_STATUS_IS_OK(status);
}

static void max_runtime_handler(int sig)
{
	DEBUG(0,("maximum runtime exceeded for smbtorture - terminating\n"));
	exit(1);
}

/****************************************************************************
  main program
****************************************************************************/
 int main(int argc,char *argv[])
{
	int opt, i;
	char *p;
	BOOL correct = True;
	int max_runtime=0;
	int argc_new;
	char **argv_new;
	poptContext pc;
	enum {OPT_LOADFILE=1000,OPT_UNCLIST,OPT_TIMELIMIT,OPT_DNS,OPT_DANGEROUS};
	struct poptOption long_options[] = {
		POPT_AUTOHELP
		{"smb-ports",	'p', POPT_ARG_STRING, NULL, 		0,	"SMB ports", 	NULL},
		{"seed",	  0, POPT_ARG_INT,  &torture_seed, 	0,	"seed", 	NULL},
		{"num-progs",	  0, POPT_ARG_INT,  &torture_nprocs, 	0,	"num progs",	NULL},
		{"num-ops",	  0, POPT_ARG_INT,  &torture_numops, 	0, 	"num ops",	NULL},
		{"entries",	  0, POPT_ARG_INT,  &torture_entries, 	0,	"entries",	NULL},
		{"use-oplocks",	'L', POPT_ARG_NONE, &use_oplocks, 	0,	"use oplocks", 	NULL},
		{"show-all",	  0, POPT_ARG_NONE, &torture_showall, 	0,	"show all", 	NULL},
		{"loadfile",	  0, POPT_ARG_STRING,	NULL, 	OPT_LOADFILE,	"loadfile", 	NULL},
		{"unclist",	  0, POPT_ARG_STRING,	NULL, 	OPT_UNCLIST,	"unclist", 	NULL},
		{"timelimit",	't', POPT_ARG_STRING,	NULL, 	OPT_TIMELIMIT,	"timelimit", 	NULL},
		{"failures",	'f', POPT_ARG_INT,  &torture_failures, 	0,	"failures", 	NULL},
		{"parse-dns",	'D', POPT_ARG_STRING,	NULL, 	OPT_DNS,	"parse-dns", 	NULL},
		{"dangerous",	'X', POPT_ARG_NONE,	NULL,   OPT_DANGEROUS,	"dangerous", 	NULL},
		{"maximum-runtime", 0, POPT_ARG_INT, &max_runtime, 0, 
		 "set maximum time for smbtorture to live", "seconds"},
		POPT_COMMON_SAMBA
		POPT_COMMON_CONNECTION
		POPT_COMMON_CREDENTIALS
		POPT_COMMON_VERSION
		POPT_TABLEEND
	};

#ifdef HAVE_SETBUFFER
	setbuffer(stdout, NULL, 0);
#endif

	pc = poptGetContext("smbtorture", argc, (const char **) argv, long_options, 
			    POPT_CONTEXT_KEEP_FIRST);

	poptSetOtherOptionHelp(pc, "<binding>|<unc> TEST1 TEST2 ...");

	while((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
		case OPT_LOADFILE:
			lp_set_cmdline("torture:loadfile", poptGetOptArg(pc));
			break;
		case OPT_UNCLIST:
			lp_set_cmdline("torture:unclist", poptGetOptArg(pc));
			break;
		case OPT_TIMELIMIT:
			lp_set_cmdline("torture:timelimit", poptGetOptArg(pc));
			break;
		case OPT_DNS:
			parse_dns(poptGetOptArg(pc));
			break;
		case OPT_DANGEROUS:
			lp_set_cmdline("torture:dangerous", "Yes");
			break;
		default:
			d_printf("Invalid option %s: %s\n", 
				 poptBadOption(pc, 0), poptStrerror(opt));
			usage(pc);
			exit(1);
		}
	}

	if (max_runtime) {
		/* this will only work if nobody else uses alarm(),
		   which means it won't work for some tests, but we
		   can't use the event context method we use for smbd
		   as so many tests create their own event
		   context. This will at least catch most cases. */
		signal(SIGALRM, max_runtime_handler);
		alarm(max_runtime);
	}

	smbtorture_init_subsystems;


	if (torture_seed == 0) {
		torture_seed = time(NULL);
	} 
	printf("Using seed %d\n", torture_seed);
	srandom(torture_seed);

	argv_new = discard_const_p(char *, poptGetArgs(pc));

	argc_new = argc;
	for (i=0; i<argc; i++) {
		if (argv_new[i] == NULL) {
			argc_new = i;
			break;
		}
	}

	if (argc_new < 3) {
		usage(pc);
		exit(1);
	}

        for(p = argv_new[1]; *p; p++) {
		if(*p == '\\')
			*p = '/';
	}

	/* see if its a RPC transport specifier */
	if (is_binding_string(argv_new[1])) {
		lp_set_cmdline("torture:binding", argv_new[1]);
	} else {
		char *binding = NULL;
		const char *host = NULL, *share = NULL;

		if (!smbcli_parse_unc(argv_new[1], NULL, &host, &share)) {
			d_printf("Invalid option: %s is not a valid torture target (share or binding string)\n\n", argv_new[1]);
			usage(pc);
		}

		lp_set_cmdline("torture:host", host);
		lp_set_cmdline("torture:share", share);
		asprintf(&binding, "ncacn_np:%s", host);
		lp_set_cmdline("torture:binding", binding);
	}

	if (argc_new == 0) {
		printf("You must specify a test to run, or 'ALL'\n");
	} else {
		for (i=2;i<argc_new;i++) {
			if (!run_test(argv_new[i])) {
				correct = False;
			}
		}
	}

	if (correct) {
		return(0);
	} else {
		return(1);
	}
}
