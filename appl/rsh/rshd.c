#include "rsh_locl.h"
RCSID("$Id$");

enum auth_method auth_method;

krb5_context context;
krb5_keyblock *keyblock;
des_key_schedule schedule;
des_cblock iv;

int do_encrypt;

static void
syslog_and_die (const char *m, ...)
{
    va_list args;

    va_start(args, m);
    vsyslog (LOG_ERR, m, args);
    va_end(args);
    exit (1);
}

static void
fatal (int sock, const char *m, ...)
{
    va_list args;
    char buf[BUFSIZ];
    size_t len;

    *buf = 1;
    va_start(args, m);
    len = vsnprintf (buf + 1, sizeof(buf) - 1, m, args);
    va_end(args);
    syslog (LOG_ERR, buf + 1);
    krb_net_write (sock, buf, len + 1);
    exit (1);
}

static void
read_str (int s, char *str, size_t sz, char *expl)
{
    while (sz > 0) {
	if (krb_net_read (s, str, 1) != 1)
	    syslog_and_die ("read: %m");
	if (*str == '\0')
	    return;
	--sz;
	++str;
    }
    fatal (s, "%s too long", expl);
}

static int
recv_krb4_auth (int s, u_char *buf,
		struct sockaddr_in thisaddr,
		struct sockaddr_in thataddr,
		char *client_username,
		char *server_username,
		char *cmd)
{
    int status;
    int32_t options;
    KTEXT_ST ticket;
    AUTH_DAT auth;
    char instance[INST_SZ + 1];
    char version[KRB_SENDAUTH_VLEN + 1];

    if (memcmp (buf, KRB_SENDAUTH_VERS, 4) != 0)
	return -1;
    if (krb_net_read (s, buf + 4, KRB_SENDAUTH_VLEN - 4) !=
	KRB_SENDAUTH_VLEN - 4)
	syslog_and_die ("reading auth info: %m");
    if (memcmp (buf, KRB_SENDAUTH_VERS, KRB_SENDAUTH_VLEN) != 0)
	syslog_and_die("unrecognized auth protocol: %.8s", buf);

    options = KOPT_IGNORE_PROTOCOL;
    if (do_encrypt)
	options |= KOPT_DO_MUTUAL;
    k_getsockinst (s, instance, sizeof(instance));
    status = krb_recvauth (options,
			   s,
			   &ticket,
			   "rcmd",
			   instance,
			   &thataddr,
			   &thisaddr,
			   &auth,
			   "",
			   schedule,
			   version);
    if (status != KSUCCESS)
	syslog_and_die ("recvauth: %s", krb_get_err_text(status));
    if (strncmp (version, KCMD_VERSION, KRB_SENDAUTH_VLEN) != 0)
	syslog_and_die ("bad version: %s", version);

    read_str (s, server_username, USERNAME_SZ, "remote username");
    if (kuserok (&auth, server_username) != 0)
	fatal (s, "Permission denied");
    read_str (s, cmd, COMMAND_SZ, "command");
    return 0;
}

static int
recv_krb5_auth (int s, u_char *buf,
		struct sockaddr_in thisaddr,
		struct sockaddr_in thataddr,
		char *client_username,
		char *server_username,
		char *cmd)
{
    u_int32_t len;
    krb5_auth_context auth_context = NULL;
    krb5_ticket *ticket;
    krb5_error_code status;
    krb5_authenticator authenticator;
    krb5_data cksum_data;

    if (memcmp (buf, "\x00\x00\x00\x13", 4) != 0)
	return -1;
    len = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3]);
	
    if (krb_net_read(s, buf, len) != len)
	syslog_and_die ("reading auth info: %m");
    if (len != sizeof(KRB5_SENDAUTH_VERSION)
	|| memcmp (buf, KRB5_SENDAUTH_VERSION, len) != 0)
	syslog_and_die ("bad sendauth version: %.8s", buf);
    
    krb5_init_context (&context);

    status = krb5_recvauth(context,
			   &auth_context,
			   &s,
			   KCMD_VERSION,
			   NULL /*server */,
			   KRB5_RECVAUTH_IGNORE_VERSION,
			   NULL,
			   &ticket);
    if (status)
	syslog_and_die ("krb5_recvauth: %s",
			krb5_get_err_text(context, status));

    read_str (s, client_username, USERNAME_SZ, "local username");
    read_str (s, cmd, COMMAND_SZ, "command");
    read_str (s, server_username, USERNAME_SZ, "remote username");

    status = krb5_auth_getauthenticator (context,
					 auth_context,
					 &authenticator);
    if (status)
	syslog_and_die ("krb5_auth_getauthenticator: %s",
			krb5_get_err_text(context, status));

    cksum_data.length = asprintf ((char **)&cksum_data.data,
				  "%u:%s%s",
				  ntohs(thisaddr.sin_port),
				  cmd,
				  server_username);

    status = krb5_verify_checksum (context,
				   cksum_data.data,
				   cksum_data.length,
				   authenticator->cksum);
    if (status)
	syslog_and_die ("krb5_verify_checksum: %s",
			krb5_get_err_text(context, status));

    free (cksum_data.data);
    krb5_free_authenticator (context, &authenticator);

    status = krb5_auth_con_getkey (context, auth_context, &keyblock);
    if (status)
	syslog_and_die ("krb5_auth_con_getkey: %s",
			krb5_get_err_text(context, status));

    /* discard forwarding information */
    krb_net_read (s, buf, 4);

    if(!krb5_kuserok (context,
		     ticket->enc_part2.client,
		     server_username))
	fatal (s, "Permission denied");

    if (strncmp (cmd, "-x ", 3) == 0) {
	do_encrypt = 1;
	memmove (cmd, cmd + 3, strlen(cmd) - 2);
    }
    return 0;
}

