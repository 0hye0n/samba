/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   SMB torture tester
   Copyright (C) Andrew Tridgell 1997-1998
   
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

#define NO_SYSLOG

#include "includes.h"

static fstring host, workgroup, share, password, username, myname;
static int max_protocol = PROTOCOL_NT1;
static char *sockops="TCP_NODELAY";
static int nprocs=1, numops=100;
static pid_t master_pid;
static struct cli_state current_cli;

static double create_procs(void (*fn)(int ));


static struct timeval tp1,tp2;

static void start_timer(void)
{
	gettimeofday(&tp1,NULL);
}

static double end_timer(void)
{
	gettimeofday(&tp2,NULL);
	return((tp2.tv_sec - tp1.tv_sec) + 
	       (tp2.tv_usec - tp1.tv_usec)*1.0e-6);
}


/* return a pointer to a anonymous shared memory segment of size "size"
   which will persist across fork() but will disappear when all processes
   exit 

   The memory is not zeroed 

   This function uses system5 shared memory. It takes advantage of a property
   that the memory is not destroyed if it is attached when the id is removed
   */
static void *shm_setup(int size)
{
	int shmid;
	void *ret;

	shmid = shmget(IPC_PRIVATE, size, SHM_R | SHM_W);
	if (shmid == -1) {
		printf("can't get shared memory\n");
		exit(1);
	}
	ret = (void *)shmat(shmid, 0, 0);
	if (!ret || ret == (void *)-1) {
		printf("can't attach to shared memory\n");
		return NULL;
	}
	/* the following releases the ipc, but note that this process
	   and all its children will still have access to the memory, its
	   just that the shmid is no longer valid for other shm calls. This
	   means we don't leave behind lots of shm segments after we exit 

	   See Stevens "advanced programming in unix env" for details
	   */
	shmctl(shmid, IPC_RMID, 0);
	
	return ret;
}


static BOOL open_connection(struct cli_state *c)
{
	struct nmb_name called, calling;
	struct in_addr ip;
	extern struct in_addr ipzero;

	ZERO_STRUCTP(c);

	make_nmb_name(&calling, myname, 0x0, "");
	make_nmb_name(&called , host, 0x20, "");

	ip = ipzero;

	if (!cli_initialise(c) || !cli_connect(c, host, &ip)) {
		printf("Failed to connect with %s\n", host);
		return False;
	}

	c->timeout = 120000; /* set a really long timeout (2 minutes) */

	if (!cli_session_request(c, &calling, &called)) {
		printf("%s rejected the session\n",host);
		cli_shutdown(c);
		return False;
	}

	if (!cli_negprot(c)) {
		printf("%s rejected the negprot (%s)\n",host, cli_errstr(c));
		cli_shutdown(c);
		return False;
	}

	if (!cli_session_setup(c, username, 
			       password, strlen(password),
			       password, strlen(password),
			       workgroup)) {
		printf("%s rejected the sessionsetup (%s)\n", host, cli_errstr(c));
		cli_shutdown(c);
		return False;
	}

	if (!cli_send_tconX(c, share, "?????",
			    password, strlen(password)+1)) {
		printf("%s refused tree connect (%s)\n", host, cli_errstr(c));
		cli_shutdown(c);
		return False;
	}

	return True;
}



static void close_connection(struct cli_state *c)
{
	if (!cli_tdis(c)) {
		printf("tdis failed (%s)\n", cli_errstr(c));
	}

	cli_shutdown(c);
}


/* check if the server produced the expected error code */
static BOOL check_error(struct cli_state *c, 
			uint8 eclass, uint32 ecode, uint32 nterr)
{
	uint8 class;
	uint32 num;
	int eno;
	eno = cli_error(c, &class, &num, NULL);
	if ((eclass != class || ecode != num) &&
	    num != (nterr&0xFFFFFF)) {
		printf("unexpected error code class=%d code=%d\n", 
			 (int)class, (int)num);
		printf(" expected %d/%d %d\n", 
		       (int)eclass, (int)ecode, (int)nterr);
		return False;
	}
	return True;
}


static BOOL wait_lock(struct cli_state *c, int fnum, uint32 offset, uint32 len)
{
	while (!cli_lock(c, fnum, offset, len, -1)) {
		if (!check_error(c, ERRDOS, ERRlock, 0)) return False;
	}
	return True;
}


