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

/* $Id$ */

#ifndef __KRB5_H__
#define __KRB5_H__

#include <time.h>
#include <bits.h>

#include <des.h>
#include <asn1_err.h>
#include <krb5_err.h>
#include <heim_err.h>

#include <asn1.h>

/* simple constants */

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

typedef int krb5_boolean;

typedef int32_t krb5_error_code;

typedef int krb5_kvno;

typedef u_int32_t krb5_flags;

typedef void *krb5_pointer;
typedef const void *krb5_const_pointer;

typedef octet_string krb5_data;

typedef enum krb5_cksumtype { 
  CKSUMTYPE_NONE		= 0,
  CKSUMTYPE_CRC32		= 1,
  CKSUMTYPE_RSA_MD4		= 2,
  CKSUMTYPE_RSA_MD4_DES		= 3,
  CKSUMTYPE_DES_MAC		= 4,
  CKSUMTYPE_DES_MAC_K		= 5,
  CKSUMTYPE_RSA_MD4_DES_K	= 6,
  CKSUMTYPE_RSA_MD5		= 7,
  CKSUMTYPE_RSA_MD5_DES		= 8,
  CKSUMTYPE_RSA_MD5_DES3	= 9,
  CKSUMTYPE_HMAC_SHA1_DES3	= 10
} krb5_cksumtype;



typedef enum krb5_enctype { 
  ETYPE_NULL			= 0,
  ETYPE_DES_CBC_CRC		= 1,
  ETYPE_DES_CBC_MD4		= 2,
  ETYPE_DES_CBC_MD5		= 3,
  ETYPE_DES3_CBC_MD5		= 5,
  ETYPE_DES3_CBC_SHA1		= 7,
  ETYPE_SIGN_DSA_GENERATE	= 8,
  ETYPE_ENCRYPT_RSA_PRIV	= 9,
  ETYPE_ENCRYPT_RSA_PUB		= 10,
  ETYPE_ENCTYPE_PK_CROSS	= 48
} krb5_enctype;

typedef enum krb5_preauthtype {
  KRB5_PADATA_NONE		= 0,
  KRB5_PADATA_AP_REQ,
  KRB5_PADATA_TGS_REQ		= 1,
  KRB5_PADATA_PW_SALT		= 3,
  KRB5_PADATA_ENC_TIMESTAMP	= 2,
  KRB5_PADATA_ENC_SECURID
} krb5_preauthtype;


typedef enum krb5_address_type { 
    KRB5_ADDRESS_INET = 2
} krb5_address_type;

enum {
  AP_OPTS_USE_SESSION_KEY = 1,
  AP_OPTS_MUTUAL_REQUIRED = 2
};

typedef HostAddress krb5_address;

typedef HostAddresses krb5_addresses;

typedef enum krb5_keytype { 
    KEYTYPE_NULL = 0,
    KEYTYPE_DES = 1 
} krb5_keytype;

typedef EncryptionKey krb5_keyblock;

typedef AP_REQ krb5_ap_req;

struct krb5_cc_ops;

typedef struct krb5_ccache_data{
    char *residual;
    struct krb5_cc_ops *ops;
    krb5_data data;
}krb5_ccache_data;

typedef struct krb5_cc_cursor{
    union {
	int fd;
	void *v;
    } u;
}krb5_cc_cursor;

typedef struct krb5_ccache_data *krb5_ccache;

typedef struct krb5_context_data *krb5_context;

typedef Realm krb5_realm;
typedef const char *krb5_const_realm; /* stupid language */
typedef Principal krb5_principal_data;
typedef struct Principal *krb5_principal;
typedef const struct Principal *krb5_const_principal;

typedef time_t krb5_time;

typedef struct krb5_times{
  krb5_time authtime;
  krb5_time starttime;
  krb5_time endtime;
  krb5_time renew_till;
} krb5_times;

typedef union {
    TicketFlags b;
    krb5_flags i;
} krb5_ticket_flags;

typedef union {
    KDCOptions b;
    krb5_flags i;
} krb5_kdc_flags;

typedef struct krb5_creds {
    krb5_principal client;
    krb5_principal server;
    krb5_keyblock session;
    krb5_times times;
    krb5_data ticket;
    krb5_data second_ticket;
    krb5_data authdata; /* huh? */
    krb5_addresses addresses;
    krb5_ticket_flags flags;
} krb5_creds;

