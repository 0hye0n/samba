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
krb5_mk_priv(krb5_context context,
	     krb5_auth_context auth_context,
	     const krb5_data *userdata,
	     krb5_data *outbuf,
	     /*krb5_replay_data*/ void *outdata)
{
  krb5_error_code r;
  KRB_PRIV s;
  EncKrbPrivPart part;
  u_char buf[1024];
  size_t len;
  unsigned tmp_seq;
  krb5_keyblock *key;
  int32_t sec, usec;
  KerberosTime sec2;
  unsigned usec2;

  krb5_us_timeofday (context, &sec, &usec);

  part.user_data = *userdata;
  sec2 = sec;
  part.timestamp  = &sec2;
  usec2 = usec;
  part.usec       = &usec2;
  if (auth_context->flags & KRB5_AUTH_CONTEXT_DO_SEQUENCE) {
    tmp_seq = ++auth_context->local_seqnumber;
    part.seq_number = &tmp_seq;
  } else {
    part.seq_number = NULL;
  }

  part.s_address = auth_context->local_address;
  part.r_address = auth_context->remote_address;

  r = encode_EncKrbPrivPart (buf + sizeof(buf) - 1, sizeof(buf), &part, &len);
  if (r)
      return r;

  s.pvno = 5;
  s.msg_type = krb_priv;
  s.enc_part.etype = auth_context->enctype;
  s.enc_part.kvno = NULL;

  /* XXX - Is this right? */

  if (auth_context->local_subkey.keytype)
      key = &auth_context->local_subkey;
  else if (auth_context->remote_subkey.keytype)
      key = &auth_context->remote_subkey;
  else
      key = &auth_context->key;

  r = krb5_encrypt (context, buf + sizeof(buf) - len, len,
		    s.enc_part.etype, key, &s.enc_part.cipher);
  if (r)
    return r;

  r = encode_KRB_PRIV (buf + sizeof(buf) - 1, sizeof(buf), &s, &len);
  krb5_data_free (&s.enc_part.cipher);
  if (r)
    return r;
  outbuf->length = len;
  outbuf->data   = malloc (len);
  if (outbuf->data == NULL)
    return ENOMEM;
  memcpy (outbuf->data, buf + sizeof(buf) - len, len);
  return 0;
}
