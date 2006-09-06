/*
 * Copyright (c) 2003 - 2006 Kungliga Tekniska H�gskolan
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

#define ALLOC(X, N) (X) = calloc((N), sizeof(*(X)))
#define ALLOC_SEQ(X, N) do { (X)->len = (N); ALLOC((X)->val, (N)); } while(0)

int
hx509_cms_wrap_ContentInfo(const heim_oid *oid,
			   const heim_octet_string *buf, 
			   heim_octet_string *res)
{
    ContentInfo ci;
    size_t size;
    int ret;

    memset(res, 0, sizeof(*res));
    memset(&ci, 0, sizeof(ci));

    ret = copy_oid(oid, &ci.contentType);
    if (ret)
	return ret;
    ALLOC(ci.content, 1);
    if (ci.content == NULL) {
	free_ContentInfo(&ci);
	return ENOMEM;
    }
    ci.content->data = malloc(buf->length);
    if (ci.content->data == NULL) {
	free_ContentInfo(&ci);
	return ENOMEM;
    }
    memcpy(ci.content->data, buf->data, buf->length);
    ci.content->length = buf->length;

    ASN1_MALLOC_ENCODE(ContentInfo, res->data, res->length, &ci, &size, ret);
    free_ContentInfo(&ci);
    if (ret)
	return ret;
    if (res->length != size)
	_hx509_abort("internal ASN.1 encoder error");

    return 0;
}

int
hx509_cms_unwrap_ContentInfo(const heim_octet_string *in,
			     heim_oid *oid,
			     heim_octet_string *out,
			     int *have_data)
{
    ContentInfo ci;
    size_t size;
    int ret;

    memset(oid, 0, sizeof(*oid));
    memset(out, 0, sizeof(*out));

    ret = decode_ContentInfo(in->data, in->length, &ci, &size);
    if (ret)
	return ret;

    ret = copy_oid(&ci.contentType, oid);
    if (ret) {
	free_ContentInfo(&ci);
	return ret;
    }
    if (ci.content) {
	ret = copy_octet_string(ci.content, out);
	if (ret) {
	    free_oid(oid);
	    free_ContentInfo(&ci);
	    return ret;
	}
    } else
	memset(out, 0, sizeof(*out));

    if (have_data)
	*have_data = (ci.content != NULL) ? 1 : 0;

    free_ContentInfo(&ci);

    return 0;
}

static int
fill_CMSIdentifier(const hx509_cert cert, CMSIdentifier *id)
{
    hx509_name name;
    int ret;

    id->element = choice_CMSIdentifier_issuerAndSerialNumber;
    ret = hx509_cert_get_issuer(cert, &name);
    if (ret)
	return ret;
    ret = copy_Name(&name->der_name,
		    &id->u.issuerAndSerialNumber.issuer);
    hx509_name_free(&name);
    if (ret)
	return ret;

    ret = hx509_cert_get_serialnumber(cert,
				      &id->u.issuerAndSerialNumber.serialNumber);
    return ret;
}

static int
find_CMSIdentifier(hx509_context context,
		   CMSIdentifier *client,
		   hx509_certs certs,
		   hx509_cert *signer_cert,
		   int match)
{
    hx509_query q;
    hx509_cert cert;
    Certificate c;
    int ret;

    memset(&c, 0, sizeof(c));
    _hx509_query_clear(&q);

    *signer_cert = NULL;

    switch (client->element) {
    case choice_CMSIdentifier_issuerAndSerialNumber:
	q.serial = &client->u.issuerAndSerialNumber.serialNumber;
	q.issuer_name = &client->u.issuerAndSerialNumber.issuer;
	q.match = HX509_QUERY_MATCH_SERIALNUMBER|HX509_QUERY_MATCH_ISSUER_NAME;
	break;
    case choice_CMSIdentifier_subjectKeyIdentifier:
	q.subject_id = &client->u.subjectKeyIdentifier;
	q.match = HX509_QUERY_MATCH_SUBJECT_KEY_ID;
	break;
    default:
	hx509_set_error_string(context, 0, HX509_CMS_NO_RECIPIENT_CERTIFICATE,
			       "unknown CMS identifier element");
	return HX509_CMS_NO_RECIPIENT_CERTIFICATE;
    }

    q.match |= match;

    ret = hx509_certs_find(context, certs, &q, &cert);
    if (ret == HX509_CERT_NOT_FOUND) {
	switch (client->element) {
	case choice_CMSIdentifier_issuerAndSerialNumber: {
	    IssuerAndSerialNumber *iasn;
	    char *serial, *name;

	    iasn = &client->u.issuerAndSerialNumber;

	    ret = _hx509_Name_to_string(&iasn->issuer, &name);
	    if(ret)
		return HX509_CMS_NO_RECIPIENT_CERTIFICATE;
	    ret = der_print_hex_heim_integer(&iasn->serialNumber, &serial);
	    if (ret) {
		free(name);
		return HX509_CMS_NO_RECIPIENT_CERTIFICATE;
	    }
	    hx509_set_error_string(context, 0,
				   HX509_CMS_NO_RECIPIENT_CERTIFICATE,
				   "Failed to find cert issued by %s "
				   "with serial number %s",
				   name, serial);
	    free(name);
	    free(serial);
	    return HX509_CMS_NO_RECIPIENT_CERTIFICATE;
	}
	case choice_CMSIdentifier_subjectKeyIdentifier: {
	    KeyIdentifier *ki  = &client->u.subjectKeyIdentifier;
	    char *str;
	    ssize_t len;

	    len = hex_encode(ki->data, ki->length, &str);
	    if (len < 0)
		hx509_clear_error_string(context);
	    else
		hx509_set_error_string(context, 0,
				       HX509_CMS_NO_RECIPIENT_CERTIFICATE,
				       "Failed to find cert with id: %s",
				       str);
	    free(str);
	    
	    return HX509_CMS_NO_RECIPIENT_CERTIFICATE;
	}
	default:
	    _hx509_abort("unknown CMS id type");
	    break;
	}
    } else if (ret) {
	hx509_set_error_string(context, HX509_ERROR_APPEND,
			       HX509_CMS_NO_RECIPIENT_CERTIFICATE,
			       "Failed to find CMS id in cert store");
	return HX509_CMS_NO_RECIPIENT_CERTIFICATE;
    }
    
    *signer_cert = cert;

    return 0;
}

int
hx509_cms_unenvelope(hx509_context context,
		     hx509_certs certs,
		     const void *data,
		     size_t length,
		     heim_oid *contentType,
		     heim_octet_string *content)
{
    heim_octet_string key;
    EnvelopedData ed;
    KeyTransRecipientInfo *ri;
    hx509_cert cert;
    AlgorithmIdentifier *ai;
    heim_octet_string *enccontent;
    heim_octet_string *params, params_data;
    heim_octet_string ivec;
    size_t size;
    int ret, i;


    memset(&key, 0, sizeof(key));
    memset(&ed, 0, sizeof(ed));
    memset(&ivec, 0, sizeof(ivec));
    memset(content, 0, sizeof(*content));
    memset(contentType, 0, sizeof(*contentType));

    ret = decode_EnvelopedData(data, length, &ed, &size);
    if (ret) {
	hx509_clear_error_string(context);
	return ret;
    }

    /* XXX check content of ed */

    if (ed.encryptedContentInfo.encryptedContent == NULL) {
	ret = HX509_CMS_NO_DATA_AVAILABLE;
	hx509_clear_error_string(context);
	goto out;
    }

    if (ed.recipientInfos.len == 0) {
	ret = HX509_CMS_NO_RECIPIENT_CERTIFICATE;
	hx509_set_error_string(context, 0, ret,
			       "No recipient info in enveloped data");
	goto out;
    }

    cert = NULL;
    for (i = 0; i < ed.recipientInfos.len; i++) {
	ri = &ed.recipientInfos.val[i];

	/* ret = search_keyset(ri,
	 * 	PRIVATE_KEY,
	 * 	ki->keyEncryptionAlgorithm.algorithm);
	 */

	ret = find_CMSIdentifier(context, &ri->rid, certs, &cert, 
				 HX509_QUERY_PRIVATE_KEY|
				 HX509_QUERY_KU_ENCIPHERMENT);
	if (ret)
	    continue;

	ret = _hx509_cert_private_decrypt(&ri->encryptedKey, 
					  &ri->keyEncryptionAlgorithm.algorithm,
					  cert, &key);

	hx509_cert_free(cert);
	if (ret == 0)
	    break;
	cert = NULL;
    }
    
    if (cert == NULL) {
	ret = HX509_CMS_NO_RECIPIENT_CERTIFICATE;
	hx509_set_error_string(context, 0, ret,
			       "No private key decrypted the transfer key");
	goto out;
    }

    ret = copy_oid(&ed.encryptedContentInfo.contentType, contentType);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    enccontent = ed.encryptedContentInfo.encryptedContent;

    ai = &ed.encryptedContentInfo.contentEncryptionAlgorithm;
    if (ai->parameters) {
	params_data.data = ai->parameters->data;
	params_data.length = ai->parameters->length;
	params = &params_data;
    } else
	params = NULL;

    {
	hx509_crypto crypto;

	ret = hx509_crypto_init(context, NULL, &ai->algorithm, &crypto);
	if (ret)
	    goto out;
	
	if (params) {
	    ret = hx509_crypto_set_params(context, crypto, params, &ivec);
	    if (ret)
		goto out;
	}

	ret = hx509_crypto_set_key_data(crypto, key.data, key.length);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
	
	ret = hx509_crypto_decrypt(crypto, 
				   enccontent->data,
				   enccontent->length,
				   ivec.length ? &ivec : NULL,
				   content);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
    }