typedef struct krb5_cc_ops{
    char *prefix;
    char* (*get_name)(krb5_context, krb5_ccache);
    krb5_error_code (*resolve)(krb5_context, krb5_ccache *, const char *);
    krb5_error_code (*gen_new)(krb5_context, krb5_ccache *);
    krb5_error_code (*init)(krb5_context, krb5_ccache, krb5_principal);
    krb5_error_code (*destroy)(krb5_context, krb5_ccache);
    krb5_error_code (*close)(krb5_context, krb5_ccache);
    krb5_error_code (*store)(krb5_context, krb5_ccache, krb5_creds*);
    krb5_error_code (*retrieve)(krb5_context, krb5_ccache, 
				krb5_flags, krb5_creds*, krb5_creds);
    krb5_error_code (*get_princ)(krb5_context, krb5_ccache, krb5_principal*);
    krb5_error_code (*get_first)(krb5_context, krb5_ccache, krb5_cc_cursor *);
    krb5_error_code (*get_next)(krb5_context, krb5_ccache, 
				krb5_cc_cursor*, krb5_creds*);
    krb5_error_code (*end_get)(krb5_context, krb5_ccache, krb5_cc_cursor*);
    krb5_error_code (*remove_cred)(krb5_context, krb5_ccache, 
				   krb5_flags, krb5_creds*);
    krb5_error_code (*set_flags)(krb5_context, krb5_ccache, krb5_flags);
} krb5_cc_ops;

struct krb5_log_facility;

struct krb5_config_binding {
    enum { STRING, LIST } type;
    char *name;
    struct krb5_config_binding *next;
    union {
	char *string;
	struct krb5_config_binding *list;
	void *generic;
    } u;
};

typedef struct krb5_config_binding krb5_config_binding;

typedef krb5_config_binding krb5_config_section;

typedef struct krb5_context_data{
    krb5_enctype *etypes;
    char *default_realm;
    time_t max_skew;
    time_t kdc_timeout;
    int32_t kdc_sec_offset;
    int32_t kdc_usec_offset;
    krb5_config_section *cf;
    struct error_table *et_list;
    struct krb5_log_facility *warn_dest;
    krb5_cc_ops *cc_ops;
    int num_ops;
} krb5_context_data;

enum{
  KRB5_NT_UNKNOWN	= 0,
  KRB5_NT_PRINCIPAL	= 1,
  KRB5_NT_SRV_INST	= 2,
  KRB5_NT_SRV_HST	= 3,
  KRB5_NT_SRV_XHST	= 4,
  KRB5_NT_UID		= 5
};


typedef struct krb5_ticket {
    EncTicketPart ticket;
    krb5_principal client;
    krb5_principal server;
} krb5_ticket;

typedef Authenticator krb5_authenticator_data;

typedef krb5_authenticator_data *krb5_authenticator;

typedef struct krb5_rcache{
    int dummy;
}krb5_rcache;

typedef struct krb5_keytab_data {
  char *filename;
}krb5_keytab_data;

typedef struct krb5_keytab_data *krb5_keytab;

typedef struct krb5_keytab_entry {
    krb5_principal principal;
    krb5_kvno vno;
    krb5_keyblock keyblock;
} krb5_keytab_entry;

typedef struct krb5_storage{
    void *data;
    size_t (*fetch)(struct krb5_storage*, void*, size_t);
    size_t (*store)(struct krb5_storage*, void*, size_t);
    off_t (*seek)(struct krb5_storage*, off_t, int);
    void (*free)(struct krb5_storage*);
} krb5_storage;

typedef struct krb5_kt_cursor {
    int fd;
    krb5_storage *sp;
} krb5_kt_cursor;

enum {
    KRB5_AUTH_CONTEXT_DO_TIME      = 1,
    KRB5_AUTH_CONTEXT_RET_TIME     = 2,
    KRB5_AUTH_CONTEXT_DO_SEQUENCE  = 4,
    KRB5_AUTH_CONTEXT_RET_SEQUENCE = 8
};

