#include "krb5_locl.h"

RCSID("$Id$");

krb5_error_code
krb5_storage_free(krb5_storage *sp)
{
    if(sp->free)
	(*sp->free)(sp);
    free(sp->data);
    free(sp);
    return 0;
}

krb5_error_code
krb5_storage_to_data(krb5_storage *sp, krb5_data *data)
{
    off_t pos;
    size_t size;
    pos = sp->seek(sp, 0, SEEK_CUR);
    size = (size_t)sp->seek(sp, 0, SEEK_END);
    data->length = size;
    data->data = malloc(data->length);
    if(data->data == NULL){
	data->length = 0;
	sp->seek(sp, pos, SEEK_SET);
	return ENOMEM;
    }
    sp->seek(sp, 0, SEEK_SET);
    sp->fetch(sp, data->data, data->length);
    sp->seek(sp, pos, SEEK_SET);
    return 0;
}

krb5_error_code
krb5_store_int32(krb5_storage *sp,
		 int32_t value)
{
    int ret;

    value = htonl(value);
    ret = sp->store(sp, &value, sizeof(value));
    if (ret != sizeof(value))
	return (ret<0)?errno:KRB5_CC_END;
    return 0;
}

krb5_error_code
krb5_ret_int32(krb5_storage *sp,
	       int32_t *value)
{
    int32_t v;
    int ret;
    ret = sp->fetch(sp, &v, sizeof(v));
    if(ret != sizeof(v))
	return (ret<0)?errno:KRB5_CC_END;

    *value = ntohl(v);
    return 0;
}

krb5_error_code
krb5_store_int16(krb5_storage *sp,
		 int16_t value)
{
    int ret;

    value = htons(value);
    ret = sp->store(sp, &value, sizeof(value));
    if (ret != sizeof(value))
	return (ret<0)?errno:KRB5_CC_END;
    return 0;
}

krb5_error_code
krb5_ret_int16(krb5_storage *sp,
	       int16_t *value)
{
    int16_t v;
    int ret;
    ret = sp->fetch(sp, &v, sizeof(v));
    if(ret != sizeof(v))
	return (ret<0)?errno:KRB5_CC_END; /* XXX */
  
    *value = ntohs(v);
    return 0;
}

krb5_error_code
krb5_store_int8(krb5_storage *sp,
		int8_t value)
{
    int ret;

    ret = sp->store(sp, &value, sizeof(value));
    if (ret != sizeof(value))
	return (ret<0)?errno:KRB5_CC_END;
    return 0;
}

krb5_error_code
krb5_ret_int8(krb5_storage *sp,
	      int8_t *value)
{
    int ret;

    ret = sp->fetch(sp, value, sizeof(*value));
    if (ret != sizeof(*value))
	return (ret<0)?errno:KRB5_CC_END;
    return 0;
}

krb5_error_code
krb5_store_data(krb5_storage *sp,
		krb5_data data)
{
    int ret;
    ret = krb5_store_int32(sp, data.length);
    if(ret < 0)
	return ret;
    ret = sp->store(sp, data.data, data.length);
    if(ret != data.length){
	if(ret < 0)
	    return errno;
	return KRB5_CC_END;
    }
    return 0;
}

krb5_error_code
krb5_ret_data(krb5_storage *sp,
	      krb5_data *data)
{
    int ret;
    int size;
    ret = krb5_ret_int32(sp, &size);
    if(ret)
	return ret;
    data->length = size;
    if (size) {
	data->data = malloc(size);
	ret = sp->fetch(sp, data->data, size);
	if(ret != size)
	    return (ret < 0)? errno : KRB5_CC_END;
    } else
	data->data = NULL;
    return 0;
}

krb5_error_code
krb5_store_string(krb5_storage *sp,
		  char *s)
{
    krb5_data data;
    data.length = strlen(s);
    data.data = s;
    return krb5_store_data(sp, data);
}

krb5_error_code
krb5_ret_string(krb5_storage *sp,
		char **string)
{
    int ret;
    krb5_data data;
    ret = krb5_ret_data(sp, &data);
    if(ret)
	return ret;
    *string = realloc(data.data, data.length + 1);
    if(*string == NULL){
	free(data.data);
	return ENOMEM;
    }
    (*string)[data.length] = 0;
    return 0;
}

krb5_error_code
krb5_store_stringz(krb5_storage *sp,
		  char *s)
{
    size_t len = strlen(s) + 1;
    size_t ret;
    ret = sp->store(sp, s, len);
    if(ret != len)
	if((int)ret < 0)
	    return ret;
	else
	    return KRB5_CC_END;
    return 0;
}

krb5_error_code
krb5_ret_stringz(krb5_storage *sp,
		char **string)
{
    char c;
    char *s = NULL;
    size_t len = 0;
    size_t ret;
    while((ret = sp->fetch(sp, &c, 1)) == 1){
	len++;
	s = realloc(s, len);
	s[len - 1] = c;
	if(c == 0)
	    break;
    }
    if(ret != 1){
	free(s);
	if(ret == 0)
	    return KRB5_CC_END;
	return ret;
    }
    *string = s;
    return 0;
}


