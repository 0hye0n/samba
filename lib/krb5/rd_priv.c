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

#include <krb5_locl.h>

RCSID("$Id$");

krb5_error_code
krb5_rd_priv(krb5_context context,
	     krb5_auth_context auth_context,
	     const krb5_data *inbuf,
	     krb5_data *outbuf,
	     /*krb5_replay_data*/ void *outdata)
{
  krb5_error_code r;
  KRB_PRIV priv;
  EncKrbPrivPart part;
  size_t len;
  krb5_data plain;
  krb5_keyblock *key;

  r = decode_KRB_PRIV (inbuf->data, inbuf->length, &priv, &len);
  if (r) 
      goto failure;
  if (priv.pvno != 5) {
      r = KRB5KRB_AP_ERR_BADVERSION;
      goto failure;
  }
  if (priv.msg_type != krb_priv) {
      r = KRB5KRB_AP_ERR_MSG_TYPE;
      goto failure;
  }

  /* XXX - Is this right? */

  if (auth_context->local_subkey.keytype)
      key = &auth_context->local_subkey;
  else if (auth_context->remote_subkey.keytype)
      key = &auth_context->remote_subkey;
  else
      key = &auth_context->key;

  r = krb5_decrypt (context,
		    priv.enc_part.cipher.data,
		    priv.enc_part.cipher.length,
		    priv.enc_part.etype,
		    key,
		    &plain);
  if (r) 
      goto failure;

  r = decode_EncKrbPrivPart (plain.data, plain.length, &part, &len);
  krb5_data_free (&plain);
  if (r) 
      goto failure;
  
  /* check sender address */

  if (part.s_address
      && auth_context->remote_address
      && !krb5_address_compare (context,
				auth_context->remote_address,
				part.s_address)) {
      r = KRB5KRB_AP_ERR_BADADDR;
      goto failure_part;
  }

  /* check receiver address */

  if (part.r_address
      && !krb5_address_compare (context,
				auth_context->local_address,
				part.r_address)) {
      r = KRB5KRB_AP_ERR_BADADDR;
      goto failure_part;
  }

  /* check timestamp */
  if (auth_context->flags & KRB5_AUTH_CONTEXT_DO_TIME) {
    struct timeval tv;

    gettimeofday (&tv, NULL);
    if (part.timestamp == NULL ||
	part.usec      == NULL ||
	abs(*part.timestamp - tv.tv_sec) > context->max_skew) {
	r = KRB5KRB_AP_ERR_SKEW;
	goto failure_part;
    }
  }

  /* XXX - check replay cache */

  /* check sequence number */
  if (auth_context->flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) {
    if (part.seq_number == NULL ||
	*part.seq_number != ++auth_context->remote_seqnumber) {
      r = KRB5KRB_AP_ERR_BADORDER;
      goto failure_part;
    }
  }

  r = krb5_data_copy (outbuf, part.user_data.data, part.user_data.length);
  if (r)
      goto failure_part;

  free_EncKrbPrivPart (&part);
  free_KRB_PRIV (&priv);
  return 0;

failure_part:
  free_EncKrbPrivPart (&part);

failure:
  free_KRB_PRIV (&priv);
  return r;
}
