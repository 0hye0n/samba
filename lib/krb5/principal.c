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

/* Public principal handling functions */

#define princ_num_comp(P) ((P)->name.name_string.len)
#define princ_type(P) ((P)->name.name_type)
#define princ_comp(P) ((P)->name.name_string.val)
#define princ_ncomp(P, N) ((P)->name.name_string.val[(N)])
#define princ_realm(P) ((P)->realm)

void
krb5_free_principal(krb5_context context,
		    krb5_principal p)
{
    if(p){
	free_Principal(p);
	free(p);
    }
}

krb5_error_code
krb5_parse_name(krb5_context context,
		const char *name,
		krb5_principal *principal)
{

    general_string *comp;
    general_string realm;
    int ncomp;

    char *p;
    char *q;
    char *s;
    char *start;

    int n;
    char c;
    int got_realm = 0;
  
    /* count number of component */
    ncomp = 1;
    for(p = (char*)name; *p; p++){
	if(*p=='\\'){
	    if(!p[1])
		return KRB5_PARSE_MALFORMED;
	    p++;
	} else if(*p == '/')
	    ncomp++;
    }
    comp = calloc(ncomp, sizeof(*comp));
  
    n = 0;
    start = q = p = s = strdup(name);
    while(*p){
	c = *p++;
	if(c == '\\'){
	    c = *p++;
	    if(c == 'n')
		c = '\n';
	    else if(c == 't')
		c = '\t';
	    else if(c == 'b')
		c = '\b';
	    else if(c == '0')
		c = '\0';
	}else if(c == '/' || c == '@'){
	    if(got_realm){
	    exit:
		while(n>0){
		    free(comp[--n]);
		}
		free(comp);
		free(s);
		return KRB5_PARSE_MALFORMED;
	    }else{
		comp[n] = malloc(q - start + 1);
		strncpy(comp[n], start, q - start);
		comp[n][q - start] = 0;
		n++;
	    }
	    if(c == '@')
		got_realm = 1;
	    start = q;
	    continue;
	}
	if(got_realm && (c == ':' || c == '/' || c == '\0'))
	    goto exit;
	*q++ = c;
    }
    if(got_realm){
	realm = malloc(q - start + 1);
	strncpy(realm, start, q - start);
	realm[q - start] = 0;
    }else{
	krb5_get_default_realm (context, &realm);

	comp[n] = malloc(q - start + 1);
	strncpy(comp[n], start, q - start);
	comp[n][q - start] = 0;
	n++;
    }
    *principal = malloc(sizeof(**principal));
    (*principal)->name.name_type = KRB5_NT_PRINCIPAL;
    (*principal)->name.name_string.val = comp;
    princ_num_comp(*principal) = n;
    (*principal)->realm = realm;
    free(s);
    return 0;
}

static size_t
quote_string(char *s, char **out)
{
    size_t len;
    char *p;
    char *tmp;
    char c = 0;
    tmp = *out;
    if(tmp)
	len = strlen(tmp);
    else 
	len = 0;
    for(p = s; *p; p++){
	if(*p == '\n')
	    c = 'n';
	else if(*p == '\t')
	    c = 't';
	else if(*p == '\b')
	    c = 'b';
	else if(*p == '\0')
	    c = '0';
	else if(*p == '/')
	    c='/';      
	else if(*p == '@')
	    c = '@';
	if(c){
	    tmp = realloc(tmp, len + 2);
	    tmp[len] = '\\';
	    tmp[len + 1] = c;
	    len += 2;
	    c = 0;
	}else{
	    tmp = realloc(tmp, len + 1);
	    tmp[len] = *p;
	    len++;
	}
    }
    tmp = realloc(tmp, len + 1);
    tmp[len] = 0;
    *out = tmp;
    return len;
}


krb5_error_code
krb5_unparse_name(krb5_context context,
		  krb5_principal principal,
		  char **name)
{
    size_t len;
    char *s;
    int i;
    int ncomp = princ_num_comp(principal);
    s = NULL;
    for (i = 0; i < ncomp; i++){
	if(i){
	    s = realloc(s, len + 2);
	    s[len] = '/';
	    s[len + 1] = 0;
	}
	len = quote_string(princ_ncomp(principal, i), &s);
    }
    s = realloc(s, len + 2);
    s[len] = '@';
    s[len + 1] = 0;
    len = quote_string(princ_realm(principal), &s);
    *name = s;
    return 0;
}