out:

    free_octet_string(&key);
    if (ivec.length)
	free_octet_string(&ivec);
    if (ret) {
	free_oid(contentType);
	free_octet_string(content);
    }

    return ret;
}

int
hx509_cms_envelope_1(hx509_context context,
		     hx509_cert cert,
		     const void *data,
		     size_t length,
		     const heim_oid *encryption_type,
		     const heim_oid *contentType,
		     heim_octet_string *content)
{
    KeyTransRecipientInfo *ri;
    heim_octet_string ivec;
    heim_octet_string key;
    hx509_crypto crypto;
    EnvelopedData ed;
    size_t size;
    int ret;

    memset(&ivec, 0, sizeof(ivec));
    memset(&key, 0, sizeof(key));
    memset(&ed, 0, sizeof(ed));
    memset(content, 0, sizeof(*content));

    if (encryption_type == NULL)
	encryption_type = oid_id_aes_256_cbc();

    ret = _hx509_check_key_usage(context, cert, 1 << 2, TRUE);
    if (ret)
	goto out;

    ret = hx509_crypto_init(context, NULL, encryption_type, &crypto);
    if (ret)
	goto out;

    ret = hx509_crypto_set_random_key(crypto, &key);
    if (ret) {
	hx509_clear_error_string(context);
	hx509_crypto_destroy(crypto);
	goto out;
    }

    ret = hx509_crypto_encrypt(crypto, 
			       data,
			       length,
			       &ivec,
			       &ed.encryptedContentInfo.encryptedContent);
    if (ret) {
	hx509_clear_error_string(context);
	hx509_crypto_destroy(crypto);
	goto out;
    }

    {
	AlgorithmIdentifier *enc_alg;
	enc_alg = &ed.encryptedContentInfo.contentEncryptionAlgorithm;
	ret = copy_oid(encryption_type, &enc_alg->algorithm);
	if (ret) {
	    hx509_crypto_destroy(crypto);
	    hx509_clear_error_string(context);
	    goto out;
	}	
	ALLOC(enc_alg->parameters, 1);
	if (enc_alg->parameters == NULL) {
	    hx509_clear_error_string(context);
	    hx509_crypto_destroy(crypto);
	    ret = ENOMEM;
	    goto out;
	}

	ret = hx509_crypto_get_params(context,
				      crypto,
				      &ivec,
				      enc_alg->parameters);
	hx509_crypto_destroy(crypto);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
    }

    ALLOC_SEQ(&ed.recipientInfos, 1);
    if (ed.recipientInfos.val == NULL) {
	hx509_clear_error_string(context);
	ret = ENOMEM;
	goto out;
    }

    ri = &ed.recipientInfos.val[0];

    ri->version = 0;
    ret = fill_CMSIdentifier(cert, &ri->rid);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    ret = _hx509_cert_public_encrypt(&key, cert,
				     &ri->keyEncryptionAlgorithm.algorithm,
				     &ri->encryptedKey);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    /*
     *
     */

    ed.version = 0;
    ed.originatorInfo = NULL;

    ret = copy_oid(contentType, &ed.encryptedContentInfo.contentType);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    ed.unprotectedAttrs = NULL;

    ASN1_MALLOC_ENCODE(EnvelopedData, content->data, content->length,
		       &ed, &size, ret);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }
    if (size != content->length)
	_hx509_abort("internal ASN.1 encoder error");

