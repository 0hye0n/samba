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

#include "kadmin_locl.h"

RCSID("$Id$");

static struct getargs args[] = {
    { "random-key",	'r',	arg_flag,	NULL, "set random key" },
    { "password",	'p',	arg_string,	NULL, "princial's password" },
};

static int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(void)
{
    arg_printusage (args, num_args, "principal");
}


int
add_new_key(int argc, char **argv)
{
    kadm5_principal_ent_rec princ;
    char pwbuf[1024];
    char *password = NULL;
    int rkey = 0;
    int optind = 0;
    int mask = 0;
    krb5_error_code ret;
    krb5_principal princ_ent = NULL;

    args[0].value = &rkey;
    args[1].value = &password;
    
    if(getarg(args, num_args, argc, argv, &optind))
	goto usage;
    if(optind == argc)
	goto usage;
    memset(&princ, 0, sizeof(princ));
    krb5_parse_name(context, argv[optind], &princ_ent);
    princ.principal = princ_ent;
    mask |= KADM5_PRINCIPAL;
    edit_entry(&princ, &mask);
    if(rkey){
	princ.attributes |= KRB5_KDB_DISALLOW_ALL_TIX;
	mask |= KADM5_ATTRIBUTES;
	password = "hemlig";
    }
    if(password == NULL){
	if(des_read_pw_string(pwbuf, sizeof(pwbuf), "Password: ", 1))
	    goto out;
	password = pwbuf;
    }
    
    ret = kadm5_create_principal(kadm_handle, &princ, mask, password);
    if(ret)
	krb5_warn(context, ret, "kadm5_create_principal");
    if(rkey){
	krb5_keyblock *new_keys;
	int n_keys;
	ret = kadm5_randkey_principal(kadm_handle, princ_ent, 
				      &new_keys, &n_keys);
	if(ret)
	    krb5_warn(context, ret, "kadm5_randkey_principal");
	kadm5_get_principal(kadm_handle, princ_ent, &princ, 
			    KADM5_PRINCIPAL | KADM5_ATTRIBUTES);
	princ.attributes &= (~KRB5_KDB_DISALLOW_ALL_TIX);
	kadm5_modify_principal(kadm_handle, &princ, KADM5_ATTRIBUTES);
	kadm5_free_principal_ent(kadm_handle, &princ);
    }
out:
    if(princ_ent)
	krb5_free_principal(context, princ_ent);
    if(password)
	memset(password, 0, strlen(password));
    return 0;
usage:
    usage();
    goto out;
}