static BOOL rw_torture(struct cli_state *c)
{
	char *lockfname = "\\torture.lck";
	fstring fname;
	int fnum;
	int fnum2;
	pid_t pid2, pid = getpid();
	int i, j;
	char buf[1024];

	fnum2 = cli_open(c, lockfname, O_RDWR | O_CREAT | O_EXCL, 
			 DENY_NONE);
	if (fnum2 == -1)
		fnum2 = cli_open(c, lockfname, O_RDWR, DENY_NONE);
	if (fnum2 == -1) {
		printf("open of %s failed (%s)\n", lockfname, cli_errstr(c));
		return False;
	}


	for (i=0;i<numops;i++) {
		unsigned n = (unsigned)sys_random()%10;
		if (i % 10 == 0) {
			printf("%d\r", i); fflush(stdout);
		}
		slprintf(fname, sizeof(fstring) - 1, "\\torture.%u", n);

		if (!wait_lock(c, fnum2, n*sizeof(int), sizeof(int))) {
			return False;
		}

		fnum = cli_open(c, fname, O_RDWR | O_CREAT | O_TRUNC, DENY_ALL);
		if (fnum == -1) {
			printf("open failed (%s)\n", cli_errstr(c));
			break;
		}

		if (cli_write(c, fnum, 0, (char *)&pid, 0, sizeof(pid)) != sizeof(pid)) {
			printf("write failed (%s)\n", cli_errstr(c));
		}

		for (j=0;j<50;j++) {
			if (cli_write(c, fnum, 0, (char *)buf, 
				      sizeof(pid)+(j*sizeof(buf)), 
				      sizeof(buf)) != sizeof(buf)) {
				printf("write failed (%s)\n", cli_errstr(c));
			}
		}

		pid2 = 0;

		if (cli_read(c, fnum, (char *)&pid2, 0, sizeof(pid)) != sizeof(pid)) {
			printf("read failed (%s)\n", cli_errstr(c));
		}

		if (pid2 != pid) {
			printf("data corruption!\n");
		}

		if (!cli_close(c, fnum)) {
			printf("close failed (%s)\n", cli_errstr(c));
		}

		if (!cli_unlink(c, fname)) {
			printf("unlink failed (%s)\n", cli_errstr(c));
		}

		if (!cli_unlock(c, fnum2, n*sizeof(int), sizeof(int), -1)) {
			printf("unlock failed (%s)\n", cli_errstr(c));
		}
	}

	cli_close(c, fnum2);
	cli_unlink(c, lockfname);

	printf("%d\n", i);

	return True;
}

static void run_torture(void)
{
	struct cli_state cli;

	cli = current_cli;

	cli_sockopt(&cli, sockops);

	rw_torture(&cli);
	
	close_connection(&cli);
}

int line_count = 0;

/* run a test that simulates an approximate netbench client load */
static void run_netbench(int client)
{
	struct cli_state cli;
	int i;
	fstring fname;
	pstring line;
	char cname[20];
	FILE *f;
	char *params[20];

	cli = current_cli;

	cli_sockopt(&cli, sockops);

	nb_setup(&cli);

	slprintf(cname,sizeof(fname), "CLIENT%d", client);

	f = fopen("client.txt", "r");

	if (!f) {
		perror("client.txt");
		return;
	}

	while (fgets(line, sizeof(line)-1, f)) {
		line_count++;

		line[strlen(line)-1] = 0;

		/* printf("[%d] %s\n", line_count, line); */

		all_string_sub(line,"CLIENT1", cname, sizeof(line));
		
		for (i=0;i<20;i++) params[i] = "";

		/* parse the command parameters */
		params[0] = strtok(line," ");
		i = 0;
		while (params[i]) params[++i] = strtok(NULL," ");

		params[i] = "";

		if (i < 2) continue;

		if (strcmp(params[1],"REQUEST") == 0) {
			if (!strcmp(params[0],"SMBopenX")) {
				fstrcpy(fname, params[5]);
			} else if (!strcmp(params[0],"SMBclose")) {
				nb_close(atoi(params[3]));
			} else if (!strcmp(params[0],"SMBmkdir")) {
				nb_mkdir(params[3]);
			} else if (!strcmp(params[0],"CREATE")) {
				nb_create(params[3], atoi(params[5]));
			} else if (!strcmp(params[0],"SMBrmdir")) {
				nb_rmdir(params[3]);
			} else if (!strcmp(params[0],"SMBunlink")) {
				fstrcpy(fname, params[3]);
			} else if (!strcmp(params[0],"SMBmv")) {
				nb_rename(params[3], params[5]);
			} else if (!strcmp(params[0],"SMBgetatr")) {
				fstrcpy(fname, params[3]);
			} else if (!strcmp(params[0],"SMBwrite")) {
				nb_write(atoi(params[3]), 
					 atoi(params[5]), atoi(params[7]));
			} else if (!strcmp(params[0],"SMBwritebraw")) {
				nb_write(atoi(params[3]), 
					 atoi(params[7]), atoi(params[5]));
			} else if (!strcmp(params[0],"SMBreadbraw")) {
				nb_read(atoi(params[3]), 
					 atoi(params[7]), atoi(params[5]));
			} else if (!strcmp(params[0],"SMBread")) {
				nb_read(atoi(params[3]), 
					 atoi(params[5]), atoi(params[7]));
			}
		} else {
			if (!strcmp(params[0],"SMBopenX")) {
				if (!strncmp(params[2], "ERR", 3)) continue;
				nb_open(fname, atoi(params[3]), atoi(params[5]));
			} else if (!strcmp(params[0],"SMBgetatr")) {
				if (!strncmp(params[2], "ERR", 3)) continue;
				nb_stat(fname, atoi(params[3]));
			} else if (!strcmp(params[0],"SMBunlink")) {
				if (!strncmp(params[2], "ERR", 3)) continue;
				nb_unlink(fname);
			}
		}
	}
	fclose(f);

	slprintf(fname,sizeof(fname), "CLIENTS/CLIENT%d", client);
	rmdir(fname);
	rmdir("CLIENTS");

	printf("+");	

	close_connection(&cli);
}


