/*
 * Copyright (c) 1997 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "kdc_locl.h"

RCSID("$Id$");

struct port_desc{
    int family;
    int type;
    int port;
};

static struct port_desc *ports;
static int num_ports;

static void
add_port(int family, const char *port_str, const char *protocol)
{
    struct servent *sp;
    int type;
    int port;
    int i;

    sp = roken_getservbyname(port_str, protocol);
    if(sp){
	port = sp->s_port;
    }else{
	char *end;
	port = htons(strtol(port_str, &end, 0));
	if(end == port_str)
	    return;
    }
    if(strcmp(protocol, "udp") == 0)
	type = SOCK_DGRAM;
    else if(strcmp(protocol, "tcp") == 0)
	type = SOCK_STREAM;
    else
	return;
    for(i = 0; i < num_ports; i++){
	if(ports[i].type == type
	   && ports[i].port == port
	   && ports[i].family == family)
	    return;
    }
    ports = realloc(ports, (num_ports + 1) * sizeof(*ports));
    ports[num_ports].family = family;
    ports[num_ports].type   = type;
    ports[num_ports].port   = port;
    num_ports++;
}

static void
add_standard_ports (int family)
{
    add_port(family, "kerberos", "udp");
    add_port(family, "kerberos", "tcp");
    add_port(family, "kerberos-sec", "udp");
    add_port(family, "kerberos-sec", "tcp");
    add_port(family, "kerberos-iv", "udp");
    add_port(family, "kerberos-iv", "tcp");
    if(enable_http)
	add_port(family, "http", "tcp");
#ifdef KASERVER
    add_port(family, "7004", "udp");
#endif
}

static void
parse_ports(char *str)
{
    char *pos = NULL;
    char *p;
    p = strtok_r(str, " \t", &pos);
    while(p){
	if(strcmp(p, "+") == 0) {
#if defined(AF_INET6) && defined(HAVE_STRUCT_SOCKADDR_IN6)
	    add_standard_ports(AF_INET6);
#else
	    add_standard_ports(AF_INET);
#endif
	} else {
	    char *q = strchr(p, '/');
	    if(q){
		*q++ = 0;
#if defined(AF_INET6) && defined(HAVE_STRUCT_SOCKADDR_IN6)
		add_port(AF_INET6, p, q);
#else
		add_port(AF_INET, p, q);
#endif
	    }else {
#if defined(AF_INET6) && defined(HAVE_STRUCT_SOCKADDR_IN6)
		add_port(AF_INET6, p, "udp");
		add_port(AF_INET6, p, "tcp");
#else
		add_port(AF_INET, p, "udp");
		add_port(AF_INET, p, "tcp");
#endif
	    }
	}
	    
	p = strtok_r(NULL, " \t", &pos);
    }
}

struct descr {
    int s;
    int type;
    unsigned char *buf;
    size_t size;
    size_t len;
    time_t timeout;
};

static void 
init_socket(struct descr *d, int family, int type, int port)
{
    krb5_error_code ret;
    struct sockaddr *sa;
    void *sa_buf;
    int sa_size;

    sa_size = krb5_max_sockaddr_size ();
    sa_buf = malloc(sa_size);
    if (sa_buf == NULL) {
	kdc_log(0, "Failed to allocate %u bytes", sa_size);
	return;
    }
    sa = (struct sockaddr *)sa_buf;

    memset(d, 0, sizeof(*d));
    d->s = socket(family, type, 0);
    if(d->s < 0){
	krb5_warn(context, errno, "socket(%d, %d, 0)", family, type);
	d->s = -1;
	goto out;
    }
#if defined(HAVE_SETSOCKOPT) && defined(SOL_SOCKET) && defined(SO_REUSEADDR)
    {
	int one = 1;
	setsockopt(d->s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }
#endif
    d->type = type;
    ret = krb5_anyaddr (family, sa, &sa_size, port);
    if (ret) {
	krb5_warn(context, ret, "krb5_anyaddr");
	close(d->s);
	d->s = -1;
	goto out;
    }

    if(bind(d->s, sa, sa_size) < 0){
	krb5_warn(context, errno, "bind(%d)", ntohs(port));
	close(d->s);
	d->s = -1;
	goto out;
    }
    if(type == SOCK_STREAM && listen(d->s, SOMAXCONN) < 0){
	krb5_warn(context, errno, "listen");
	close(d->s);
	d->s = -1;
    }
out:
    free (sa_buf);
}

static int
init_sockets(struct descr **desc)
{
    int i;
    struct descr *d;
    int num = 0;

    parse_ports(port_str);
    d = malloc(num_ports * sizeof(*d));
    if (d == NULL)
	krb5_errx(context, 1, "malloc(%u) failed", num_ports * sizeof(*d));

    for (i = 0; i < num_ports; i++){
	init_socket(&d[num], ports[i].family, ports[i].type, ports[i].port);
	if(d[num].s != -1){
	    kdc_log(5, "listening to port %u/%s", ntohs(ports[i].port), 
		    (ports[i].type == SOCK_STREAM) ? "tcp" : "udp"); /* XXX */
	    num++;
	}
    }
    d = realloc(d, num * sizeof(*d));
    if (d == NULL)
	krb5_errx(context, 1, "realloc(%u) failed", num * sizeof(*d));
    *desc = d;
    return num;
}

    
static int
process_request(unsigned char *buf, 
		size_t len, 
		krb5_data *reply,
		const char *from,
		struct sockaddr *addr)
{
    KDC_REQ req;
#ifdef KRB4
    Ticket ticket;
#endif
    krb5_error_code ret;
    size_t i;

