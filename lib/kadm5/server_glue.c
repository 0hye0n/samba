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

kadm5_ret_t
kadm5_chpass_principal(void *server_handle,
		       krb5_principal princ,
		       char *password)
{
    return kadm5_s_chpass_principal(server_handle, princ, password);
}

kadm5_ret_t
kadm5_create_principal(void *server_handle,
		       kadm5_principal_ent_t princ,
		       u_int32_t mask,
		       char *password)
{
    return kadm5_s_create_principal(server_handle, princ, mask, password);
}

kadm5_ret_t
kadm5_delete_principal(void *server_handle,
		       krb5_principal princ)
{
    return kadm5_s_delete_principal(server_handle, princ);
}

kadm5_ret_t
kadm5_destroy (void *server_handle)
{
    return kadm5_s_destroy(server_handle);
}

kadm5_ret_t
kadm5_flush (void *server_handle)
{
    return kadm5_s_flush(server_handle);
}

kadm5_ret_t
kadm5_get_principal(void *server_handle,
		    krb5_principal princ,
		    kadm5_principal_ent_t out,
		    u_int32_t mask)
{
    return kadm5_s_get_principal(server_handle, princ, out, mask);
}

kadm5_ret_t
kadm5_init_with_password(char *client_name,
			 char *pass,
			 char *service_name,
			 kadm5_config_params *realm_params,
			 unsigned long struct_version,
			 unsigned long api_version,
			 void **server_handle)
{
    return kadm5_s_init_with_password(client_name,
				      pass,
				      service_name,
				      realm_params,
				      struct_version,
				      api_version,
				      server_handle);
}

kadm5_ret_t
kadm5_init_with_password_ctx(krb5_context context,
			     char *client_name,
			     char *pass,
			     char *service_name,
			     kadm5_config_params *realm_params,
			     unsigned long struct_version,
			     unsigned long api_version,
			     void **server_handle)
{
    return kadm5_s_init_with_password_ctx(context,
					  client_name,
					  pass,
					  service_name,
					  realm_params,
					  struct_version,
					  api_version,
					  server_handle);
}

kadm5_ret_t
kadm5_modify_principal(void *server_handle,
		       kadm5_principal_ent_t princ,
		       u_int32_t mask)
{
    return kadm5_s_modify_principal(server_handle, princ, mask);
}

kadm5_ret_t
kadm5_randkey_principal(void *server_handle,
			krb5_principal princ,
			krb5_keyblock **new_keys,
			int *n_keys)
{
    return kadm5_s_randkey_principal(server_handle, princ, new_keys, n_keys);
}

kadm5_ret_t
kadm5_rename_principal(void *server_handle,
		       krb5_principal source,
		       krb5_principal target)
{
    return kadm5_s_rename_principal(server_handle, source, target);
}

