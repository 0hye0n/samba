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

#define FORBIDDEN_MASK (KADM5_LAST_PWD_CHANGE | KADM5_MOD_TIME | KADM5_MOD_NAME | KADM5_MKVNO | KADM5_AUX_ATTRIBUTES | KADM5_LAST_SUCCESS | KADM5_LAST_FAILED | KADM5_KEY_DATA)


kadm5_ret_t
kadm5_s_modify_principal(void *server_handle,
			 kadm5_principal_ent_t princ, 
			 u_int32_t mask)
{
    kadm5_server_context *context = server_handle;
    hdb_entry ent;
    kadm5_ret_t ret;
    if((mask & FORBIDDEN_MASK))
	return KADM5_BAD_MASK;
    if((mask & KADM5_POLICY) && strcmp(princ->policy, "default"))
	return KADM5_UNK_POLICY;
    
    ent.principal = princ->principal;
    ret = context->db->open(context->context, context->db, O_RDWR, 0);
    if(ret)
	return ret;
    ret = context->db->fetch(context->context, context->db, &ent);
    if(ret)
	goto out;
    ret = _kadm5_setup_entry(&ent, princ, NULL, mask);
    if(ret)
	goto out2;
    ret = _kadm5_set_modifier(context, &ent);
    if(ret)
	goto out2;
    ret = context->db->store(context->context, context->db, 1, &ent);
out2:
    hdb_free_entry(context->context, &ent);
out:
    context->db->close(context->context, context->db);
    return _kadm5_error_code(ret);
}

