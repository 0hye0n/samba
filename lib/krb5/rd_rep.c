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
krb5_rd_rep(krb5_context context,
	    krb5_auth_context auth_context,
	    const krb5_data *inbuf,
	    krb5_ap_rep_enc_part **repl)
{
  krb5_error_code ret;
  AP_REP ap_rep;
  size_t len;
  des_key_schedule schedule;
  char *buf;
  krb5_data data;

  ret = decode_AP_REP(inbuf->data, inbuf->length, &ap_rep, &len);
  if (ret)
      return ret;
  if (ap_rep.pvno != 5)
    return KRB5KRB_AP_ERR_BADVERSION;
  if (ap_rep.msg_type != krb_ap_rep)
    return KRB5KRB_AP_ERR_MSG_TYPE;

  ret = krb5_decrypt (context,
		      ap_rep.enc_part.cipher.data,
		      ap_rep.enc_part.cipher.length,
		      ap_rep.enc_part.etype,
		      &auth_context->key,
		      &data);
  if (ret)
    return ret;

  *repl = malloc(sizeof(**repl));
  if (*repl == NULL)
    return ENOMEM;
  ret = decode_EncAPRepPart(data.data,
			    data.length,
			    *repl, 
			    &len);
  if (ret)
      return ret;
  if ((*repl)->ctime != auth_context->authenticator->ctime ||
      (*repl)->cusec != auth_context->authenticator->cusec) {
#if 0
    printf("KRB5KRB_AP_ERR_MUT_FAIL\n");
    printf ("(%u, %lu) != (%u, %lu)\n",
	    (*repl)->ctime, (*repl)->cusec,
	    auth_context->authenticator->ctime,
	    auth_context->authenticator->cusec);
#endif				/* Something wrong with the coding??? */
    return KRB5KRB_AP_ERR_MUT_FAIL;
  }
  if ((*repl)->seq_number)
    auth_context->remote_seqnumber = *((*repl)->seq_number);
  
  return 0;
}

void
krb5_free_ap_rep_enc_part (krb5_context context,
			   krb5_ap_rep_enc_part *val)
{
  free (val);
}
