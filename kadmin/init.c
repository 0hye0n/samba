/*
 * Copyright (c) 1997, 1998 Kungliga Tekniska H�gskolan
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
#include <kadm5/private.h>

RCSID("$Id$");

static kadm5_ret_t
create_random_entry(krb5_principal princ, time_t max_life, time_t max_rlife,
		    u_int32_t attributes)
{
    kadm5_principal_ent_rec ent;
    kadm5_ret_t ret;
    int mask = 0;
    krb5_keyblock *keys;
    int n_keys, i;

    memset(&ent, 0, sizeof(ent));
    ent.principal = princ;
    mask |= KADM5_PRINCIPAL;
    ent.max_life = max_life;
    mask |= KADM5_MAX_LIFE;
    ent.max_renewable_life = max_rlife;
    mask |= KADM5_MAX_RLIFE;
    ent.attributes |= attributes | KRB5_KDB_DISALLOW_ALL_TIX;
    mask |= KADM5_ATTRIBUTES;

    ret = kadm5_create_principal(kadm_handle, &ent, mask, "hemlig");
    if(ret)
	return ret;
    ret = kadm5_randkey_principal(kadm_handle, princ, &keys, &n_keys);
    if(ret)
	return ret;
    for(i = 0; i < n_keys; i++)
	krb5_free_keyblock_contents(context, &keys[i]);
    free(keys);
    ret = kadm5_get_principal(kadm_handle, princ, &ent, 
			      KADM5_PRINCIPAL | KADM5_ATTRIBUTES);
    if(ret)
	return ret;
    ent.attributes &= (~KRB5_KDB_DISALLOW_ALL_TIX);
    ent.kvno = 1;
    ret = kadm5_modify_principal(kadm_handle, &ent, 
				 KADM5_ATTRIBUTES|KADM5_KVNO);
    if(ret)
	return ret;
    return 0;
}

int
init(int argc, char **argv)
{
    kadm5_ret_t ret;
    int i;

    HDB *db = _kadm5_s_get_db(kadm_handle);

    ret = db->open(context, db, O_RDWR | O_CREAT, 0600);
    if(ret){
	krb5_warn(context, ret, "hdb_open");
	return 0;
    }
    db->close(context, db);
    for(i = 1; i < argc; i++){
	krb5_principal princ;
	unsigned max_life, max_rlife;

	/* Create `krbtgt/REALM' */
	krb5_make_principal(context, &princ, argv[i], "krbtgt", argv[i], NULL);
	get_deltat("Realm max ticket life", 
		   "unlimited",
		   &max_life);
	get_deltat("Realm max renewable ticket life", 
		   "unlimited",
		   &max_rlife);
	create_random_entry(princ, max_life, max_rlife, 0);
	krb5_free_principal(context, princ);
	/* Create `kadmin/changepw' */
	krb5_make_principal(context, &princ, argv[i], 
			    "kadmin", "changepw", NULL);
	create_random_entry(princ, 5*60, 5*60, 
			    KRB5_KDB_DISALLOW_TGT_BASED|
			    KRB5_KDB_PWCHANGE_SERVICE|
			    KRB5_KDB_DISALLOW_POSTDATED|
			    KRB5_KDB_DISALLOW_FORWARDABLE|
			    KRB5_KDB_DISALLOW_RENEWABLE|
			    KRB5_KDB_DISALLOW_PROXIABLE|
			    KRB5_KDB_REQUIRES_PRE_AUTH);
	krb5_free_principal(context, princ);
	/* Create `kadmin/admin' */
	krb5_make_principal(context, &princ, argv[i], 
			    "kadmin", "admin", NULL);
	create_random_entry(princ, 60*60, 60*60, KRB5_KDB_REQUIRES_PRE_AUTH);
	krb5_free_principal(context, princ);
	/* Create `default' */
	{
	    kadm5_principal_ent_rec ent;
	    int mask = 0;

	    memset (&ent, 0, sizeof(ent));
	    mask |= KADM5_PRINCIPAL;
	    krb5_make_principal(context, &ent.principal, argv[i],
				"default", NULL);
	    mask |= KADM5_MAX_LIFE;
	    ent.max_life = 24 * 60 * 60;
	    mask |= KADM5_MAX_RLIFE;
	    ent.max_renewable_life = 7 * ent.max_life;
	    ent.attributes = KRB5_KDB_DISALLOW_ALL_TIX;
	    mask |= KADM5_ATTRIBUTES;

	    ret = kadm5_create_principal(kadm_handle, &ent, mask, "");
	    if (ret)
		krb5_err (context, 1, ret, "kadm5_create_principal");

	    krb5_free_principal(context, ent.principal);
	}
    }
    return 0;
}