krb5_error_code
krb5_store_principal(krb5_storage *sp,
		     krb5_principal p)
{
    int i;
    int ret;
    ret = krb5_store_int32(sp, p->name.name_type);
    if(ret) return ret;
    ret = krb5_store_int32(sp, p->name.name_string.len);
    if(ret) return ret;
    ret = krb5_store_string(sp, p->realm);
    if(ret) return ret;
    for(i = 0; i < p->name.name_string.len; i++){
	ret = krb5_store_string(sp, p->name.name_string.val[i]);
	if(ret) return ret;
    }
    return 0;
}

krb5_error_code
krb5_ret_principal(krb5_storage *sp,
		   krb5_principal *princ)
{
    int i;
    int ret;
    krb5_principal p;
    int32_t type;
    int32_t ncomp;
    
    p = calloc(1, sizeof(*p));
    if(p == NULL)
	return ENOMEM;

    if((ret = krb5_ret_int32(sp, &type))){
	free(p);
	return ret;
    }
    if((ret = krb5_ret_int32(sp, &ncomp))){
	free(p);
	return ret;
    }
    p->name.name_type = type;
    p->name.name_string.len = ncomp;
    ret = krb5_ret_string(sp, &p->realm);
    if(ret) return ret;
    p->name.name_string.val = calloc(ncomp, sizeof(*p->name.name_string.val));
    if(p->name.name_string.val == NULL){
	free(p->realm);
	return ENOMEM;
    }
    for(i = 0; i < ncomp; i++){
	ret = krb5_ret_string(sp, &p->name.name_string.val[i]);
	if(ret) return ret; /* XXX */
    }
    *princ = p;
    return 0;
}

krb5_error_code
krb5_store_keyblock(krb5_storage *sp, krb5_keyblock p)
{
    int ret;
    ret =krb5_store_int32(sp, p.keytype);
    if(ret) return ret;
    ret = krb5_store_data(sp, p.keyvalue);
    return ret;
}

krb5_error_code
krb5_ret_keyblock(krb5_storage *sp, krb5_keyblock *p)
{
    int ret;
    ret = krb5_ret_int32(sp, (int32_t*)&p->keytype); /* keytype + etype */
    if(ret) return ret;
    ret = krb5_ret_data(sp, &p->keyvalue);
    return ret;
}

krb5_error_code
krb5_store_times(krb5_storage *sp, krb5_times times)
{
    int ret;
    ret = krb5_store_int32(sp, times.authtime);
    if(ret) return ret;
    ret = krb5_store_int32(sp, times.starttime);
    if(ret) return ret;
    ret = krb5_store_int32(sp, times.endtime);
    if(ret) return ret;
    ret = krb5_store_int32(sp, times.renew_till);
    return ret;
}

krb5_error_code
krb5_ret_times(krb5_storage *sp, krb5_times *times)
{
    int ret;
    int32_t tmp;
    ret = krb5_ret_int32(sp, &tmp);
    times->authtime = tmp;
    if(ret) return ret;
    ret = krb5_ret_int32(sp, &tmp);
    times->starttime = tmp;
    if(ret) return ret;
    ret = krb5_ret_int32(sp, &tmp);
    times->endtime = tmp;
    if(ret) return ret;
    ret = krb5_ret_int32(sp, &tmp);
    times->renew_till = tmp;
    return ret;
}

krb5_error_code
krb5_store_address(krb5_storage *sp, krb5_address p)
{
    int ret;
    ret = krb5_store_int16(sp, p.addr_type);
    if(ret) return ret;
    ret = krb5_store_data(sp, p.address);
    return ret;
}

krb5_error_code
krb5_ret_address(krb5_storage *sp, krb5_address *adr)
{
    int16_t t;
    int ret;
    ret = krb5_ret_int16(sp, &t);
    if(ret) return ret;
    adr->addr_type = t;
    ret = krb5_ret_data(sp, &adr->address);
    return ret;
}

krb5_error_code
krb5_store_addrs(krb5_storage *sp, krb5_addresses p)
{
    int i;
    int ret;
    ret = krb5_store_int32(sp, p.len);
    if(ret) return ret;
    for(i = 0; i<p.len; i++){
	ret = krb5_store_address(sp, p.val[i]);
	if(ret) break;
    }
    return ret;
}

krb5_error_code
krb5_ret_addrs(krb5_storage *sp, krb5_addresses *adr)
{
    int i;
    int ret;
    int32_t tmp;

    ret = krb5_ret_int32(sp, &tmp);
    if(ret) return ret;
    adr->len = tmp;
    adr->val = ALLOC(adr->len, krb5_address);
    for(i = 0; i < adr->len; i++){
	ret = krb5_ret_address(sp, &adr->val[i]);
	if(ret) break;
    }
    return ret;
}

krb5_error_code
krb5_store_authdata(krb5_storage *sp, krb5_data p)
{
    return krb5_store_data(sp, p);
}

krb5_error_code
krb5_ret_authdata(krb5_storage *sp, krb5_data *auth)
{
    return krb5_ret_data(sp, auth);
}
