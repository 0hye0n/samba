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

#if 0
/* This is the supposedly MIT-api version */

krb5_boolean
krb5_address_search(krb5_context context,
		    const krb5_address *addr,
		    krb5_address *const *addrlist)
{
  krb5_address *a;

  while((a = *addrlist++))
    if (krb5_address_compare (context, addr, a))
      return TRUE;
  return FALSE;
}
#endif

krb5_boolean
krb5_address_search(krb5_context context,
		    const krb5_address *addr,
		    const krb5_addresses *addrlist)
{
    int i;

    for (i = 0; i < addrlist->len; ++i)
	if (krb5_address_compare (context, addr, &addrlist->val[i]))
	    return TRUE;
    return FALSE;
}

int
krb5_address_order(krb5_context context,
		   const krb5_address *addr1,
		   const krb5_address *addr2)
{
    return (addr1->addr_type - addr2->addr_type)
	|| memcmp (addr1->address.data,
		   addr2->address.data,
		   addr1->address.length);
}

krb5_boolean
krb5_address_compare(krb5_context context,
		     const krb5_address *addr1,
		     const krb5_address *addr2)
{
    return krb5_address_order (context, addr1, addr2) == 0;
}
#if 0
  return addr1->addr_type == addr2->addr_type
    && memcmp (addr1->address.data,
	       addr2->address.data,
	       addr1->address.length) == 0;
#endif

krb5_error_code
krb5_copy_address(krb5_context context,
		  const krb5_address *inaddr,
		  krb5_address *outaddr)
{
    copy_HostAddress(inaddr, outaddr);
    return 0;
}

krb5_error_code
krb5_copy_addresses(krb5_context context,
		    const krb5_addresses *inaddr,
		    krb5_addresses *outaddr)
{
    copy_HostAddresses(inaddr, outaddr);
    return 0;
}

krb5_error_code
krb5_free_address(krb5_context context,
		  krb5_address *address)
{
  krb5_data_free (&address->address);
  return 0;
}

krb5_error_code
krb5_free_addresses(krb5_context context,
		    krb5_addresses *addresses)
{
    free_HostAddresses(addresses);
    return 0;
}
