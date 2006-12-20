/*
 * Copyright (c) 2006 Kungliga Tekniska H�gskolan
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

#include <config.h>

RCSID("$Id$");

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <krb5.h>
#include <roken.h>

#include "krb5-types.h"
#include "crypto-headers.h"

#include <heimntlm.h>


struct sec_buffer {
    uint16_t length;
    uint16_t allocated;
    uint32_t offset;
};

static const unsigned char ntlmsigature[8] = "NTLMSSP\x00";

/*
 *
 */

#define CHECK(f, e)							\
    do { ret = f ; if (ret != (e)) { ret = EINVAL; goto out; } } while(0)

/*
 *
 */

static krb5_error_code
ret_sec_buffer(krb5_storage *sp, struct sec_buffer *buf)
{
    krb5_error_code ret;
    CHECK(krb5_ret_uint16(sp, &buf->length), 0);
    CHECK(krb5_ret_uint16(sp, &buf->allocated), 0);
    CHECK(krb5_ret_uint32(sp, &buf->offset), 0);
out:
    return ret;
}

static krb5_error_code
store_sec_buffer(krb5_storage *sp, const struct sec_buffer *buf)
{
    krb5_error_code ret;
    CHECK(krb5_store_uint16(sp, buf->length), 0);
    CHECK(krb5_store_uint16(sp, buf->allocated), 0);
    CHECK(krb5_store_uint32(sp, buf->offset), 0);
out:
    return ret;
}

/*
 * Strings are either OEM or UNICODE. The later is encoded as ucs2 on
 * wire, but using utf8 in memory.
 */

static krb5_error_code
len_string(int ucs2, const char *s)
{
    size_t len = strlen(s);
    if (ucs2)
	len *= 2;
    return len;
}

static krb5_error_code
ret_string(krb5_storage *sp, int ucs2, struct sec_buffer *desc, char **s)
{
    krb5_error_code ret;

    *s = malloc(desc->length + 1);
    CHECK(krb5_storage_seek(sp, desc->offset, SEEK_SET), desc->offset);
    CHECK(krb5_storage_read(sp, *s, desc->length), desc->length);
    (*s)[desc->length] = '\0';

    if (ucs2) {
	size_t i;
	for (i = 0; i < desc->length / 2; i++) {
	    (*s)[i] = (*s)[i * 2];
	    if ((*s)[i * 2 + 1]) {
		free(*s);
		*s = NULL;
		return EINVAL;
	    }
	}
	(*s)[i] = '\0';
    }
    ret = 0;
out:
    return ret;

    return 0;
}

static krb5_error_code
put_string(krb5_storage *sp, int ucs2, const char *s)
{
    krb5_error_code ret;
    size_t len = strlen(s);
    char *p;

    /* ascii -> ucs2-le */
    if (ucs2) {
	size_t i;
	p = malloc(len * 2);
	for (i = 0; i < len; i++) {
	    p[(i * 2) + 0] = s[i];
	    p[(i * 2) + 1] = 0;
	}
	len *= 2;
    } else
	p = rk_UNCONST(s);

    CHECK(krb5_storage_write(sp, p, len), len);
    if (p != s)
	free(p);
    ret = 0;
out:
    return ret;
}

/*
 *
 */

static krb5_error_code
ret_buf(krb5_storage *sp, struct sec_buffer *desc, struct ntlm_buf *buf)
{
    krb5_error_code ret;

    buf->data = malloc(desc->length);
    buf->length = desc->length;
    CHECK(krb5_storage_seek(sp, desc->offset, SEEK_SET), desc->offset);
    CHECK(krb5_storage_read(sp, buf->data, buf->length), buf->length);
    ret = 0;
out:
    return ret;
}

static krb5_error_code
put_buf(krb5_storage *sp, struct ntlm_buf *buf)
{
    krb5_error_code ret;
    CHECK(krb5_storage_write(sp, buf->data, buf->length), buf->length);
    ret = 0;
out:
    return ret;
}

/*
 *
 */

