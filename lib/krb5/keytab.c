/*
 * Copyright (c) 1997, 1998, 1999 Kungliga Tekniska H�gskolan
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

#include "krb5_locl.h"

RCSID("$Id$");

#ifndef O_BINARY
#define O_BINARY 0
#endif

#define KRB5_KT_VNO_1 1
#define KRB5_KT_VNO_2 2
#define KRB5_KT_VNO   KRB5_KT_VNO_2

static struct krb5_keytab_data *kt_types;
static int num_kt_types;

static krb5_kt_ops fops, mops; /* forward declaration */

krb5_error_code
krb5_kt_register(krb5_context context,
		 krb5_kt_ops *ops)
{
    struct krb5_keytab_data *tmp;
    tmp = realloc(kt_types, (num_kt_types + 1) * sizeof(*kt_types));
    if(tmp == NULL)
	return ENOMEM;
    memcpy(&tmp[num_kt_types], ops, sizeof(tmp[num_kt_types]));
    kt_types = tmp;
    num_kt_types++;
    return 0;
}

krb5_error_code
krb5_kt_resolve(krb5_context context,
		const char *name,
		krb5_keytab *id)
{
    krb5_keytab k;
    int i;
    const char *type, *residual;
    size_t type_len;
    krb5_error_code ret;

    /* register default set of types */
    if(num_kt_types == 0) {
	ret = krb5_kt_register(context, &fops);
	if(ret) return ret;
	ret = krb5_kt_register(context, &mops);
	if(ret) return ret;
    }
    
    residual = strchr(name, ':');
    if(residual == NULL) {
	type = "FILE";
	type_len = strlen(type);
	residual = name;
    } else {
	type = name;
	type_len = residual - name;
	residual++;
    }
    
    for(i = 0; i < num_kt_types; i++) {
	if(strncmp(type, kt_types[i].prefix, type_len) == 0)
	    break;
    }
    if(i == num_kt_types)
	return KRB5_KT_UNKNOWN_TYPE;
    
    ALLOC(k, 1);
    memcpy(k, &kt_types[i], sizeof(*k));
    k->data = NULL;
    ret = (*k->resolve)(context, residual, k);
    if(ret) {
	free(k);
	k = NULL;
    }
    *id = k;
    return ret;
}

krb5_error_code
krb5_kt_default_name(krb5_context context, char *name, size_t namesize)
{
    strncpy(name, context->default_keytab, namesize);
    if(strlen(context->default_keytab) >= namesize)
	return KRB5_CONFIG_NOTENUFSPACE;
    return 0;
}

krb5_error_code
krb5_kt_default(krb5_context context, krb5_keytab *id)
{
    return krb5_kt_resolve (context, context->default_keytab, id);
}

krb5_error_code
krb5_kt_read_service_key(krb5_context context,
			 krb5_pointer keyprocarg,
			 krb5_principal principal,
			 krb5_kvno vno,
			 krb5_enctype enctype,
			 krb5_keyblock **key)
{
    krb5_keytab keytab;
    krb5_keytab_entry entry;
    krb5_error_code r;

    if (keyprocarg)
	r = krb5_kt_resolve (context, keyprocarg, &keytab);
    else
	r = krb5_kt_default (context, &keytab);

    if (r)
	return r;

    r = krb5_kt_get_entry (context, keytab, principal, vno, enctype, &entry);
    krb5_kt_close (context, keytab);
    if (r)
	return r;
    r = krb5_copy_keyblock (context, &entry.keyblock, key);
    krb5_kt_free_entry(context, &entry);
    return r;
}

krb5_error_code
krb5_kt_remove_entry(krb5_context context,
		     krb5_keytab id,
		     krb5_keytab_entry *entry)
{
    if(id->remove == NULL)
	return KRB5_KT_NOWRITE;
    return (*id->remove)(context, id, entry);
}

krb5_error_code
krb5_kt_get_name(krb5_context context, 
		 krb5_keytab keytab,
		 char *name,
		 size_t namesize)
{
    return (*keytab->get_name)(context, keytab, name, namesize);
}

krb5_error_code
krb5_kt_close(krb5_context context, 
	      krb5_keytab id)
{
    krb5_error_code ret;
    ret = (*id->close)(context, id);
    if(ret == 0)
	free(id);
    return ret;
}