/* run a test that simulates an approximate netbench w9X client load */
static void run_nbw95(void)
{
	double t;
	t = create_procs(run_netbench);
	/* to produce a netbench result we scale accoding to the
           netbench measured throughput for the run that produced the
           sniff that was used to produce client.txt. That run used 2
           clients and ran for 660 seconds to produce a result of
           4MBit/sec. */
	printf("Throughput %g MB/sec (NB=%g MB/sec  %g MBit/sec)\n", 
	       132*nprocs/t, 0.5*0.5*nprocs*660/t, 2*nprocs*660/t);
}

/* run a test that simulates an approximate netbench wNT client load */
static void run_nbwnt(void)
{
	double t;
	t = create_procs(run_netbench);
	printf("Throughput %g MB/sec (NB=%g MB/sec  %g MBit/sec)\n", 
	       132*nprocs/t, 0.5*0.5*nprocs*660/t, 2*nprocs*660/t);
}



/*
  This test checks for two things:

  1) correct support for retaining locks over a close (ie. the server
     must not use posix semantics)
  2) support for lock timeouts
 */
static void run_locktest1(void)
{
	static struct cli_state cli1, cli2;
	char *fname = "\\lockt1.lck";
	int fnum1, fnum2, fnum3;
	time_t t1, t2;

	if (!open_connection(&cli1) || !open_connection(&cli2)) {
		return;
	}
	cli_sockopt(&cli1, sockops);
	cli_sockopt(&cli2, sockops);

	printf("starting locktest1\n");

	cli_unlink(&cli1, fname);

	fnum1 = cli_open(&cli1, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, cli_errstr(&cli1));
		return;
	}
	fnum2 = cli_open(&cli1, fname, O_RDWR, DENY_NONE);
	if (fnum2 == -1) {
		printf("open2 of %s failed (%s)\n", fname, cli_errstr(&cli1));
		return;
	}
	fnum3 = cli_open(&cli2, fname, O_RDWR, DENY_NONE);
	if (fnum3 == -1) {
		printf("open3 of %s failed (%s)\n", fname, cli_errstr(&cli2));
		return;
	}

	if (!cli_lock(&cli1, fnum1, 0, 4, 0)) {
		printf("lock1 failed (%s)\n", cli_errstr(&cli1));
		return;
	}


	if (cli_lock(&cli2, fnum3, 0, 4, 0)) {
		printf("lock2 succeeded! This is a locking bug\n");
		return;
	} else {
		if (!check_error(&cli2, ERRDOS, ERRlock, 0)) return;
	}


	printf("Testing lock timeouts\n");
	t1 = time(NULL);
	if (cli_lock(&cli2, fnum3, 0, 4, 10*1000)) {
		printf("lock3 succeeded! This is a locking bug\n");
		return;
	} else {
		if (!check_error(&cli2, ERRDOS, ERRlock, 0)) return;
	}
	t2 = time(NULL);

	if (t2 - t1 < 5) {
		printf("error: This server appears not to support timed lock requests\n");
	}

	if (!cli_close(&cli1, fnum2)) {
		printf("close1 failed (%s)\n", cli_errstr(&cli1));
		return;
	}

	if (cli_lock(&cli2, fnum3, 0, 4, 0)) {
		printf("lock4 succeeded! This is a locking bug\n");
		return;
	} else {
		if (!check_error(&cli2, ERRDOS, ERRlock, 0)) return;
	}

	if (!cli_close(&cli1, fnum1)) {
		printf("close2 failed (%s)\n", cli_errstr(&cli1));
		return;
	}

	if (!cli_close(&cli2, fnum3)) {
		printf("close3 failed (%s)\n", cli_errstr(&cli2));
		return;
	}

	if (!cli_unlink(&cli1, fname)) {
		printf("unlink failed (%s)\n", cli_errstr(&cli1));
		return;
	}


	close_connection(&cli1);
	close_connection(&cli2);

	printf("Passed locktest1\n");
}


