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

#include "kadm5_locl.h"

RCSID("$Id$");

#define set_value(X, V) do { if((X) == NULL) (X) = malloc(sizeof(*(X))); *(X) = V; } while(0)

static void
attr_to_flags(unsigned attr, HDBFlags *flags)
{
    flags->postdate =		!(attr & KRB5_KDB_DISALLOW_POSTDATED);
    flags->forwardable =	!(attr & KRB5_KDB_DISALLOW_FORWARDABLE);
    flags->initial =	       !!(attr & KRB5_KDB_DISALLOW_TGT_BASED);
    flags->renewable =		!(attr & KRB5_KDB_DISALLOW_RENEWABLE);
    flags->proxiable =		!(attr & KRB5_KDB_DISALLOW_PROXIABLE);
    /* DUP_SKEY */
    flags->invalid =	       !!(attr & KRB5_KDB_DISALLOW_ALL_TIX);
    flags->require_preauth =   !!(attr & KRB5_KDB_REQUIRES_PRE_AUTH);
    /* HW_AUTH */
    flags->server =		!(attr & KRB5_KDB_DISALLOW_SVR);
    flags->change_pw = 	       !!(attr & KRB5_KDB_PWCHANGE_SERVICE);
}

kadm5_ret_t
_kadm5_setup_entry(hdb_entry *ent, kadm5_principal_ent_t princ, 
		   kadm5_principal_ent_t def, u_int32_t mask)
{
    if(mask & KADM5_PRINC_EXPIRE_TIME)
	set_value(ent->valid_end, princ->princ_expire_time);
    if(mask & KADM5_PW_EXPIRATION)
	set_value(ent->pw_end, princ->pw_expiration);
    if(mask & KADM5_ATTRIBUTES)
	attr_to_flags(princ->attributes, &ent->flags);
    else if(def){
	/* attr_to_flags(def->attributes, &ent->flags); */
	ent->flags.client = 1;
	ent->flags.server = 1;
	ent->flags.forwardable = 1;
	ent->flags.proxiable = 1;
	ent->flags.renewable = 1;
	ent->flags.postdate = 1;
    }
    if(mask & KADM5_MAX_LIFE)
	set_value(ent->max_life, princ->max_life);
    else if(def && def->max_life)
	set_value(ent->max_life, def->max_life);
    if(mask & KADM5_KVNO)
	ent->kvno = princ->kvno;
    if(mask & KADM5_MAX_RLIFE)
	set_value(ent->max_renew, princ->max_renewable_life);
    else if(def && def->max_renewable_life)
	set_value(ent->max_renew, def->max_renewable_life);
    if(mask & KADM5_TL_DATA){
	/* XXX */
    }
    if(mask & KADM5_FAIL_AUTH_COUNT){
	/* XXX */
    }
    return 0;
}