void
heim_ntlm_free_targetinfo(struct ntlm_targetinfo *ti)
{
    free(ti->servername);
    free(ti->domainname);
    free(ti->dnsdomainname);
    free(ti->dnsservername);
    memset(ti, 0, sizeof(*ti));
}

static int
encode_ti_blob(krb5_storage *out, uint16_t type, int ucs2, char *s)
{
    krb5_error_code ret;
    CHECK(krb5_store_uint16(out, type), 0);
    CHECK(krb5_store_uint16(out, len_string(ucs2, s)), 0);
    CHECK(put_string(out, ucs2, s), 0);
out:
    return ret;
}

int
heim_ntlm_encode_targetinfo(struct ntlm_targetinfo *ti,
			    int ucs2, 
			    struct ntlm_buf *data)
{
    krb5_error_code ret;
    krb5_storage *out;

    data->data = NULL;
    data->length = 0;

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    if (ti->servername)
	CHECK(encode_ti_blob(out, 1, ucs2, ti->servername), 0);
    if (ti->domainname)
	CHECK(encode_ti_blob(out, 2, ucs2, ti->domainname), 0);
    if (ti->dnsservername)
	CHECK(encode_ti_blob(out, 3, ucs2, ti->dnsservername), 0);
    if (ti->dnsdomainname)
	CHECK(encode_ti_blob(out, 4, ucs2, ti->dnsdomainname), 0);

    /* end tag */
    CHECK(krb5_store_int16(out, 0), 0);
    CHECK(krb5_store_int16(out, 0), 0);

    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }
out:
    krb5_storage_free(out);
    return ret;
}

int
heim_ntlm_decode_targetinfo(struct ntlm_buf *data, int ucs2,
			    struct ntlm_targetinfo *ti)
{
    memset(ti, 0, sizeof(*ti));
    return 0;
}

/*
 * encoder/decoder type1 messages
 */

void
heim_ntlm_free_type1(struct ntlm_type1 *data)
{
    free(data->domain);
    free(data->hostname);
    memset(data, 0, sizeof(*data));
}

int
heim_ntlm_decode_type1(const struct ntlm_buf *buf, struct ntlm_type1 *data)
{
    krb5_error_code ret;
    unsigned char sig[8];
    uint32_t type;
    struct sec_buffer domain, hostname;
    krb5_storage *in;
    
    memset(data, 0, sizeof(*data));

    in = krb5_storage_from_readonly_mem(buf->data, buf->length);
    if (in == NULL) {
	ret = EINVAL;
	goto out;
    }
    krb5_storage_set_byteorder(in, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_storage_read(in, sig, sizeof(sig)), sizeof(sig));
    CHECK(memcmp(ntlmsigature, sig, sizeof(ntlmsigature)), 0);
    CHECK(krb5_ret_uint32(in, &type), 0);
    CHECK(type, 1);
    CHECK(krb5_ret_uint32(in, &data->flags), 0);
    if (data->flags & NTLM_SUPPLIED_DOMAIN)
	CHECK(ret_sec_buffer(in, &domain), 0);
    if (data->flags & NTLM_SUPPLIED_WORKSTAION)
	CHECK(ret_sec_buffer(in, &hostname), 0);
#if 0
    if (domain.offset > 32) {
	CHECK(krb5_ret_uint32(in, &data->os[0]), 0);
	CHECK(krb5_ret_uint32(in, &data->os[1]), 0);
    }
#endif
    if (data->flags & NTLM_SUPPLIED_DOMAIN)
	CHECK(ret_string(in, 0, &domain, &data->domain), 0);
    if (data->flags & NTLM_SUPPLIED_WORKSTAION)
	CHECK(ret_string(in, 0, &hostname, &data->hostname), 0);

out:
    krb5_storage_free(in);
    if (ret)
	heim_ntlm_free_type1(data);

    return ret;
}