out:
    if (ret) {
	free_octet_string(content);
    }
    free_octet_string(&key);
    free_octet_string(&ivec);
    free_EnvelopedData(&ed);

    return ret;
}

static int
any_to_certs(hx509_context context, const SignedData *sd, hx509_certs certs)
{
    int ret, i;
    
    if (sd->certificates == NULL)
	return 0;

    for (i = 0; i < sd->certificates->len; i++) {
	Certificate cert;
	hx509_cert c;

	const void *p = sd->certificates->val[i].data;
	size_t size, length = sd->certificates->val[i].length;

	ret = decode_Certificate(p, length, &cert, &size);
	if (ret) {
	    hx509_clear_error_string(context);
	    return ret;
	}

	ret = hx509_cert_init(context, &cert, &c);
	free_Certificate(&cert);
	if (ret)
	    return ret;
	ret = hx509_certs_add(context, certs, c);
	if (ret) {
	    hx509_cert_free(c);
	    return ret;
	}
    }

    return 0;
}

static const Attribute *
find_attribute(const CMSAttributes *attr, const heim_oid *oid)
{
    int i;
    for (i = 0; i < attr->len; i++)
	if (heim_oid_cmp(&attr->val[i].type, oid) == 0)
	    return &attr->val[i];
    return NULL;
}

