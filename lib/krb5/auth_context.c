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

krb5_error_code
krb5_auth_con_init(krb5_context context,
		   krb5_auth_context *auth_context)
{
    krb5_auth_context p;

    ALLOC(p, 1);
    if(!p)
	return ENOMEM;
    memset(p, 0, sizeof(*p));
    ALLOC(p->authenticator, 1);
    if (!p->authenticator)
	return ENOMEM;
    memset (p->authenticator, 0, sizeof(*p->authenticator));
    p->flags = KRB5_AUTH_CONTEXT_DO_TIME;

    /*
     * These choices use checksum and encryption methods from the
     * spec.  Hopefully they are supported by all implementations.
     */

    p->cksumtype = CKSUMTYPE_RSA_MD5_DES;
    p->enctype   = ETYPE_DES_CBC_MD5;
    p->local_address = NULL;
    p->remote_address = NULL;
    *auth_context = p;
    return 0;
}

krb5_error_code
krb5_auth_con_free(krb5_context context,
		   krb5_auth_context auth_context)
{
    krb5_free_authenticator(context, &auth_context->authenticator);
    if(auth_context->local_address){
	free_HostAddress(auth_context->local_address);
	free(auth_context->local_address);
    }
    if(auth_context->remote_address){
	free_HostAddress(auth_context->remote_address);
	free(auth_context->remote_address);
    }
    free_EncryptionKey(&auth_context->key);
    free_EncryptionKey(&auth_context->remote_subkey);
    free_EncryptionKey(&auth_context->local_subkey);
    free (auth_context);
    return 0;
}

krb5_error_code
krb5_auth_con_setflags(krb5_context context,
		       krb5_auth_context auth_context,
		       int32_t flags)
{
    auth_context->flags = flags;
    return 0;
}


krb5_error_code
krb5_auth_con_getflags(krb5_context context,
		       krb5_auth_context auth_context,
		       int32_t *flags)
{
    *flags = auth_context->flags;
    return 0;
}


krb5_error_code
krb5_auth_con_setaddrs(krb5_context context,
		       krb5_auth_context auth_context,
		       krb5_address *local_addr,
		       krb5_address *remote_addr)
{
    if (local_addr) {
	if (auth_context->local_address)
	    krb5_free_address (context, auth_context->local_address);
	else
	    auth_context->local_address = malloc(sizeof(krb5_address));
	krb5_copy_address(context, local_addr, auth_context->local_address);
    }
    if (remote_addr) {
	if (auth_context->remote_address)
	    krb5_free_address (context, auth_context->remote_address);
	else
	    auth_context->remote_address = malloc(sizeof(krb5_address));
	krb5_copy_address(context, remote_addr, auth_context->remote_address);
    }
    return 0;
}


krb5_error_code
krb5_auth_con_getaddrs(krb5_context context,
		       krb5_auth_context auth_context,
		       krb5_address **local_addr,
		       krb5_address **remote_addr)
{
    krb5_error_code ret;

    if(*local_addr)
	krb5_free_address (context, *local_addr);
    *local_addr = malloc (sizeof(**local_addr));
    if (*local_addr == NULL)
	return ENOMEM;
    krb5_copy_address(context,
		      auth_context->local_address,
		      *local_addr);

    if(*remote_addr)
	krb5_free_address (context, *remote_addr);
    *remote_addr = malloc (sizeof(**remote_addr));
    if (*remote_addr == NULL)
	return ENOMEM;
    krb5_copy_address(context,
		      auth_context->remote_address,
		      *remote_addr);
    return 0;
}

krb5_error_code
krb5_auth_con_getkey(krb5_context context,
		     krb5_auth_context auth_context,
		     krb5_keyblock **keyblock)
{
  *keyblock = malloc(sizeof(**keyblock));
  if (*keyblock == NULL)
    return ENOMEM;
  (*keyblock)->keytype = auth_context->key.keytype;
  (*keyblock)->keyvalue.length = 0;
  return krb5_data_copy (&(*keyblock)->keyvalue,
			 auth_context->key.keyvalue.data,
			 auth_context->key.keyvalue.length);
}