/*
  This test checks that 

  1) the server supports multiple locking contexts on the one SMB
  connection, distinguished by PID.  

  2) the server correctly fails overlapping locks made by the same PID (this
     goes against POSIX behaviour, which is why it is tricky to implement)

  3) the server denies unlock requests by an incorrect client PID
*/
static void run_locktest2(void)
{
	static struct cli_state cli;
	char *fname = "\\lockt2.lck";
	int fnum1, fnum2, fnum3;

	if (!open_connection(&cli)) {
		return;
	}

	cli_sockopt(&cli, sockops);

	printf("starting locktest2\n");

	cli_unlink(&cli, fname);

	cli_setpid(&cli, 1);

	fnum1 = cli_open(&cli, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, cli_errstr(&cli));
		return;
	}

	fnum2 = cli_open(&cli, fname, O_RDWR, DENY_NONE);
	if (fnum2 == -1) {
		printf("open2 of %s failed (%s)\n", fname, cli_errstr(&cli));
		return;
	}

	cli_setpid(&cli, 2);

	fnum3 = cli_open(&cli, fname, O_RDWR, DENY_NONE);
	if (fnum3 == -1) {
		printf("open3 of %s failed (%s)\n", fname, cli_errstr(&cli));
		return;
	}

	cli_setpid(&cli, 1);

	if (!cli_lock(&cli, fnum1, 0, 4, 0)) {
		printf("lock1 failed (%s)\n", cli_errstr(&cli));
		return;
	}

	if (cli_lock(&cli, fnum2, 0, 4, 0)) {
		printf("lock2 succeeded! This is a locking bug\n");
	} else {
		if (!check_error(&cli, ERRDOS, ERRlock, 0)) return;
	}

	cli_setpid(&cli, 2);

	if (cli_unlock(&cli, fnum1, 0, 4, 0)) {
		printf("unlock1 succeeded! This is a locking bug\n");
	}

	if (cli_lock(&cli, fnum3, 0, 4, 0)) {
		printf("lock3 succeeded! This is a locking bug\n");
	} else {
		if (!check_error(&cli, ERRDOS, ERRlock, 0)) return;
	}

	cli_setpid(&cli, 1);

	if (!cli_close(&cli, fnum1)) {
		printf("close1 failed (%s)\n", cli_errstr(&cli));
		return;
	}

	if (!cli_close(&cli, fnum2)) {
		printf("close2 failed (%s)\n", cli_errstr(&cli));
		return;
	}

	if (!cli_close(&cli, fnum3)) {
		printf("close3 failed (%s)\n", cli_errstr(&cli));
		return;
	}

	close_connection(&cli);

	printf("locktest2 finished\n");
}


