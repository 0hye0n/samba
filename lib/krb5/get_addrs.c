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

#include "krb5_locl.h"

RCSID("$Id$");

#ifdef __osf__
/* hate */
struct rtentry;
struct mbuf;
#endif
#ifdef HAVE_NET_IF_H
#include <net/if.h>
#endif

#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif /* HAVE_SYS_SOCKIO_H */

#ifdef HAVE_NETINET_IN6_VAR_H
#include <netinet/in6_var.h>
#endif /* HAVE_NETINET_IN6_VAR_H */

static krb5_error_code
gethostname_fallback (krb5_addresses *res)
{
     krb5_error_code err;
     char hostname[MAXHOSTNAMELEN];
     struct hostent *hostent;

     if (gethostname (hostname, sizeof(hostname)))
	  return errno;
     hostent = gethostbyname (hostname);
     if (hostent == NULL)
	  return errno;
     res->len = 1;
     res->val = malloc (sizeof(*res->val));
     if (res->val == NULL)
	 return ENOMEM;
     res->val[0].addr_type = hostent->h_addrtype;
     res->val[0].address.data = NULL;
     res->val[0].address.length = 0;
     err = krb5_data_copy (&res->val[0].address,
			   hostent->h_addr,
			   hostent->h_length);
     if (err) {
	 free (res->val);
	 return err;
     }
     return 0;
}

/*
 *
 * Try to figure out the addresses of all configured interfaces with a
 * lot of magic ioctls.
 * Include loopback (lo*) interfaces iff loop.
 */

static krb5_error_code
find_all_addresses (krb5_addresses *res, int loop,
		    int af, int siocgifconf, int siocgifflags,
		    size_t ifreq_sz)
{
     krb5_error_code ret;
     int fd;
     size_t buf_size;
     char *buf;
     struct ifconf ifconf;
     int num, j;
     char *p;
     size_t sz;
     struct sockaddr sa_zero;
     struct ifreq *ifr;

     buf = NULL;
     res->val = NULL;

     memset (&sa_zero, 0, sizeof(sa_zero));
     fd = socket(af, SOCK_DGRAM, 0);
     if (fd < 0)
	  return -1;

     buf_size = 8192;
     do {
	 buf = malloc(buf_size);
	 if (buf == NULL) {
	     ret = ENOMEM;
	     goto error_out;
	 }
	 ifconf.ifc_len = buf_size;
	 ifconf.ifc_buf = buf;
	 if (ioctl (fd, siocgifconf, &ifconf) < 0) {
	     ret = errno;
	     goto error_out;
	 }
	 /*
	  * Can the difference between a full and a overfull buf
	  * be determined?
	  */

	 if (ifconf.ifc_len == buf_size)
	     free (buf);
     } while (ifconf.ifc_len == buf_size);

     num = ifconf.ifc_len / ifreq_sz;
     res->len = num;
     res->val = calloc(num, sizeof(*res->val));
     if (res->val == NULL) {
	 ret = ENOMEM;
	 goto error_out;
     }

     j = 0;
     for (p = ifconf.ifc_buf;
	  p < ifconf.ifc_buf + ifconf.ifc_len;
	  p += sz) {
	 struct ifreq ifreq;
	 struct sockaddr *sa;

	 ifr = (struct ifreq *)p;
	 sa  = &ifr->ifr_addr;

	 sz = ifreq_sz;
#ifdef SOCKADDR_HAS_SA_LEN
	 sz = max(sz, sizeof(ifr->ifr_name) + sa->sa_len);
#endif

	 memcpy (ifreq.ifr_name, ifr->ifr_name, sizeof(ifr->ifr_name));

	 if (ioctl(fd, siocgifflags, &ifreq) < 0) {
	     ret = errno;
	     goto error_out;
	 }

	 if(!(ifreq.ifr_flags & IFF_UP)
	    || (!loop && (ifreq.ifr_flags & IFF_LOOPBACK))
	    || memcmp (sa, &sa_zero, sizeof(sa_zero)) == 0)
	     continue;

	 switch (sa->sa_family) {
#ifdef AF_INET
	 case AF_INET: {
	     unsigned char addr[4];
	     struct sockaddr_in *sin;
	     res->val[j].addr_type = AF_INET;
	     /* This is somewhat XXX */
	     sin = (struct sockaddr_in*)sa;
	     memcpy(addr, &sin->sin_addr, 4);
	     ret = krb5_data_copy(&res->val[j].address,
				  addr, 4);
	     if (ret)
		 goto error_out;
	     ++j;
	     break;
	 }
#endif /* AF_INET */
#if defined(AF_INET6) && defined(HAVE_SOCKADDR_IN6)
	 case AF_INET6: {
	     struct in6_addr *sin6;

	     sin6 = &((struct sockaddr_in6 *)(&ifr->ifr_addr))->sin6_addr;

	     if (IN6_IS_ADDR_LOOPBACK(sin6)
		 || IN6_IS_ADDR_LINKLOCAL(sin6)
		 || IN6_IS_ADDR_V4COMPAT(sin6)) {
		 break;
	     } else {
		 res->val[j].addr_type = AF_INET6;
		 ret = krb5_data_copy(&res->val[j].address,
				      sin6,
				      sizeof(struct in6_addr));
	     }
	     if (ret)
		 goto error_out;
	     ++j;
	     break;
	 }
#endif /* AF_INET6 */
	 default:
	     break;
	 }
     }
     if (j != num) {
	 void *tmp;

	 res->len = j;
	 tmp = realloc (res->val, j * sizeof(*res->val));
	 if (j != 0 && tmp == NULL) {
	     ret = ENOMEM;
	     goto error_out;
	 }
	 res->val = tmp;
     }
     ret = 0;
     goto cleanup;

error_out:
     while(j--) {
	 krb5_data_free (&res->val[j].address);
     }
     free (res->val);
cleanup:
     close (fd);
     free (buf);
     return ret;
}

/*
 * Try to get all addresses, but return the one corresponding to
 * `hostname' if we fail.
 *
 * Don't include any loopback addresses (interfaces of name lo*).
 *
 */

krb5_error_code
krb5_get_all_client_addrs (krb5_addresses *res)
{
#if defined(AF_INET6) && defined(SIOCGIF6CONF) && defined(SIOCGIF6FLAGS)
    return find_all_addresses (res, 1,
			       AF_INET6, SIOCGIF6CONF, SIOCGIF6FLAGS,
			       sizeof(struct in6_ifreq));
#elif defined(AF_INET) && defined(SIOCGIFCONF) && defined(SIOCGIFFLAGS)
    return find_all_addresses (res, 0,
			       AF_INET, SIOCGIFCONF, SIOCGIFFLAGS,
			       sizeof(struct ifreq));
#else
    return gethostname_fallback (res);
#endif
}

/*
 * XXX
 */

#if 0
krb5_error_code
krb5_get_all_server_addrs ()
{
    return 0;
}
#endif