int
heim_ntlm_encode_type1(const struct ntlm_type1 *type1, struct ntlm_buf *data)
{
    krb5_error_code ret;
    struct sec_buffer domain, hostname;
    krb5_storage *out;
    uint32_t base, flags;
    
    flags = type1->flags;
    base = 16;

    if (type1->domain) {
	base += 8;
	flags |= NTLM_SUPPLIED_DOMAIN;
    }
    if (type1->hostname) {
	base += 8;
	flags |= NTLM_SUPPLIED_WORKSTAION;
    }
    if (type1->os[0])
	base += 8;

    if (type1->domain) {
	domain.offset = base;
	domain.length = len_string(0, type1->domain);
	domain.allocated = domain.length;
    }
    if (type1->hostname) {
	hostname.offset = domain.allocated + domain.offset;
	hostname.length = len_string(0, type1->hostname);
	hostname.allocated = hostname.length;
    }

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(out, KRB5_STORAGE_BYTEORDER_LE);
    CHECK(krb5_storage_write(out, ntlmsigature, sizeof(ntlmsigature)), 
	  sizeof(ntlmsigature));
    CHECK(krb5_store_uint32(out, 1), 0);
    CHECK(krb5_store_uint32(out, flags), 0);
    
    if (type1->domain)
	CHECK(store_sec_buffer(out, &domain), 0);
    if (type1->hostname)
	CHECK(store_sec_buffer(out, &hostname), 0);
    if (type1->os[0]) {
	CHECK(krb5_store_uint32(out, type1->os[0]), 0);
	CHECK(krb5_store_uint32(out, type1->os[1]), 0);
    }
    if (type1->domain)
	CHECK(put_string(out, 0, type1->domain), 0);
    if (type1->hostname)
	CHECK(put_string(out, 0, type1->hostname), 0);

    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }
out:
    krb5_storage_free(out);

    return ret;
}

/*
 * encoder/decoder type 2 messages
 */

void
heim_ntlm_free_type2(struct ntlm_type2 *type2)
{
    memset(type2, 0, sizeof(*type2));
}

int
heim_ntlm_decode_type2(const struct ntlm_buf *buf, struct ntlm_type2 *type2)
{
    krb5_error_code ret;
    unsigned char sig[8];
    uint32_t type, ctx[2];
    struct sec_buffer targetname, targetinfo;
    krb5_storage *in;
    int ucs2 = 0;
    
    memset(type2, 0, sizeof(*type2));

    in = krb5_storage_from_readonly_mem(buf->data, buf->length);
    if (in == NULL) {
	ret = EINVAL;
	goto out;
    }
    krb5_storage_set_byteorder(in, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_storage_read(in, sig, sizeof(sig)), sizeof(sig));
    CHECK(memcmp(ntlmsigature, sig, sizeof(ntlmsigature)), 0);
    CHECK(krb5_ret_uint32(in, &type), 0);
    CHECK(type, 2);

    CHECK(ret_sec_buffer(in, &targetname), 0);
    CHECK(krb5_ret_uint32(in, &type2->flags), 0);
    if (type2->flags & NTLM_NEG_UNICODE)
	ucs2 = 1;
    CHECK(krb5_storage_read(in, type2->challange, sizeof(type2->challange)),
	  sizeof(type2->challange));
    CHECK(krb5_ret_uint32(in, &ctx[0]), 0); /* context */
    CHECK(krb5_ret_uint32(in, &ctx[1]), 0);
    CHECK(ret_sec_buffer(in, &targetinfo), 0);
    /* os version */
#if 0
    CHECK(krb5_ret_uint32(in, &type2->os[0]), 0);
    CHECK(krb5_ret_uint32(in, &type2->os[1]), 0);
#endif

    CHECK(ret_string(in, ucs2, &targetname, &type2->targetname), 0);
    CHECK(ret_buf(in, &targetinfo, &type2->targetinfo), 0);
    ret = 0;

out:
    krb5_storage_free(in);
    if (ret)
	heim_ntlm_free_type2(type2);

    return ret;
}

