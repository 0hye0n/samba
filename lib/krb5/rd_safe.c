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
krb5_rd_safe(krb5_context context,
	     krb5_auth_context auth_context,
	     const krb5_data *inbuf,
	     krb5_data *outbuf,
	     /*krb5_replay_data*/ void *outdata)
{
  krb5_error_code r;
  KRB_SAFE safe;
  size_t len;

  r = decode_KRB_SAFE (inbuf->data, inbuf->length, &safe, &len);
  if (r) 
      goto failure;
  if (safe.pvno != 5) {
      r = KRB5KRB_AP_ERR_BADVERSION;
      goto failure;
  }
  if (safe.msg_type != krb_safe) {
      r = KRB5KRB_AP_ERR_MSG_TYPE;
      goto failure;
  }
  /* XXX - checksum collision-proff and keyed */
  if (safe.cksum.cksumtype != CKSUMTYPE_RSA_MD5_DES) {
      r = KRB5KRB_AP_ERR_INAPP_CKSUM;
      goto failure;
  }

  /* check sender address */

  if (safe.safe_body.s_address
      && auth_context->remote_address
      && !krb5_address_compare (context,
				auth_context->remote_address,
				safe.safe_body.s_address)) {
      r = KRB5KRB_AP_ERR_BADADDR;
      goto failure;
  }

  /* check receiver address */

  if (safe.safe_body.r_address
      && auth_context->local_address
      && !krb5_address_compare (context,
				auth_context->local_address,
				safe.safe_body.r_address)) {
      r = KRB5KRB_AP_ERR_BADADDR;
      goto failure;
  }

  /* check timestamp */
  if (auth_context->flags & KRB5_AUTH_CONTEXT_DO_TIME) {
      int32_t sec;

      krb5_timeofday (context, &sec);

      if (safe.safe_body.timestamp == NULL ||
	  safe.safe_body.usec      == NULL ||
	  abs(*safe.safe_body.timestamp - sec) > context->max_skew) {
	  r = KRB5KRB_AP_ERR_SKEW;
	  goto failure;
      }
  }
  /* XXX - check replay cache */

  /* check sequence number */
  if (auth_context->flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) {
      if (safe.safe_body.seq_number == NULL ||
	  *safe.safe_body.seq_number != ++auth_context->remote_seqnumber) {
	  r = KRB5KRB_AP_ERR_BADORDER;
	  goto failure;
      }
  }

  {
      u_char buf[1024];
      size_t len;
      Checksum c;

      copy_Checksum (&safe.cksum, &c);
      
      safe.cksum.cksumtype       = 0;
      safe.cksum.checksum.data   = NULL;
      safe.cksum.checksum.length = 0;

      encode_KRB_SAFE (buf + sizeof(buf) - 1,
		       sizeof(buf),
		       &safe,
		       &len);

      r = krb5_verify_checksum (context,
				buf + sizeof(buf) - len,
				len,
				&auth_context->key,
				&c);
      free_Checksum (&c);
      if (r)
	  goto failure;
  }
  outbuf->length = safe.safe_body.user_data.length;
  outbuf->data   = malloc(outbuf->length);
  if (outbuf->data == NULL) {
      r = ENOMEM;
      goto failure;
  }
  memcpy (outbuf->data, safe.safe_body.user_data.data, outbuf->length);
  free_KRB_SAFE (&safe);
  return 0;
failure:
  free_KRB_SAFE (&safe);
  return r;
}
