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

#include "kdc_locl.h"

RCSID("$Id$");

#define MAX_TIME ((time_t)((1U << 31) - 1))

static void
fix_time(time_t **t)
{
    if(*t == NULL){
	ALLOC(*t);
	**t = MAX_TIME;
    }
    if(**t == 0) **t = MAX_TIME; /* fix for old clients */
}

static void
set_salt_padata (METHOD_DATA **m, Salt *salt)
{
    if (salt) {
	ALLOC(*m);
	(*m)->len = 1;
	ALLOC((*m)->val);
	(*m)->val->padata_type = salt->type;
	copy_octet_string(&salt->salt,
			  &(*m)->val->padata_value);
    }
}

static PA_DATA*
find_padata(KDC_REQ *req, int *start, int type)
{
    while(*start < req->padata->len){
	(*start)++;
	if(req->padata->val[*start - 1].padata_type == type)
	    return &req->padata->val[*start - 1];
    }
    return NULL;
}

static krb5_error_code
find_etype(hdb_entry *princ, unsigned *etypes, unsigned len, 
	   Key **key, int *index)
{
    int i;
    krb5_error_code ret = -1;
    for(i = 0; i < len ; i++)
	if((ret = hdb_etype2key(context, princ, etypes[i], key)) == 0)
	    break;
    if(index) *index = i;
    return ret;
}

static krb5_error_code
find_keys(hdb_entry *client, hdb_entry *server, 
	  Key **ckey, krb5_enctype *cetype,
	  Key **skey, krb5_enctype *setype, krb5_keytype *sess_ktype,
	  unsigned *etypes, unsigned num_etypes)
{
    int i;
    krb5_error_code ret;
    if(client){
	/* find client key */
	ret = find_etype(client, etypes, num_etypes, ckey, &i);
	if(ret){
	    kdc_log(0, "Client has no support for etypes");
	    return KRB5KDC_ERR_ETYPE_NOSUPP;
	}
	*cetype = etypes[i];
    }

    if(server){
	/* find sesion key type */
	ret = find_etype(server, etypes, num_etypes, skey, NULL);
	if(ret){
	    kdc_log(0, "Server has no support for etypes");
	    return KRB5KDC_ERR_ETYPE_NOSUPP;
	}
	*sess_ktype = (*skey)->key.keytype;
    }
    if(server){
	/* find server key */
	*skey = NULL;
#define is_better(x, y) ((x) > (y))
	for(i = 0; i < server->keys.len; i++){
	    if(*skey == NULL || is_better(server->keys.val[i].key.keytype, 
					  (*skey)->key.keytype))
		*skey = &server->keys.val[i];
	}
	if(*skey == NULL){
	    kdc_log(0, "No key found for server");
	    return KRB5KDC_ERR_NULL_KEY;
	}
	ret = krb5_keytype_to_etype(context, (*skey)->key.keytype, setype);
	if(ret)
	    return ret;
    }
    return 0;
}

static krb5_error_code
encode_reply(KDC_REP *rep, EncTicketPart *et, EncKDCRepPart *ek, 
	     krb5_enctype setype, int skvno, EncryptionKey *skey,
	     krb5_enctype cetype, int ckvno, EncryptionKey *ckey,
	     krb5_data *reply)
{
    unsigned char buf[8192]; /* XXX The data could be indefinite */
    size_t len;
    krb5_error_code ret;

    ret = encode_EncTicketPart(buf + sizeof(buf) - 1, sizeof(buf), et, &len);
    if(ret) {
	kdc_log(0, "Failed to encode ticket: %s", 
		krb5_get_err_text(context, ret));
	return ret;
    }
    
    krb5_encrypt_EncryptedData(context, 
			       buf + sizeof(buf) - len,
			       len,
			       setype,
			       skvno,
			       skey,
			       &rep->ticket.enc_part);
    
    if(rep->msg_type == krb_as_rep)
	ret = encode_EncASRepPart(buf + sizeof(buf) - 1, sizeof(buf), 
				  ek, &len);
    else
	ret = encode_EncTGSRepPart(buf + sizeof(buf) - 1, sizeof(buf), 
				   ek, &len);
    if(ret) {
	kdc_log(0, "Failed to encode KDC-REP: %s", 
		krb5_get_err_text(context, ret));
	return ret;
    }
    krb5_encrypt_EncryptedData(context,
			       buf + sizeof(buf) - len,
			       len,
			       cetype,
			       ckvno,
			       ckey,
			       &rep->enc_part);
	
    if(rep->msg_type == krb_as_rep)
	ret = encode_AS_REP(buf + sizeof(buf) - 1, sizeof(buf), rep, &len);
    else
	ret = encode_TGS_REP(buf + sizeof(buf) - 1, sizeof(buf), rep, &len);
    if(ret) {
	kdc_log(0, "Failed to encode KDC-REP: %s", 
		krb5_get_err_text(context, ret));
	return ret;
    }
    krb5_data_copy(reply, buf + sizeof(buf) - len, len);
    return 0;
}