int
heim_ntlm_encode_type2(struct ntlm_type2 *type2, struct ntlm_buf *data)
{
    struct sec_buffer targetname, targetinfo;
    krb5_error_code ret;
    krb5_storage *out = NULL;
    uint32_t base;
    int ucs2 = 0;

    if (type2->os[0])
	base = 56;
    else
	base = 48;

    if (type2->flags & NTLM_NEG_UNICODE)
	ucs2 = 1;

    targetname.offset = base;
    targetname.length = len_string(ucs2, type2->targetname);
    targetname.allocated = targetname.length;

    targetinfo.offset = targetname.allocated + targetname.offset;
    targetinfo.length = type2->targetinfo.length;
    targetinfo.allocated = type2->targetinfo.length;

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(out, KRB5_STORAGE_BYTEORDER_LE);
    CHECK(krb5_storage_write(out, ntlmsigature, sizeof(ntlmsigature)), 
	  sizeof(ntlmsigature));
    CHECK(krb5_store_uint32(out, 2), 0);
    CHECK(store_sec_buffer(out, &targetname), 0);
    CHECK(krb5_store_uint32(out, type2->flags), 0);
    CHECK(krb5_storage_write(out, type2->challange, sizeof(type2->challange)),
	  sizeof(type2->challange));
    CHECK(krb5_store_uint32(out, 0), 0); /* context */
    CHECK(krb5_store_uint32(out, 0), 0);
    CHECK(store_sec_buffer(out, &targetinfo), 0);
    /* os version */
    if (type2->os[0]) {
	CHECK(krb5_store_uint32(out, type2->os[0]), 0);
	CHECK(krb5_store_uint32(out, type2->os[1]), 0);
    }
    CHECK(put_string(out, ucs2, type2->targetname), 0);
    CHECK(krb5_storage_write(out, type2->targetinfo.data, 
			     type2->targetinfo.length),
	  type2->targetinfo.length);
    
    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }

out:
    krb5_storage_free(out);

    return ret;
}

/*
 * encoder/decoder type 2 messages
 */

void
heim_ntlm_free_type3(struct ntlm_type3 *data)
{
    memset(data, 0, sizeof(*data));
}


/*
 *
 */

int
heim_ntlm_decode_type3(const struct ntlm_buf *buf,
		       int ucs2,
		       struct ntlm_type3 *type3)
{
    krb5_error_code ret;
    unsigned char sig[8];
    uint32_t type;
    krb5_storage *in;
    struct sec_buffer lm, ntlm, target, username, sessionkey, ws;

    memset(type3, 0, sizeof(*type3));
    memset(&sessionkey, 0, sizeof(sessionkey));

    in = krb5_storage_from_readonly_mem(buf->data, buf->length);
    if (in == NULL) {
	ret = EINVAL;
	goto out;
    }
    krb5_storage_set_byteorder(in, KRB5_STORAGE_BYTEORDER_LE);

    CHECK(krb5_storage_read(in, sig, sizeof(sig)), sizeof(sig));
    CHECK(memcmp(ntlmsigature, sig, sizeof(ntlmsigature)), 0);
    CHECK(krb5_ret_uint32(in, &type), 0);
    CHECK(type, 3);
    CHECK(ret_sec_buffer(in, &lm), 0);
    CHECK(ret_sec_buffer(in, &ntlm), 0);
    CHECK(ret_sec_buffer(in, &target), 0);
    CHECK(ret_sec_buffer(in, &username), 0);
    CHECK(ret_sec_buffer(in, &ws), 0);
    if (lm.offset >= 60) {
	CHECK(ret_sec_buffer(in, &sessionkey), 0);
    }
    if (lm.offset >= 64) {
	CHECK(krb5_ret_uint32(in, &type3->flags), 0);
    }
    if (lm.offset >= 72) {
	CHECK(krb5_ret_uint32(in, &type3->os[0]), 0);
	CHECK(krb5_ret_uint32(in, &type3->os[1]), 0);
    }
    CHECK(ret_buf(in, &lm, &type3->lm), 0);
    CHECK(ret_buf(in, &ntlm, &type3->ntlm), 0);
    CHECK(ret_string(in, ucs2, &target, &type3->targetname), 0);
    CHECK(ret_string(in, ucs2, &username, &type3->username), 0);
    CHECK(ret_string(in, ucs2, &username, &type3->ws), 0);
    if (sessionkey.offset)
	CHECK(ret_buf(in, &sessionkey, &type3->sessionkey), 0);

out:
    krb5_storage_free(in);
    if (ret)
	heim_ntlm_free_type3(type3);