krb5_error_code
krb5_unparse_name_ext(krb5_context context,
		      krb5_const_principal principal,
		      char **name,
		      size_t *size)
{
    fprintf(stderr, "krb5_unparse_name_ext: not implemented\n");
    abort();
}


krb5_realm*
krb5_princ_realm(krb5_context context,
		 krb5_principal principal)
{
    return &princ_realm(principal);
}


void
krb5_princ_set_realm(krb5_context context,
		     krb5_principal principal,
		     krb5_realm *realm)
{
    princ_realm(principal) = *realm;
}


krb5_error_code
krb5_build_principal(krb5_context context,
		     krb5_principal *principal,
		     int rlen,
		     krb5_const_realm realm,
		     ...)
{
    krb5_error_code ret;
    va_list ap;
    va_start(ap, realm);
    ret = krb5_build_principal_va(context, principal, rlen, realm, ap);
    va_end(ap);
    return ret;
}

static krb5_error_code
append_component(krb5_context context, krb5_principal p, 
		 general_string comp,
		 size_t comp_len)
{
    general_string *tmp;
    size_t len = princ_num_comp(p);
    tmp = realloc(princ_comp(p), (len + 1) * sizeof(*tmp));
    if(tmp == NULL)
	return ENOMEM;
    princ_comp(p) = tmp;
    princ_ncomp(p, len) = malloc(comp_len + 1);
    memcpy (princ_ncomp(p, len), comp, comp_len);
    princ_ncomp(p, len)[comp_len] = '\0';
    princ_num_comp(p)++;
    return 0;
}

static void
va_ext_princ(krb5_context context, krb5_principal p, va_list ap)
{
    while(1){
	char *s;
	int len;
	len = va_arg(ap, int);
	if(len == 0)
	    break;
	s = va_arg(ap, char*);
	append_component(context, p, s, len);
    }
}

static void
va_princ(krb5_context context, krb5_principal p, va_list ap)
{
    while(1){
	char *s;
	s = va_arg(ap, char*);
	if(s == NULL)
	    break;
	append_component(context, p, s, strlen(s));
    }
}


static krb5_error_code
build_principal(krb5_context context,
		krb5_principal *principal,
		int rlen,
		krb5_const_realm realm,
		void (*func)(krb5_context, krb5_principal, va_list),
		va_list ap)
{
    krb5_principal p;
    int n;
  
    p = calloc(1, sizeof(*p));
    if (p == NULL)
	return ENOMEM;
    princ_type(p) = KRB5_NT_PRINCIPAL;

    princ_realm(p) = strdup(realm);
    if(p->realm == NULL){
	free(p);
	return ENOMEM;
    }
  
    (*func)(context, p, ap);
    *principal = p;
    return 0;
}

krb5_error_code
krb5_make_principal(krb5_context context,
		    krb5_principal *principal,
		    krb5_const_realm realm,
		    ...)
{
    krb5_error_code ret;
    krb5_realm r = NULL;
    va_list ap;
    if(realm == NULL){
	ret = krb5_get_default_realm(context, &r);
	if(ret)
	    return ret;
	realm = r;
    }
    va_start(ap, realm);
    ret = krb5_build_principal_va(context, principal, strlen(realm), realm, ap);
    va_end(ap);
    if(r)
	free(r);
    return ret;
}

krb5_error_code
krb5_build_principal_va(krb5_context context, 
			krb5_principal *principal, 
			int rlen,
			krb5_const_realm realm,
			va_list ap)
{
    return build_principal(context, principal, rlen, realm, va_princ, ap);
}

krb5_error_code
krb5_build_principal_va_ext(krb5_context context, 
			    krb5_principal *principal, 
			    int rlen,
			    krb5_const_realm realm,
			    va_list ap)
{
    return build_principal(context, principal, rlen, realm, va_ext_princ, ap);
}


krb5_error_code
krb5_build_principal_ext(krb5_context context,
			 krb5_principal *principal,
			 int rlen,
			 krb5_const_realm realm,
			 ...)
{
    krb5_error_code ret;
    va_list ap;
    va_start(ap, realm);
    ret = krb5_build_principal_va_ext(context, principal, rlen, realm, ap);
    va_end(ap);
    return ret;
}


krb5_error_code
krb5_copy_principal(krb5_context context,
		    krb5_const_principal inprinc,
		    krb5_principal *outprinc)
{
    krb5_principal p = malloc(sizeof(*p));
    if (p == NULL)
	return ENOMEM;
    copy_Principal(inprinc, p);
    *outprinc = p;
    return 0;
}


