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

#include "hx_locl.h"
RCSID("$Id$");

static struct hx509_keyset_ops **ks_ops;
static int ks_num_ops;

struct hx509_certs_data {
    struct hx509_keyset_ops *ops;
    void *ops_data;
};

static struct hx509_keyset_ops *
_hx509_ks_type(const char *type)
{
    int i;

    for (i = 0; i < ks_num_ops; i++)
	if (strcasecmp(type, ks_ops[i]->name) == 0)
	    return ks_ops[i];

    return NULL;
}

void
_hx509_ks_register(struct hx509_keyset_ops *ops)
{
    struct hx509_keyset_ops **val;

    if (_hx509_ks_type(ops->name))
	return;

    val = realloc(ks_ops, (ks_num_ops + 1) * sizeof(ks_ops[0]));
    if (val == NULL)
	return;
    val[ks_num_ops] = ops;
    ks_ops = val;
    ks_num_ops++;
}

int
hx509_certs_init(const char *name, int flags,
		 hx509_lock lock, hx509_certs *certs)
{
    struct hx509_keyset_ops *ops;
    const char *residue;
    hx509_certs c;
    char *type;
    int ret;

    *certs = NULL;

    if (ks_ops == NULL) {
	_hx509_ks_mem_register();
	_hx509_ks_file_register();
	_hx509_ks_pkcs12_register();
    }

    residue = strchr(name, ':');
    if (residue) {
	type = strndup(name, residue - name);
	residue++;
	if (residue[0] == '\0')
	    residue = NULL;
    } else {
	type = strdup("MEMORY");
	residue = name;
    }
    if (type == NULL)
	return ENOMEM;
    
    ops = _hx509_ks_type(type);
    free(type);
    if (ops == NULL)
	return ENOENT;

    c = calloc(1, sizeof(*c));
    if (c == NULL)
	return ENOMEM;

    c->ops = ops;

    ret = (*ops->init)(c, &c->ops_data, flags, residue, lock);
    if (ret) {
	free(c);
	return ENOMEM;
    }

    *certs = c;
    return 0;
}

void
hx509_certs_free(hx509_certs *certs)
{
    (*(*certs)->ops->free)(*certs, (*certs)->ops_data);

    free(*certs);
    *certs = NULL;
}

int
hx509_certs_start_seq(hx509_certs certs, hx509_cursor cursor)
{
    int ret;

    if (certs->ops->iter_start == NULL)
	return ENOENT;

    ret = (*certs->ops->iter_start)(certs, certs->ops_data, cursor);
    if (ret)
	return ret;

    return 0;
}

int
hx509_certs_next_cert(hx509_certs certs, hx509_cursor cursor,
		      hx509_cert *cert)
{
    *cert = NULL;
    return (*certs->ops->iter)(certs, certs->ops_data, cursor, cert);
}

int
hx509_certs_end_seq(hx509_certs certs, hx509_cursor cursor)
{
    (*certs->ops->iter_end)(certs, certs->ops_data, cursor);
    return 0;
}


int
hx509_certs_iter(hx509_certs certs, int (*fn)(void *, hx509_cert), void *ctx)
{
    hx509_cursor cursor;
    hx509_cert c;
    int ret;

    ret = hx509_certs_start_seq(certs, &cursor);
    if (ret)
	return ret;
    
    while (1) {
	ret = hx509_certs_next_cert(certs, cursor, &c);
	if (ret)
	    break;
	if (c == NULL) {
	    ret = 0;
	    break;
	}
	ret = (*fn)(ctx, c);
	hx509_cert_free(c);
	if (ret)
	    break;
    }

    hx509_certs_end_seq(certs, cursor);

    return ret;
}

int
hx509_ci_print_names(void *ctx, hx509_cert c)
{
    Certificate *cert;
    hx509_name n;
    char *str;

    cert = _hx509_get_cert(c);

    _hx509_name_from_Name(&cert->tbsCertificate.subject, &n);
    hx509_name_to_string(n, &str);
    hx509_name_free(&n);
    fprintf(ctx, "%s\n", str);
    free(str);
    return 0;
}

int
hx509_certs_add(hx509_certs certs, hx509_cert cert)
{
    if (certs->ops->add == NULL)
	return ENOENT;

    return (*certs->ops->add)(certs, certs->ops_data, cert);
}

int
_hx509_certs_find(hx509_certs certs, const hx509_query *q, hx509_cert *r)
{
    hx509_cursor cursor;
    hx509_cert c;
    int ret;

    *r = NULL;

    if (certs->ops->query)
	return (*certs->ops->query)(certs, certs->ops_data, q, r);

    ret = hx509_certs_start_seq(certs, &cursor);
    if (ret)
	return ret;

    c = NULL;
    while (1) {
	ret = hx509_certs_next_cert(certs, cursor, &c);
	if (ret)
	    break;
	if (c == NULL)
	    break;
	if (_hx509_query_match_cert(q, c)) {
	    *r = c;
	    break;
	}
	hx509_cert_free(c);
    }

    hx509_certs_end_seq(certs, cursor);
    if (ret)
	return ret;
    if (c == NULL)
	return ENOENT;

    return 0;
}

static int
certs_merge_func(void *ctx, hx509_cert c)
{
    return hx509_certs_add((hx509_certs)ctx, c);
}

int
hx509_certs_merge(hx509_certs to, hx509_certs from)
{
    return hx509_certs_iter(from, certs_merge_func, to);
}

int
hx509_certs_append(hx509_certs to, hx509_lock lock, const char *name)
{
    hx509_certs s;
    int ret;

    ret = hx509_certs_init(name, 0, lock, &s);
    if (ret)
	return ret;
    ret = hx509_certs_merge(to, s);
    hx509_certs_free(&s);
    return ret;
}
