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

static krb5_error_code
decrypt_tkt_enc_part (krb5_context context,
		      const krb5_keyblock *key,
		      EncryptedData *enc_part,
		      EncTicketPart *decr_part)
{
    krb5_error_code ret;
    krb5_data plain;
    size_t len;

    ret = krb5_decrypt (context,
			enc_part->cipher.data,
			enc_part->cipher.length,
			enc_part->etype,
			key, &plain);
    if (ret)
	return ret;

    ret = decode_EncTicketPart(plain.data, plain.length, decr_part, &len);
    krb5_data_free (&plain);
    if (ret)
	return ret;
    return 0;
}

static krb5_error_code
decrypt_authenticator (krb5_context context,
		       EncryptionKey *key,
		       EncryptedData *enc_part,
		       Authenticator *authenticator)
{
    krb5_error_code ret;
    krb5_data plain;
    size_t len;

    ret = krb5_decrypt (context,
			enc_part->cipher.data,
			enc_part->cipher.length,
			enc_part->etype,
			key, &plain);
    if (ret)
	return ret;

    ret = decode_Authenticator(plain.data, plain.length, authenticator, &len);
    krb5_data_free (&plain);
    if (ret) 
	return ret;
    return 0;
}

krb5_error_code
krb5_decode_ap_req(krb5_context context,
		   const krb5_data *inbuf,
		   krb5_ap_req *ap_req)
{
    krb5_error_code ret;
    size_t len;
    ret = decode_AP_REQ(inbuf->data, inbuf->length, ap_req, &len);
    if (ret)
	return ret;
    if (ap_req->pvno != 5){
	free_AP_REQ(ap_req);
	return KRB5KRB_AP_ERR_BADVERSION;
    }
    if (ap_req->msg_type != krb_ap_req){
	free_AP_REQ(ap_req);
	return KRB5KRB_AP_ERR_MSG_TYPE;
    }
    if (ap_req->ticket.tkt_vno != 5){
	free_AP_REQ(ap_req);
	return KRB5KRB_AP_ERR_BADVERSION;
    }
    if (ap_req->ap_options.use_session_key)
	abort ();
    return 0;
}

krb5_error_code
krb5_verify_ap_req(krb5_context context,
		   krb5_auth_context *auth_context,
		   krb5_ap_req *ap_req,
		   krb5_const_principal server,
		   krb5_keyblock *keyblock,
		   krb5_flags *ap_req_options,
		   krb5_ticket **ticket)
{
    krb5_ticket t;
    krb5_auth_context ac;
    krb5_error_code ret;
    
    if(auth_context){
	if(*auth_context == NULL){
	    krb5_auth_con_init(context, &ac);
	    *auth_context = ac;
	}else
	    ac = *auth_context;
    }else
	krb5_auth_con_init(context, &ac);
	
    ret = decrypt_tkt_enc_part (context,
				keyblock,
				&ap_req->ticket.enc_part,
				&t.ticket);
    if (ret)
	return ret;
    
    principalname2krb5_principal(&t.server,
				 ap_req->ticket.sname,
				 ap_req->ticket.realm);

    principalname2krb5_principal(&t.client,
				 t.ticket.cname,
				 t.ticket.crealm);
    /* save key */

    copy_EncryptionKey(&t.ticket.key, &ac->key);

    ret = decrypt_authenticator (context,
				 &t.ticket.key,
				 &ap_req->authenticator,
				 ac->authenticator);
    if (ret){
	/* XXX free data */
	return ret;
    }

    {
	krb5_principal p1, p2;
	krb5_boolean res;
	
	principalname2krb5_principal(&p1,
				     ac->authenticator->cname,
				     ac->authenticator->crealm);
	principalname2krb5_principal(&p2, 
				     t.ticket.cname,
				     t.ticket.crealm);
	res = krb5_principal_compare (context, p1, p2);
	krb5_free_principal (context, p1);
	krb5_free_principal (context, p2);
	if (!res)
	    return KRB5KRB_AP_ERR_BADMATCH;
    }

    /* check addresses */

    if (t.ticket.caddr
	&& ac->remote_address
	&& !krb5_address_search (context,
				 ac->remote_address,
				 t.ticket.caddr))
	return KRB5KRB_AP_ERR_BADADDR;

    if (ac->authenticator->seq_number)
	ac->remote_seqnumber = *ac->authenticator->seq_number;

    /* XXX - Xor sequence numbers */

    /* XXX - subkeys? */
    /* And where should it be stored? */

    if (ac->authenticator->subkey) {
	copy_EncryptionKey (ac->authenticator->subkey,
			    &ac->remote_subkey);
    }

    if (ap_req_options) {
	*ap_req_options = 0;
	if (ap_req->ap_options.use_session_key)
	    *ap_req_options |= AP_OPTS_USE_SESSION_KEY;
	if (ap_req->ap_options.mutual_required)
	    *ap_req_options |= AP_OPTS_MUTUAL_REQUIRED;
    }

    {
	time_t now = time (NULL);
	time_t start = t.ticket.authtime;
	if(t.ticket.starttime)
	    start = *t.ticket.starttime;
	if(start - now > context->max_skew || t.ticket.flags.invalid)
	    return KRB5KRB_AP_ERR_TKT_NYV;
	if(now - t.ticket.endtime > context->max_skew)
	    return KRB5KRB_AP_ERR_TKT_EXPIRED;
    }
    
    if(ticket){
	*ticket = malloc(sizeof(**ticket));
	**ticket = t;
    } else
	free_EncTicketPart(&t.ticket);
    return 0;
}
		   

krb5_error_code
krb5_rd_req_with_keyblock(krb5_context context,
			  krb5_auth_context *auth_context,
			  const krb5_data *inbuf,
			  krb5_const_principal server,
			  krb5_keyblock *keyblock,
			  krb5_flags *ap_req_options,
			  krb5_ticket **ticket)
{
    krb5_error_code ret;
    krb5_ap_req ap_req;
    size_t len;
    struct timeval now;

    if (*auth_context == NULL) {
	ret = krb5_auth_con_init(context, auth_context);
	if (ret)
	    return ret;
    }

    ret = krb5_decode_ap_req(context, inbuf, &ap_req);
    if(ret)
	return ret;
    
    ret = krb5_verify_ap_req(context,
			     auth_context,
			     &ap_req,
			     server,
			     keyblock,
			     ap_req_options,
			     ticket);
    free_AP_REQ(&ap_req);
    return ret;
}

krb5_error_code
krb5_rd_req(krb5_context context,
	    krb5_auth_context *auth_context,
	    const krb5_data *inbuf,
	    krb5_const_principal server,
	    krb5_keytab keytab,
	    krb5_flags *ap_req_options,
	    krb5_ticket **ticket)
{
    krb5_keytab_entry entry;
    krb5_error_code ret;
    krb5_keytab real_keytab;

    if(keytab == NULL)
	krb5_kt_default(context, &real_keytab);
    else
	real_keytab = keytab;

    ret = krb5_kt_get_entry(context,
			    real_keytab,
			    server,
			    0,
			    KEYTYPE_DES,
			    &entry);
    if(ret)
	goto out;
    
    ret = krb5_rd_req_with_keyblock(context,
				    auth_context,
				    inbuf,
				    server,
				    &entry.keyblock,
				    ap_req_options,
				    ticket);
    krb5_kt_free_entry (context, &entry);
out:
    if (keytab == NULL)
	krb5_kt_close (context, real_keytab);

    return ret;
}
