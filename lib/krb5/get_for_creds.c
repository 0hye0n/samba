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
krb5_get_forwarded_creds (krb5_context	    context,
			  krb5_auth_context auth_context,
			  krb5_ccache       ccache,
			  krb5_flags        flags,
			  const char        *hostname,
			  krb5_creds        *in_creds,
			  krb5_data         *out_data)
{
    krb5_error_code ret;
    krb5_creds *out_creds;
    krb5_addresses addrs;
    struct hostent *hostent;
    unsigned n;
    struct in_addr **h;
    KRB_CRED cred;
    KrbCredInfo *krb_cred_info;
    EncKrbCredPart enc_krb_cred_part;
    size_t len;
    u_char buf[1024];
    int32_t sec, usec;
    krb5_kdc_flags kdc_flags;
    krb5_enctype enctype;

    if (auth_context->enctype)
	enctype = auth_context->enctype;
    else {
	ret = krb5_keytype_to_etype (context,
				     auth_context->local_subkey.keytype,
				     &enctype);
	if (ret)
	    return ret;
    }

    out_creds = calloc(1, sizeof(*out_creds));
    if (out_creds == NULL)
	return ENOMEM;

    hostent = gethostbyname (hostname);
    if (hostent == NULL)
	return h_errno;		/* XXX */

    if (hostent->h_addrtype != AF_INET)
	abort ();

    n = 0;
    for (h = (struct in_addr **)hostent->h_addr_list;
	 *h != NULL;
	 ++h)
	++n;

    addrs.len = n;
    addrs.val = calloc (n, sizeof(*addrs.val));
    if (addrs.val == NULL)
	return ENOMEM;

    n = 0;
    for (h = (struct in_addr **)hostent->h_addr_list;
	 *h != NULL;
	 ++h) {
	addrs.val[n].addr_type      = AF_INET;
	addrs.val[n].address.length = sizeof(struct in_addr);
	addrs.val[n].address.data   = malloc(sizeof(struct in_addr));
	if (addrs.val[n].address.data == NULL) {
	    krb5_free_addresses (context, &addrs);
	    return ENOMEM;
	}
	memcpy (addrs.val[n].address.data, *h, sizeof(struct in_addr));
    }

    kdc_flags.i = flags;

    ret = krb5_get_kdc_cred (context,
			     ccache,
			     kdc_flags,
			     &addrs,
			     NULL,
			     in_creds,
			     &out_creds);
    if (ret)
	return ret;

    memset (&cred, 0, sizeof(cred));
    cred.pvno = 5;
    cred.msg_type = krb_cred;
    cred.tickets.len = 1;
    ALLOC(cred.tickets.val, 1);
    if (cred.tickets.val == NULL)
	return ENOMEM;		/* XXX */
    if (decode_Ticket(out_creds->ticket.data,
		      out_creds->ticket.length,
		      cred.tickets.val, &len))
	abort ();		/* XXX */
    

    memset (&enc_krb_cred_part, 0, sizeof(enc_krb_cred_part));
    enc_krb_cred_part.ticket_info.len = 1;
    ALLOC(enc_krb_cred_part.ticket_info.val, 1);
    
    krb5_us_timeofday (context, &sec, &usec);

    ALLOC(enc_krb_cred_part.timestamp, 1);
    *enc_krb_cred_part.timestamp = sec;
    ALLOC(enc_krb_cred_part.usec, 1);
    *enc_krb_cred_part.usec      = usec;

    enc_krb_cred_part.s_address = NULL;	/* XXX */
    enc_krb_cred_part.r_address = NULL;	/* XXX */

    /* fill ticket_info.val[0] */

    krb_cred_info = enc_krb_cred_part.ticket_info.val;

    copy_EncryptionKey (&out_creds->session, &krb_cred_info->key);
    ALLOC(krb_cred_info->prealm, 1);
    copy_Realm (&out_creds->client->realm, krb_cred_info->prealm);
    ALLOC(krb_cred_info->pname, 1);
    copy_PrincipalName(&out_creds->client->name, krb_cred_info->pname);
    ALLOC(krb_cred_info->flags, 1);
    *krb_cred_info->flags          = out_creds->flags.b;
    ALLOC(krb_cred_info->authtime, 1);
    *krb_cred_info->authtime       = out_creds->times.authtime;
    ALLOC(krb_cred_info->starttime, 1);
    *krb_cred_info->starttime      = out_creds->times.starttime;
    ALLOC(krb_cred_info->endtime, 1);
    *krb_cred_info->endtime        = out_creds->times.endtime;
    ALLOC(krb_cred_info->renew_till, 1);
    *krb_cred_info->renew_till = out_creds->times.renew_till;
    ALLOC(krb_cred_info->srealm, 1);
    copy_Realm (&out_creds->server->realm, krb_cred_info->srealm);
    ALLOC(krb_cred_info->sname, 1);
    copy_PrincipalName (&out_creds->server->name, krb_cred_info->sname);
    ALLOC(krb_cred_info->caddr, 1);
    copy_HostAddresses (&out_creds->addresses, krb_cred_info->caddr);

    /* encode EncKrbCredPart */

    ret = encode_EncKrbCredPart (buf + sizeof(buf) - 1, sizeof(buf),
				 &enc_krb_cred_part, &len);
    free_EncKrbCredPart (&enc_krb_cred_part);
    if (ret)
	return ret; /* XXX */

    ret = krb5_encrypt_EncryptedData (context,
				      buf + sizeof(buf) - len,
				      len,
				      enctype,
				      0,
				      &auth_context->local_subkey,
				      &cred.enc_part);
    if (ret)
	return ret;		/* XXX */

    ret = encode_KRB_CRED (buf + sizeof(buf) - 1, sizeof(buf),
			   &cred, &len);
    free_KRB_CRED (&cred);
    if (ret)
	return ret;
    out_data->length = len;
    out_data->data   = malloc(len);
    memcpy (out_data->data, buf + sizeof(buf) - len, len);
    return 0;
}