static void
loop (int from0, int to0,
      int to1,   int from1,
      int to2,   int from2)
{
    struct fd_set real_readset;
    int max_fd;
    int count = 2;

    FD_ZERO(&real_readset);
    FD_SET(from0, &real_readset);
    FD_SET(from1, &real_readset);
    FD_SET(from2, &real_readset);
    max_fd = max(from0, max(from1, from2)) + 1;
    for (;;) {
	int ret;
	struct fd_set readset = real_readset;
	char buf[RSH_BUFSIZ];

	ret = select (max_fd, &readset, NULL, NULL, NULL);
	if (ret < 0)
	    if (errno == EINTR)
		continue;
	    else
		syslog_and_die ("select: %m");
	if (FD_ISSET(from0, &readset)) {
	    ret = do_read (from0, buf, sizeof(buf));
	    if (ret < 0)
		syslog_and_die ("read: %m");
	    else if (ret == 0) {
		close (from0);
		FD_CLR(from0, &real_readset);
	    } else
		krb_net_write (to0, buf, ret);
	}
	if (FD_ISSET(from1, &readset)) {
	    ret = read (from1, buf, sizeof(buf));
	    if (ret < 0)
		syslog_and_die ("read: %m");
	    else if (ret == 0) {
		close (from1);
		FD_CLR(from1, &real_readset);
		if (--count == 0)
		    exit (0);
	    } else
		do_write (to1, buf, ret);
	}
	if (FD_ISSET(from2, &readset)) {
	    ret = read (from2, buf, sizeof(buf));
	    if (ret < 0)
		syslog_and_die ("read: %m");
	    else if (ret == 0) {
		close (from2);
		FD_CLR(from2, &real_readset);
		if (--count == 0)
		    exit (0);
	    } else
		do_write (to2, buf, ret);
	}
   }
}

static void
setup_copier (void)
{
    int p0[2], p1[2], p2[2];
    pid_t pid;

    if (pipe(p0) < 0)
	fatal (STDOUT_FILENO, "pipe: %m");
    if (pipe(p1) < 0)
	fatal (STDOUT_FILENO, "pipe: %m");
    if (pipe(p2) < 0)
	fatal (STDOUT_FILENO, "pipe: %m");
    pid = fork ();
    if (pid < 0)
	fatal (STDOUT_FILENO, "fork: %m");
    if (pid == 0) { /* child */
	close (p0[1]);
	close (p1[0]);
	close (p2[0]);
	dup2 (p0[0], STDIN_FILENO);
	dup2 (p1[1], STDOUT_FILENO);
	dup2 (p2[1], STDERR_FILENO);
	close (p0[0]);
	close (p1[1]);
	close (p2[1]);
    } else {
	close (p0[0]);
	close (p1[1]);
	close (p2[1]);

	if (krb_net_write (STDOUT_FILENO, "", 1) != 1)
	    fatal (STDOUT_FILENO, "write failed");

	loop (STDIN_FILENO, p0[1],
	      STDOUT_FILENO, p1[0],
	      STDERR_FILENO, p2[0]);
    }
}