/*
  This test checks that 

  1) the server supports the full offset range in lock requests
*/
static void run_locktest3(void)
{
	static struct cli_state cli1, cli2;
	char *fname = "\\lockt3.lck";
	int fnum1, fnum2, i;
	uint32 offset;

#define NEXT_OFFSET offset += (~(uint32)0) / numops

	if (!open_connection(&cli1) || !open_connection(&cli2)) {
		return;
	}
	cli_sockopt(&cli1, sockops);
	cli_sockopt(&cli2, sockops);

	printf("starting locktest3\n");

	cli_unlink(&cli1, fname);

	fnum1 = cli_open(&cli1, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, cli_errstr(&cli1));
		return;
	}
	fnum2 = cli_open(&cli2, fname, O_RDWR, DENY_NONE);
	if (fnum2 == -1) {
		printf("open2 of %s failed (%s)\n", fname, cli_errstr(&cli2));
		return;
	}

	for (offset=i=0;i<numops;i++) {
		NEXT_OFFSET;
		if (!cli_lock(&cli1, fnum1, offset-1, 1, 0)) {
			printf("lock1 %d failed (%s)\n", 
			       i,
			       cli_errstr(&cli1));
			return;
		}

		if (!cli_lock(&cli2, fnum2, offset-2, 1, 0)) {
			printf("lock2 %d failed (%s)\n", 
			       i,
			       cli_errstr(&cli1));
			return;
		}
	}

	for (offset=i=0;i<numops;i++) {
		NEXT_OFFSET;

		if (cli_lock(&cli1, fnum1, offset-2, 1, 0)) {
			printf("error: lock1 %d succeeded!\n", i);
			return;
		}

		if (cli_lock(&cli2, fnum2, offset-1, 1, 0)) {
			printf("error: lock2 %d succeeded!\n", i);
			return;
		}

		if (cli_lock(&cli1, fnum1, offset-1, 1, 0)) {
			printf("error: lock3 %d succeeded!\n", i);
			return;
		}

		if (cli_lock(&cli2, fnum2, offset-2, 1, 0)) {
			printf("error: lock4 %d succeeded!\n", i);
			return;
		}
	}

	for (offset=i=0;i<numops;i++) {
		NEXT_OFFSET;

		if (!cli_unlock(&cli1, fnum1, offset-1, 1, 0)) {
			printf("unlock1 %d failed (%s)\n", 
			       i,
			       cli_errstr(&cli1));
			return;
		}

		if (!cli_unlock(&cli2, fnum2, offset-2, 1, 0)) {
			printf("unlock2 %d failed (%s)\n", 
			       i,
			       cli_errstr(&cli1));
			return;
		}
	}

	if (!cli_close(&cli1, fnum1)) {
		printf("close1 failed (%s)\n", cli_errstr(&cli1));
	}

	if (!cli_close(&cli2, fnum2)) {
		printf("close2 failed (%s)\n", cli_errstr(&cli2));
	}

	if (!cli_unlink(&cli1, fname)) {
		printf("unlink failed (%s)\n", cli_errstr(&cli1));
		return;
	}

	close_connection(&cli1);
	close_connection(&cli2);

	printf("finished locktest3\n");
}


/*
test whether fnums and tids open on one VC are available on another (a major
security hole)
*/
static void run_fdpasstest(void)
{
	static struct cli_state cli1, cli2;
	char *fname = "\\fdpass.tst";
	int fnum1;
	pstring buf;

	if (!open_connection(&cli1) || !open_connection(&cli2)) {
		return;
	}
	cli_sockopt(&cli1, sockops);
	cli_sockopt(&cli2, sockops);

	printf("starting fdpasstest\n");

	cli_unlink(&cli1, fname);

	fnum1 = cli_open(&cli1, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum1 == -1) {
		printf("open of %s failed (%s)\n", fname, cli_errstr(&cli1));
		return;
	}

	if (cli_write(&cli1, fnum1, 0, "hello world\n", 0, 13) != 13) {
		printf("write failed (%s)\n", cli_errstr(&cli1));
		return;
	}

	cli2.vuid = cli1.vuid;
	cli2.cnum = cli1.cnum;
	cli2.pid = cli1.pid;


	if (cli_read(&cli2, fnum1, buf, 0, 13) == 13) {
		printf("read succeeded! nasty security hole [%s]\n",
		       buf);
		return;
	}

	cli_close(&cli1, fnum1);
	cli_unlink(&cli1, fname);

	close_connection(&cli1);
	close_connection(&cli2);

	printf("finished fdpasstest\n");
}


/*
  This test checks that 

  1) the server does not allow an unlink on a file that is open
*/
static void run_unlinktest(void)
{
	static struct cli_state cli;
	char *fname = "\\unlink.tst";
	int fnum;

	if (!open_connection(&cli)) {
		return;
	}

	cli_sockopt(&cli, sockops);

	printf("starting unlink test\n");

	cli_unlink(&cli, fname);

	cli_setpid(&cli, 1);

	fnum = cli_open(&cli, fname, O_RDWR|O_CREAT|O_EXCL, DENY_NONE);
	if (fnum == -1) {
		printf("open of %s failed (%s)\n", fname, cli_errstr(&cli));
		return;
	}

	if (cli_unlink(&cli, fname)) {
		printf("error: server allowed unlink on an open file\n");
	}

	cli_close(&cli, fnum);
	cli_unlink(&cli, fname);

	close_connection(&cli);

	printf("unlink test finished\n");
}