static krb5_boolean
kt_compare(krb5_context context,
	   krb5_keytab_entry *entry, 
	   krb5_const_principal principal,
	   krb5_kvno vno,
	   krb5_enctype enctype)
{
    if(principal != NULL && 
       !krb5_principal_compare(context, entry->principal, principal))
	return FALSE;
    if(vno && vno != entry->vno)
	return FALSE;
    if(enctype && enctype != entry->keyblock.keytype)
	return FALSE;
    return TRUE;
}

krb5_error_code
krb5_kt_get_entry(krb5_context context,
		  krb5_keytab id,
		  krb5_const_principal principal,
		  krb5_kvno kvno,
		  krb5_enctype enctype,
		  krb5_keytab_entry *entry)
{
    krb5_keytab_entry tmp;
    krb5_error_code r;
    krb5_kt_cursor cursor;

    if(id->get) return (*id->get)(context, id, principal, kvno, enctype, entry);

    r = krb5_kt_start_seq_get (context, id, &cursor);
    if (r)
	return KRB5_KT_NOTFOUND; /* XXX i.e. file not found */

    entry->vno = 0;
    while (krb5_kt_next_entry(context, id, &tmp, &cursor) == 0) {
	if (kt_compare(context, &tmp, principal, 0, enctype)) {
	    if (kvno == tmp.vno) {
		krb5_kt_copy_entry_contents (context, &tmp, entry);
		krb5_kt_free_entry (context, &tmp);
		krb5_kt_end_seq_get(context, id, &cursor);
		return 0;
	    } else if (kvno == 0 && tmp.vno > entry->vno) {
		if (entry->vno)
		    krb5_kt_free_entry (context, entry);
		krb5_kt_copy_entry_contents (context, &tmp, entry);
	    }
	}
	krb5_kt_free_entry(context, &tmp);
    }
    krb5_kt_end_seq_get (context, id, &cursor);
    if (entry->vno)
	return 0;
    else
	return KRB5_KT_NOTFOUND;
}

krb5_error_code
krb5_kt_copy_entry_contents(krb5_context context,
			    const krb5_keytab_entry *in,
			    krb5_keytab_entry *out)
{
    krb5_error_code ret;

    memset(out, 0, sizeof(*out));
    out->vno = in->vno;

    ret = krb5_copy_principal (context, in->principal, &out->principal);
    if (ret)
	goto fail;
    ret = krb5_copy_keyblock_contents (context,
				       &in->keyblock,
				       &out->keyblock);
    if (ret)
	goto fail;
    return 0;
fail:
    krb5_kt_free_entry (context, out);
    return ret;
}

krb5_error_code
krb5_kt_free_entry(krb5_context context,
		   krb5_keytab_entry *entry)
{
  krb5_free_principal (context, entry->principal);
  krb5_free_keyblock_contents (context, &entry->keyblock);
  return 0;
}

#if 0
static int
xxxlock(int fd, int write)
{
    if(flock(fd, (write ? LOCK_EX : LOCK_SH) | LOCK_NB) < 0) {
	sleep(1);
	if(flock(fd, (write ? LOCK_EX : LOCK_SH) | LOCK_NB) < 0) 
	    return -1;
    }
    return 0;
}

static void
xxxunlock(int fd)
{
    flock(fd, LOCK_UN);
}
#endif

krb5_error_code
krb5_kt_start_seq_get(krb5_context context,
		      krb5_keytab id,
		      krb5_kt_cursor *cursor)
{
    return (*id->start_seq_get)(context, id, cursor);
}

krb5_error_code
krb5_kt_next_entry(krb5_context context,
		   krb5_keytab id,
		   krb5_keytab_entry *entry,
		   krb5_kt_cursor *cursor)
{
    return (*id->next_entry)(context, id, entry, cursor);
}


krb5_error_code
krb5_kt_end_seq_get(krb5_context context,
		    krb5_keytab id,
		    krb5_kt_cursor *cursor)
{
    return (*id->end_seq_get)(context, id, cursor);
}

static krb5_error_code
krb5_kt_ret_data(krb5_storage *sp,
		 krb5_data *data)
{
    int ret;
    int16_t size;
    ret = krb5_ret_int16(sp, &size);
    if(ret)
	return ret;
    data->length = size;
    data->data = malloc(size);
    if (data->data == NULL)
	return ENOMEM;
    ret = sp->fetch(sp, data->data, size);
    if(ret != size)
	return (ret < 0)? errno : KRB5_KT_END;
    return 0;
}

