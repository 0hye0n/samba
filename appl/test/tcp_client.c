#include "test_locl.h"
RCSID("$Id$");

static void
usage (void)
{
    errx (1, "Usage: %s [-p port] [-s service] host", __progname);
}

static int
proto (int sock, const char *hostname, const char *service)
{
    struct sockaddr_in remote, local;
    int addrlen;
    krb5_address remote_addr, local_addr;
    krb5_context context;
    krb5_ccache ccache;
    krb5_auth_context auth_context;
    krb5_error_code status;
    krb5_principal server;
    krb5_data data;
    krb5_data packet;
    u_int32_t len, net_len;

    addrlen = sizeof(local);
    if (getsockname (sock, (struct sockaddr *)&local, &addrlen) < 0
	|| addrlen != sizeof(local))
	err (1, "getsockname(%s)", hostname);

    addrlen = sizeof(remote);
    if (getpeername (sock, (struct sockaddr *)&remote, &addrlen) < 0
	|| addrlen != sizeof(remote))
	err (1, "getpeername(%s)", hostname);

    status = krb5_init_context(&context);
    if (status)
	errx (1, "krb5_init_context: %s",
	      krb5_get_err_text(context, status));

    status = krb5_cc_default (context, &ccache);
    if (status)
	errx (1, "krb5_cc_default: %s",
	      krb5_get_err_text(context, status));

    status = krb5_auth_con_init (context, &auth_context);
    if (status)
	errx (1, "krb5_auth_con_init: %s",
	      krb5_get_err_text(context, status));

    local_addr.addr_type = AF_INET;
    local_addr.address.length = sizeof(local.sin_addr);
    local_addr.address.data   = &local.sin_addr;

    remote_addr.addr_type = AF_INET;
    remote_addr.address.length = sizeof(remote.sin_addr);
    remote_addr.address.data   = &remote.sin_addr;

    status = krb5_auth_con_setaddrs (context,
				     auth_context,
				     &local_addr,
				     &remote_addr);
    if (status)
	errx (1, "krb5_auth_con_setaddr: %s",
	      krb5_get_err_text(context, status));

    status = krb5_sname_to_principal (context,
				      hostname,
				      service,
				      KRB5_NT_SRV_HST,
				      &server);
    if (status)
	errx (1, "krb5_sname_to_principal: %s",
	      krb5_get_err_text(context, status));

    status = krb5_sendauth (context,
			    &auth_context,
			    &sock,
			    VERSION,
			    NULL,
			    server,
			    AP_OPTS_MUTUAL_REQUIRED,
			    NULL,
			    NULL,
			    ccache,
			    NULL,
			    NULL,
			    NULL);
    if (status)
	errx (1, "krb5_sendauth: %s",
	      krb5_get_err_text(context, status));

    data.data   = "hej";
    data.length = 3;

    krb5_data_zero (&packet);

    status = krb5_mk_safe (context,
			   auth_context,
			   &data,
			   &packet,
			   NULL);
    if (status)
	errx (1, "krb5_mk_safe: %s",
	      krb5_get_err_text(context, status));

    len = packet.length;
    net_len = htonl(len);

    if (krb5_net_write (context, sock, &net_len, 4) != 4)
	err (1, "krb5_net_write");
    if (krb5_net_write (context, sock, packet.data, len) != len)
	err (1, "krb5_net_write");

    data.data   = "hemligt";
    data.length = 7;

    krb5_data_free (&packet);

    status = krb5_mk_priv (context,
			   auth_context,
			   &data,
			   &packet,
			   NULL);
    if (status)
	errx (1, "krb5_mk_priv: %s",
	      krb5_get_err_text(context, status));

    len = packet.length;
    net_len = htonl(len);

    if (krb5_net_write (context, sock, &net_len, 4) != 4)
	err (1, "krb5_net_write");
    if (krb5_net_write (context, sock, packet.data, len) != len)
	err (1, "krb5_net_write");
    return 0;
}

static int
doit (const char *hostname, int port, const char *service)
{
    struct in_addr **h;
    struct hostent *hostent;

    hostent = gethostbyname (hostname);
    if (hostent == NULL)
	errx (1, "gethostbyname '%s' failed: %s",
	      hostname,
	      hstrerror(h_errno));

    for (h = (struct in_addr **)hostent->h_addr_list;
	*h != NULL;
	 ++h) {
	struct sockaddr_in addr;
	int s;

	memset (&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = port;
	addr.sin_addr   = **h;

	s = socket (AF_INET, SOCK_STREAM, 0);
	if (s < 0)
	    err (1, "socket");
	if (connect (s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	    warn ("connect(%s)", hostname);
	    close (s);
	    continue;
	}
	return proto (s, hostname, service);
    }
    return 1;
}

int
main(int argc, char **argv)
{
    int c;
    int port = 0;
    char *service = SERVICE;

    set_progname (argv[0]);

    while ((c = getopt (argc, argv, "p:s:")) != EOF) {
	switch (c) {
	case 'p': {
	    struct servent *s = getservbyname (optarg, "tcp");

	    if (s)
		port = s->s_port;
	    else {
		char *ptr;

		port = strtol (optarg, &ptr, 10);
		if (port == 0 && ptr == optarg)
		    errx (1, "Bad port `%s'", optarg);
		port = htons(port);
	    }
	    break;
	}
	case 's':
	    service = optarg;
	    break;
	default:
	    usage ();
	    break;
	}
    }
    argc -= optind;
    argv += optind;

    if (argc != 1)
	usage ();

    if (port == 0)
	port = krb5_getportbyname (PORT, "tcp", htons(4711));

    return doit (*argv, port, service);
}