krb5_boolean
krb5_principal_compare_any_realm(krb5_context context,
				 krb5_const_principal princ1,
				 krb5_const_principal princ2)
{
    int i;
    if(princ_num_comp(princ1) != princ_num_comp(princ2))
	return FALSE;
    for(i = 0; i < princ_num_comp(princ1); i++){
	if(strcmp(princ_ncomp(princ1, i), princ_ncomp(princ2, i)) != 0)
	    return FALSE;
    }
    return TRUE;
}

krb5_boolean
krb5_principal_compare(krb5_context context,
		       krb5_const_principal princ1,
		       krb5_const_principal princ2)
{
    if(!krb5_realm_compare(context, princ1, princ2))
	return FALSE;
    return krb5_principal_compare_any_realm(context, princ1, princ2);
}


krb5_boolean
krb5_realm_compare(krb5_context context,
		   krb5_const_principal princ1,
		   krb5_const_principal princ2)
{
    return strcmp(princ_realm(princ1), princ_realm(princ2)) == 0;
}

krb5_error_code
krb5_425_conv_principal_ext(krb5_context context,
			    const char *name,
			    const char *instance,
			    const char *realm,
			    krb5_boolean (*func)(krb5_context, krb5_principal),
			    krb5_boolean resolve,
			    krb5_principal *princ)
{
    const char *p;
    krb5_error_code ret;
    krb5_principal pr;
    char host[128];

    /* do the following: if the name is found in the
       `v4_name_convert:host' part, is is assumed to be a `host' type
       principal, and the instance is looked up in the
       `v4_instance_convert' part. if not found there the name is
       (optionally) looked up as a hostname, and if that doesn't yield
       anything, the `default_domain' is appended to the instance
       */

    if(instance == NULL)
	goto no_host;
    if(instance[0] == 0){
	instance = NULL;
	goto no_host;
    }
    p = krb5_config_get_string(context->cf, "realms", realm,
			       "v4_name_convert", "host", name, NULL);
    if(p == NULL)
	p = krb5_config_get_string(context->cf, "libdefaults", 
				   "v4_name_convert", "host", name, NULL);
    if(p == NULL)
	goto no_host;
    name = p;
    p = krb5_config_get_string(context->cf, "realms", realm, 
			       "v4_instance_convert", instance, NULL);
    if(p){
	instance = p;
	ret = krb5_make_principal(context, &pr, realm, name, instance, NULL);
	if(func == NULL || (*func)(context, pr)){
	    *princ = pr;
	    return 0;
	}
	krb5_free_principal(context, pr);
	*princ = NULL;
	return HEIM_ERR_V4_PRINC_NO_CONV;
    }
    if(resolve){
	struct hostent *hp = gethostbyname(instance);
	if(hp){
	    instance = hp->h_name;
	    ret = krb5_make_principal(context, &pr, 
				      realm, name, instance, NULL);
	    if(func == NULL || (*func)(context, pr)){
		*princ = pr;
		return 0;
	    }
	    krb5_free_principal(context, pr);
	}
    }
    {
	char **domains, **d;
	domains = krb5_config_get_strings(context->cf, "realms", realm,
					  "v4_domains", NULL);
	for(d = domains; d && *d; d++){
	    snprintf(host, sizeof(host), "%s.%s", instance, *d);
	    ret = krb5_make_principal(context, &pr, realm, name, host, NULL);
	    if(func == NULL || (*func)(context, pr)){
		*princ = pr;
		krb5_config_free_strings(domains);
		return 0;
	    }
	    krb5_free_principal(context, pr);
	}
	krb5_config_free_strings(domains);
    }
    
    
    p = krb5_config_get_string(context->cf, "realms", realm, 
			       "default_domain", NULL);
    if(p == NULL){
	/* should this be an error or should it silently
	   succeed? */
	return HEIM_ERR_V4_PRINC_NO_CONV;
    }
	
    snprintf(host, sizeof(host), "%s.%s", instance, p);
    ret = krb5_make_principal(context, &pr, realm, name, host, NULL);
    if(func == NULL || (*func)(context, pr)){
	*princ = pr;
	return 0;
    }
    krb5_free_principal(context, pr);
    return HEIM_ERR_V4_PRINC_NO_CONV;
no_host:
    p = krb5_config_get_string(context->cf,
			       "realms",
			       realm,
			       "v4_name_convert",
			       "plain",
			       name,
			       NULL);
    if(p == NULL)
	p = krb5_config_get_string(context->cf,
				   "libdefaults",
				   "v4_name_convert",
				   "plain",
				   name,
				   NULL);
    if(p)
	name = p;
    
    ret = krb5_make_principal(context, &pr, realm, name, instance, NULL);
    if(func == NULL || (*func)(context, pr)){
	*princ = pr;
	return 0;
    }
    krb5_free_principal(context, pr);
    return HEIM_ERR_V4_PRINC_NO_CONV;
}