typedef struct krb5_auth_context_data{
    int32_t flags;
    krb5_cksumtype cksumtype;
    krb5_enctype enctype;

    krb5_address *local_address;
    krb5_address *remote_address;
    krb5_keyblock key;
    krb5_keyblock local_subkey;
    krb5_keyblock remote_subkey;

    int32_t local_seqnumber;
    int32_t remote_seqnumber;

    krb5_authenticator authenticator;
  
    krb5_pointer i_vector;
  
    krb5_rcache rcache;
  
}krb5_auth_context_data, *krb5_auth_context;

typedef struct {
    KDC_REP part1;
    EncKDCRepPart part2;
} krb5_kdc_rep;

extern char *heimdal_version, *heimdal_long_version;

typedef void (*krb5_log_log_func_t)(const char*, const char*, void*);
typedef void (*krb5_log_close_func_t)(void*);

typedef struct krb5_log_facility{
    const char *program;
    int len;
    struct facility *val;
} krb5_log_facility;

typedef EncAPRepPart krb5_ap_rep_enc_part;

#define KRB5_RECVAUTH_IGNORE_VERSION 1

#define KRB5_SENDAUTH_VERSION "KRB5_SENDAUTH_V1.0"

/* variables */

extern const char krb5_config_file[];
extern const char krb5_defkeyname[];

typedef krb5_time krb5_deltat;

typedef struct _krb5_prompt {
    char *prompt;
    int hidden;
    krb5_data *reply;
} krb5_prompt;

typedef int (*krb5_prompter_fct)(krb5_context context,
				 void *data,
				 const char *banner,
				 int num_prompts,
				 krb5_prompt prompts[]);

typedef krb5_error_code (*krb5_key_proc)(krb5_context context,
					 krb5_keytype type,
					 krb5_data *salt,
					 krb5_const_pointer keyseed,
					 krb5_keyblock **key);
typedef krb5_error_code (*krb5_decrypt_proc)(krb5_context context,
					     const krb5_keyblock *key,
					     krb5_const_pointer decrypt_arg,
					     krb5_kdc_rep *dec_rep);


typedef struct _krb5_get_init_creds_opt {
    krb5_flags flags;
    krb5_deltat tkt_life;
    krb5_deltat renew_life;
    int forwardable;
    int proxiable;
    krb5_enctype *etype_list;
    int etype_list_length;
    krb5_addresses *address_list;
#if 0 /* this is the MIT-way */
    krb5_address **address_list;
#endif
    /* XXX the next three should not be used, as they may be
       removed later */
    krb5_preauthtype *preauth_list;
    int preauth_list_length;
    krb5_data *salt;
} krb5_get_init_creds_opt;

#define KRB5_GET_INIT_CREDS_OPT_TKT_LIFE	0x0001
#define KRB5_GET_INIT_CREDS_OPT_RENEW_LIFE	0x0002
#define KRB5_GET_INIT_CREDS_OPT_FORWARDABLE	0x0004
#define KRB5_GET_INIT_CREDS_OPT_PROXIABLE	0x0008
#define KRB5_GET_INIT_CREDS_OPT_ETYPE_LIST	0x0010
#define KRB5_GET_INIT_CREDS_OPT_ADDRESS_LIST	0x0020
#define KRB5_GET_INIT_CREDS_OPT_PREAUTH_LIST	0x0040
#define KRB5_GET_INIT_CREDS_OPT_SALT		0x0080

typedef struct _krb5_verify_init_creds_opt {
    krb5_flags flags;
    int ap_req_nofail;
} krb5_verify_init_creds_opt;

#define KRB5_VERIFY_INIT_CREDS_OPT_AP_REQ_NOFAIL	0x0001

extern krb5_cc_ops krb5_fcc_ops;
extern krb5_cc_ops krb5_mcc_ops;

#define KRB5_KPASSWD_SUCCESS	0
#define KRB5_KPASSWD_MALFORMED	0
#define KRB5_KPASSWD_HARDERROR	0
#define KRB5_KPASSWD_AUTHERROR	0
#define KRB5_KPASSWD_SOFTERROR	0

#define KPASSWD_PORT 464

struct credentials; /* this is to keep the compiler happy */

#include <krb5-protos.h>

#endif /* __KRB5_H__ */

