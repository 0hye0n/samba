/*
 * Copyright (c) 2007 Kungliga Tekniska H�gskolan
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

#ifdef HAVE_FRAMEWORK_SECURITY

#include <Security/Security.h>

/*
 *
 */

struct kc_rsa {
    SecKeychainItemRef item;
};


static int
kc_rsa_public_encrypt(int flen,
		      const unsigned char *from,
		      unsigned char *to,
		      RSA *rsa,
		      int padding)
{
    return -1;
}

static int
kc_rsa_public_decrypt(int flen,
		      const unsigned char *from,
		      unsigned char *to,
		      RSA *rsa,
		      int padding)
{
    return -1;
}


static int
kc_rsa_private_encrypt(int flen, 
		       const unsigned char *from,
		       unsigned char *to,
		       RSA *rsa,
		       int padding)
{
    return -1;
}

static int
kc_rsa_private_decrypt(int flen, const unsigned char *from, unsigned char *to,
		       RSA * rsa, int padding)
{
    return -1;
}

static int 
kc_rsa_init(RSA *rsa)
{
    return 1;
}

static int
kc_rsa_finish(RSA *rsa)
{
    struct kc_rsa *kc_rsa = RSA_get_app_data(rsa);
    CFRelease(kc_rsa->item);
    memset(kc_rsa, 0, sizeof(*kc_rsa));
    free(kc_rsa);
    return 1;
}

static const RSA_METHOD kc_rsa_pkcs1_method = {
    "hx509 Keychain PKCS#1 RSA",
    kc_rsa_public_encrypt,
    kc_rsa_public_decrypt,
    kc_rsa_private_encrypt,
    kc_rsa_private_decrypt,
    NULL,
    NULL,
    kc_rsa_init,
    kc_rsa_finish,
    0,
    NULL,
    NULL,
    NULL
};

static int
private_key(hx509_context context, SecKeychainItemRef itemRef,
	    hx509_cert cert)
{
    struct kc_rsa *kc_rsa;
    hx509_private_key key;
    RSA *rsa;
    int ret;

    ret = _hx509_private_key_init(&key, NULL, NULL);
    if (ret)
	return ret;

    rsa = RSA_new();
    if (rsa == NULL)
	_hx509_abort("out of memory");

    kc_rsa = calloc(1, sizeof(*kc_rsa));
    if (kc_rsa == NULL)
	_hx509_abort("out of memory");

    kc_rsa->item = itemRef;
    
    RSA_set_method(rsa, &kc_rsa_pkcs1_method);
    ret = RSA_set_app_data(rsa, kc_rsa);
    if (ret != 1)
	_hx509_abort("RSA_set_app_data");

    _hx509_private_key_assign_rsa(key, rsa);
    _hx509_cert_assign_key(cert, key);

    return 0;
}

/*
 *
 */

struct ks_keychain {
    SecKeychainRef keychain;
};

static int
keychain_init(hx509_context context,
	      hx509_certs certs, void **data, int flags,
	      const char *residue, hx509_lock lock)
{
    struct ks_keychain *ctx;
    OSStatus ret;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    if (residue) {
	if (strcasecmp(residue, "system") == 0)
	    residue = "/System/Library/Keychains/X509Anchors";

	ret = SecKeychainOpen(residue, &ctx->keychain);
	if (ret != noErr) {
	    hx509_set_error_string(context, 0, ENOENT, 
				   "Failed to open %s", residue);
	    return ENOENT;
	}
    }

    *data = ctx;
    return 0;
}

/*
 *
 */

static int
keychain_free(hx509_certs certs, void *data)
{
    struct ks_keychain *ctx = data;
    if (ctx->keychain)
	CFRelease(ctx->keychain);
    memset(ctx, 0, sizeof(*ctx));
    free(ctx);
    return 0;
}

/*
 *
 */

struct iter {
    SecKeychainSearchRef searchRef;
};