static krb5_error_code
krb5_kt_ret_string(krb5_storage *sp,
		   general_string *data)
{
    int ret;
    int16_t size;
    ret = krb5_ret_int16(sp, &size);
    if(ret)
	return ret;
    *data = malloc(size + 1);
    if (*data == NULL)
	return ENOMEM;
    ret = sp->fetch(sp, *data, size);
    (*data)[size] = '\0';
    if(ret != size)
	return (ret < 0)? errno : KRB5_KT_END;
    return 0;
}

static krb5_error_code
krb5_kt_ret_principal(krb5_storage *sp,
		      krb5_principal *princ)
{
    int i;
    int ret;
    krb5_principal p;
    int16_t tmp;
    
    ALLOC(p, 1);
    if(p == NULL)
	return ENOMEM;

    ret = krb5_ret_int16(sp, &tmp);
    if(ret)
	return ret;
    if (sp->flags & KRB5_STORAGE_PRINCIPAL_WRONG_NUM_COMPONENTS)
	tmp--;
    p->name.name_string.len = tmp;
    ret = krb5_kt_ret_string(sp, &p->realm);
    if(ret) return ret;
    p->name.name_string.val = calloc(p->name.name_string.len, 
				     sizeof(*p->name.name_string.val));
    if(p->name.name_string.val == NULL)
	return ENOMEM;
    for(i = 0; i < p->name.name_string.len; i++){
	ret = krb5_kt_ret_string(sp, p->name.name_string.val + i);
	if(ret) return ret;
    }
    if (krb5_storage_is_flags(sp, KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE))
	p->name.name_type = KRB5_NT_UNKNOWN;
    else {
	int32_t tmp32;
	ret = krb5_ret_int32(sp, &tmp32);
	p->name.name_type = tmp32;
	if (ret)
	    return ret;
    }
    *princ = p;
    return 0;
}

static krb5_error_code
krb5_kt_ret_keyblock(krb5_storage *sp, krb5_keyblock *p)
{
    int ret;
    int16_t tmp;

    ret = krb5_ret_int16(sp, &tmp); /* keytype + etype */
    if(ret) return ret;
    p->keytype = tmp;
    ret = krb5_kt_ret_data(sp, &p->keyvalue);
    return ret;
}

static krb5_error_code
krb5_kt_store_data(krb5_storage *sp,
		   krb5_data data)
{
    int ret;
    ret = krb5_store_int16(sp, data.length);
    if(ret < 0)
	return ret;
    ret = sp->store(sp, data.data, data.length);
    if(ret != data.length){
	if(ret < 0)
	    return errno;
	return KRB5_KT_END;
    }
    return 0;
}

static krb5_error_code
krb5_kt_store_string(krb5_storage *sp,
		     general_string data)
{
    int ret;
    size_t len = strlen(data);
    ret = krb5_store_int16(sp, len);
    if(ret < 0)
	return ret;
    ret = sp->store(sp, data, len);
    if(ret != len){
	if(ret < 0)
	    return errno;
	return KRB5_KT_END;
    }
    return 0;
}

static krb5_error_code
krb5_kt_store_keyblock(krb5_storage *sp, 
		       krb5_keyblock *p)
{
    int ret;

    ret = krb5_store_int16(sp, p->keytype); /* keytype + etype */
    if(ret) return ret;
    ret = krb5_kt_store_data(sp, p->keyvalue);
    return ret;
}


static krb5_error_code
krb5_kt_store_principal(krb5_storage *sp,
			krb5_principal p)
{
    int i;
    int ret;
    
    if(krb5_storage_is_flags(sp, KRB5_STORAGE_PRINCIPAL_WRONG_NUM_COMPONENTS))
	ret = krb5_store_int16(sp, p->name.name_string.len + 1);
    else
	ret = krb5_store_int16(sp, p->name.name_string.len);
    if(ret) return ret;
    ret = krb5_kt_store_string(sp, p->realm);
    if(ret) return ret;
    for(i = 0; i < p->name.name_string.len; i++){
	ret = krb5_kt_store_string(sp, p->name.name_string.val[i]);
	if(ret) return ret;
    }
    if(!krb5_storage_is_flags(sp, KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE)) {
	ret = krb5_store_int32(sp, p->name.name_type);
	if(ret)
	    return ret;
    }

    return 0;
}