int
hx509_cms_verify_signed(hx509_context context,
			hx509_verify_ctx ctx,
			const void *data,
			size_t length,
			hx509_certs store,
			heim_oid *contentType,
			heim_octet_string *content,
			hx509_certs *signer_certs)
{
    SignerInfo *signer_info;
    hx509_cert cert = NULL;
    hx509_certs certs = NULL;
    SignedData sd;
    size_t size;
    int ret, i, found_valid_sig;
    
    *signer_certs = NULL;
    content->data = NULL;
    content->length = 0;
    contentType->length = 0;
    contentType->components = NULL;

    memset(&sd, 0, sizeof(sd));

    ret = decode_SignedData(data, length, &sd, &size);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    if (sd.encapContentInfo.eContent == NULL) {
	hx509_clear_error_string(context);
	ret = HX509_CMS_NO_DATA_AVAILABLE;
	goto out;
    }

    ret = hx509_certs_init(context, "MEMORY:cms-cert-buffer",
			   0, NULL, &certs);
    if (ret)
	goto out;

    ret = hx509_certs_init(context, "MEMORY:cms-signer-certs", 
			   0, NULL, signer_certs);
    if (ret)
	goto out;

    /* XXX Check CMS version */

    ret = any_to_certs(context, &sd, certs);
    if (ret) {
	goto out;
    }

    if (store) {
	ret = hx509_certs_merge(context, certs, store);
	if (ret)
	    goto out;
    }

    hx509_clear_error_string(context);

    ret = HX509_CMS_SIGNER_NOT_FOUND;
    for (found_valid_sig = 0, i = 0; i < sd.signerInfos.len; i++) {
	heim_octet_string *signed_data;
	const heim_oid *match_oid;
	heim_oid decode_oid;

	signer_info = &sd.signerInfos.val[i];
	match_oid = NULL;

	if (signer_info->signature.length == 0) {
	    ret = HX509_CMS_MISSING_SIGNER_DATA;
	    hx509_clear_error_string(context);
	    continue;
	}

	ret = find_CMSIdentifier(context, &signer_info->sid, certs, &cert,
				 HX509_QUERY_KU_DIGITALSIGNATURE);
	if (ret)
	    continue;

	if (signer_info->signedAttrs) {
	    const Attribute *attr;
	    
	    CMSAttributes sa;
	    heim_octet_string os;

	    sa.val = signer_info->signedAttrs->val;
	    sa.len = signer_info->signedAttrs->len;

	    /* verify that sigature exists */
	    attr = find_attribute(&sa, oid_id_pkcs9_messageDigest());
	    if (attr == NULL) {
		ret = HX509_CRYPTO_SIGNATURE_MISSING;
		hx509_clear_error_string(context);
		continue;
	    }
	    if (attr->value.len != 1) {
		ret = HX509_CRYPTO_SIGNATURE_MISSING;
		hx509_clear_error_string(context);
		continue;
	    }
	    
	    ret = decode_MessageDigest(attr->value.val[0].data, 
				       attr->value.val[0].length,
				       &os,
				       &size);
	    if (ret) {
		hx509_clear_error_string(context);
		continue;
	    }

	    ret = _hx509_verify_signature(NULL,
					  &signer_info->digestAlgorithm,
					  sd.encapContentInfo.eContent,
					  &os);
	    free_octet_string(&os);
	    if (ret) {
		hx509_clear_error_string(context);
		continue;
	    }

	    /* 
	     * Fetch content oid inside signedAttrs or set it to
	     * id-pkcs7-data.
	     */
	    attr = find_attribute(&sa, oid_id_pkcs9_contentType());
	    if (attr == NULL) {
		match_oid = oid_id_pkcs7_data();
	    } else {
		if (attr->value.len != 1) {
		    ret = HX509_CMS_DATA_OID_MISMATCH;
		    hx509_clear_error_string(context);
		    continue;
		}
		ret = decode_ContentType(attr->value.val[0].data, 
					 attr->value.val[0].length,
					 &decode_oid, 
					 &size);
		if (ret) {
		    hx509_clear_error_string(context);
		    continue;
		}
		match_oid = &decode_oid;
	    }

	    ALLOC(signed_data, 1);
	    if (signed_data == NULL) {
		if (match_oid == &decode_oid)
		    free_oid(&decode_oid);
		ret = ENOMEM;
		hx509_clear_error_string(context);
		continue;
	    }
	    
	    ASN1_MALLOC_ENCODE(CMSAttributes,
			       signed_data->data,
			       signed_data->length,
			       &sa, 
			       &size, ret);
	    if (ret) {
		if (match_oid == &decode_oid)
		    free_oid(&decode_oid);
		free(signed_data);
		hx509_clear_error_string(context);
		continue;
	    }
	    if (size != signed_data->length)
		_hx509_abort("internal ASN.1 encoder error");

	} else {
	    signed_data = sd.encapContentInfo.eContent;
	    match_oid = oid_id_pkcs7_data();
	}
	if (ret)
	    return ret;

	if (heim_oid_cmp(match_oid, &sd.encapContentInfo.eContentType)) {
	    ret = HX509_CMS_DATA_OID_MISMATCH;
		    hx509_clear_error_string(context);
	}	
	if (match_oid == &decode_oid)
	    free_oid(&decode_oid);
	
	if (ret == 0)
	    ret = hx509_verify_signature(context,
					 cert,
					 &signer_info->signatureAlgorithm,
					 signed_data,
					 &signer_info->signature);

	if (signed_data != sd.encapContentInfo.eContent) {
	    free_octet_string(signed_data);
	    free(signed_data);
	}
	if (ret) {
	    hx509_cert_free(cert);
	    continue;
	}

	ret = hx509_verify_path(context, ctx, cert, certs);
	if (ret) {
	    hx509_cert_free(cert);
	    continue;
	}

	ret = hx509_certs_add(context, *signer_certs, hx509_cert_ref(cert));
	if (ret) {
	    hx509_cert_free(cert);
	    continue;
	}
	found_valid_sig++;
    }
    if (found_valid_sig == 0) {
	return ret;
    }

    ret = copy_oid(&sd.encapContentInfo.eContentType, contentType);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    content->data = malloc(sd.encapContentInfo.eContent->length);
    if (content->data == NULL) {
	hx509_clear_error_string(context);
	ret = ENOMEM;
	goto out;
    }
    content->length = sd.encapContentInfo.eContent->length;
    memcpy(content->data,sd.encapContentInfo.eContent->data,content->length);

out:
    free_SignedData(&sd);
    if (certs)
	hx509_certs_free(&certs);
    if (ret) {
	if (*signer_certs)
	    hx509_certs_free(signer_certs);
	free_oid(contentType);
	free_octet_string(content);
    }

    return ret;
}