static int 
keychain_iter_start(hx509_context context,
		    hx509_certs certs, void *data, void **cursor)
{
    struct ks_keychain *ctx = data;
    struct iter *iter;
    OSStatus ret;

    iter = calloc(1, sizeof(*iter));
    if (iter == NULL) {
	hx509_set_error_string(context, 0, ENOMEM, "out of memory");
	return ENOMEM;
    }

    ret = SecKeychainSearchCreateFromAttributes(ctx->keychain,
						kSecCertificateItemClass,
						NULL,
						&iter->searchRef);
    if (ret) {
	free(iter);
	hx509_set_error_string(context, 0, ret, 
			       "Failed to start search for attributes");
	return ENOMEM;
    }

    *cursor = iter;
    return 0;
}

/*
 *
 */

static int
keychain_iter(hx509_context context,
	      hx509_certs certs, void *data, void *cursor, hx509_cert *cert)
{
    SecKeychainAttributeList *attrs = NULL;
    SecKeychainAttributeInfo attrInfo;
    uint32 attrFormat = 0;
    SecKeychainItemRef itemRef;
    SecItemAttr item;
    struct iter *iter = cursor;
    Certificate t;
    OSStatus ret;
    UInt32 len;
    void *ptr = NULL;
    size_t size;

    *cert = NULL;

    ret = SecKeychainSearchCopyNext(iter->searchRef, &itemRef);
    if (ret == errSecItemNotFound)
	return 0;
    else if (ret != 0)
	return EINVAL;
	
    /*
     * Pick out certificate and matching "keyid"
     */

    item = kSecPublicKeyHashItemAttr;

    attrInfo.count = 1;
    attrInfo.tag = &item;
    attrInfo.format = &attrFormat;
  
    ret = SecKeychainItemCopyAttributesAndData(itemRef, &attrInfo, NULL,
					       &attrs, &len, &ptr);
    if (ret)
	return EINVAL;
    
    ret = decode_Certificate(ptr, len, &t, &size);
    CFRelease(itemRef);
    if (ret) {
	hx509_set_error_string(context, 0, ret, "Failed to parse certificate");
	goto out;
    }

    ret = hx509_cert_init(context, &t, cert);
    free_Certificate(&t);
    if (ret)
	goto out;

    /* 
     * Find related private key if there is one by looking at
     * kSecPublicKeyHashItemAttr == kSecKeyLabel
     */
    {
	SecKeychainSearchRef search;
	SecKeychainAttribute attrKeyid;
	SecKeychainAttributeList attrList;

	attrKeyid.tag = kSecKeyLabel;
	attrKeyid.length = attrs->attr[0].length;
	attrKeyid.data = attrs->attr[0].data;
	
	attrList.count = 1;
	attrList.attr = &attrKeyid;

	ret = SecKeychainSearchCreateFromAttributes(NULL,
						    CSSM_DL_DB_RECORD_PRIVATE_KEY,
						    &attrList,
						    &search);
	if (ret) {
	    ret = 0;
	    goto out;
	}

	ret = SecKeychainSearchCopyNext(search, &itemRef);
	CFRelease(search);
	if (ret == errSecItemNotFound) {
	    ret = 0;
	    goto out;
	} else if (ret) {
	    ret = EINVAL;
	    goto out;
	}

	private_key(context, itemRef, *cert);
    }

out:
    SecKeychainItemFreeAttributesAndData(attrs, ptr);

    return ret;
}

/*
 *
 */

static int
keychain_iter_end(hx509_context context,
		  hx509_certs certs,
		  void *data,
		  void *cursor)
{
    struct iter *iter = cursor;

    CFRelease(iter->searchRef);
    memset(iter, 0, sizeof(*iter));
    free(iter);
    return 0;
}

/*
 *
 */

struct hx509_keyset_ops keyset_keychain = {
    "KEYCHAIN",
    0,
    keychain_init,
    NULL,
    keychain_free,
    NULL,
    NULL,
    keychain_iter_start,
    keychain_iter,
    keychain_iter_end
};

#endif /* HAVE_FRAMEWORK_SECURITY */

/*
 *
 */

void
_hx509_ks_keychain_register(hx509_context context)
{
#ifdef HAVE_FRAMEWORK_SECURITY
    _hx509_ks_register(context, &keyset_keychain);
#endif
}