krb5_error_code
krb5_kt_add_entry(krb5_context context,
		  krb5_keytab id,
		  krb5_keytab_entry *entry)
{
    if(id->add == NULL)
	return KRB5_KT_NOWRITE;
    return (*id->add)(context, id,entry);
}


/* file operations -------------------------------------------- */

struct fkt_data {
    char *filename;
};

static krb5_error_code
fkt_resolve(krb5_context context, const char *name, krb5_keytab id)
{
    struct fkt_data *d;
    d = malloc(sizeof(*d));
    if(d == NULL)
	return ENOMEM;
    d->filename = strdup(name);
    if(d->filename == NULL) {
	free(d);
	return ENOMEM;
    }
    id->data = d;
    return 0;
}

static krb5_error_code
fkt_close(krb5_context context, krb5_keytab id)
{
    struct fkt_data *d = id->data;
    free(d->filename);
    free(d);
    return 0;
}

static krb5_error_code 
fkt_get_name(krb5_context context, 
	     krb5_keytab id, 
	     char *name, 
	     size_t namesize)
{
    /* This function is XXX */
    struct fkt_data *d = id->data;
    strcpy_truncate(name, d->filename, namesize);
    return 0;
}

static void
storage_set_flags(krb5_context context, krb5_storage *sp, int vno)
{
    int flags = 0;
    switch(vno) {
    case KRB5_KT_VNO_1:
	flags |= KRB5_STORAGE_PRINCIPAL_WRONG_NUM_COMPONENTS;
	flags |= KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE;
	flags |= KRB5_STORAGE_HOST_BYTEORDER;
	break;
    case KRB5_KT_VNO_2:
	break;
    default:
	krb5_abortx(context, 
		    "storage_set_flags called with bad vno (%x)", vno);
    }
    krb5_storage_set_flags(sp, flags);
}

static krb5_error_code
fkt_start_seq_get_int(krb5_context context, 
		      krb5_keytab id, 
		      int flags,
		      krb5_kt_cursor *c)
{
    int8_t pvno, tag;
    krb5_error_code ret;
    struct fkt_data *d = id->data;
    
    c->fd = open (d->filename, flags);
    if (c->fd < 0)
	return errno;
    c->sp = krb5_storage_from_fd(c->fd);
    ret = krb5_ret_int8(c->sp, &pvno);
    if(ret) {
	krb5_storage_free(c->sp);
	close(c->fd);
	return ret;
    }
    if(pvno != 5) {
	krb5_storage_free(c->sp);
	close(c->fd);
	return KRB5_KEYTAB_BADVNO;
    }
    ret = krb5_ret_int8(c->sp, &tag);
    if (ret) {
	krb5_storage_free(c->sp);
	close(c->fd);
	return ret;
    }
    id->version = tag;
    storage_set_flags(context, c->sp, id->version);
    return 0;
}

static krb5_error_code
fkt_start_seq_get(krb5_context context, 
		  krb5_keytab id, 
		  krb5_kt_cursor *c)
{
    return fkt_start_seq_get_int(context, id, O_RDONLY | O_BINARY, c);
}

static krb5_error_code
fkt_next_entry_int(krb5_context context, 
		   krb5_keytab id, 
		   krb5_keytab_entry *entry, 
		   krb5_kt_cursor *cursor,
		   off_t *start,
		   off_t *end)
{
    int32_t len;
    u_int32_t timestamp;
    int ret;
    int8_t tmp8;
    int32_t tmp32;
    off_t pos;

    pos = cursor->sp->seek(cursor->sp, 0, SEEK_CUR);
loop:
    ret = krb5_ret_int32(cursor->sp, &len);
    if (ret)
	return ret;
    if(len < 0) {
	pos = cursor->sp->seek(cursor->sp, -len, SEEK_CUR);
	goto loop;
    }
    ret = krb5_kt_ret_principal (cursor->sp, &entry->principal);
    if (ret)
	goto out;
    ret = krb5_ret_int32(cursor->sp, &tmp32);
    timestamp = tmp32;
    if (ret)
	goto out;
    ret = krb5_ret_int8(cursor->sp, &tmp8);
    if (ret)
	goto out;
    entry->vno = tmp8;
    ret = krb5_kt_ret_keyblock (cursor->sp, &entry->keyblock);
    if (ret)
	goto out;
    if(start) *start = pos;
    if(end) *end = *start + 4 + len;
 out:
    cursor->sp->seek(cursor->sp, pos + 4 + len, SEEK_SET);
    return ret;
}