int
_hx509_set_digest_alg(DigestAlgorithmIdentifier *id,
		      const heim_oid *oid,
		      void *param, size_t length)
{
    int ret;
    if (param) {
	id->parameters = malloc(sizeof(*id->parameters));
	if (id->parameters == NULL)
	    return ENOMEM;
	id->parameters->data = malloc(length);
	if (id->parameters->data == NULL) {
	    free(id->parameters);
	    id->parameters = NULL;
	    return ENOMEM;
	}
	memcpy(id->parameters->data, param, length);
	id->parameters->length = length;
    } else
	id->parameters = NULL;
    ret = copy_oid(oid, &id->algorithm);
    if (ret) {
	if (id->parameters) {
	    free(id->parameters->data);
	    free(id->parameters);
	    id->parameters = NULL;
	}
	return ret;
    }
    return 0;
}

static int
add_one_attribute(Attribute **attr,
		  unsigned int *len,
		  const heim_oid *oid,
		  heim_octet_string *data)
{
    void *d;
    int ret;

    d = realloc(*attr, sizeof((*attr)[0]) * (*len + 1));
    if (d == NULL)
	return ENOMEM;
    (*attr) = d;

    ret = copy_oid(oid, &(*attr)[*len].type);
    if (ret)
	return ret;

    ALLOC_SEQ(&(*attr)[*len].value, 1);
    if ((*attr)[*len].value.val == NULL) {
	free_oid(&(*attr)[*len].type);
	return ENOMEM;
    }

    (*attr)[*len].value.val[0].data = data->data;
    (*attr)[*len].value.val[0].length = data->length;

    *len += 1;

    return 0;
}
	      