    gettimeofday(&now, NULL);
    if(decode_AS_REQ(buf, len, &req, &i) == 0){
	ret = as_rep(&req, reply, from);
	free_AS_REQ(&req);
	return ret;
    }else if(decode_TGS_REQ(buf, len, &req, &i) == 0){
	ret = tgs_rep(&req, reply, from);
	free_TGS_REQ(&req);
	return ret;
    }
#ifdef KRB4
    else if(maybe_version4(buf, len))
	do_version4(buf, len, reply, from, (struct sockaddr_in*)addr);
    else if(decode_Ticket(buf, len, &ticket, &i) == 0){
	ret = do_524(&ticket, reply, from);
	free_Ticket(&ticket);
	return ret;
    }
#endif
#ifdef KASERVER
    else {
	ret = do_kaserver (buf, len, reply, from, (struct sockaddr_in*)addr);
	return ret;
    }
#endif
			  
    return -1;
}

static void
addr_to_string(struct sockaddr *addr, size_t addr_len, char *str, size_t len)
{
    switch(addr->sa_family){
    case AF_INET:
	strncpy(str, inet_ntoa(((struct sockaddr_in*)addr)->sin_addr), len);
	break;
#if defined(AF_INET6) && defined(HAVE_STRUCT_SOCKADDR_IN6) && defined(HAVE_INET_NTOP)
    case AF_INET6 :
	inet_ntop(AF_INET6, &((struct sockaddr_in6*)addr)->sin6_addr,
		  str, len);
	break;
#endif
    default:
	snprintf(str, len, "<%d addr>", addr->sa_family);
    }
    str[len - 1] = 0;
}

static void
do_request(void *buf, size_t len, 
	   int socket, struct sockaddr *from, size_t from_len)
{
    krb5_error_code ret;
    krb5_data reply;
    
    char addr[128];
    addr_to_string(from, from_len, addr, sizeof(addr));
    
    reply.length = 0;
    ret = process_request(buf, len, &reply, addr, from);
    if(reply.length){
	kdc_log(5, "sending %d bytes to %s", reply.length, addr);
	sendto(socket, reply.data, reply.length, 0, from, from_len);
	krb5_data_free(&reply);
    }
    if(ret)
	kdc_log(0, "Failed processing %lu byte request from %s", 
		(unsigned long)len, addr);
}