krb5_error_code
krb5_425_conv_principal(krb5_context context,
			const char *name,
			const char *instance,
			const char *realm,
			krb5_principal *princ)
{
    krb5_boolean resolve = krb5_config_get_bool(context->cf, 
						"libdefaults", 
						"v4_instance_resolve", 
						NULL);

    return krb5_425_conv_principal_ext(context, name, instance, realm, 
				       NULL, resolve, princ);
}


static char*
name_convert(krb5_context context, const char *name, const char *realm, 
	     const char *section)
{
    const krb5_config_binding *l;
    l = krb5_config_get_list (context->cf,
			      "realms",
			      realm,
			      "v4_name_convert",
			      section,
			      NULL);
    if(l == NULL)
	l = krb5_config_get_list (context->cf,
				  "libdefaults",
				  "v4_name_convert",
				  section,
				  NULL);
    
    while(l){
	if (l->type != STRING)
	    continue;
	if(strcmp(name, l->u.string) == 0)
	    return l->name;
	l = l->next;
    }
    return NULL;
}

krb5_error_code
krb5_524_conv_principal(krb5_context context,
			const krb5_principal principal,
			char *name, 
			char *instance,
			char *realm)
{
    char *n, *i, *r;
    char tmpinst[40];
    int type = princ_type(principal);

    r = principal->realm;

    switch(principal->name.name_string.len){
    case 1:
	n = principal->name.name_string.val[0];
	i = "";
	break;
    case 2:
	n = principal->name.name_string.val[0];
	i = principal->name.name_string.val[1];
	break;
    default:
	return KRB5_PARSE_MALFORMED;
    }

    {
	char *tmp = name_convert(context, n, r, "host");
	if(tmp){
	    type = KRB5_NT_SRV_HST;
	    n = tmp;
	}else{
	    tmp = name_convert(context, n, r, "plain");
	    if(tmp){
		type = KRB5_NT_UNKNOWN;
		n = tmp;
	    }
	}
    }

    if(type == KRB5_NT_SRV_HST){
	char *p;
	strncpy(tmpinst, i, sizeof(tmpinst));
	tmpinst[sizeof(tmpinst) - 1] = 0;
	p = strchr(tmpinst, '.');
	if(p) *p = 0;
	i = tmpinst;
    }
    
    if(strlen(r) >= 40)
	return KRB5_PARSE_MALFORMED;
    if(strlen(n) >= 40)
	return KRB5_PARSE_MALFORMED;
    if(strlen(i) >= 40)
	return KRB5_PARSE_MALFORMED;
    strcpy(realm, r);
    strcpy(name, n);
    strcpy(instance, i);
    return 0;
}


			
krb5_error_code
krb5_sname_to_principal (krb5_context context,
			 const char *hostname,
			 const char *sname,
			 int32_t type,
			 krb5_principal *ret_princ)
{
    krb5_error_code ret;
    char localhost[128];
    char **realms, *host = NULL;
    
    if(type != KRB5_NT_SRV_HST && type != KRB5_NT_UNKNOWN)
	return KRB5_SNAME_UNSUPP_NAMETYPE;
    if(hostname == NULL){
	gethostname(localhost, sizeof(localhost));
	hostname = localhost;
    }
    if(sname == NULL)
	sname = "host";
    if(type == KRB5_NT_SRV_HST){
	struct hostent *hp;
	hp = gethostbyname(hostname);
	if(hp != NULL)
	    hostname = hp->h_name;
    }
    if(type == KRB5_NT_SRV_HST){
	host = strdup(hostname);
	if(host == NULL){
	    return ENOMEM;
	}
	strlwr(host);
	hostname = host;
    }
    ret = krb5_get_host_realm(context, hostname, &realms);
    if(ret)
	return ret;
    ret = krb5_make_principal(context, ret_princ, realms[0], sname,
			      hostname, NULL);
    if(host)
	free(host);
    krb5_free_host_realm(context, realms);
    return ret;
}