int
hx509_cms_create_signed_1(hx509_context context,
			  const heim_oid *eContentType,
			  const void *data, size_t length,
			  const AlgorithmIdentifier *digest_alg,
			  hx509_cert cert,
			  hx509_certs trust_anchors,
			  hx509_certs pool,
			  heim_octet_string *signed_data)
{
    hx509_name name;
    SignerInfo *signer_info;
    heim_octet_string buf;
    SignedData sd;
    int ret;
    size_t size;
    hx509_path path;
    
    memset(&sd, 0, sizeof(sd));
    memset(&name, 0, sizeof(name));
    memset(&path, 0, sizeof(path));

    if (_hx509_cert_private_key(cert) == NULL) {
	hx509_clear_error_string(context);
	return HX509_PRIVATE_KEY_MISSING;
    }

    /* XXX */
    if (digest_alg == NULL)
	digest_alg = hx509_signature_sha1();

    sd.version = 3;

    copy_oid(eContentType, &sd.encapContentInfo.eContentType);
    ALLOC(sd.encapContentInfo.eContent, 1);
    if (sd.encapContentInfo.eContent == NULL) {
	hx509_clear_error_string(context);
	ret = ENOMEM;
	goto out;
    }

    sd.encapContentInfo.eContent->data = malloc(length);
    if (sd.encapContentInfo.eContent->data == NULL) {
	hx509_clear_error_string(context);
	ret = ENOMEM;
	goto out;
    }
    memcpy(sd.encapContentInfo.eContent->data, data, length);
    sd.encapContentInfo.eContent->length = length;

    ALLOC_SEQ(&sd.signerInfos, 1);
    if (sd.signerInfos.val == NULL) {
	hx509_clear_error_string(context);
	ret = ENOMEM;
	goto out;
    }

    signer_info = &sd.signerInfos.val[0];

    signer_info->version = 1;

    ret = fill_CMSIdentifier(cert, &signer_info->sid);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }			    

    signer_info->signedAttrs = NULL;
    signer_info->unsignedAttrs = NULL;

    ALLOC(signer_info->signedAttrs, 1);
    if (signer_info->signedAttrs == NULL) {
	ret = ENOMEM;
	goto out;
    }

    {
	heim_octet_string digest;

	ret = copy_AlgorithmIdentifier(digest_alg,
				       &signer_info->digestAlgorithm);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}

	ret = _hx509_create_signature(context,
				      NULL,
				      digest_alg,
				      sd.encapContentInfo.eContent,
				      NULL,
				      &digest);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}

	ASN1_MALLOC_ENCODE(MessageDigest,
			   buf.data,
			   buf.length,
			   &digest,
			   &size,
			   ret);
	free_octet_string(&digest);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
	if (size != buf.length)
	    _hx509_abort("internal ASN.1 encoder error");

	ret = add_one_attribute(&signer_info->signedAttrs->val,
				&signer_info->signedAttrs->len,
				oid_id_pkcs9_messageDigest(),
				&buf);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}

    }

    if (heim_oid_cmp(eContentType, oid_id_pkcs7_data()) != 0) {

	ASN1_MALLOC_ENCODE(ContentType,
			   buf.data,
			   buf.length,
			   eContentType,
			   &size,
			   ret);
	if (ret)
	    goto out;
	if (size != buf.length)
	    _hx509_abort("internal ASN.1 encoder error");

	ret = add_one_attribute(&signer_info->signedAttrs->val,
				&signer_info->signedAttrs->len,
				oid_id_pkcs9_contentType(),
				&buf);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
    }


    {
	CMSAttributes sa;
	heim_octet_string os;
	
	sa.val = signer_info->signedAttrs->val;
	sa.len = signer_info->signedAttrs->len;
	
	ASN1_MALLOC_ENCODE(CMSAttributes,
			   os.data,
			   os.length,
			   &sa,
			   &size,
			   ret);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
	if (size != os.length)
	    _hx509_abort("internal ASN.1 encoder error");
			   
	ret = _hx509_create_signature(context,
				      _hx509_cert_private_key(cert),
				      hx509_signature_rsa_with_sha1(),
				      &os,
				      &signer_info->signatureAlgorithm,
				      &signer_info->signature);
				      
	free_octet_string(&os);
	if (ret) {
	    hx509_clear_error_string(context);
	    goto out;
	}
    }

    ALLOC_SEQ(&sd.digestAlgorithms, 1);
    if (sd.digestAlgorithms.val == NULL) {
	ret = ENOMEM;
	hx509_clear_error_string(context);
	goto out;
    }

    ret = copy_AlgorithmIdentifier(digest_alg,
				   &sd.digestAlgorithms.val[0]);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    if (trust_anchors) {
	ret = _hx509_calculate_path(context,
				    trust_anchors,
				    0,
				    cert,
				    pool,
				    &path);
	if (ret) {
	    _hx509_path_free(&path);
	    ret = _hx509_path_append(context, &path, cert);
	}
    } else
	ret = _hx509_path_append(context, &path, cert);
    if (ret)
	goto out;


    if (path.len) {
	int i;

	ALLOC(sd.certificates, 1);
	if (sd.certificates == NULL) {
	    hx509_clear_error_string(context);
	    ret = ENOMEM;
	    goto out;
	}
	ALLOC_SEQ(sd.certificates, path.len);
	if (sd.certificates->val == NULL) {
	    hx509_clear_error_string(context);
	    ret = ENOMEM;
	    goto out;
	}

	for (i = 0; i < path.len; i++) {
	    ASN1_MALLOC_ENCODE(Certificate, 
			       sd.certificates->val[i].data,
			       sd.certificates->val[i].length,
			       _hx509_get_cert(path.val[i]),
			       &size, ret);
	    if (ret) {
		hx509_clear_error_string(context);
		goto out;
	    }
	}
    }

    ASN1_MALLOC_ENCODE(SignedData,
		       signed_data->data, signed_data->length,
		       &sd, &size, ret);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }
    if (signed_data->length != size)
	_hx509_abort("internal ASN.1 encoder error");