static void
handle_udp(struct descr *d)
{
    unsigned char *buf;
    struct sockaddr *sa;
    void *sa_buf;
    int sa_size;
    int from_len;
    size_t n;

    sa_size = krb5_max_sockaddr_size ();
    sa_buf = malloc(sa_size);
    if (sa_buf == NULL) {
	kdc_log(0, "Failed to allocate %u bytes", sa_size);
	return;
    }
    sa = (struct sockaddr *)sa_buf;
    
    buf = malloc(max_request);
    if(buf == NULL){
	kdc_log(0, "Failed to allocate %u bytes", max_request);
	free (sa_buf);
	return;
    }

    from_len = sa_size;
    n = recvfrom(d->s, buf, max_request, 0, 
		 sa, &from_len);
    if(n < 0){
	krb5_warn(context, errno, "recvfrom");
	goto out;
    }
    if(n == 0){
	goto out;
    }
    do_request(buf, n, d->s, sa, from_len);
out:
    free (buf);
    free (sa_buf);
}

static void
clear_descr(struct descr *d)
{
    if(d->buf)
	memset(d->buf, 0, d->size);
    d->len = 0;
    if(d->s != -1)
	close(d->s);
    d->s = -1;
}

#define TCP_TIMEOUT 4

static void
handle_tcp(struct descr *d, int index, int min_free)
{
    unsigned char buf[1024];
    char addr[32];
    void *sa_buf;
    struct sockaddr *sa;
    int sa_size;
    int from_len;
    size_t n;

    sa_size = krb5_max_sockaddr_size ();
    sa_buf = malloc(sa_size);
    if (sa_buf == NULL) {
	kdc_log(0, "Failed to allocate %u bytes", sa_size);
	return;
    }
    sa = (struct sockaddr *)sa_buf;

    if(d[index].timeout == 0){
	int s;

	from_len = sa_size;
	s = accept(d[index].s, sa, &from_len);
	if(s < 0){
	    krb5_warn(context, errno, "accept");
	    goto out;
	}
	if(min_free == -1){
	    close(s);
	    goto out;
	}
	    
	d[min_free].s = s;
	d[min_free].timeout = time(NULL) + TCP_TIMEOUT;
	d[min_free].type = SOCK_STREAM;
	goto out;
    }
    from_len = sa_size;
    n = recvfrom(d[index].s, buf, sizeof(buf), 0, 
		 sa, &from_len);
    if(n < 0){
	krb5_warn(context, errno, "recvfrom");
	goto out;
    }
    /* sometimes recvfrom doesn't return an address */
    if(from_len == 0){
	from_len = sa_size;
	getpeername(d[index].s, sa, &from_len);
    }
    addr_to_string(sa, from_len, addr, sizeof(addr));
    if(d[index].size - d[index].len < n){
	unsigned char *tmp;
	d[index].size += 1024;
	if(d[index].size >= max_request){
	    kdc_log(0, "Request exceeds max request size (%u bytes).",
		    d[index].size);
	    clear_descr(d + index);
	    goto out;
	}
	tmp = realloc(d[index].buf, d[index].size);
	if(tmp == NULL){
	    kdc_log(0, "Failed to re-allocate %u bytes.", d[index].size);
	    clear_descr(d + index);
	    goto out;
	}
	d[index].buf = tmp;
    }
    memcpy(d[index].buf + d[index].len, buf, n);
    d[index].len += n;
    if(d[index].len > 4 && d[index].buf[0] == 0){
	krb5_storage *sp;
	int32_t len;
	sp = krb5_storage_from_mem(d[index].buf, d[index].len);
	krb5_ret_int32(sp, &len);
	krb5_storage_free(sp);
	if(d[index].len - 4 >= len){
	    memcpy(d[index].buf, d[index].buf + 4, d[index].len - 4);
	    n = 0;
	}
    }
    else if(enable_http &&
	    strncmp((char *)d[index].buf, "GET ", 4) == 0 && 
	    strncmp((char *)d[index].buf + d[index].len - 4,
		    "\r\n\r\n", 4) == 0){
	char *s, *p, *t;
	void *data;
	int len;
	s = (char *)d[index].buf;
	p = strstr(s, "\r\n");
	*p = 0;
	p = NULL;
	strtok_r(s, " \t", &p);
	t = strtok_r(NULL, " \t", &p);
	if(t == NULL){
	    kdc_log(0, "Malformed HTTP request from %s", addr);
	    clear_descr(d + index);
	    goto out;
	}
	data = malloc(strlen(t));
	if (data == NULL) {
	    kdc_log(0, "Failed to allocate %u bytes", strlen(t));
	    goto out;
	}
	if(*t == '/')
	    t++;
	len = base64_decode(t, data);
	if(len <= 0){
	    const char *msg = 
		"HTTP/1.1 404 Not found\r\n"
		"Server: Heimdal/" VERSION "\r\n"
		"Content-type: text/html\r\n"
		"Content-transfer-encoding: 8bit\r\n\r\n"
		"<TITLE>404 Not found</TITLE>\r\n"
		"<H1>404 Not found</H1>\r\n"
		"That page doesn't exist, maybe you are looking for "
		"<A HREF=\"http://www.pdc.kth.se/heimdal\">Heimdal</A>?\r\n";
	    write(d[index].s, msg, strlen(msg));
	    free(data);
	    clear_descr(d + index);
	    kdc_log(0, "HTTP request from %s is non KDC request", addr);
	    goto out;
	}
	{
	    const char *msg = 
		"HTTP/1.1 200 OK\r\n"
		"Server: Heimdal/" VERSION "\r\n"
		"Content-type: application/octet-stream\r\n"
		"Content-transfer-encoding: binary\r\n\r\n";
	    write(d[index].s, msg, strlen(msg));
	}
	memcpy(d[index].buf, data, len);
	d[index].len = len;
	n = 0;
	free(data);
    }
    if(n == 0){
	do_request(d[index].buf, d[index].len, 
		   d[index].s, sa, from_len);
	clear_descr(d + index);
    }
out:
    free (sa_buf);
    return;
}