    return ret;
}

int
heim_ntlm_encode_type3(struct ntlm_type3 *type3, struct ntlm_buf *data)
{
    struct sec_buffer lm, ntlm, target, username, sessionkey, ws;
    krb5_error_code ret;
    krb5_storage *out = NULL;
    uint32_t base;
    int ucs2 = 0;

    memset(&lm, 0, sizeof(lm));
    memset(&ntlm, 0, sizeof(ntlm));
    memset(&target, 0, sizeof(target));
    memset(&username, 0, sizeof(username));
    memset(&ws, 0, sizeof(ws));
    memset(&sessionkey, 0, sizeof(sessionkey));

    base = 52;
    if (type3->sessionkey.length) {
	base += 8; /* sessionkey sec buf */
	base += 4; /* flags */
    }
    if (type3->os[0]) {
	base += 8;
    }

    if (type3->flags & NTLM_NEG_UNICODE)
	ucs2 = 1;

    lm.offset = base;
    lm.length = type3->lm.length;
    lm.allocated = type3->lm.length;

    ntlm.offset = lm.offset + lm.allocated;
    ntlm.length = type3->ntlm.length;
    ntlm.allocated = ntlm.length;

    target.offset = ntlm.offset + ntlm.allocated;
    target.length = len_string(ucs2, type3->targetname);
    target.allocated = target.length;

    username.offset = target.offset + target.allocated;
    username.length = len_string(ucs2, type3->username);
    username.allocated = username.length;

    ws.offset = username.offset + username.allocated;
    ws.length = len_string(ucs2, type3->ws);
    ws.allocated = ws.length;

    sessionkey.offset = ws.offset + ws.allocated;
    sessionkey.length = type3->sessionkey.length;
    sessionkey.allocated = type3->sessionkey.length;

    out = krb5_storage_emem();
    if (out == NULL)
	return ENOMEM;

    krb5_storage_set_byteorder(out, KRB5_STORAGE_BYTEORDER_LE);
    CHECK(krb5_storage_write(out, ntlmsigature, sizeof(ntlmsigature)), 
	  sizeof(ntlmsigature));
    CHECK(krb5_store_uint32(out, 3), 0);

    CHECK(store_sec_buffer(out, &lm), 0);
    CHECK(store_sec_buffer(out, &ntlm), 0);
    CHECK(store_sec_buffer(out, &target), 0);
    CHECK(store_sec_buffer(out, &username), 0);
    CHECK(store_sec_buffer(out, &ws), 0);
    /* optional */
    if (type3->sessionkey.length) {
	CHECK(store_sec_buffer(out, &sessionkey), 0);
	CHECK(krb5_store_uint32(out, type3->flags), 0);
    }
#if 0
    CHECK(krb5_store_uint32(out, 0), 0); /* os0 */
    CHECK(krb5_store_uint32(out, 0), 0); /* os1 */
#endif

    CHECK(put_buf(out, &type3->lm), 0);
    CHECK(put_buf(out, &type3->ntlm), 0);
    CHECK(put_string(out, ucs2, type3->targetname), 0);
    CHECK(put_string(out, ucs2, type3->username), 0);
    CHECK(put_string(out, ucs2, type3->ws), 0);
    CHECK(put_buf(out, &type3->sessionkey), 0);
    
    {
	krb5_data d;
	ret = krb5_storage_to_data(out, &d);
	data->data = d.data;
	data->length = d.length;
    }

out:
    krb5_storage_free(out);

    return ret;
}


/*
 *
 */

