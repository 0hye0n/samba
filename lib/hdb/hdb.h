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

#ifndef __HDB_H__
#define __HDB_H__

#include <hdb_err.h>

#include <hdb_asn1.h>

enum hdb_lockop{ HDB_RLOCK, HDB_WLOCK };

typedef struct HDB{
    void *db;
    char *name;

    krb5_error_code (*close)(krb5_context, struct HDB*);
    krb5_error_code (*fetch)(krb5_context, struct HDB*, hdb_entry*);
    krb5_error_code (*store)(krb5_context, struct HDB*, hdb_entry*);
    krb5_error_code (*delete)(krb5_context, struct HDB*, hdb_entry*);
    krb5_error_code (*firstkey)(krb5_context, struct HDB*, hdb_entry*);
    krb5_error_code (*nextkey)(krb5_context, struct HDB*, hdb_entry*);
    krb5_error_code (*lock)(krb5_context, struct HDB*, int operation);
    krb5_error_code (*unlock)(krb5_context, struct HDB*);
    krb5_error_code (*_get)(krb5_context, struct HDB*, krb5_data, krb5_data*);
    krb5_error_code (*rename)(krb5_context, struct HDB*, const char*);
}HDB;

void hdb_free_entry(krb5_context, hdb_entry*);
krb5_error_code hdb_db_open(krb5_context, HDB**, const char*, int, mode_t);
krb5_error_code hdb_ndbm_open(krb5_context, HDB**, const char*, int, mode_t);
krb5_error_code hdb_open(krb5_context, HDB**, const char*, int, mode_t);

krb5_error_code hdb_etype2key(krb5_context, hdb_entry*, 
			      krb5_enctype, Key**);
krb5_error_code hdb_next_etype2key(krb5_context, hdb_entry*, 
				   krb5_enctype, Key**);
krb5_error_code hdb_keytype2key(krb5_context, hdb_entry*, 
				krb5_keytype, Key**);
krb5_error_code hdb_next_keytype2key(krb5_context, hdb_entry*, 
				     krb5_keytype, Key**);

typedef krb5_error_code (*hdb_foreach_func_t)(krb5_context, HDB*, hdb_entry*, void*);
krb5_error_code hdb_foreach(krb5_context context, HDB *db, hdb_foreach_func_t func, void *data);

Key *hdb_unseal_key(Key*, des_key_schedule);
void hdb_seal_key(Key*, des_key_schedule);
void hdb_free_key(Key*);

#define HDB_DB_DIR "/var/heimdal"
#define HDB_DEFAULT_DB HDB_DB_DIR "/heimdal"

#endif /* __HDB_H__ */