krb5_error_code
as_rep(KDC_REQ *req, 
       krb5_data *reply,
       const char *from)
{
    KDC_REQ_BODY *b = &req->req_body;
    AS_REP rep;
    KDCOptions f = b->kdc_options;
    hdb_entry *client = NULL, *server = NULL;
    krb5_enctype cetype, setype;
    krb5_keytype sess_ktype;
    EncTicketPart et;
    EncKDCRepPart ek;
    krb5_principal client_princ, server_princ;
    char *client_name, *server_name;
    krb5_error_code ret = 0;
    const char *e_text = NULL;

    Key *ckey, *skey;

    if(b->sname == NULL){
	server_name = "<unknown server>";
	ret = KRB5KRB_ERR_GENERIC;
	e_text = "No server in request";
    } else{
	principalname2krb5_principal (&server_princ, *(b->sname), b->realm);
	krb5_unparse_name(context, server_princ, &server_name);
    }
    
    if(b->cname == NULL){
	client_name = "<unknown client>";
	ret = KRB5KRB_ERR_GENERIC;
	e_text = "No client in request";
    } else {
	principalname2krb5_principal (&client_princ, *(b->cname), b->realm);
	krb5_unparse_name(context, client_princ, &client_name);
    }
    kdc_log(0, "AS-REQ %s from %s for %s", 
	    client_name, from, server_name);

    if(ret)
	goto out;

    client = db_fetch(client_princ);
    if(client == NULL){
	kdc_log(0, "UNKNOWN -- %s", client_name);
	ret = KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN;
	goto out;
    }

    if (client->valid_start && *client->valid_start > kdc_time) {
	kdc_log(0, "Client not yet valid -- %s", client_name);
	ret = KRB5KDC_ERR_CLIENT_NOTYET;
	goto out;
    }

    if (client->valid_end && *client->valid_end < kdc_time) {
	kdc_log(0, "Client expired -- %s", client_name);
	ret = KRB5KDC_ERR_NAME_EXP;
	goto out;
    }

    if(!client->flags.client){
	ret = KRB5KDC_ERR_POLICY;
	kdc_log(0, "Principal may not act as client -- %s", 
		client_name);
	goto out;
    }

    if (client->flags.invalid) {
	ret = KRB5KDC_ERR_POLICY;
	kdc_log(0, "Client (%s) has invalid bit set", client_name);
	goto out;
    }

    server = db_fetch(server_princ);

    if(server == NULL){
	kdc_log(0, "UNKNOWN -- %s", server_name);
	ret = KRB5KDC_ERR_S_PRINCIPAL_UNKNOWN;
	goto out;
    }

    if (server->valid_start && *server->valid_start > kdc_time) {
	kdc_log(0, "Server not yet valid -- %s", server_name);
	ret = KRB5KDC_ERR_SERVICE_NOTYET;
	goto out;
    }

    if (server->valid_end && *server->valid_end < kdc_time) {
	kdc_log(0, "Server expired -- %s", server_name);
	ret = KRB5KDC_ERR_SERVICE_EXP;
	goto out;
    }

    if(!server->flags.server){
	ret = KRB5KDC_ERR_POLICY;
	kdc_log(0, "Principal (%s) may not act as server -- %s", 
		server_name, client_name);
	goto out;
    }

    if (server->flags.invalid) {
	ret = KRB5KDC_ERR_POLICY;
	kdc_log(0, "Server (%s) has invalid bit set", server_name);
	goto out;
    }

    if (server->pw_end && *server->pw_end < kdc_time) {
	ret = KRB5KDC_ERR_KEY_EXPIRED;
	kdc_log(0, "Servers key has expired", server_name);
	goto out;
    }

    if (client->pw_end && *client->pw_end < kdc_time
	&& !server->flags.change_pw) {
	ret = KRB5KDC_ERR_KEY_EXPIRED;
	kdc_log(0, "Client (%s)'s key has expired", client_name);
	goto out;
    }

    memset(&et, 0, sizeof(et));
    memset(&ek, 0, sizeof(ek));

    if(req->padata){
	int i = 0;
	PA_DATA *pa;
	int found_pa = 0;
	kdc_log(5, "Looking for pa-data -- %s", client_name);
	while((pa = find_padata(req, &i, pa_enc_timestamp))){
	    krb5_data ts_data;
	    PA_ENC_TS_ENC p;
	    time_t patime;
	    size_t len;
	    EncryptedData enc_data;
	    Key *pa_key;
	    
	    found_pa = 1;
	    
	    ret = decode_EncryptedData(pa->padata_value.data,
				       pa->padata_value.length,
				       &enc_data,
				       &len);
	    if (ret) {
		ret = KRB5KRB_AP_ERR_BAD_INTEGRITY;
		kdc_log(5, "Failed to decode PA-DATA -- %s", 
			client_name);
		goto out;
	    }
	    
	    ret = hdb_etype2key(context, client, enc_data.etype, &pa_key);
	    if(ret){
		e_text = "No key matches pa-data";
		ret = KRB5KDC_ERR_PREAUTH_FAILED;
		kdc_log(5, "No client key matching pa-data -- %s", 
			client_name);
		free_EncryptedData(&enc_data);
		continue;
	    }
	    
	    ret = krb5_decrypt_EncryptedData (context,
					      &enc_data,
					      &pa_key->key,
					      &ts_data);
	    free_EncryptedData(&enc_data);
	    if(ret){
		e_text = "Failed to decrypt PA-DATA";
		kdc_log (5, "Failed to decrypt PA-DATA -- %s",
			 client_name);
		ret = KRB5KRB_AP_ERR_BAD_INTEGRITY;
		continue;
	    }
	    ret = decode_PA_ENC_TS_ENC(ts_data.data,
				       ts_data.length,
				       &p,
				       &len);
	    krb5_data_free(&ts_data);
	    if(ret){
		e_text = "Failed to decode PA-ENC-TS-ENC";
		ret = KRB5KRB_AP_ERR_BAD_INTEGRITY;
		kdc_log (5, "Failed to decode PA-ENC-TS_ENC -- %s",
			 client_name);
		continue;
	    }
	    patime = p.patimestamp;
	    free_PA_ENC_TS_ENC(&p);
	    if (abs(kdc_time - p.patimestamp) > context->max_skew) {
		ret = KRB5KDC_ERR_PREAUTH_FAILED;
		e_text = "Too large time skew";
		kdc_log(0, "Too large time skew -- %s", client_name);
		goto out;
	    }
	    et.flags.pre_authent = 1;
	    kdc_log(2, "Pre-authentication succeded -- %s", client_name);
	    break;
	}
	if(found_pa == 0 && require_preauth)
	    goto use_pa;
	/* We come here if we found a pa-enc-timestamp, but if there
           was some problem with it, other than too large skew */
	if(found_pa && et.flags.pre_authent == 0){
	    kdc_log(0, "%s -- %s", e_text, client_name);
	    e_text = NULL;
	    goto out;
	}
    }else if (require_preauth || client->flags.require_preauth) {
	/* XXX check server->flags.require_preauth? */
	METHOD_DATA method_data;
	PA_DATA pa_data;
	u_char buf[16];
	size_t len;
	krb5_data foo_data;
	
    use_pa:
	method_data.len = 1;
	method_data.val = &pa_data;

	pa_data.padata_type         = pa_enc_timestamp;
	pa_data.padata_value.length = 0;
	pa_data.padata_value.data   = NULL;
	
	encode_METHOD_DATA(buf + sizeof(buf) - 1,
			   sizeof(buf),
			   &method_data,
			   &len);
	foo_data.length = len;
	foo_data.data   = buf + sizeof(buf) - len;
	
	ret = KRB5KDC_ERR_PREAUTH_REQUIRED;
	krb5_mk_error(context,
		      ret,
		      "Need to use PA-ENC-TIMESTAMP",
		      &foo_data,
		      client_princ,
		      server_princ,
		      0,
		      reply);
	
	kdc_log(0, "No PA-ENC-TIMESTAMP -- %s", client_name);
	goto out2;
    }



    ret = find_keys(client, server, &ckey, &cetype, &skey, &setype, 
		    &sess_ktype, b->etype.val, b->etype.len);
    if(ret)
	goto out;
	
    {
	char *cet, *set, *skt;
	krb5_etype_to_string(context, cetype, &cet);
	if(cetype != setype)
	    krb5_etype_to_string(context, setype, &set);
	krb5_keytype_to_string(context, sess_ktype, &skt);
	if(cetype != setype)
	    kdc_log(5, "Using %s/%s/%s", cet, set, skt);
	else
	    kdc_log(5, "Using %s/%s", cet, skt);
	free(cet);
	if(cetype != setype)
	    free(set);
	free(skt);
    }
    

    memset(&rep, 0, sizeof(rep));
    rep.pvno = 5;
    rep.msg_type = krb_as_rep;
    copy_Realm(&b->realm, &rep.crealm);
    copy_PrincipalName(b->cname, &rep.cname);
    rep.ticket.tkt_vno = 5;
    copy_Realm(&b->realm, &rep.ticket.realm);
    copy_PrincipalName(b->sname, &rep.ticket.sname);

    {
	char str[128];
	unparse_flags(KDCOptions2int(f), KDCOptions_units, str, sizeof(str));
	if(*str)
	    kdc_log(2, "Requested flags: %s", str);
    }
    
    if(f.renew || f.validate || f.proxy || f.forwarded || f.enc_tkt_in_skey || 
       f.request_anonymous){
	ret = KRB5KDC_ERR_BADOPTION;
	kdc_log(0, "Bad KDC options -- %s", client_name);
	goto out;
    }
    
    et.flags.initial = 1;
    if(client->flags.forwardable && server->flags.forwardable)
	et.flags.forwardable = f.forwardable;
    else if (f.forwardable) {
	ret = KRB5KDC_ERR_POLICY;
	kdc_log(0, "Ticket may not be forwardable -- %s", client_name);
	goto out;
    }
    if(client->flags.proxiable && server->flags.proxiable)
	et.flags.proxiable = f.proxiable;
    else if (f.proxiable) {
	ret = KRB5KDC_ERR_POLICY;
	kdc_log(0, "Ticket may not be proxiable -- %s", client_name);
	goto out;
    }
    if(client->flags.postdate && server->flags.postdate)
	et.flags.may_postdate = f.allow_postdate;
    else if (f.allow_postdate){
	ret = KRB5KDC_ERR_POLICY;
	kdc_log(0, "Ticket may not be postdatable -- %s", client_name);
	goto out;
    }

    krb5_generate_random_keyblock(context, sess_ktype, &et.key);
    copy_PrincipalName(b->cname, &et.cname);
    copy_Realm(&b->realm, &et.crealm);
    
    {
	time_t start;
	time_t t;
	
	start = et.authtime = kdc_time;
    
	if(f.postdated && req->req_body.from){
	    ALLOC(et.starttime);
	    start = *et.starttime = *req->req_body.from;
	    et.flags.invalid = 1;
	    et.flags.postdated = 1; /* XXX ??? */
	}
	fix_time(&b->till);
	t = *b->till;
	if(client->max_life)
	    t = min(t, start + *client->max_life);
	if(server->max_life)
	    t = min(t, start + *server->max_life);
#if 0
	t = min(t, start + realm->max_life);
#endif
	et.endtime = t;
	if(f.renewable_ok && et.endtime < *b->till){
	    f.renewable = 1;
	    if(b->rtime == NULL){
		ALLOC(b->rtime);
		*b->rtime = 0;
	    }
	    if(*b->rtime < *b->till)
		*b->rtime = *b->till;
	}
	if(f.renewable && b->rtime){
	    t = *b->rtime;
	    if(t == 0)
		t = MAX_TIME;
	    if(client->max_renew)
		t = min(t, start + *client->max_renew);
	    if(server->max_renew)
		t = min(t, start + *server->max_renew);
#if 0
	    t = min(t, start + realm->max_renew);
#endif
	    ALLOC(et.renew_till);
	    *et.renew_till = t;
	    et.flags.renewable = 1;
	}
    }
    
    if(b->addresses){
	ALLOC(et.caddr);
	copy_HostAddresses(b->addresses, et.caddr);
    }

    copy_EncryptionKey(&et.key, &ek.key);

    /* The MIT ASN.1 library (obviously) doesn't tell lengths encoded
     * as 0 and as 0x80 (meaning indefinite length) apart, and is thus
     * incapable of correctly decoding SEQUENCE OF's of zero length.
     *
     * To fix this, always send at least one no-op last_req
     *
     * If there's a pw_end or valid_end we will use that,
     * otherwise just a dummy lr.
     */
    ek.last_req.val = malloc(2 * sizeof(*ek.last_req.val));
    ek.last_req.len = 0;
    if (client->pw_end
	&& (kdc_warn_pwexpire == 0
	    || kdc_time + kdc_warn_pwexpire <= *client->pw_end)) {
	ek.last_req.val[ek.last_req.len].lr_type  = 6;
	ek.last_req.val[ek.last_req.len].lr_value = *client->pw_end;
	++ek.last_req.len;
    }
    if (client->valid_end) {
	ek.last_req.val[ek.last_req.len].lr_type  = 7;
	ek.last_req.val[ek.last_req.len].lr_value = *client->valid_end;
	++ek.last_req.len;
    }
    if (ek.last_req.len == 0) {
	ek.last_req.val[ek.last_req.len].lr_type  = 0;
	ek.last_req.val[ek.last_req.len].lr_value = 0;
	++ek.last_req.len;
    }
    ek.nonce = b->nonce;
    if (client->valid_end || client->pw_end) {
	ALLOC(ek.key_expiration);
	if (client->valid_end)
	    if (client->pw_end)
		*ek.key_expiration = min(*client->valid_end, *client->pw_end);
	    else
		*ek.key_expiration = *client->valid_end;
	else
	    *ek.key_expiration = *client->pw_end;
    } else
	ek.key_expiration = NULL;
    ek.flags = et.flags;
    ek.authtime = et.authtime;
    if (et.starttime) {
	ALLOC(ek.starttime);
	*ek.starttime = *et.starttime;
    }
    ek.endtime = et.endtime;
    if (et.renew_till) {
	ALLOC(ek.renew_till);
	*ek.renew_till = *et.renew_till;
    }
    copy_Realm(&rep.ticket.realm, &ek.srealm);
    copy_PrincipalName(&rep.ticket.sname, &ek.sname);
    if(et.caddr){
	ALLOC(ek.caddr);
	copy_HostAddresses(et.caddr, ek.caddr);
    }

    set_salt_padata (&rep.padata, ckey->salt);
    ret = encode_reply(&rep, &et, &ek, setype, server->kvno, &skey->key,
		       cetype, client->kvno, &ckey->key, reply);
    free_EncTicketPart(&et);
    free_EncKDCRepPart(&ek);
    free_AS_REP(&rep);
out:
    if(ret){
	/* XXX should just return protocol errors */
	krb5_mk_error(context,
		      ret,
		      e_text,
		      NULL,
		      client_princ,
		      server_princ,
		      0,
		      reply);
	ret = 0;
    }
out2:
    krb5_free_principal(context, client_princ);
    free(client_name);
    krb5_free_principal(context, server_princ);
    free(server_name);
    if(client){
	hdb_free_entry(context, client);
	free(client);
    }
    if(server){
	hdb_free_entry(context, server);
	free(server);
    }
    
    return ret;
}