/*
test how many open files this server supports on the one socket
*/
static void run_maxfidtest(void)
{
	static struct cli_state cli;
	char *template = "\\maxfid.%d.%d";
	fstring fname;
	int fnum;
	int retries=4;
	int n = numops;

	cli = current_cli;

	if (retries <= 0) {
		printf("failed to connect\n");
		return;
	}

	cli_sockopt(&cli, sockops);

	fnum = 0;
	while (1) {
		slprintf(fname,sizeof(fname)-1,template, fnum,(int)getpid());
		if (cli_open(&cli, fname, 
			     O_RDWR|O_CREAT|O_TRUNC, DENY_NONE) ==
		    -1) {
			printf("open of %s failed (%s)\n", 
			       fname, cli_errstr(&cli));
			printf("maximum fnum is %d\n", fnum);
			break;
		}
		fnum++;
	}

	printf("cleaning up\n");
	while (fnum > n) {
		fnum--;
		slprintf(fname,sizeof(fname)-1,template, fnum,(int)getpid());
		if (cli_unlink(&cli, fname)) {
			printf("unlink of %s failed (%s)\n", 
			       fname, cli_errstr(&cli));
		}
	}

	printf("maxfid test finished\n");
	close_connection(&cli);
}

/* generate a random buffer */
static void rand_buf(char *buf, int len)
{
	while (len--) {
		*buf = sys_random();
		buf++;
	}
}

/* send random IPC commands */
static void run_randomipc(void)
{
	char *rparam = NULL;
	char *rdata = NULL;
	int rdrcnt,rprcnt;
	pstring param;
	int api, param_len, i;
	static struct cli_state cli;

	printf("starting random ipc test\n");

	if (!open_connection(&cli)) {
		return;
	}

	for (i=0;i<50000;i++) {
		api = sys_random() % 500;
		param_len = (sys_random() % 64);

		rand_buf(param, param_len);
  
		SSVAL(param,0,api); 

		cli_api(&cli, 
			param, param_len, 8,  
			NULL, 0, BUFFER_SIZE, 
			&rparam, &rprcnt,     
			&rdata, &rdrcnt);
	}

	close_connection(&cli);

	printf("finished random ipc test\n");
}



static void browse_callback(const char *sname, uint32 stype, 
			    const char *comment)
{
	printf("\t%20.20s %08x %s\n", sname, stype, comment);
}



/*
  This test checks the browse list code

*/
static void run_browsetest(void)
{
	static struct cli_state cli;

	printf("starting browse test\n");

	if (!open_connection(&cli)) {
		return;
	}

	printf("domain list:\n");
	cli_NetServerEnum(&cli, cli.server_domain, 
			  SV_TYPE_DOMAIN_ENUM,
			  browse_callback);

	printf("machine list:\n");
	cli_NetServerEnum(&cli, cli.server_domain, 
			  SV_TYPE_ALL,
			  browse_callback);

	close_connection(&cli);

	printf("browse test finished\n");
}


