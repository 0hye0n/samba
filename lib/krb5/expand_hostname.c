/*
 * Copyright (c) 1999 Kungliga Tekniska H�gskolan
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

krb5_error_code
krb5_expand_hostname (krb5_context context,
		      const char *orig_hostname,
		      char **new_hostname)
{
    struct hostent *he = NULL;
    int error;
    char *tmp;

#ifdef HAVE_IPV6
    {
	struct in6_addr sin6;

	if (he == NULL && inet_pton (AF_INET6, orig_hostname, &sin6) == 1)
	    he = getipnodebyaddr (&sin6, sizeof(sin6), AF_INET6, &error);
    }
#endif
    {
	struct in_addr sin;

	if (he == NULL && inet_pton (AF_INET, orig_hostname, &sin) == 1)
	    he = getipnodebyaddr (&sin, sizeof(sin), AF_INET, &error);
    }
#ifdef HAVE_IPV6
    if (he == NULL)
	he = getipnodebyname (orig_hostname, AF_INET6, 0, &error);
#endif
    if (he == NULL)
	he = getipnodebyname (orig_hostname, AF_INET, 0, &error);

    if (he == NULL) {
	*new_hostname = strdup (orig_hostname);
	if (*new_hostname == NULL)
	    return ENOMEM;
	return 0;
    }
    tmp = he->h_name;
    if (strchr (tmp, '.') == NULL
	&& he->h_aliases != NULL
	&& he->h_aliases[0] != NULL
	&& strchr (he->h_aliases[0], '.') != NULL)
	tmp = he->h_aliases[0];
    *new_hostname = strdup (tmp);
    freehostent (he);
    if (*new_hostname == NULL)
	return ENOMEM;
    return 0;
}
