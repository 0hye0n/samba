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
krb5_cc_register(krb5_context context, krb5_cc_ops *ops, int override)
{
    int i;
    if(context->cc_ops == NULL){
	context->num_ops = 4;
	context->cc_ops = calloc(context->num_ops, sizeof(*context->cc_ops));
    }
    for(i = 0; context->cc_ops[i].prefix && i < context->num_ops; i++){
	if(strcmp(context->cc_ops[i].prefix, ops->prefix) == 0){
	    if(override)
		free(context->cc_ops[i].prefix);
	    else
		return KRB5_CC_TYPE_EXISTS; /* XXX */
	}
    }
    if(i == context->num_ops){
	krb5_cc_ops *o = realloc(context->cc_ops, 
				 (context->num_ops + 4) * 
				 sizeof(*context->cc_ops));
	if(o == NULL)
	    return KRB5_CC_NOMEM;
	context->num_ops += 4;
	context->cc_ops = o;
	memset(context->cc_ops + i, 0, 
	       (context->num_ops - i) * sizeof(*context->cc_ops));
    }
    memcpy(&context->cc_ops[i], ops, sizeof(context->cc_ops[i]));
    context->cc_ops[i].prefix = strdup(ops->prefix);
    if(context->cc_ops[i].prefix == NULL)
	return KRB5_CC_NOMEM;
    
    return 0;
}


krb5_error_code
krb5_cc_resolve(krb5_context context,
		const char *residual,
		krb5_ccache *id)
{
    krb5_error_code ret;
    int i;

    if(context->cc_ops == NULL){
	ret = krb5_cc_register(context, &krb5_fcc_ops, 1);
	if(ret) return ret;
	ret = krb5_cc_register(context, &krb5_mcc_ops, 1);
	if(ret) return ret;
    }

    for(i = 0; context->cc_ops[i].prefix && i < context->num_ops; i++)
	if(strncmp(context->cc_ops[i].prefix, residual, 
		   strlen(context->cc_ops[i].prefix)) == 0){
	    krb5_ccache p;
	    p = malloc(sizeof(*p));
	    if(p == NULL)
		return KRB5_CC_NOMEM;
	    p->ops = &context->cc_ops[i];
	    *id = p;
	    ret =  p->ops->resolve(context, id, residual + 
				   strlen(p->ops->prefix) + 1);
	    if(ret) free(p);
	    return ret;
	}
    return KRB5_CC_UNKNOWN_TYPE;
}

krb5_error_code
krb5_cc_gen_new(krb5_context context,
		krb5_cc_ops *ops,
		krb5_ccache *id)
{
    krb5_ccache p;

    p = malloc (sizeof(*p));
    if (p == NULL)
	return KRB5_CC_NOMEM;
    p->ops = ops;
    *id = p;
    return p->ops->gen_new(context, id);
}

char*
krb5_cc_get_name(krb5_context context,
		 krb5_ccache id)
{
    return id->ops->get_name(context, id);
}

char*
krb5_cc_default_name(krb5_context context)
{
    static char name[1024];
    char *p;
    p = getenv("KRB5CCNAME");
    if(p) {
	strncpy (name, p, sizeof(name));
	name[sizeof(name) - 1] = '\0';
    } else
	snprintf(name,
		 sizeof(name),
		 "FILE:/tmp/krb5cc_%u",
		 (unsigned)getuid());
    return name;
}




krb5_error_code
krb5_cc_default(krb5_context context,
		krb5_ccache *id)
{
    return krb5_cc_resolve(context, 
			   krb5_cc_default_name(context), 
			   id);
}

krb5_error_code
krb5_cc_initialize(krb5_context context,
		   krb5_ccache id,
		   krb5_principal primary_principal)
{
    return id->ops->init(context, id, primary_principal);
}


krb5_error_code
krb5_cc_destroy(krb5_context context,
		krb5_ccache id)
{
    krb5_error_code ret;

    ret = id->ops->destroy(context, id);
    krb5_cc_close (context, id);
    return ret;
}

krb5_error_code
krb5_cc_close(krb5_context context,
	      krb5_ccache id)
{
    krb5_error_code ret;
    ret = id->ops->close(context, id);
#if 0 /* XXX */
    free(id->residual);
#endif
    free(id);
    return ret;
}

krb5_error_code
krb5_cc_store_cred(krb5_context context,
		   krb5_ccache id,
		   krb5_creds *creds)
{
    return id->ops->store(context, id, creds);
}

krb5_error_code
krb5_cc_retrieve_cred(krb5_context context,
		      krb5_ccache id,
		      krb5_flags whichfields,
		      krb5_creds *mcreds,
		      krb5_creds *creds)
{
    krb5_error_code ret;
    krb5_cc_cursor cursor;
    krb5_cc_start_seq_get(context, id, &cursor);
    while((ret = krb5_cc_next_cred(context, id, creds, &cursor)) == 0){
	if(krb5_principal_compare(context, mcreds->server, creds->server)){
	    ret = 0;
	    break;
	}
	krb5_free_creds_contents (context, creds);
    }
    krb5_cc_end_seq_get(context, id, &cursor);
    return ret;
}

krb5_error_code
krb5_cc_retrieve_cred_any_realm(krb5_context context,
				krb5_ccache id,
				krb5_flags whichfields,
				krb5_creds *mcreds,
				krb5_creds *creds)
{
    krb5_error_code ret;
    krb5_cc_cursor cursor;
    krb5_cc_start_seq_get(context, id, &cursor);
    while((ret = krb5_cc_next_cred(context, id, creds, &cursor)) == 0){
	if(krb5_principal_compare_any_realm(context, 
					    mcreds->server, 
					    creds->server)){
	    ret = 0;
	    break;
	}
	krb5_free_creds_contents (context, creds);
    }
    krb5_cc_end_seq_get(context, id, &cursor);
    return ret;
}

krb5_error_code
krb5_cc_get_principal(krb5_context context,
		      krb5_ccache id,
		      krb5_principal *principal)
{
    return id->ops->get_princ(context, id, principal);
}

krb5_error_code
krb5_cc_start_seq_get (krb5_context context,
		       krb5_ccache id,
		       krb5_cc_cursor *cursor)
{
    return id->ops->get_first(context, id, cursor);
}

krb5_error_code
krb5_cc_next_cred (krb5_context context,
		   krb5_ccache id,
		   krb5_creds *creds,
		   krb5_cc_cursor *cursor)
{
    return id->ops->get_next(context, id, cursor, creds);
}

krb5_error_code
krb5_cc_end_seq_get (krb5_context context,
		     krb5_ccache id,
		     krb5_cc_cursor *cursor)
{
    return id->ops->end_get(context, id, cursor);
}

krb5_error_code
krb5_cc_remove_cred(krb5_context context,
		    krb5_ccache id,
		    krb5_flags which,
		    krb5_creds *cred)
{
    return id->ops->remove_cred(context, id, which, cred);
}

krb5_error_code
krb5_cc_set_flags(krb5_context context,
		  krb5_ccache id,
		  krb5_flags flags)
{
    return id->ops->set_flags(context, id, flags);
}
		    