/*
  This checks how the getatr calls works
*/
static void run_attrtest(void)
{
	static struct cli_state cli;
	int fnum;
	time_t t, t2;
	char *fname = "\\attrib.tst";

	printf("starting attrib test\n");

	if (!open_connection(&cli)) {
		return;
	}

	cli_unlink(&cli, fname);
	fnum = cli_open(&cli, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	cli_close(&cli, fnum);
	if (!cli_getatr(&cli, fname, NULL, NULL, &t)) {
		printf("getatr failed (%s)\n", cli_errstr(&cli));
	}

	if (abs(t - time(NULL)) > 2) {
		printf("ERROR: SMBgetatr bug. time is %s",
		       ctime(&t));
		t = time(NULL);
	}

	t2 = t-60*60*24; /* 1 day ago */

	if (!cli_setatr(&cli, fname, 0, t2)) {
		printf("setatr failed (%s)\n", cli_errstr(&cli));
	}

	if (!cli_getatr(&cli, fname, NULL, NULL, &t)) {
		printf("getatr failed (%s)\n", cli_errstr(&cli));
	}

	if (t != t2) {
		printf("ERROR: getatr/setatr bug. times are\n%s",
		       ctime(&t));
		printf("%s", ctime(&t2));
	}

	cli_unlink(&cli, fname);

	close_connection(&cli);

	printf("attrib test finished\n");
}


/*
  This checks a couple of trans2 calls
*/
static void run_trans2test(void)
{
	static struct cli_state cli;
	int fnum;
	size_t size;
	time_t c_time, a_time, m_time, w_time, m_time2;
	char *fname = "\\trans2.tst";
	char *dname = "\\trans2";
	char *fname2 = "\\trans2\\trans2.tst";

	printf("starting trans2 test\n");

	if (!open_connection(&cli)) {
		return;
	}

	cli_unlink(&cli, fname);
	fnum = cli_open(&cli, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	if (!cli_qfileinfo(&cli, fnum, NULL, &size, &c_time, &a_time, &m_time,
			   NULL, NULL)) {
		printf("ERROR: qfileinfo failed (%s)\n", cli_errstr(&cli));
	}
	cli_close(&cli, fnum);

	sleep(2);

	cli_unlink(&cli, fname);
	fnum = cli_open(&cli, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	cli_close(&cli, fnum);

	if (!cli_qpathinfo(&cli, fname, &c_time, &a_time, &m_time, &size, NULL)) {
		printf("ERROR: qpathinfo failed (%s)\n", cli_errstr(&cli));
	} else {
		if (c_time != m_time) {
			printf("create time=%s", ctime(&c_time));
			printf("modify time=%s", ctime(&m_time));
			printf("This system appears to have sticky create times\n");
		}
		if (a_time % (60*60) == 0) {
			printf("access time=%s", ctime(&a_time));
			printf("This system appears to set a midnight access time\n");
		}

		if (abs(m_time - time(NULL)) > 60*60*24*7) {
			printf("ERROR: totally incorrect times - maybe word reversed?\n");
		}
	}


	cli_unlink(&cli, fname);
	fnum = cli_open(&cli, fname, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	cli_close(&cli, fnum);
	if (!cli_qpathinfo2(&cli, fname, &c_time, &a_time, &m_time, 
			    &w_time, &size, NULL, NULL)) {
		printf("ERROR: qpathinfo2 failed (%s)\n", cli_errstr(&cli));
	} else {
		if (w_time < 60*60*24*2) {
			printf("write time=%s", ctime(&w_time));
			printf("This system appears to set a initial 0 write time\n");
		}
	}

	cli_unlink(&cli, fname);


	/* check if the server updates the directory modification time
           when creating a new file */
	if (!cli_mkdir(&cli, dname)) {
		printf("ERROR: mkdir failed (%s)\n", cli_errstr(&cli));
	}
	sleep(3);
	if (!cli_qpathinfo2(&cli, "\\trans2\\", &c_time, &a_time, &m_time, 
			    &w_time, &size, NULL, NULL)) {
		printf("ERROR: qpathinfo2 failed (%s)\n", cli_errstr(&cli));
	}

	fnum = cli_open(&cli, fname2, 
			O_RDWR | O_CREAT | O_TRUNC, DENY_NONE);
	cli_write(&cli, fnum,  0, (char *)&fnum, 0, sizeof(fnum));
	cli_close(&cli, fnum);
	if (!cli_qpathinfo2(&cli, "\\trans2\\", &c_time, &a_time, &m_time2, 
			    &w_time, &size, NULL, NULL)) {
		printf("ERROR: qpathinfo2 failed (%s)\n", cli_errstr(&cli));
	} else {
		if (m_time2 == m_time)
			printf("This system does not update directory modification times\n");
	}
	cli_unlink(&cli, fname2);
	cli_rmdir(&cli, dname);


	close_connection(&cli);

	printf("trans2 test finished\n");
}

static double create_procs(void (*fn)(int ))
{
	int i, status;
	volatile int *child_status;
	int synccount;
	int tries = 8;

	start_timer();

	master_pid = getpid();
	synccount = 0;

	child_status = (volatile int *)shm_setup(sizeof(int)*nprocs);
	if (!child_status) {
		printf("Failed to setup shared memory\n");
		return end_timer();
	}

	memset(child_status, 0, sizeof(int)*nprocs);

	for (i=0;i<nprocs;i++) {
		if (fork() == 0) {
			pid_t mypid = getpid();
			sys_srandom(((int)mypid) ^ ((int)time(NULL)));

			slprintf(myname,sizeof(myname),"CLIENT%d", i);

			while (1) {
				memset(&current_cli, 0, sizeof(current_cli));
				if (open_connection(&current_cli)) break;
				if (tries-- == 0) {
					printf("pid %d failed to start\n", (int)getpid());
					_exit(1);
				}
				msleep(10);
			}

			child_status[i] = getpid();

			while (child_status[i]) msleep(2);

			fn(i);
			_exit(0);
		}
	}

	do {
		synccount = 0;
		for (i=0;i<nprocs;i++) {
			if (child_status[i]) synccount++;
		}
		if (synccount == nprocs) break;
		msleep(10);
	} while (end_timer() < 30);

	if (synccount != nprocs) {
		printf("FAILED TO START %d CLIENTS (started %d)\n", nprocs, synccount);
		return end_timer();
	}

	/* start the client load */
	start_timer();

	for (i=0;i<nprocs;i++) {
		child_status[i] = 0;
	}

	printf("%d clients started\n", nprocs);

	for (i=0;i<nprocs;i++) {
		waitpid(0, &status, 0);
		printf("*");
	}
	printf("\n");
	return end_timer();
}


#define FLAG_MULTIPROC 1

static struct {
	char *name;
	void (*fn)(void);
	unsigned flags;
} torture_ops[] = {
	{"FDPASS", run_fdpasstest, 0},
	{"LOCK1",  run_locktest1,  0},
	{"LOCK2",  run_locktest2,  0},
	{"LOCK3",  run_locktest3,  0},
	{"UNLINK", run_unlinktest, 0},
	{"BROWSE", run_browsetest, 0},
	{"ATTR",   run_attrtest,   0},
	{"TRANS2", run_trans2test, 0},
	{"MAXFID", run_maxfidtest, FLAG_MULTIPROC},
	{"TORTURE",run_torture,    FLAG_MULTIPROC},
	{"RANDOMIPC", run_randomipc, 0},
	{"NBW95",  run_nbw95, 0},
	{"NBWNT",  run_nbwnt, 0},
	{NULL, NULL, 0}};


/****************************************************************************
run a specified test or "ALL"
****************************************************************************/
static void run_test(char *name)
{
	int i;
	if (strequal(name,"ALL")) {
		for (i=0;torture_ops[i].name;i++) {
			run_test(torture_ops[i].name);
		}
	}
	
	for (i=0;torture_ops[i].name;i++) {
		if (strequal(name, torture_ops[i].name)) {
			start_timer();
			printf("Running %s\n", name);
			if (torture_ops[i].flags & FLAG_MULTIPROC) {
				create_procs(torture_ops[i].fn);
			} else {
				torture_ops[i].fn();
			}
			printf("%s took %g secs\n\n", name, end_timer());
		}
	}
}


static void usage(void)
{
	int i;

	printf("Usage: smbtorture //server/share <options> TEST1 TEST2 ...\n");

	printf("\t-U user%%pass\n");
	printf("\t-N numprocs\n");
	printf("\t-n my_netbios_name\n");
	printf("\t-W workgroup\n");
	printf("\t-o num_operations\n");
	printf("\t-O socket_options\n");
	printf("\t-m maximum protocol\n");
	printf("\n\n");

	printf("tests are:");
	for (i=0;torture_ops[i].name;i++) {
		printf(" %s", torture_ops[i].name);
	}
	printf("\n");

	printf("default test is ALL\n");
	
	exit(1);
}





/****************************************************************************
  main program
****************************************************************************/
 int main(int argc,char *argv[])
{
	int opt, i;
	char *p;
	int gotpass = 0;
	extern char *optarg;
	extern int optind;
	extern FILE *dbf;
	static pstring servicesf = CONFIGFILE;

	dbf = stdout;

	setbuffer(stdout, NULL, 0);

	charset_initialise();

	lp_load(servicesf,True,False,False);
	load_interfaces();

	if (argc < 2) {
		usage();
	}

        for(p = argv[1]; *p; p++)
          if(*p == '\\')
            *p = '/';
 
	if (strncmp(argv[1], "//", 2)) {
		usage();
	}

	fstrcpy(host, &argv[1][2]);
	p = strchr(&host[2],'/');
	if (!p) {
		usage();
	}
	*p = 0;
	fstrcpy(share, p+1);

	get_myname(myname,NULL);

	if (*username == 0 && getenv("LOGNAME")) {
	  pstrcpy(username,getenv("LOGNAME"));
	}

	argc--;
	argv++;


	while ((opt = getopt(argc, argv, "hW:U:n:N:O:o:m:")) != EOF) {
		switch (opt) {
		case 'W':
			fstrcpy(workgroup,optarg);
			break;
		case 'm':
			max_protocol = interpret_protocol(optarg, max_protocol);
			break;
		case 'N':
			nprocs = atoi(optarg);
			break;
		case 'o':
			numops = atoi(optarg);
			break;
		case 'O':
			sockops = optarg;
			break;
		case 'n':
			fstrcpy(myname, optarg);
			break;
		case 'U':
			pstrcpy(username,optarg);
			p = strchr(username,'%');
			if (p) {
				*p = 0;
				pstrcpy(password, p+1);
				gotpass = 1;
			}
			break;
		default:
			printf("Unknown option %c (%d)\n", (char)opt, opt);
			usage();
		}
	}


	while (!gotpass) {
		p = getpass("Password:");
		if (p) {
			pstrcpy(password, p);
			gotpass = 1;
		}
	}

	printf("host=%s share=%s user=%s myname=%s\n", 
	       host, share, username, myname);

	if (argc == 1) {
		run_test("ALL");
	} else {
		for (i=1;i<argc;i++) {
			run_test(argv[i]);
		}
	}

	return(0);
}