static krb5_error_code
check_tgs_flags(KDC_REQ_BODY *b, EncTicketPart *tgt, EncTicketPart *et)
{
    KDCOptions f = b->kdc_options;
	
    if(f.validate){
	if(!tgt->flags.invalid || tgt->starttime == NULL){
	    kdc_log(0, "Bad request to validate ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	if(*tgt->starttime < kdc_time){
	    kdc_log(0, "Early request to validate ticket");
	    return KRB5KRB_AP_ERR_TKT_NYV;
	}
	/* XXX  tkt = tgt */
	et->flags.invalid = 0;
    }else if(tgt->flags.invalid){
	kdc_log(0, "Ticket-granting ticket has INVALID flag set");
	return KRB5KRB_AP_ERR_TKT_INVALID;
    }

    if(f.forwardable){
	if(!tgt->flags.forwardable){
	    kdc_log(0, "Bad request for forwardable ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	et->flags.forwardable = 1;
    }
    if(f.forwarded){
	if(!tgt->flags.forwardable){
	    kdc_log(0, "Request to forward non-forwardable ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	et->flags.forwarded = 1;
	et->caddr = b->addresses;
    }
    if(tgt->flags.forwarded)
	et->flags.forwarded = 1;
	
    if(f.proxiable){
	if(!tgt->flags.proxiable){
	    kdc_log(0, "Bad request for proxiable ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	et->flags.proxiable = 1;
    }
    if(f.proxy){
	if(!tgt->flags.proxiable){
	    kdc_log(0, "Request to proxy non-proxiable ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	et->flags.proxy = 1;
	et->caddr = b->addresses;
    }
    if(tgt->flags.proxy)
	et->flags.proxy = 1;

    if(f.allow_postdate){
	if(!tgt->flags.may_postdate){
	    kdc_log(0, "Bad request for post-datable ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	et->flags.may_postdate = 1;
    }
    if(f.postdated){
	if(!tgt->flags.may_postdate){
	    kdc_log(0, "Bad request for postdated ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	if(b->from)
	    *et->starttime = *b->from;
	et->flags.postdated = 1;
	et->flags.invalid = 1;
    }else if(b->from && *b->from > kdc_time + context->max_skew){
	kdc_log(0, "Ticket cannot be postdated");
	return KRB5KDC_ERR_CANNOT_POSTDATE;
    }

    if(f.renewable){
	if(!tgt->flags.renewable){
	    kdc_log(0, "Bad request for renewable ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	et->flags.renewable = 1;
	ALLOC(et->renew_till);
	*et->renew_till = *b->rtime;
    }
    if(f.renew){
	time_t old_life;
	if(!tgt->flags.renewable || tgt->renew_till == NULL){
	    kdc_log(0, "Request to renew non-renewable ticket");
	    return KRB5KDC_ERR_BADOPTION;
	}
	old_life = tgt->endtime;
	if(tgt->starttime)
	    old_life -= *tgt->starttime;
	else
	    old_life -= tgt->authtime;
	et->endtime = *et->starttime + old_life;
    }	    
    
    /* checks for excess flags */
    if(f.request_anonymous){
	kdc_log(0, "Request for anonymous ticket");
	return KRB5KDC_ERR_BADOPTION;
    }
    return 0;
}

static krb5_error_code
fix_transited_encoding(TransitedEncoding *tr, 
		       const char *client_realm, 
		       const char *server_realm, 
		       const char *tgt_realm)
{
    krb5_error_code ret = 0;
    if(strcmp(client_realm, tgt_realm) && strcmp(server_realm, tgt_realm)){
	char **realms = NULL, **tmp;
	int num_realms = 0;
	int i;
	if(tr->tr_type){
	    if(tr->tr_type != DOMAIN_X500_COMPRESS){
		kdc_log(0, "Unknown transited type: %u", 
			tr->tr_type);
		return KRB5KDC_ERR_TRTYPE_NOSUPP;
	    }
	    ret = krb5_domain_x500_decode(tr->contents,
					  &realms, 
					  &num_realms,
					  client_realm,
					  server_realm);
	    if(ret){
		krb5_warn(context, ret, "Decoding transited encoding");
		return ret;
	    }
	}
	tmp = realloc(realms, (num_realms + 1) * sizeof(*realms));
	if(tmp == NULL){
	    ret = ENOMEM;
	    goto free_realms;
	}
	realms = tmp;
	realms[num_realms] = strdup(tgt_realm);
	if(realms[num_realms] == NULL){
	    ret = ENOMEM;
	    goto free_realms;
	}
	num_realms++;
	free_TransitedEncoding(tr);
	tr->tr_type = DOMAIN_X500_COMPRESS;
	ret = krb5_domain_x500_encode(realms, num_realms, &tr->contents);
	if(ret)
	    krb5_warn(context, ret, "Encoding transited encoding");
    free_realms:
	for(i = 0; i < num_realms; i++)
	    free(realms[i]);
	free(realms);
    }
    return ret;
}


static krb5_error_code
tgs_make_reply(KDC_REQ_BODY *b, 
	       EncTicketPart *tgt, 
	       EncTicketPart *adtkt, 
	       hdb_entry *server, 
	       hdb_entry *client, 
	       krb5_principal client_principal, 
	       hdb_entry *krbtgt,
	       krb5_enctype cetype,
	       krb5_data *reply)
{
    KDC_REP rep;
    EncKDCRepPart ek;
    EncTicketPart et;
    KDCOptions f = b->kdc_options;
    krb5_error_code ret;
    krb5_enctype setype;
    Key *skey;
    EncryptionKey *ekey;
    krb5_keytype sess_ktype;
    
    ret = find_keys(NULL, server, NULL, NULL, &skey, &setype, 
		    &sess_ktype, b->etype.val, b->etype.len);
    if(ret)
	return ret;
    if(adtkt)
	ekey = &adtkt->key;
    else
	ekey = &skey->key;
    ret = krb5_keytype_to_etype(context, ekey->keytype, &setype);
    
    memset(&rep, 0, sizeof(rep));
    memset(&et, 0, sizeof(et));
    memset(&ek, 0, sizeof(ek));
    
    rep.pvno = 5;
    rep.msg_type = krb_tgs_rep;

    et.authtime = tgt->authtime;
    fix_time(&b->till);
    et.endtime = min(tgt->endtime, *b->till);
    ALLOC(et.starttime);
    *et.starttime = kdc_time;
    
    ret = check_tgs_flags(b, tgt, &et);
    if(ret)
	return ret;

    copy_TransitedEncoding(&tgt->transited, &et.transited);
    ret = fix_transited_encoding(&et.transited,
				 *krb5_princ_realm(context, client_principal),
				 *krb5_princ_realm(context, server->principal),
				 *krb5_princ_realm(context, krbtgt->principal));
    if(ret){
	free_TransitedEncoding(&et.transited);
	return ret;
    }


    copy_Realm(krb5_princ_realm(context, server->principal), 
	       &rep.ticket.realm);
    krb5_principal2principalname(&rep.ticket.sname, server->principal);
    copy_Realm(&tgt->crealm, &rep.crealm);
    copy_PrincipalName(&tgt->cname, &rep.cname);
    rep.ticket.tkt_vno = 5;

    ek.caddr = et.caddr;
    if(et.caddr == NULL)
	et.caddr = tgt->caddr;

    {
	time_t life;
	life = et.endtime - *et.starttime;
	if(client && client->max_life)
	    life = min(life, *client->max_life);
	if(server->max_life)
	    life = min(life, *server->max_life);
	et.endtime = *et.starttime + life;
    }
    if(f.renewable_ok && tgt->flags.renewable && 
       et.renew_till == NULL && et.endtime < *b->till){
	et.flags.renewable = 1;
	ALLOC(et.renew_till);
	*et.renew_till = *b->till;
    }
    if(et.renew_till){
	time_t renew;
	renew = *et.renew_till - et.authtime;
	if(client && client->max_renew)
	    renew = min(renew, *client->max_renew);
	if(server->max_renew)
	    renew = min(renew, *server->max_renew);
	*et.renew_till = et.authtime + renew;
    }
	    
    if(et.renew_till){
	*et.renew_till = min(*et.renew_till, *tgt->renew_till);
	*et.starttime = min(*et.starttime, *et.renew_till);
	et.endtime = min(et.endtime, *et.renew_till);
    }
    
    *et.starttime = min(*et.starttime, et.endtime);

    if(*et.starttime == et.endtime){
	ret = KRB5KDC_ERR_NEVER_VALID;
	goto out;
    }
    if(et.renew_till && et.endtime == *et.renew_till){
	free(et.renew_till);
	et.renew_till = NULL;
	et.flags.renewable = 0;
    }
    
    et.flags.pre_authent = tgt->flags.pre_authent;
    et.flags.hw_authent = tgt->flags.hw_authent;
	    
    /* XXX Check enc-authorization-data */

    krb5_generate_random_keyblock(context, sess_ktype, &et.key);
    et.crealm = tgt->crealm;
    et.cname = tgt->cname;
	    
    ek.key = et.key;
    /* MIT must have at least one last_req */
    ek.last_req.len = 1;
    ek.last_req.val = calloc(1, sizeof(*ek.last_req.val));
    ek.nonce = b->nonce;
    ek.flags = et.flags;
    ek.authtime = et.authtime;
    ek.starttime = et.starttime;
    ek.endtime = et.endtime;
    ek.renew_till = et.renew_till;
    ek.srealm = rep.ticket.realm;
    ek.sname = rep.ticket.sname;
	    
    /* It is somewhat unclear where the etype in the following
       encryption should come from. What we have is a session
       key in the passed tgt, and a list of preferred etypes
       *for the new ticket*. Should we pick the best possible
       etype, given the keytype in the tgt, or should we look
       at the etype list here as well?  What if the tgt
       session key is DES3 and we want a ticket with a (say)
       CAST session key. Should the DES3 etype be added to the
       etype list, even if we don't want a session key with
       DES3? */
    ret = encode_reply(&rep, &et, &ek, setype, adtkt ? 0 : server->kvno, ekey,
		       cetype, 0, &tgt->key, reply);
out:
    free_TGS_REP(&rep);
    free_TransitedEncoding(&et.transited);
    if(et.starttime)
	free(et.starttime);
    if(et.renew_till)
	free(et.renew_till);
    free_LastReq(&ek.last_req);
    memset(et.key.keyvalue.data, 0, et.key.keyvalue.length);
    free_EncryptionKey(&et.key);
    return ret;
}

static krb5_error_code
tgs_check_authenticator(krb5_auth_context ac,
			KDC_REQ_BODY *b, krb5_keyblock *key)
{
    krb5_authenticator auth;
    size_t len;
    unsigned char buf[8192];
    krb5_error_code ret;
    
    krb5_auth_getauthenticator(context, ac, &auth);
    if(auth->cksum == NULL){
	kdc_log(0, "No authenticator in request");
	ret = KRB5KRB_AP_ERR_INAPP_CKSUM;
	goto out;
    }
    /*
     * according to RFC1510 it doesn't need to be keyed,
     * but according to the latest draft it needs to.
     */
    if (
#if 0
!krb5_checksum_is_keyed(auth->cksum->cksumtype)
	||
#endif
 !krb5_checksum_is_collision_proof(auth->cksum->cksumtype)) {
	kdc_log(0, "Bad checksum type in authenticator: %d", 
		auth->cksum->cksumtype);
	ret =  KRB5KRB_AP_ERR_INAPP_CKSUM;
	goto out;
    }
		
    /* XXX */
    ret = encode_KDC_REQ_BODY(buf + sizeof(buf) - 1, sizeof(buf),
			      b, &len);
    if(ret){
	kdc_log(0, "Failed to encode KDC-REQ-BODY: %s", 
		krb5_get_err_text(context, ret));
	goto out;
    }
    ret = krb5_verify_checksum(context, buf + sizeof(buf) - len, len,
			       key,
			       auth->cksum);
    if(ret){
	kdc_log(0, "Failed to verify checksum: %s", 
		krb5_get_err_text(context, ret));
    }
out:
    free_Authenticator(auth);
    free(auth);
    return ret;
}

static Realm 
is_krbtgt(PrincipalName *p)
{
    if(p->name_string.len == 2 && strcmp(p->name_string.val[0], "krbtgt") == 0)
	return p->name_string.val[1];
    else
	return NULL;
}

static Realm
find_rpath(Realm r)
{
    const char *new_realm = krb5_config_get_string(context->cf, 
						   "libdefaults", 
						   "capath", 
						   r, 
						   NULL);
    return (Realm)new_realm;
}
	    

static krb5_error_code
tgs_rep2(KDC_REQ_BODY *b,
	 PA_DATA *pa_data,
	 krb5_data *reply,
	 const char *from)
{
    krb5_ap_req ap_req;
    krb5_error_code ret;
    krb5_principal princ;
    krb5_auth_context ac = NULL;
    krb5_ticket *ticket = NULL;
    krb5_flags ap_req_options;
    const char *e_text = NULL;

    hdb_entry *krbtgt;
    EncTicketPart *tgt;
    Key *tkey;
    krb5_enctype cetype;
    krb5_principal cp = NULL;
    krb5_principal sp = NULL;

    memset(&ap_req, 0, sizeof(ap_req));
    ret = krb5_decode_ap_req(context, &pa_data->padata_value, &ap_req);
    if(ret){
	kdc_log(0, "Failed to decode AP-REQ: %s", 
		krb5_get_err_text(context, ret));
	goto out2;
    }
    
    if(!is_krbtgt(&ap_req.ticket.sname)){
	kdc_log(0, "PA-DATA is not a ticket-granting ticket");
	ret = KRB5KDC_ERR_POLICY; /* ? */
	goto out2;
    }
    
    principalname2krb5_principal(&princ,
				 ap_req.ticket.sname,
				 ap_req.ticket.realm);
    
    krbtgt = db_fetch(princ);

    if(krbtgt == NULL) {
	char *p;
	krb5_unparse_name(context, princ, &p);
	kdc_log(0, "Ticket-granting ticket not found in database: %s", p);
	free(p);
	ret = KRB5KRB_AP_ERR_NOT_US;
	goto out2;
    }
    
    if(ap_req.ticket.enc_part.kvno && 
       *ap_req.ticket.enc_part.kvno != krbtgt->kvno){
	kdc_log(0, "Ticket kvno = %d, DB kvno = %d", 
		*ap_req.ticket.enc_part.kvno,
		krbtgt->kvno);
	ret = KRB5KRB_AP_ERR_BADKEYVER;
	goto out2;
    }

    ret = hdb_etype2key(context, krbtgt, ap_req.ticket.enc_part.etype, &tkey);
    if(ret){
	char *str;
	krb5_etype_to_string(context, ap_req.ticket.enc_part.etype, &str);
	kdc_log(0, "No server key found for %s", str);
	free(str);
	ret = KRB5KRB_AP_ERR_BADKEYVER;
	goto out2;
    }
    
    ret = krb5_verify_ap_req(context,
			     &ac,
			     &ap_req,
			     princ,
			     &tkey->key,
			     &ap_req_options,
			     &ticket);
			     
    krb5_free_principal(context, princ);
    if(ret) {
	kdc_log(0, "Failed to verify AP-REQ: %s", 
		krb5_get_err_text(context, ret));
	goto out2;
    }

    cetype = ap_req.authenticator.etype;

    tgt = &ticket->ticket;

    ret = tgs_check_authenticator(ac, b, &tgt->key);

    krb5_auth_con_free(context, ac);

    if(ret){
	kdc_log(0, "Failed to verify authenticator: %s", 
		krb5_get_err_text(context, ret));
	goto out2;
    }
    
    {
	PrincipalName *s;
	Realm r;
	char *spn = NULL, *cpn = NULL;
	hdb_entry *server = NULL, *client = NULL;
	int loop = 0;
	EncTicketPart adtkt;
	char opt_str[128];

	s = b->sname;
	r = b->realm;
	if(b->kdc_options.enc_tkt_in_skey){
	    Ticket *t;
	    hdb_entry *uu;
	    krb5_principal p;
	    Key *tkey;
	    
	    if(b->additional_tickets == NULL || 
	       b->additional_tickets->len == 0){
		ret = KRB5KDC_ERR_BADOPTION; /* ? */
		kdc_log(0, "No second ticket present in request");
		goto out;
	    }
	    t = &b->additional_tickets->val[0];
	    if(!is_krbtgt(&t->sname)){
		kdc_log(0, "Additional ticket is not a ticket-granting ticket");
		ret = KRB5KDC_ERR_POLICY;
		goto out2;
	    }
	    principalname2krb5_principal(&p, t->sname, t->realm);
	    uu = db_fetch(p);
	    krb5_free_principal(context, p);
	    if(uu == NULL){
		ret = KRB5KDC_ERR_S_PRINCIPAL_UNKNOWN;
		goto out;
	    }
	    ret = hdb_etype2key(context, uu, t->enc_part.etype, &tkey);
	    if(ret){
		ret = KRB5KDC_ERR_ETYPE_NOSUPP; /* XXX */
		goto out;
	    }
	    ret = krb5_decrypt_ticket(context, t, &tkey->key, &adtkt);

	    if(ret)
		goto out;
	    s = &adtkt.cname;
	    r = adtkt.crealm;
	}

	principalname2krb5_principal(&sp, *s, r);
	krb5_unparse_name(context, sp, &spn);	
	principalname2krb5_principal(&cp, tgt->cname, tgt->crealm);
	krb5_unparse_name(context, cp, &cpn);
	unparse_flags (KDCOptions2int(b->kdc_options), KDCOptions_units,
		       opt_str, sizeof(opt_str));
	if(*opt_str)
	    kdc_log(0, "TGS-REQ %s from %s for %s [%s]", 
		    cpn, from, spn, opt_str);
	else
	    kdc_log(0, "TGS-REQ %s from %s for %s", cpn, from, spn);
    server_lookup:
	server = db_fetch(sp);
    

	if(server == NULL){
	    Realm req_rlm, new_rlm;
	    if(loop++ < 2 && (req_rlm = is_krbtgt(&sp->name))){
		new_rlm = find_rpath(req_rlm);
		if(new_rlm){
		    kdc_log(5, "krbtgt for realm %s not found, trying %s", 
			    req_rlm, new_rlm);
		    krb5_free_principal(context, sp);
		    free(spn);
		    krb5_make_principal(context, &sp, r, 
					"krbtgt", new_rlm, NULL);
		    krb5_unparse_name(context, sp, &spn);	
		    goto server_lookup;
		}
	    }
	    kdc_log(0, "Server not found in database: %s", spn);
	    ret = KRB5KDC_ERR_S_PRINCIPAL_UNKNOWN;
	    goto out;
	}

	client = db_fetch(cp);
	if(client == NULL)
	    kdc_log(1, "Client not found in database: %s", cpn);
#if 0
	/* XXX check client only if same realm as krbtgt-instance */
	if(client == NULL){
	    kdc_log(0, "Client not found in database: %s", cpn);
	    ret = KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN;
	    goto out;
	}
#endif


	if((b->kdc_options.validate || b->kdc_options.renew) && 
	   !krb5_principal_compare(context, 
				   krbtgt->principal,
				   server->principal)){
	    kdc_log(0, "Inconsistent request.");
	    ret = KRB5KDC_ERR_SERVER_NOMATCH;
	    goto out;
	}
	
	ret = tgs_make_reply(b, tgt, 
			     b->kdc_options.enc_tkt_in_skey ? &adtkt : NULL, 
			     server, client, cp, krbtgt, cetype, reply);
	
    out:
	free(spn);
	free(cpn);
	    
	if(server){
	    hdb_free_entry(context, server);
	    free(server);
	}
	if(client){
	    hdb_free_entry(context, client);
	    free(client);
	}
	
    }
out2:
    if(ret)
	krb5_mk_error(context,
		      ret,
		      e_text,
		      NULL,
		      cp,
		      sp,
		      0,
		      reply);
    krb5_free_principal(context, cp);
    krb5_free_principal(context, sp);
    if (ticket) {
	krb5_free_ticket(context, ticket);
	free(ticket);
    }
    free_AP_REQ(&ap_req);

    if(krbtgt){
	hdb_free_entry(context, krbtgt);
	free(krbtgt);
    }
    return ret;
}


krb5_error_code
tgs_rep(KDC_REQ *req, 
	krb5_data *data,
	const char *from)
{
    krb5_error_code ret;
    int i;
    PA_DATA *pa_data = NULL;

    if(req->padata == NULL){
	ret = KRB5KDC_ERR_PREAUTH_REQUIRED; /* XXX ??? */
	kdc_log(0, "TGS-REQ from %s without PA-DATA", from);
	goto out;
    }
    
    for(i = 0; i < req->padata->len; i++)
	if(req->padata->val[i].padata_type == pa_tgs_req){
	    pa_data = &req->padata->val[i];
	    break;
	}
    if(pa_data == NULL){
	ret = KRB5KDC_ERR_PADATA_TYPE_NOSUPP;
	
	kdc_log(0, "TGS-REQ from %s without PA-TGS-REQ", from);
	goto out;
    }
    ret = tgs_rep2(&req->req_body, pa_data, data, from);
out:
    if(ret && data->data == NULL){
	krb5_mk_error(context,
		      ret,
		      NULL,
		      NULL,
		      NULL,
		      NULL,
		      0,
		      data);
    }
    return 0;
}
