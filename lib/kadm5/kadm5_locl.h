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

/* $Id$ */

#ifndef __KADM5_LOCL_H__
#define __KADM5_LOCL_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#include <fnmatch.h>
#include "admin.h"
#include "kadm5_err.h"
#include <hdb.h>
#include <roken.h>
#include <parse_units.h>
#include "private.h"

struct kadm_func {
    kadm5_ret_t (*chpass_principal) (void *, krb5_principal, char*);
    kadm5_ret_t (*create_principal) (void*, kadm5_principal_ent_t, 
				     u_int32_t, char*);
    kadm5_ret_t (*delete_principal) (void*, krb5_principal);
    kadm5_ret_t (*destroy) (void*);
    kadm5_ret_t (*flush) (void*);
    kadm5_ret_t (*get_principal) (void*, krb5_principal, 
				  kadm5_principal_ent_t, u_int32_t);
    kadm5_ret_t (*get_principals) (void*, const char*, char***, int*);
    kadm5_ret_t (*get_privs) (void*, u_int32_t*);
    kadm5_ret_t (*modify_principal) (void*, kadm5_principal_ent_t, u_int32_t);
    kadm5_ret_t (*randkey_principal) (void*, krb5_principal, 
				      krb5_keyblock**, int*);
    kadm5_ret_t (*rename_principal) (void*, krb5_principal, krb5_principal);
};

/* XXX should be integrated */
typedef struct kadm5_common_context {
    krb5_context context;
    krb5_boolean my_context;
    struct kadm_func funcs;
    void *data;
}kadm5_common_context;

typedef struct kadm5_log_peer {
    int fd;
    char *name;
    krb5_auth_context ac;
    struct kadm5_log_peer *next;
} kadm5_log_peer;

typedef struct kadm5_log_context {
    char *log_file;
    int log_fd;
    u_int32_t version;
} kadm5_log_context;

typedef struct kadm5_server_context {
    krb5_context context;
    krb5_boolean my_context;
    struct kadm_func funcs;
    /* */
    HDB *db;
    krb5_principal caller;
    unsigned acl_flags;
    char *acl_file;
    kadm5_log_context log_context;
}kadm5_server_context;

typedef struct kadm5_client_context {
    krb5_context context;
    krb5_boolean my_context;
    struct kadm_func funcs;
    /* */
    krb5_auth_context ac;
    char *realm;
    char *admin_server;
    int sock;
}kadm5_client_context;

enum kadm_ops {
    kadm_get,
    kadm_delete,
    kadm_create,
    kadm_rename,
    kadm_chpass,
    kadm_modify,
    kadm_randkey,
    kadm_get_privs,
    kadm_get_princs
};

#define KADMIN_APPL_VERSION "KADM0.0"

kadm5_ret_t
_kadm5_acl_check_permission __P((
	kadm5_server_context *context,
	unsigned op));

kadm5_ret_t
_kadm5_acl_init __P((kadm5_server_context *context));

kadm5_ret_t
_kadm5_c_init_context __P((
	kadm5_client_context **ctx,
	kadm5_config_params *params,
	krb5_context context));

kadm5_ret_t
_kadm5_client_recv __P((
	kadm5_client_context *context,
	krb5_storage *sp));

kadm5_ret_t
_kadm5_client_send __P((
	kadm5_client_context *context,
	krb5_storage *sp));

kadm5_ret_t
_kadm5_error_code __P((kadm5_ret_t code));

kadm5_ret_t
_kadm5_privs_to_string __P((
	u_int32_t privs,
	char *string,
	size_t len));

kadm5_ret_t
_kadm5_s_init_context __P((
	kadm5_server_context **ctx,
	kadm5_config_params *params,
	krb5_context context));

kadm5_ret_t
_kadm5_set_keys __P((
	kadm5_server_context *context,
	hdb_entry *ent,
	const char *password));

kadm5_ret_t
_kadm5_set_modifier __P((
	kadm5_server_context *context,
	hdb_entry *ent));

kadm5_ret_t
_kadm5_setup_entry __P((
	hdb_entry *ent,
	kadm5_principal_ent_t princ,
	kadm5_principal_ent_t def,
	u_int32_t mask));

kadm5_ret_t
_kadm5_string_to_privs __P((
	const char *s,
	u_int32_t* privs));

kadm5_ret_t
kadm5_log_init (kadm5_server_context *context);

kadm5_ret_t
kadm5_log_create (kadm5_server_context *context,
		  hdb_entry *ent);

kadm5_ret_t
kadm5_log_delete (kadm5_server_context *context,
		  krb5_principal princ);

kadm5_ret_t
kadm5_log_rename (kadm5_server_context *context,
		  krb5_principal source,
		  hdb_entry *ent);

kadm5_ret_t
kadm5_log_modify (kadm5_server_context *context,
		  hdb_entry *ent,
		  u_int32_t mask);

kadm5_ret_t
kadm5_log_end (kadm5_server_context *context);

kadm5_ret_t
kadm5_log_foreach (kadm5_server_context *context,
		   void (*func)(u_int32_t ver,
				time_t timestamp,
				enum kadm_ops op,
				u_int32_t len,
				krb5_storage *sp));

kadm5_ret_t
kadm5_log_replay_create (kadm5_server_context *context,
			 u_int32_t ver,
			 u_int32_t len,
			 krb5_storage *sp);

kadm5_ret_t
kadm5_log_replay_delete (kadm5_server_context *context,
			 u_int32_t ver,
			 u_int32_t len,
			 krb5_storage *sp);

kadm5_ret_t
kadm5_log_replay_rename (kadm5_server_context *context,
			 u_int32_t ver,
			 u_int32_t len,
			 krb5_storage *sp);

kadm5_ret_t
kadm5_log_replay_modify (kadm5_server_context *context,
			 u_int32_t ver,
			 u_int32_t len,
			 krb5_storage *sp);

kadm5_ret_t
kadm5_log_replay (kadm5_server_context *context,
		  enum kadm_ops op,
		  u_int32_t ver,
		  u_int32_t len,
		  krb5_storage *sp);

#endif /* __KADM5_LOCL_H__ */