krb5_error_code
krb5_auth_con_getlocalsubkey(krb5_context context,
			     krb5_auth_context auth_context,
			     krb5_keyblock **keyblock)
{
  *keyblock = malloc(sizeof(**keyblock));
  if (*keyblock == NULL)
    return ENOMEM;
  (*keyblock)->keytype = auth_context->local_subkey.keytype;
  (*keyblock)->keyvalue.length = 0;
  return krb5_data_copy (&(*keyblock)->keyvalue,
			 auth_context->local_subkey.keyvalue.data,
			 auth_context->local_subkey.keyvalue.length);
}

krb5_error_code
krb5_auth_con_getremotesubkey(krb5_context context,
			      krb5_auth_context auth_context,
			      krb5_keyblock **keyblock)
{
  *keyblock = malloc(sizeof(**keyblock));
  if (*keyblock == NULL)
    return ENOMEM;
  (*keyblock)->keytype = auth_context->remote_subkey.keytype;
  (*keyblock)->keyvalue.length = 0;
  return krb5_data_copy (&(*keyblock)->keyvalue,
			 auth_context->remote_subkey.keyvalue.data,
			 auth_context->remote_subkey.keyvalue.length);
}

void
krb5_free_keyblock(krb5_context context,
		   krb5_keyblock *keyblock)
{
    memset(keyblock->keyvalue.data, 0, keyblock->keyvalue.length);
    krb5_data_free (&keyblock->keyvalue);
}

krb5_error_code
krb5_auth_setcksumtype(krb5_context context,
		       krb5_auth_context auth_context,
		       krb5_cksumtype cksumtype)
{
    auth_context->cksumtype = cksumtype;
    return 0;
}

krb5_error_code
krb5_auth_getcksumtype(krb5_context context,
		       krb5_auth_context auth_context,
		       krb5_cksumtype *cksumtype)
{
    *cksumtype = auth_context->cksumtype;
    return 0;
}

krb5_error_code
krb5_auth_getlocalseqnumber(krb5_context context,
			    krb5_auth_context auth_context,
			    int32_t *seqnumber)
{
  *seqnumber = auth_context->local_seqnumber;
  return 0;
}

krb5_error_code
krb5_auth_setlocalseqnumber (krb5_context context,
			     krb5_auth_context auth_context,
			     int32_t seqnumber)
{
  auth_context->local_seqnumber = seqnumber;
  return 0;
}

krb5_error_code
krb5_auth_getremoteseqnumber(krb5_context context,
			     krb5_auth_context auth_context,
			     int32_t *seqnumber)
{
  *seqnumber = auth_context->remote_seqnumber;
  return 0;
}

krb5_error_code
krb5_auth_setremoteseqnumber (krb5_context context,
			      krb5_auth_context auth_context,
			      int32_t seqnumber)
{
  auth_context->remote_seqnumber = seqnumber;
  return 0;
}


krb5_error_code
krb5_auth_getauthenticator(krb5_context context,
			   krb5_auth_context auth_context,
			   krb5_authenticator *authenticator)
{
    *authenticator = malloc(sizeof(**authenticator));
    if (*authenticator == NULL)
	return ENOMEM;

    copy_Authenticator(auth_context->authenticator,
		       *authenticator);
    return 0;
}


void
krb5_free_authenticator(krb5_context context,
			krb5_authenticator *authenticator)
{
    free_Authenticator (*authenticator);
    free (*authenticator);
    *authenticator = NULL;
}


#if 0 /* not implemented */

krb5_error_code
krb5_auth_con_initivector(krb5_context context,
			  krb5_auth_context auth_context)
{
    abort ();
}


krb5_error_code
krb5_auth_con_setivector(krb5_context context,
			 krb5_auth_context auth_context,
			 krb5_pointer ivector)
{
    abort ();
}


krb5_error_code
krb5_auth_con_setrcache(krb5_context context,
			krb5_auth_context auth_context,
			krb5_rcache rcache)
{
    abort ();
}

krb5_error_code
krb5_auth_con_setuserkey(krb5_context context,
			 krb5_auth_context auth_context,
			 krb5_keyblock *keyblock)
{
    abort ();
}
#endif /* not implemented */
