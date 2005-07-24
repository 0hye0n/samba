/*
 * Copyright (c) 2004 - 2005 Kungliga Tekniska H�gskolan
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
 * 3. Neither the name of the Institute nor the names of its contributors 
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

typedef struct hx509_verify_ctx_data *hx509_verify_ctx;
typedef struct hx509_certs_data *hx509_certs;
typedef struct hx509_cert_data *hx509_cert;
typedef struct hx509_cert_attribute_data *hx509_cert_attribute;
typedef struct hx509_validate_ctx_data *hx509_validate_ctx;
typedef struct hx509_name_data *hx509_name;
typedef void * hx509_cursor;
typedef struct hx509_lock_data *hx509_lock;
typedef struct hx509_private_key *hx509_private_key;

/* current unused */
typedef struct hx509_crypto_data *hx509_crypto;
typedef struct hx509_digest_data *hx509_digest;

typedef void (*hx509_vprint_func)(void *, const char *, va_list);

enum {
    HX509_VALIDATE_F_VALIDATE = 1,
    HX509_VALIDATE_F_VERBOSE = 2
};

struct hx509_cert_attribute_data {
    heim_oid oid;
    heim_octet_string data;
};

typedef enum {
    HX509_PROMPT_TYPE_PASSWORD		= 0x1,
    HX509_PROMPT_TYPE_QUESTION		= 0x2,
    HX509_PROMPT_TYPE_CONFIRMATION	= 0x4
} hx509_prompt_type;

typedef struct hx509_prompt {
    const char *prompt;
    int hidden;
    hx509_prompt_type type;
    heim_octet_string *reply;
} hx509_prompt;

typedef int (*hx509_prompter_fct)(void *, const hx509_prompt *);

#include <hx509-protos.h>