static krb5_error_code
fkt_next_entry(krb5_context context, 
	       krb5_keytab id, 
	       krb5_keytab_entry *entry, 
	       krb5_kt_cursor *cursor)
{
    return fkt_next_entry_int(context, id, entry, cursor, NULL, NULL);
}

static krb5_error_code
fkt_end_seq_get(krb5_context context, 
		krb5_keytab id,
		krb5_kt_cursor *cursor)
{
    krb5_storage_free(cursor->sp);
    close(cursor->fd);
    return 0;
}

static krb5_error_code
fkt_add_entry(krb5_context context,
	      krb5_keytab id,
	      krb5_keytab_entry *entry)
{
    int ret;
    int fd;
    krb5_storage *sp;
    struct fkt_data *d = id->data;
    krb5_data keytab;
    int32_t len;
    
    fd = open (d->filename, O_RDWR | O_BINARY);
    if (fd < 0) {
	fd = open (d->filename, O_RDWR | O_CREAT | O_BINARY, 0600);
	if (fd < 0)
	    return errno;
	sp = krb5_storage_from_fd(fd);
	ret = krb5_store_int8(sp, 5);
	if(ret) {
	    krb5_storage_free(sp);
	    close(fd);
	    return ret;
	}
	if(id->version == 0)
	    id->version = KRB5_KT_VNO;
	ret = krb5_store_int8 (sp, id->version);
	if (ret) {
	    krb5_storage_free(sp);
	    close(fd);
	    return ret;
	}
	storage_set_flags(context, sp, id->version);
    } else {
	int8_t pvno, tag;
	sp = krb5_storage_from_fd(fd);
	ret = krb5_ret_int8(sp, &pvno);
	if(ret) {
	    krb5_storage_free(sp);
	    close(fd);
	    return ret;
	}
	if(pvno != 5) {
	    krb5_storage_free(sp);
	    close(fd);
	    return KRB5_KEYTAB_BADVNO;
	}
	ret = krb5_ret_int8 (sp, &tag);
	if (ret) {
	    krb5_storage_free(sp);
	    close(fd);
	    return ret;
	}
	id->version = tag;
	storage_set_flags(context, sp, id->version);
    }

    {
	krb5_storage *emem;
	emem = krb5_storage_emem();
	if(emem == NULL) {
	    ret = ENOMEM;
	    goto out;
	}
	ret = krb5_kt_store_principal(emem, entry->principal);
	if(ret) {
	    krb5_storage_free(emem);
	    goto out;
	}
	ret = krb5_store_int32 (emem, time(NULL));
	if(ret) {
	    krb5_storage_free(emem);
	    goto out;
	}
	ret = krb5_store_int8 (emem, entry->vno);
	if(ret) {
	    krb5_storage_free(emem);
	    goto out;
	}
	ret = krb5_kt_store_keyblock (emem, &entry->keyblock);
	if(ret) {
	    krb5_storage_free(emem);
	    goto out;
	}
	ret = krb5_storage_to_data(emem, &keytab);
	krb5_storage_free(emem);
	if(ret)
	    goto out;
    }
    
    while(1) {
	ret = krb5_ret_int32(sp, &len);
	if(ret == KRB5_CC_END) {
	    len = keytab.length;
	    break;
	}
	if(len < 0) {
	    len = -len;
	    if(len >= keytab.length) {
		sp->seek(sp, -4, SEEK_CUR);
		break;
	    }
	}
	sp->seek(sp, len, SEEK_CUR);
    }
    ret = krb5_store_int32(sp, len);
    if(sp->store(sp, keytab.data, keytab.length) < 0)
	ret = errno;
    memset(keytab.data, 0, keytab.length);
    krb5_data_free(&keytab);
 out:
    krb5_storage_free(sp);
    close(fd);
    return ret;
}