void
loop(void)
{
    struct descr *d;
    int ndescr;
    ndescr = init_sockets(&d);
    if(ndescr <= 0)
	krb5_errx(context, 1, "No sockets!");
    while(exit_flag == 0){
	struct timeval tmout;
	fd_set fds;
	int min_free = -1;
	int max_fd = 0;
	int i;
	FD_ZERO(&fds);
	for(i = 0; i < ndescr; i++){
	    if(d[i].s >= 0){
		if(d[i].type == SOCK_STREAM && 
		   d[i].timeout && d[i].timeout < time(NULL)){
		    struct sockaddr sa;
		    int salen = sizeof(sa);
		    char addr[32];

		    getpeername(d[i].s, &sa, &salen);
		    addr_to_string(&sa, salen, addr, sizeof(addr));
		    kdc_log(1, "TCP-connection from %s expired after %u bytes",
			    addr, d[i].len);
		    clear_descr(&d[i]);
		    continue;
		}
		if(max_fd < d[i].s)
		    max_fd = d[i].s;
		FD_SET(d[i].s, &fds);
	    }else if(min_free < 0 || i < min_free)
		min_free = i;
	}
	if(min_free == -1){
	    struct descr *tmp;
	    tmp = realloc(d, (ndescr + 4) * sizeof(*d));
	    if(tmp == NULL)
		krb5_warnx(context, "No memory");
	    else{
		d = tmp;
		memset(d + ndescr, 0, 4 * sizeof(*d));
		for(i = ndescr; i < ndescr + 4; i++)
		    d[i].s = -1;
		min_free = ndescr;
		ndescr += 4;
	    }
	}
    
	tmout.tv_sec = TCP_TIMEOUT;
	tmout.tv_usec = 0;
	switch(select(max_fd + 1, &fds, 0, 0, &tmout)){
	case 0:
	    break;
	case -1:
	    krb5_warn(context, errno, "select");
	    break;
	default:
	    for(i = 0; i < ndescr; i++)
		if(d[i].s >= 0 && FD_ISSET(d[i].s, &fds))
		    if(d[i].type == SOCK_DGRAM)
			handle_udp(&d[i]);
		    else if(d[i].type == SOCK_STREAM)
			handle_tcp(d, i, min_free);
	}
    }
    free (d);
}