static void
doit (void)
{
    u_char buf[BUFSIZ];
    u_char *p;
    struct sockaddr_in thisaddr, thataddr, erraddr;
    int addrlen;
    int port;
    int errsock = -1;
    char client_user[16], server_user[16];
    char cmd[COMMAND_SZ];
    struct passwd *pwd;
    int s = STDIN_FILENO;
    char *env[5];

    addrlen = sizeof(thisaddr);
    if (getsockname (s, (struct sockaddr *)&thisaddr, &addrlen) < 0
	|| addrlen != sizeof(thisaddr)) {
	syslog_and_die("getsockname: %m");
    }
    addrlen = sizeof(thataddr);
    if (getpeername (s, (struct sockaddr *)&thataddr, &addrlen) < 0
	|| addrlen != sizeof(thataddr)) {
	syslog_and_die ("getpeername: %m");
    }

    p = buf;
    port = 0;
    for(;;) {
	if (krb_net_read (s, p, 1) != 1)
	    syslog_and_die ("reading port number: %m");
	if (*p == '\0')
	    break;
	else if (isdigit(*p))
	    port = port * 10 + *p - '0';
	else
	    syslog_and_die ("non-digit in port number: %c", *p);
    }
    if (port) {
	int priv_port = IPPORT_RESERVED - 1;

	/* 
	 * There's no reason to require a ``privileged'' port number
	 * here, but for some reason the brain dead rsh clients
	 * do... :-(
	 */

	erraddr = thataddr;
	erraddr.sin_port = htons(port);
	errsock = rresvport (&priv_port);
	if (errsock < 0)
	    syslog_and_die ("socket: %m");
	if (connect (errsock,
		     (struct sockaddr *)&erraddr,
		     sizeof(erraddr)) < 0)
	    syslog_and_die ("connect: %m");
    }
    
    if (krb_net_read (s, buf, 4) != 4)
	syslog_and_die ("reading auth info: %m");
    
    if (recv_krb4_auth (s, buf, thisaddr, thataddr,
			client_user,
			server_user,
			cmd) == 0)
	auth_method = AUTH_KRB4;
    else if(recv_krb5_auth (s, buf, thisaddr, thataddr,
			    client_user,
			    server_user,
			    cmd) == 0)
	auth_method = AUTH_KRB5;
    else
	syslog_and_die ("unrecognized auth protocol: %x %x %x %x",
			buf[0], buf[1], buf[2], buf[3]);

    pwd = getpwnam (server_user);
    if (pwd == NULL)
	fatal (s, "Login incorrect.");

    if (*pwd->pw_shell == '\0')
	pwd->pw_shell = _PATH_BSHELL;

    if (pwd->pw_uid != 0 && access (_PATH_NOLOGIN, F_OK) == 0)
	fatal (s, "Login disabled.");
    
#ifdef HAVE_SETLOGIN
    if (setlogin(pwd->pw_name) < 0)
	syslog(LOG_ERR, "setlogin() failed: %m");
#endif

#ifdef HAVE_SETPCRED
    if (setpcred (pwd->pw_name, NULL) == -1)
	syslog(LOG_ERR, "setpcred() failure: %m");
#endif /* HAVE_SETPCRED */
    if (initgroups (pwd->pw_name, pwd->pw_gid) < 0)
	fatal (s, "Login incorrect.");

    if (setuid (pwd->pw_uid) < 0)
	fatal (s, "Login incorrect.");

    if (chdir (pwd->pw_dir) < 0)
	fatal (s, "Remote directory.");

    if (errsock >= 0) {
	if (dup2 (errsock, STDERR_FILENO) < 0)
	    fatal (s, "Dup2 failed.");
	close (errsock);
    }

    asprintf (&env[0], "USER=%s",  pwd->pw_name);
    asprintf (&env[1], "HOME=%s",  pwd->pw_dir);
    asprintf (&env[2], "SHELL=%s", pwd->pw_shell);
    asprintf (&env[3], "PATH=%s",  _PATH_DEFPATH);
    env[4] = NULL;

    if (do_encrypt) {
	setup_copier ();
    } else {
	if (krb_net_write (s, "", 1) != 1)
	    fatal (s, "write failed");
    }

    execle (pwd->pw_shell, pwd->pw_shell, "-c", cmd, NULL, env);
    err(1, "exec %s", pwd->pw_shell);
}

static void
usage (void)
{
    syslog (LOG_ERR, "Usage: %s [-ix] [-p port]", __progname);
    exit (1);
}

int
main(int argc, char **argv)
{
    int c;
    int inetd = 0;
    int port = 0;

    set_progname (argv[0]);
    openlog ("rshd", LOG_ODELAY, LOG_AUTH);

    while ((c = getopt(argc, argv, "ixp:")) != EOF) {
	switch (c) {
	case 'i' :
	    inetd = 1;
	    break;
	case 'x' :
	    do_encrypt = 1;
	    break;
	case 'p': {
	    struct servent *s = getservbyname (optarg, "tcp");

	    if (s)
		port = s->s_port;
	    else {
		char *ptr;

		port = strtol (optarg, &ptr, 10);
		if (port == 0 && ptr == optarg)
		    syslog_and_die ("Bad port `%s'", optarg);
		port = htons(port);
	    }
	    break;
	}
	default :
	    usage ();
	}
    }
    if (inetd) {
	if (port == 0)
	    if (do_encrypt)
		port = k_getportbyname ("ekshell", "tcp", htons(545));
	    else
		port = k_getportbyname ("kshell",  "tcp", htons(544));
	mini_inetd (port);
    }

    doit ();
}