static krb5_error_code
fkt_remove_entry(krb5_context context,
		 krb5_keytab id,
		 krb5_keytab_entry *entry)
{
    krb5_keytab_entry e;
    krb5_kt_cursor cursor;
    off_t pos_start, pos_end;
    int found = 0;
    
    fkt_start_seq_get_int(context, id, O_RDWR | O_BINARY, &cursor);
    while(fkt_next_entry_int(context, id, &e, &cursor, 
			     &pos_start, &pos_end) == 0) {
	if(kt_compare(context, &e, entry->principal, 
		      entry->vno, entry->keyblock.keytype)) {
	    int32_t len;
	    unsigned char buf[128];
	    found = 1;
	    cursor.sp->seek(cursor.sp, pos_start, SEEK_SET);
	    len = pos_end - pos_start - 4;
	    krb5_store_int32(cursor.sp, -len);
	    memset(buf, 0, sizeof(buf));
	    while(len > 0) {
		cursor.sp->store(cursor.sp, buf, min(len, sizeof(buf)));
		len -= min(len, sizeof(buf));
	    }
	}
    }
    krb5_kt_end_seq_get(context, id, &cursor);
    if (!found)
	return KRB5_KT_NOTFOUND;
    return 0;
}

static krb5_kt_ops fops = {
    "FILE",
    fkt_resolve,
    fkt_get_name,
    fkt_close,
    NULL, /* get */
    fkt_start_seq_get,
    fkt_next_entry,
    fkt_end_seq_get,
    fkt_add_entry,
    fkt_remove_entry
};


/* memory operations -------------------------------------------- */

struct mkt_data {
    krb5_keytab_entry *entries;
    int num_entries;
};

static krb5_error_code
mkt_resolve(krb5_context context, const char *name, krb5_keytab id)
{
    struct mkt_data *d;
    d = malloc(sizeof(*d));
    if(d == NULL)
	return ENOMEM;
    d->entries = NULL;
    d->num_entries = 0;
    id->data = d;
    return 0;
}

static krb5_error_code
mkt_close(krb5_context context, krb5_keytab id)
{
    struct mkt_data *d = id->data;
    int i;
    for(i = 0; i < d->num_entries; i++)
	krb5_kt_free_entry(context, &d->entries[i]);
    free(d->entries);
    free(d);
    return 0;
}

static krb5_error_code 
mkt_get_name(krb5_context context, 
	     krb5_keytab id, 
	     char *name, 
	     size_t namesize)
{
    strncpy(name, "", namesize);
    return 0;
}

static krb5_error_code
mkt_start_seq_get(krb5_context context, 
		  krb5_keytab id, 
		  krb5_kt_cursor *c)
{
    /* XXX */
    c->fd = 0;
    return 0;
}

static krb5_error_code
mkt_next_entry(krb5_context context, 
	       krb5_keytab id, 
	       krb5_keytab_entry *entry, 
	       krb5_kt_cursor *c)
{
    struct mkt_data *d = id->data;
    if(c->fd >= d->num_entries)
	return KRB5_KT_END;
    return krb5_kt_copy_entry_contents(context, &d->entries[c->fd++], entry);
}

static krb5_error_code
mkt_end_seq_get(krb5_context context, 
		krb5_keytab id,
		krb5_kt_cursor *cursor)
{
    return 0;
}

static krb5_error_code
mkt_add_entry(krb5_context context,
	      krb5_keytab id,
	      krb5_keytab_entry *entry)
{
    struct mkt_data *d = id->data;
    krb5_keytab_entry *tmp;
    tmp = realloc(d->entries, (d->num_entries + 1) * sizeof(*d->entries));
    if(tmp == NULL)
	return ENOMEM;
    d->entries = tmp;
    return krb5_kt_copy_entry_contents(context, entry, 
				       &d->entries[d->num_entries++]);
}

static krb5_error_code
mkt_remove_entry(krb5_context context,
		 krb5_keytab id,
		 krb5_keytab_entry *entry)
{
    struct mkt_data *d = id->data;
    krb5_keytab_entry *e, *end;
    
    /* do this backwards to minimize copying */
    for(end = d->entries + d->num_entries, e = end - 1; e >= d->entries; e--) {
	if(kt_compare(context, e, entry->principal, 
		      entry->vno, entry->keyblock.keytype)) {
	    krb5_kt_free_entry(context, e);
	    memmove(e, e + 1, (end - e - 1) * sizeof(*e));
	    memset(end - 1, 0, sizeof(*end));
	    d->num_entries--;
	    end--;
	}
    }
    e = realloc(d->entries, d->num_entries * sizeof(*d->entries));
    if(e != NULL)
	d->entries = e;
    return 0;
}

static krb5_kt_ops mops = {
    "MEMORY",
    mkt_resolve,
    mkt_get_name,
    mkt_close,
    NULL, /* get */
    mkt_start_seq_get,
    mkt_next_entry,
    mkt_end_seq_get,
    mkt_add_entry,
    mkt_remove_entry
};