static void
splitandenc(unsigned char *hash, 
	    unsigned char *challange,
	    unsigned char *answer)
{
    DES_cblock key;
    DES_key_schedule sched;

    ((unsigned char*)key)[0] =  hash[0];
    ((unsigned char*)key)[1] = (hash[0] << 7) | (hash[1] >> 1);
    ((unsigned char*)key)[2] = (hash[1] << 6) | (hash[2] >> 2);
    ((unsigned char*)key)[3] = (hash[2] << 5) | (hash[3] >> 3);
    ((unsigned char*)key)[4] = (hash[3] << 4) | (hash[4] >> 4);
    ((unsigned char*)key)[5] = (hash[4] << 3) | (hash[5] >> 5);
    ((unsigned char*)key)[6] = (hash[5] << 2) | (hash[6] >> 6);
    ((unsigned char*)key)[7] = (hash[6] << 1);

    DES_set_odd_parity(&key);
    DES_set_key(&key, &sched);
    DES_ecb_encrypt((DES_cblock *)challange, (DES_cblock *)answer, &sched, 1);
    memset(&sched, 0, sizeof(sched));
    memset(key, 0, sizeof(key));
}

int
heim_ntlm_nt_key(const char *password, struct ntlm_buf *key)
{
    size_t i, len = strlen(password);
    unsigned char *p;
    MD4_CTX ctx;

    key->data = malloc(MD5_DIGEST_LENGTH);
    if (key->data == NULL)
	return ENOMEM;
    key->length = MD5_DIGEST_LENGTH;

    p = malloc(len * 2);
    if (p == NULL) {
	free(key->data);
	key->data = NULL;
	return ENOMEM;
    }

    /* ascii -> ucs2-le */
    for (i = 0; i < len; i++) {
	p[(i * 2) + 0] = password[i];
	p[(i * 2) + 1] = 0;
    }
    MD4_Init(&ctx);
    MD4_Update(&ctx, p, len * 2);
    free(p);
    MD4_Final(key->data, &ctx);
    return 0;
}

int
heim_ntlm_calculate_ntlm1(void *key, size_t len,
			  unsigned char challange[8],
			  struct ntlm_buf *answer)
{
    unsigned char res[21];

    if (len != MD4_DIGEST_LENGTH)
	return EINVAL;

    memcpy(res, key, len);
    memset(&res[MD4_DIGEST_LENGTH], 0, sizeof(res) - MD4_DIGEST_LENGTH);

    answer->data = malloc(24);
    if (answer->data == NULL)
	return ENOMEM;
    answer->length = 24;

    splitandenc(&res[0],  challange, ((unsigned char *)answer->data) + 0);
    splitandenc(&res[7],  challange, ((unsigned char *)answer->data) + 8);
    splitandenc(&res[14], challange, ((unsigned char *)answer->data) + 16);

    return 0;
}

int
heim_ntlm_build_ntlm1_master(void *key, size_t len,
			     struct ntlm_buf *session,
			     struct ntlm_buf *master)
{
    RC4_KEY rc4;

    if (len != MD4_DIGEST_LENGTH)
	return EINVAL;
    
    session->length = MD4_DIGEST_LENGTH;
    session->data = malloc(session->length);
    if (session->data == NULL)
	goto out;
    
    master->length = MD4_DIGEST_LENGTH;
    master->data = malloc(master->length);
    if (master->data == NULL) {
	free(session->data);
	goto out;
    }
    
    {
	unsigned char sessionkey[MD4_DIGEST_LENGTH];
	MD4_CTX ctx;
    
	MD4_Init(&ctx);
	MD4_Update(&ctx, key, len);
	MD4_Final(sessionkey, &ctx);
	
	RC4_set_key(&rc4, sizeof(sessionkey), sessionkey);
    }
    
    if (RAND_bytes(session->data, session->length) != 1) {
	free(master->data);
	free(session->data);
	goto out;
    }
    
    RC4(&rc4, master->length, session->data, master->data);
    memset(&rc4, 0, sizeof(rc4));
    
    return 0;
out:
    master->data = NULL;
    master->length = 0;
    session->data = NULL;
    session->length = 0;
    return EINVAL;
}