out:
    _hx509_path_free(&path);
    free_SignedData(&sd);

    return ret;
}

int
hx509_cms_decrypt_encrypted(hx509_context context,
			    hx509_lock lock,
			    const void *data,
			    size_t length,
			    heim_oid *contentType,
			    heim_octet_string *content)
{
    heim_octet_string cont;
    CMSEncryptedData ed;
    AlgorithmIdentifier *ai;
    int ret;

    memset(content, 0, sizeof(*content));
    memset(&cont, 0, sizeof(cont));

    ret = decode_CMSEncryptedData(data, length, &ed, NULL);
    if (ret) {
	hx509_clear_error_string(context);
	return ret;
    }

    if (ed.encryptedContentInfo.encryptedContent == NULL) {
	ret = HX509_CMS_NO_DATA_AVAILABLE;
	hx509_clear_error_string(context);
	goto out;
    }

    ret = copy_oid(&ed.encryptedContentInfo.contentType, contentType);
    if (ret) {
	hx509_clear_error_string(context);
	goto out;
    }

    ai = &ed.encryptedContentInfo.contentEncryptionAlgorithm;
    if (ai->parameters == NULL) {
	ret = HX509_ALG_NOT_SUPP;
	hx509_clear_error_string(context);
	goto out;
    }

    ret = _hx509_pbe_decrypt(context,
			     lock,
			     ai,
			     ed.encryptedContentInfo.encryptedContent,
			     &cont);
    if (ret)
	goto out;

    *content = cont;

out:
    if (ret) {
	if (cont.data)
	    free(cont.data);
    }
    free_CMSEncryptedData(&ed);
    return ret;
}
