/*
 * Copyright (c) 1999-2001, 2003, PADL Software Pty Ltd.
 * Copyright (c) 2004-2009, Andrew Bartlett <abartlet@samba.org>.
 * Copyright (c) 2004, Stefan Metzmacher <metze@samba.org>
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
 * 3. Neither the name of PADL Software  nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "includes.h"
#include "system/time.h"
#include "../libds/common/flags.h"
#include "lib/ldb/include/ldb.h"
#include "lib/ldb/include/ldb_errors.h"
#include "librpc/gen_ndr/netlogon.h"
#include "libcli/security/security.h"
#include "auth/auth.h"
#include "auth/credentials/credentials.h"
#include "auth/auth_sam.h"
#include "../lib/util/util_ldb.h"
#include "dsdb/samdb/samdb.h"
#include "librpc/ndr/libndr.h"
#include "librpc/gen_ndr/ndr_drsblobs.h"
#include "librpc/gen_ndr/lsa.h"
#include "libcli/auth/libcli_auth.h"
#include "param/param.h"
#include "events/events.h"
#include "kdc/kdc.h"
#include "../lib/crypto/md4.h"

enum hdb_ldb_ent_type 
{ HDB_SAMBA4_ENT_TYPE_CLIENT, HDB_SAMBA4_ENT_TYPE_SERVER, 
  HDB_SAMBA4_ENT_TYPE_KRBTGT, HDB_SAMBA4_ENT_TYPE_TRUST, HDB_SAMBA4_ENT_TYPE_ANY };

enum trust_direction {
	UNKNOWN = 0,
	INBOUND = LSA_TRUST_DIRECTION_INBOUND, 
	OUTBOUND = LSA_TRUST_DIRECTION_OUTBOUND
};

static const char *trust_attrs[] = {
	"trustPartner",
	"trustAuthIncoming",
	"trustAuthOutgoing",
	"whenCreated",
	"msDS-SupportedEncryptionTypes",
	"trustAttributes",
	"trustDirection",
	"trustType",
	NULL
};

static KerberosTime ldb_msg_find_krb5time_ldap_time(struct ldb_message *msg, const char *attr, KerberosTime default_val)
{
    const char *tmp;
    const char *gentime;
    struct tm tm;

    gentime = ldb_msg_find_attr_as_string(msg, attr, NULL);
    if (!gentime)
	return default_val;

    tmp = strptime(gentime, "%Y%m%d%H%M%SZ", &tm);
    if (tmp == NULL) {
	    return default_val;
    }

    return timegm(&tm);
}

static HDBFlags uf2HDBFlags(krb5_context context, int userAccountControl, enum hdb_ldb_ent_type ent_type) 
{
	HDBFlags flags = int2HDBFlags(0);

	/* we don't allow kadmin deletes */
	flags.immutable = 1;

	/* mark the principal as invalid to start with */
	flags.invalid = 1;

	flags.renewable = 1;

	/* All accounts are servers, but this may be disabled again in the caller */
	flags.server = 1;

	/* Account types - clear the invalid bit if it turns out to be valid */
	if (userAccountControl & UF_NORMAL_ACCOUNT) {
		if (ent_type == HDB_SAMBA4_ENT_TYPE_CLIENT || ent_type == HDB_SAMBA4_ENT_TYPE_ANY) {
			flags.client = 1;
		}
		flags.invalid = 0;
	}
	
	if (userAccountControl & UF_INTERDOMAIN_TRUST_ACCOUNT) {
		if (ent_type == HDB_SAMBA4_ENT_TYPE_CLIENT || ent_type == HDB_SAMBA4_ENT_TYPE_ANY) {
			flags.client = 1;
		}
		flags.invalid = 0;
	}
	if (userAccountControl & UF_WORKSTATION_TRUST_ACCOUNT) {
		if (ent_type == HDB_SAMBA4_ENT_TYPE_CLIENT || ent_type == HDB_SAMBA4_ENT_TYPE_ANY) {
			flags.client = 1;
		}
		flags.invalid = 0;
	}
	if (userAccountControl & UF_SERVER_TRUST_ACCOUNT) {
		if (ent_type == HDB_SAMBA4_ENT_TYPE_CLIENT || ent_type == HDB_SAMBA4_ENT_TYPE_ANY) {
			flags.client = 1;
		}
		flags.invalid = 0;
	}

	/* Not permitted to act as a client if disabled */
	if (userAccountControl & UF_ACCOUNTDISABLE) {
		flags.client = 0;
	}
	if (userAccountControl & UF_LOCKOUT) {
		flags.invalid = 1;
	}
/*
	if (userAccountControl & UF_PASSWORD_NOTREQD) {
		flags.invalid = 1;
	}
*/
/*
	UF_PASSWORD_CANT_CHANGE and UF_ENCRYPTED_TEXT_PASSWORD_ALLOWED are irrelevent
*/
	if (userAccountControl & UF_TEMP_DUPLICATE_ACCOUNT) {
		flags.invalid = 1;
	}

/* UF_DONT_EXPIRE_PASSWD and UF_USE_DES_KEY_ONLY handled in LDB_message2entry() */

/*
	if (userAccountControl & UF_MNS_LOGON_ACCOUNT) {
		flags.invalid = 1;
	}
*/
	if (userAccountControl & UF_SMARTCARD_REQUIRED) {
		flags.require_hwauth = 1;
	}
	if (userAccountControl & UF_TRUSTED_FOR_DELEGATION) {
		flags.ok_as_delegate = 1;
	}	
	if (!(userAccountControl & UF_NOT_DELEGATED)) {
		flags.forwardable = 1;
		flags.proxiable = 1;
	}

	if (userAccountControl & UF_DONT_REQUIRE_PREAUTH) {
		flags.require_preauth = 0;
	} else {
		flags.require_preauth = 1;

	}
	return flags;
}

static int hdb_ldb_destructor(struct hdb_ldb_private *p)
{
    hdb_entry_ex *entry_ex = p->entry_ex;
    free_hdb_entry(&entry_ex->entry);
    return 0;
}

static void hdb_ldb_free_entry(krb5_context context, hdb_entry_ex *entry_ex)
{
	talloc_free(entry_ex->ctx);
}

static krb5_error_code LDB_message2entry_keys(krb5_context context,
					      struct smb_iconv_convenience *iconv_convenience,
					      TALLOC_CTX *mem_ctx,
					      struct ldb_message *msg,
					      unsigned int userAccountControl,
					      hdb_entry_ex *entry_ex)
{
	krb5_error_code ret = 0;
	enum ndr_err_code ndr_err;
	struct samr_Password *hash;
	const struct ldb_val *sc_val;
	struct supplementalCredentialsBlob scb;
	struct supplementalCredentialsPackage *scpk = NULL;
	bool newer_keys = false;
	struct package_PrimaryKerberosBlob _pkb;
	struct package_PrimaryKerberosCtr3 *pkb3 = NULL;
	struct package_PrimaryKerberosCtr4 *pkb4 = NULL;
	uint32_t i;
	uint32_t allocated_keys = 0;

	entry_ex->entry.keys.val = NULL;
	entry_ex->entry.keys.len = 0;

	entry_ex->entry.kvno = ldb_msg_find_attr_as_int(msg, "msDS-KeyVersionNumber", 0);

	/* Get keys from the db */

	hash = samdb_result_hash(mem_ctx, msg, "unicodePwd");
	sc_val = ldb_msg_find_ldb_val(msg, "supplementalCredentials");

	/* unicodePwd for enctype 0x17 (23) if present */
	if (hash) {
		allocated_keys++;
	}

	/* supplementalCredentials if present */
	if (sc_val) {
		ndr_err = ndr_pull_struct_blob_all(sc_val, mem_ctx, iconv_convenience, &scb,
						   (ndr_pull_flags_fn_t)ndr_pull_supplementalCredentialsBlob);
		if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
			dump_data(0, sc_val->data, sc_val->length);
			ret = EINVAL;
			goto out;
		}

		if (scb.sub.signature != SUPPLEMENTAL_CREDENTIALS_SIGNATURE) {
			NDR_PRINT_DEBUG(supplementalCredentialsBlob, &scb);
			ret = EINVAL;
			goto out;
		}

		for (i=0; i < scb.sub.num_packages; i++) {
			if (strcmp("Primary:Kerberos-Newer-Keys", scb.sub.packages[i].name) == 0) {
				scpk = &scb.sub.packages[i];
				if (!scpk->data || !scpk->data[0]) {
					scpk = NULL;
					continue;
				}
				newer_keys = true;
				break;
			} else if (strcmp("Primary:Kerberos", scb.sub.packages[i].name) == 0) {
				scpk = &scb.sub.packages[i];
				if (!scpk->data || !scpk->data[0]) {
					scpk = NULL;
				}
				/*
				 * we don't break here in hope to find
				 * a Kerberos-Newer-Keys package
				 */
			}
		}
	}
	/*
	 * Primary:Kerberos-Newer-Keys or Primary:Kerberos element
	 * of supplementalCredentials
	 */
	if (scpk) {
		DATA_BLOB blob;

		blob = strhex_to_data_blob(mem_ctx, scpk->data);
		if (!blob.data) {
			ret = ENOMEM;
			goto out;
		}

		/* we cannot use ndr_pull_struct_blob_all() here, as w2k and w2k3 add padding bytes */
		ndr_err = ndr_pull_struct_blob(&blob, mem_ctx, iconv_convenience, &_pkb,
					       (ndr_pull_flags_fn_t)ndr_pull_package_PrimaryKerberosBlob);
		if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
			ret = EINVAL;
			krb5_set_error_message(context, ret, "LDB_message2entry_keys: could not parse package_PrimaryKerberosBlob");
			krb5_warnx(context, "LDB_message2entry_keys: could not parse package_PrimaryKerberosBlob");
			goto out;
		}

		if (newer_keys && _pkb.version != 4) {
			ret = EINVAL;
			krb5_set_error_message(context, ret, "LDB_message2entry_keys: Primary:Kerberos-Newer-Keys not version 4");
			krb5_warnx(context, "LDB_message2entry_keys: Primary:Kerberos-Newer-Keys not version 4");
			goto out;
		}

		if (!newer_keys && _pkb.version != 3) {
			ret = EINVAL;
			krb5_set_error_message(context, ret, "LDB_message2entry_keys: could not parse Primary:Kerberos not version 3");
			krb5_warnx(context, "LDB_message2entry_keys: could not parse Primary:Kerberos not version 3");
			goto out;
		}

		if (_pkb.version == 4) {
			pkb4 = &_pkb.ctr.ctr4;
			allocated_keys += pkb4->num_keys;
		} else if (_pkb.version == 3) {
			pkb3 = &_pkb.ctr.ctr3;
			allocated_keys += pkb3->num_keys;
		}
	}

	if (allocated_keys == 0) {
		/* oh, no password.  Apparently (comment in
		 * hdb-ldap.c) this violates the ASN.1, but this
		 * allows an entry with no keys (yet). */
		return 0;
	}

	/* allocate space to decode into */
	entry_ex->entry.keys.len = 0;
	entry_ex->entry.keys.val = calloc(allocated_keys, sizeof(Key));
	if (entry_ex->entry.keys.val == NULL) {
		ret = ENOMEM;
		goto out;
	}

	if (hash && !(userAccountControl & UF_USE_DES_KEY_ONLY)) {
		Key key;

		key.mkvno = 0;
		key.salt = NULL; /* No salt for this enc type */

		ret = krb5_keyblock_init(context,
					 ENCTYPE_ARCFOUR_HMAC_MD5,
					 hash->hash, sizeof(hash->hash), 
					 &key.key);
		if (ret) {
			goto out;
		}

		entry_ex->entry.keys.val[entry_ex->entry.keys.len] = key;
		entry_ex->entry.keys.len++;
	}

	if (pkb4) {
		for (i=0; i < pkb4->num_keys; i++) {
			bool use = true;
			Key key;

			if (!pkb4->keys[i].value) continue;

			if (userAccountControl & UF_USE_DES_KEY_ONLY) {
				switch (pkb4->keys[i].keytype) {
				case ENCTYPE_DES_CBC_CRC:
				case ENCTYPE_DES_CBC_MD5:
					break;
				default:
					use = false;
					break;
				}
			}

			if (!use) continue;

			key.mkvno = 0;
			key.salt = NULL;

			if (pkb4->salt.string) {
				DATA_BLOB salt;

				salt = data_blob_string_const(pkb4->salt.string);

				key.salt = calloc(1, sizeof(*key.salt));
				if (key.salt == NULL) {
					ret = ENOMEM;
					goto out;
				}

				key.salt->type = hdb_pw_salt;

				ret = krb5_data_copy(&key.salt->salt, salt.data, salt.length);
				if (ret) {
					free(key.salt);
					key.salt = NULL;
					goto out;
				}
			}

			/* TODO: maybe pass the iteration_count somehow... */

			ret = krb5_keyblock_init(context,
						 pkb4->keys[i].keytype,
						 pkb4->keys[i].value->data,
						 pkb4->keys[i].value->length,
						 &key.key);
			if (ret) {
				if (key.salt) {
					free_Salt(key.salt);
					free(key.salt);
					key.salt = NULL;
				}
				goto out;
			}

			entry_ex->entry.keys.val[entry_ex->entry.keys.len] = key;
			entry_ex->entry.keys.len++;
		}
	} else if (pkb3) {
		for (i=0; i < pkb3->num_keys; i++) {
			bool use = true;
			Key key;

			if (!pkb3->keys[i].value) continue;

			if (userAccountControl & UF_USE_DES_KEY_ONLY) {
				switch (pkb3->keys[i].keytype) {
				case ENCTYPE_DES_CBC_CRC:
				case ENCTYPE_DES_CBC_MD5:
					break;
				default:
					use = false;
					break;
				}
			}

			if (!use) continue;

			key.mkvno = 0;
			key.salt = NULL;

			if (pkb3->salt.string) {
				DATA_BLOB salt;

				salt = data_blob_string_const(pkb3->salt.string);

				key.salt = calloc(1, sizeof(*key.salt));
				if (key.salt == NULL) {
					ret = ENOMEM;
					goto out;
				}

				key.salt->type = hdb_pw_salt;

				ret = krb5_data_copy(&key.salt->salt, salt.data, salt.length);
				if (ret) {
					free(key.salt);
					key.salt = NULL;
					goto out;
				}
			}

			ret = krb5_keyblock_init(context,
						 pkb3->keys[i].keytype,
						 pkb3->keys[i].value->data,
						 pkb3->keys[i].value->length,
						 &key.key);
			if (ret) {
				if (key.salt) {
					free_Salt(key.salt);
					free(key.salt);
					key.salt = NULL;
				}
				goto out;
			}

			entry_ex->entry.keys.val[entry_ex->entry.keys.len] = key;
			entry_ex->entry.keys.len++;
		}
	}

out:
	if (ret != 0) {
		entry_ex->entry.keys.len = 0;
	}
	if (entry_ex->entry.keys.len == 0 && entry_ex->entry.keys.val) {
		free(entry_ex->entry.keys.val);
		entry_ex->entry.keys.val = NULL;
	}
	return ret;
}

/*
 * Construct an hdb_entry from a directory entry.
 */
static krb5_error_code LDB_message2entry(krb5_context context, HDB *db, 
					 struct loadparm_context *lp_ctx, 
					 TALLOC_CTX *mem_ctx, krb5_const_principal principal,
					 enum hdb_ldb_ent_type ent_type,
					 struct ldb_dn *realm_dn,
					 struct ldb_message *msg,
					 hdb_entry_ex *entry_ex)
{
	unsigned int userAccountControl;
	int i;
	krb5_error_code ret = 0;
	krb5_boolean is_computer = FALSE;
	char *realm = strupper_talloc(mem_ctx, lp_realm(lp_ctx));

	struct hdb_ldb_private *p;
	NTTIME acct_expiry;
	NTSTATUS status;

	uint32_t rid;
	struct ldb_message_element *objectclasses;
	struct ldb_val computer_val;
	const char *samAccountName = ldb_msg_find_attr_as_string(msg, "samAccountName", NULL);
	computer_val.data = discard_const_p(uint8_t,"computer");
	computer_val.length = strlen((const char *)computer_val.data);
	
	if (!samAccountName) {
		ret = ENOENT;
		krb5_set_error_message(context, ret, "LDB_message2entry: no samAccountName present");
		goto out;
	}

	objectclasses = ldb_msg_find_element(msg, "objectClass");
	
	if (objectclasses && ldb_msg_find_val(objectclasses, &computer_val)) {
		is_computer = TRUE;
	}

	memset(entry_ex, 0, sizeof(*entry_ex));

	if (!realm) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, "talloc_strdup: out of memory");
		goto out;
	}
			
	p = talloc(mem_ctx, struct hdb_ldb_private);
	if (!p) {
		ret = ENOMEM;
		goto out;
	}

	p->entry_ex = entry_ex;
	p->iconv_convenience = lp_iconv_convenience(lp_ctx);
	p->lp_ctx = lp_ctx;
	p->realm_dn = talloc_reference(p, realm_dn);
	if (!p->realm_dn) {
		ret = ENOMEM;
		goto out;
	}

	talloc_set_destructor(p, hdb_ldb_destructor);

	entry_ex->ctx = p;
	entry_ex->free_entry = hdb_ldb_free_entry;

	userAccountControl = ldb_msg_find_attr_as_uint(msg, "userAccountControl", 0);

	
	entry_ex->entry.principal = malloc(sizeof(*(entry_ex->entry.principal)));
	if (ent_type == HDB_SAMBA4_ENT_TYPE_ANY && principal == NULL) {
		krb5_make_principal(context, &entry_ex->entry.principal, realm, samAccountName, NULL);
	} else {
		ret = copy_Principal(principal, entry_ex->entry.principal);
		if (ret) {
			krb5_clear_error_message(context);
			goto out;
		}

		/* While we have copied the client principal, tests
		 * show that Win2k3 returns the 'corrected' realm, not
		 * the client-specified realm.  This code attempts to
		 * replace the client principal's realm with the one
		 * we determine from our records */
		
		/* this has to be with malloc() */
		krb5_principal_set_realm(context, entry_ex->entry.principal, realm);
	}

	/* First try and figure out the flags based on the userAccountControl */
	entry_ex->entry.flags = uf2HDBFlags(context, userAccountControl, ent_type);

	/* Windows 2008 seems to enforce this (very sensible) rule by
	 * default - don't allow offline attacks on a user's password
	 * by asking for a ticket to them as a service (encrypted with
	 * their probably patheticly insecure password) */

	if (entry_ex->entry.flags.server
	    && lp_parm_bool(lp_ctx, NULL, "kdc", "require spn for service", true)) {
		if (!is_computer && !ldb_msg_find_attr_as_string(msg, "servicePrincipalName", NULL)) {
			entry_ex->entry.flags.server = 0;
		}
	}

	{
		/* These (created_by, modified_by) parts of the entry are not relevant for Samba4's use
		 * of the Heimdal KDC.  They are stored in a the traditional
		 * DB for audit purposes, and still form part of the structure
		 * we must return */
		
		/* use 'whenCreated' */
		entry_ex->entry.created_by.time = ldb_msg_find_krb5time_ldap_time(msg, "whenCreated", 0);
		/* use '???' */
		entry_ex->entry.created_by.principal = NULL;
		
		entry_ex->entry.modified_by = (Event *) malloc(sizeof(Event));
		if (entry_ex->entry.modified_by == NULL) {
			ret = ENOMEM;
			krb5_set_error_message(context, ret, "malloc: out of memory");
			goto out;
		}
		
		/* use 'whenChanged' */
		entry_ex->entry.modified_by->time = ldb_msg_find_krb5time_ldap_time(msg, "whenChanged", 0);
		/* use '???' */
		entry_ex->entry.modified_by->principal = NULL;
	}


	/* The lack of password controls etc applies to krbtgt by
	 * virtue of being that particular RID */
	status = dom_sid_split_rid(NULL, samdb_result_dom_sid(mem_ctx, msg, "objectSid"), NULL, &rid);

	if (!NT_STATUS_IS_OK(status)) {
		ret = EINVAL;
		goto out;
	}

	if (rid == DOMAIN_RID_KRBTGT) {
		entry_ex->entry.valid_end = NULL;
		entry_ex->entry.pw_end = NULL;

		entry_ex->entry.flags.invalid = 0;
		entry_ex->entry.flags.server = 1;

		/* Don't mark all requests for the krbtgt/realm as
		 * 'change password', as otherwise we could get into
		 * trouble, and not enforce the password expirty.
		 * Instead, only do it when request is for the kpasswd service */
		if (ent_type == HDB_SAMBA4_ENT_TYPE_SERVER
		    && principal->name.name_string.len == 2
		    && (strcmp(principal->name.name_string.val[0], "kadmin") == 0)
		    && (strcmp(principal->name.name_string.val[1], "changepw") == 0)
		    && lp_is_my_domain_or_realm(lp_ctx, principal->realm)) {
			entry_ex->entry.flags.change_pw = 1;
		}
		entry_ex->entry.flags.client = 0;
		entry_ex->entry.flags.forwardable = 1;
		entry_ex->entry.flags.ok_as_delegate = 1;
	} else if (entry_ex->entry.flags.server && ent_type == HDB_SAMBA4_ENT_TYPE_SERVER) {
		/* The account/password expiry only applies when the account is used as a
		 * client (ie password login), not when used as a server */

		/* Make very well sure we don't use this for a client,
		 * it could bypass the password restrictions */
		entry_ex->entry.flags.client = 0;

		entry_ex->entry.valid_end = NULL;
		entry_ex->entry.pw_end = NULL;

	} else {
		NTTIME must_change_time
			= samdb_result_force_password_change((struct ldb_context *)db->hdb_db, mem_ctx, 
							     realm_dn, msg);
		if (must_change_time == 0x7FFFFFFFFFFFFFFFULL) {
			entry_ex->entry.pw_end = NULL;
		} else {
			entry_ex->entry.pw_end = malloc(sizeof(*entry_ex->entry.pw_end));
			if (entry_ex->entry.pw_end == NULL) {
				ret = ENOMEM;
				goto out;
			}
			*entry_ex->entry.pw_end = nt_time_to_unix(must_change_time);
		}

		acct_expiry = samdb_result_account_expires(msg);
		if (acct_expiry == 0x7FFFFFFFFFFFFFFFULL) {
			entry_ex->entry.valid_end = NULL;
		} else {
			entry_ex->entry.valid_end = malloc(sizeof(*entry_ex->entry.valid_end));
			if (entry_ex->entry.valid_end == NULL) {
				ret = ENOMEM;
				goto out;
			}
			*entry_ex->entry.valid_end = nt_time_to_unix(acct_expiry);
		}
	}

	entry_ex->entry.valid_start = NULL;

	entry_ex->entry.max_life = NULL;

	entry_ex->entry.max_renew = NULL;

	entry_ex->entry.generation = NULL;

	/* Get keys from the db */
	ret = LDB_message2entry_keys(context, p->iconv_convenience, p, msg, userAccountControl, entry_ex);
	if (ret) {
		/* Could be bougus data in the entry, or out of memory */
		goto out;
	}

	entry_ex->entry.etypes = malloc(sizeof(*(entry_ex->entry.etypes)));
	if (entry_ex->entry.etypes == NULL) {
		krb5_clear_error_message(context);
		ret = ENOMEM;
		goto out;
	}
	entry_ex->entry.etypes->len = entry_ex->entry.keys.len;
	entry_ex->entry.etypes->val = calloc(entry_ex->entry.etypes->len, sizeof(int));
	if (entry_ex->entry.etypes->val == NULL) {
		krb5_clear_error_message(context);
		ret = ENOMEM;
		goto out;
	}
	for (i=0; i < entry_ex->entry.etypes->len; i++) {
		entry_ex->entry.etypes->val[i] = entry_ex->entry.keys.val[i].key.keytype;
	}


	p->msg = talloc_steal(p, msg);
	p->samdb = (struct ldb_context *)db->hdb_db;
	
out:
	if (ret != 0) {
		/* This doesn't free ent itself, that is for the eventual caller to do */
		hdb_free_entry(context, entry_ex);
	} else {
		talloc_steal(db, entry_ex->ctx);
	}

	return ret;
}

/*
 * Construct an hdb_entry from a directory entry.
 */
static krb5_error_code LDB_trust_message2entry(krb5_context context, HDB *db, 
					       struct loadparm_context *lp_ctx,
					       TALLOC_CTX *mem_ctx, krb5_const_principal principal,
					       enum trust_direction direction,
					       struct ldb_dn *realm_dn,
					       struct ldb_message *msg,
					       hdb_entry_ex *entry_ex)
{
	
	const char *dnsdomain;
	char *realm;
	DATA_BLOB password_utf16;
	struct samr_Password password_hash;
	const struct ldb_val *password_val;
	struct trustAuthInOutBlob password_blob;
	struct hdb_ldb_private *p;

	enum ndr_err_code ndr_err;
	int i, ret, trust_direction_flags;

	p = talloc(mem_ctx, struct hdb_ldb_private);
	if (!p) {
		ret = ENOMEM;
		goto out;
	}

	p->entry_ex = entry_ex;
	p->iconv_convenience = lp_iconv_convenience(lp_ctx);
	p->lp_ctx = lp_ctx;
	p->realm_dn = realm_dn;

	talloc_set_destructor(p, hdb_ldb_destructor);

	entry_ex->ctx = p;
	entry_ex->free_entry = hdb_ldb_free_entry;

	/* use 'whenCreated' */
	entry_ex->entry.created_by.time = ldb_msg_find_krb5time_ldap_time(msg, "whenCreated", 0);
	/* use '???' */
	entry_ex->entry.created_by.principal = NULL;

	entry_ex->entry.valid_start = NULL;

	trust_direction_flags = ldb_msg_find_attr_as_int(msg, "trustDirection", 0);

	if (direction == INBOUND) {
		realm = strupper_talloc(mem_ctx, lp_realm(lp_ctx));
		password_val = ldb_msg_find_ldb_val(msg, "trustAuthIncoming");

	} else { /* OUTBOUND */
		dnsdomain = ldb_msg_find_attr_as_string(msg, "trustPartner", NULL);
		realm = strupper_talloc(mem_ctx, dnsdomain);
		password_val = ldb_msg_find_ldb_val(msg, "trustAuthOutgoing");
	}

	if (!password_val || !(trust_direction_flags & direction)) {
		ret = ENOENT;
		goto out;
	}

	ndr_err = ndr_pull_struct_blob(password_val, mem_ctx, p->iconv_convenience, &password_blob,
					   (ndr_pull_flags_fn_t)ndr_pull_trustAuthInOutBlob);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		ret = EINVAL;
		goto out;
	}

	entry_ex->entry.kvno = -1;
	for (i=0; i < password_blob.count; i++) {
		if (password_blob.current->array[i].AuthType == TRUST_AUTH_TYPE_VERSION) {
			entry_ex->entry.kvno = password_blob.current->array[i].AuthInfo.version.version;
		}
	}

	for (i=0; i < password_blob.count; i++) {
		if (password_blob.current->array[i].AuthType == TRUST_AUTH_TYPE_CLEAR) {
			password_utf16 = data_blob_const(password_blob.current->array[i].AuthInfo.clear.password,
							 password_blob.current->array[i].AuthInfo.clear.size);
			/* In the future, generate all sorts of
			 * hashes, but for now we can't safely convert
			 * the random strings windows uses into
			 * utf8 */

			/* but as it is utf16 already, we can get the NT password/arcfour-hmac-md5 key */
			mdfour(password_hash.hash, password_utf16.data, password_utf16.length);
			break;
		} else if (password_blob.current->array[i].AuthType == TRUST_AUTH_TYPE_NT4OWF) {
			password_hash = password_blob.current->array[i].AuthInfo.nt4owf.password;
			break;
		}
	}
	entry_ex->entry.keys.len = 0;
	entry_ex->entry.keys.val = NULL;

	if (i < password_blob.count) {
		Key key;
		/* Must have found a cleartext or MD4 password */
		entry_ex->entry.keys.val = calloc(1, sizeof(Key));

		key.mkvno = 0;
		key.salt = NULL; /* No salt for this enc type */

		if (entry_ex->entry.keys.val == NULL) {
			ret = ENOMEM;
			goto out;
		}
		
		ret = krb5_keyblock_init(context,
					 ENCTYPE_ARCFOUR_HMAC_MD5,
					 password_hash.hash, sizeof(password_hash.hash), 
					 &key.key);
		
		entry_ex->entry.keys.val[entry_ex->entry.keys.len] = key;
		entry_ex->entry.keys.len++;
	}
		
	entry_ex->entry.principal = malloc(sizeof(*(entry_ex->entry.principal)));

	ret = copy_Principal(principal, entry_ex->entry.principal);
	if (ret) {
		krb5_clear_error_message(context);
		goto out;
	}
	
	/* While we have copied the client principal, tests
	 * show that Win2k3 returns the 'corrected' realm, not
	 * the client-specified realm.  This code attempts to
	 * replace the client principal's realm with the one
	 * we determine from our records */
	
	krb5_principal_set_realm(context, entry_ex->entry.principal, realm);
	entry_ex->entry.flags = int2HDBFlags(0);
	entry_ex->entry.flags.immutable = 1;
	entry_ex->entry.flags.invalid = 0;
	entry_ex->entry.flags.server = 1;
	entry_ex->entry.flags.require_preauth = 1;

	entry_ex->entry.pw_end = NULL;
			
	entry_ex->entry.max_life = NULL;

	entry_ex->entry.max_renew = NULL;

	entry_ex->entry.generation = NULL;

	entry_ex->entry.etypes = malloc(sizeof(*(entry_ex->entry.etypes)));
	if (entry_ex->entry.etypes == NULL) {
		krb5_clear_error_message(context);
		ret = ENOMEM;
		goto out;
	}
	entry_ex->entry.etypes->len = entry_ex->entry.keys.len;
	entry_ex->entry.etypes->val = calloc(entry_ex->entry.etypes->len, sizeof(int));
	if (entry_ex->entry.etypes->val == NULL) {
		krb5_clear_error_message(context);
		ret = ENOMEM;
		goto out;
	}
	for (i=0; i < entry_ex->entry.etypes->len; i++) {
		entry_ex->entry.etypes->val[i] = entry_ex->entry.keys.val[i].key.keytype;
	}


	p->msg = talloc_steal(p, msg);
	p->samdb = (struct ldb_context *)db->hdb_db;
	
out:
	if (ret != 0) {
		/* This doesn't free ent itself, that is for the eventual caller to do */
		hdb_free_entry(context, entry_ex);
	} else {
		talloc_steal(db, entry_ex->ctx);
	}

	return ret;

}

static krb5_error_code LDB_lookup_trust(krb5_context context, struct ldb_context *ldb_ctx, 					
					TALLOC_CTX *mem_ctx,
					const char *realm,
					struct ldb_dn *realm_dn,
					struct ldb_message **pmsg)
{
	int lret;
	krb5_error_code ret;
	char *filter = NULL;
	const char * const *attrs = trust_attrs;

	struct ldb_result *res = NULL;
	filter = talloc_asprintf(mem_ctx, "(&(objectClass=trustedDomain)(|(flatname=%s)(trustPartner=%s)))", realm, realm);

	if (!filter) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, "talloc_asprintf: out of memory");
		return ret;
	}

	lret = ldb_search(ldb_ctx, mem_ctx, &res,
			  ldb_get_default_basedn(ldb_ctx),
			  LDB_SCOPE_SUBTREE, attrs, "%s", filter);
	if (lret != LDB_SUCCESS) {
		DEBUG(3, ("Failed to search for %s: %s\n", filter, ldb_errstring(ldb_ctx)));
		return HDB_ERR_NOENTRY;
	} else if (res->count == 0 || res->count > 1) {
		DEBUG(3, ("Failed find a single entry for %s: got %d\n", filter, res->count));
		talloc_free(res);
		return HDB_ERR_NOENTRY;
	}
	talloc_steal(mem_ctx, res->msgs);
	*pmsg = res->msgs[0];
	talloc_free(res);
	return 0;
}

static krb5_error_code LDB_open(krb5_context context, HDB *db, int flags, mode_t mode)
{
	if (db->hdb_master_key_set) {
		krb5_error_code ret = HDB_ERR_NOENTRY;
		krb5_warnx(context, "LDB_open: use of a master key incompatible with LDB\n");
		krb5_set_error_message(context, ret, "LDB_open: use of a master key incompatible with LDB\n");
		return ret;
	}		

	return 0;
}

static krb5_error_code LDB_close(krb5_context context, HDB *db)
{
	return 0;
}

static krb5_error_code LDB_lock(krb5_context context, HDB *db, int operation)
{
	return 0;
}

static krb5_error_code LDB_unlock(krb5_context context, HDB *db)
{
	return 0;
}

static krb5_error_code LDB_rename(krb5_context context, HDB *db, const char *new_name)
{
	return HDB_ERR_DB_INUSE;
}

static krb5_error_code LDB_fetch_client(krb5_context context, HDB *db, 
					struct loadparm_context *lp_ctx, 
					TALLOC_CTX *mem_ctx, 
					krb5_const_principal principal,
					unsigned flags,
					hdb_entry_ex *entry_ex) {
	NTSTATUS nt_status;
	char *principal_string;
	struct ldb_dn *realm_dn;
	krb5_error_code ret;
	struct ldb_message *msg = NULL;

	ret = krb5_unparse_name(context, principal, &principal_string);
	
	if (ret != 0) {
		return ret;
	}
	
	nt_status = sam_get_results_principal((struct ldb_context *)db->hdb_db,
					      mem_ctx, principal_string, 
					      &realm_dn, &msg);
	free(principal_string);
	if (NT_STATUS_EQUAL(nt_status, NT_STATUS_NO_SUCH_USER)) {
		return HDB_ERR_NOENTRY;
	} else if (NT_STATUS_EQUAL(nt_status, NT_STATUS_NO_MEMORY)) {
		return ENOMEM;
	} else if (!NT_STATUS_IS_OK(nt_status)) {
		return EINVAL;
	}
	
	ret = LDB_message2entry(context, db, lp_ctx, mem_ctx, 
				principal, HDB_SAMBA4_ENT_TYPE_CLIENT,
				realm_dn, msg, entry_ex);
	return ret;
}

static krb5_error_code LDB_fetch_krbtgt(krb5_context context, HDB *db, 
					struct loadparm_context *lp_ctx, 
					TALLOC_CTX *mem_ctx, 
					krb5_const_principal principal,
					unsigned flags,
					hdb_entry_ex *entry_ex)
{
	krb5_error_code ret;
	struct ldb_message *msg = NULL;
	struct ldb_dn *realm_dn = ldb_get_default_basedn(db->hdb_db);
	const char *realm;

	krb5_principal alloc_principal = NULL;
	if (principal->name.name_string.len != 2
	    || (strcmp(principal->name.name_string.val[0], KRB5_TGS_NAME) != 0)) {
		/* Not a krbtgt */
		return HDB_ERR_NOENTRY;
	}

	/* krbtgt case.  Either us or a trusted realm */

	if (lp_is_my_domain_or_realm(lp_ctx, principal->realm)
	    && lp_is_my_domain_or_realm(lp_ctx, principal->name.name_string.val[1])) {
		/* us */		
 		/* Cludge, cludge cludge.  If the realm part of krbtgt/realm,
 		 * is in our db, then direct the caller at our primary
 		 * krbtgt */

		int lret;
		char *realm_fixed;
		const char * const *princ_attrs = user_attrs;
 		
		lret = gendb_search_single_extended_dn(db->hdb_db, mem_ctx, 
						       realm_dn, LDB_SCOPE_SUBTREE,
						       &msg, princ_attrs, 
						       "(&(objectClass=user)(samAccountName=krbtgt))"); 
		if (lret == LDB_ERR_NO_SUCH_OBJECT) {
			krb5_warnx(context, "LDB_fetch: could not find own KRBTGT in DB!");
			krb5_set_error_message(context, HDB_ERR_NOENTRY, "LDB_fetch: could not find own KRBTGT in DB!");
			return HDB_ERR_NOENTRY;
		} else if (lret != LDB_SUCCESS) {
			krb5_warnx(context, "LDB_fetch: could not find own KRBTGT in DB: %s", ldb_errstring(db->hdb_db));
			krb5_set_error_message(context, HDB_ERR_NOENTRY, "LDB_fetch: could not find own KRBTGT in DB: %s", ldb_errstring(db->hdb_db));
			return HDB_ERR_NOENTRY;
		}
		
 		realm_fixed = strupper_talloc(mem_ctx, lp_realm(lp_ctx));
 		if (!realm_fixed) {
			ret = ENOMEM;
 			krb5_set_error_message(context, ret, "strupper_talloc: out of memory");
 			return ret;
 		}
 		
 		ret = krb5_copy_principal(context, principal, &alloc_principal);
 		if (ret) {
 			return ret;
 		}
 
 		free(alloc_principal->name.name_string.val[1]);
		alloc_principal->name.name_string.val[1] = strdup(realm_fixed);
 		talloc_free(realm_fixed);
 		if (!alloc_principal->name.name_string.val[1]) {
			ret = ENOMEM;
 			krb5_set_error_message(context, ret, "LDB_fetch: strdup() failed!");
 			return ret;
 		}
 		principal = alloc_principal;

		ret = LDB_message2entry(context, db, lp_ctx, mem_ctx, 
					principal, HDB_SAMBA4_ENT_TYPE_KRBTGT, 
					realm_dn, msg, entry_ex);
		if (ret != 0) {
			krb5_warnx(context, "LDB_fetch: self krbtgt message2entry failed");	
		}
		return ret;

	} else {
		enum trust_direction direction = UNKNOWN;

		/* Either an inbound or outbound trust */

		if (strcasecmp(lp_realm(lp_ctx), principal->realm) == 0) {
			/* look for inbound trust */
			direction = INBOUND;
			realm = principal->name.name_string.val[1];
		}

		if (strcasecmp(lp_realm(lp_ctx), principal->name.name_string.val[1]) == 0) {
			/* look for outbound trust */
			direction = OUTBOUND;
			realm = principal->realm;
		}

		/* Trusted domains are under CN=system */
		
		ret = LDB_lookup_trust(context, (struct ldb_context *)db->hdb_db, 
				       mem_ctx, 
				       realm, realm_dn, &msg);
		
		if (ret != 0) {
			krb5_warnx(context, "LDB_fetch: could not find principal in DB");
			krb5_set_error_message(context, ret, "LDB_fetch: could not find principal in DB");
			return ret;
		}
		
		ret = LDB_trust_message2entry(context, db, lp_ctx, mem_ctx, 
					      principal, direction, 
					      realm_dn, msg, entry_ex);
		if (ret != 0) {
			krb5_warnx(context, "LDB_fetch: trust_message2entry failed");	
		}
		return ret;

		
		/* we should lookup trusted domains */
		return HDB_ERR_NOENTRY;
	}

}

static krb5_error_code LDB_fetch_server(krb5_context context, HDB *db, 
					struct loadparm_context *lp_ctx,
					TALLOC_CTX *mem_ctx, 
					krb5_const_principal principal,
					unsigned flags,
					hdb_entry_ex *entry_ex)
{
	krb5_error_code ret;
	const char *realm;
	struct ldb_message *msg = NULL;
	struct ldb_dn *realm_dn;
	if (principal->name.name_string.len >= 2) {
		/* 'normal server' case */
		int ldb_ret;
		NTSTATUS nt_status;
		struct ldb_dn *user_dn;
		char *principal_string;
		
		ret = krb5_unparse_name_flags(context, principal, 
					      KRB5_PRINCIPAL_UNPARSE_NO_REALM, 
					      &principal_string);
		if (ret != 0) {
			return ret;
		}
		
		/* At this point we may find the host is known to be
		 * in a different realm, so we should generate a
		 * referral instead */
		nt_status = crack_service_principal_name((struct ldb_context *)db->hdb_db,
							 mem_ctx, principal_string, 
							 &user_dn, &realm_dn);
		free(principal_string);
		
		if (!NT_STATUS_IS_OK(nt_status)) {
			return HDB_ERR_NOENTRY;
		}
		
		ldb_ret = gendb_search_single_extended_dn((struct ldb_context *)db->hdb_db,
							  mem_ctx, 
							  user_dn, LDB_SCOPE_BASE,
							  &msg, user_attrs,
							  "(objectClass=*)");
		if (ldb_ret != LDB_SUCCESS) {
			return HDB_ERR_NOENTRY;
		}
		
	} else {
		int lret;
		char *filter = NULL;
		const char * const *princ_attrs = user_attrs;
		char *short_princ;
		/* server as client principal case, but we must not lookup userPrincipalNames */
		realm_dn = ldb_get_default_basedn(db->hdb_db);
		realm = krb5_principal_get_realm(context, principal);
		
		/* TODO: Check if it is our realm, otherwise give referall */
		
		ret = krb5_unparse_name_flags(context, principal,  KRB5_PRINCIPAL_UNPARSE_NO_REALM, &short_princ);
		
		if (ret != 0) {
			krb5_set_error_message(context, ret, "LDB_lookup_principal: could not parse principal");
			krb5_warnx(context, "LDB_lookup_principal: could not parse principal");
			return ret;
		}
		
		lret = gendb_search_single_extended_dn(db->hdb_db, mem_ctx, 
						       realm_dn, LDB_SCOPE_SUBTREE,
						       &msg, princ_attrs, "(&(objectClass=user)(samAccountName=%s))", 
						       ldb_binary_encode_string(mem_ctx, short_princ));
		free(short_princ);
		if (lret == LDB_ERR_NO_SUCH_OBJECT) {
			DEBUG(3, ("Failed find a entry for %s\n", filter));
			return HDB_ERR_NOENTRY;
		}
		if (lret != LDB_SUCCESS) {
			DEBUG(3, ("Failed single search for for %s - %s\n", 
				  filter, ldb_errstring(db->hdb_db)));
			return HDB_ERR_NOENTRY;
		}
	}

	ret = LDB_message2entry(context, db, lp_ctx, mem_ctx, 
				principal, HDB_SAMBA4_ENT_TYPE_SERVER,
				realm_dn, msg, entry_ex);
	if (ret != 0) {
		krb5_warnx(context, "LDB_fetch: message2entry failed");	
	}

	return ret;
}
			
static krb5_error_code LDB_fetch(krb5_context context, HDB *db, 
				 krb5_const_principal principal,
				 unsigned flags,
				 hdb_entry_ex *entry_ex)
{
	krb5_error_code ret = HDB_ERR_NOENTRY;
	TALLOC_CTX *mem_ctx = talloc_named(db, 0, "LDB_fetch context");
	struct loadparm_context *lp_ctx = talloc_get_type(ldb_get_opaque(db->hdb_db, "loadparm"), struct loadparm_context);

	if (!mem_ctx) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, "LDB_fetch: talloc_named() failed!");
		return ret;
	}

	if (flags & HDB_F_GET_CLIENT) {
		ret = LDB_fetch_client(context, db, lp_ctx, mem_ctx, principal, flags, entry_ex);
		if (ret != HDB_ERR_NOENTRY) goto done;
	}
	if (flags & HDB_F_GET_SERVER) {
		/* krbtgt fits into this situation for trusted realms, and for resolving different versions of our own realm name */
		ret = LDB_fetch_krbtgt(context, db, lp_ctx, mem_ctx, principal, flags, entry_ex);
		if (ret != HDB_ERR_NOENTRY) goto done;

		/* We return 'no entry' if it does not start with krbtgt/, so move to the common case quickly */
		ret = LDB_fetch_server(context, db, lp_ctx, mem_ctx, principal, flags, entry_ex);
		if (ret != HDB_ERR_NOENTRY) goto done;
	}
	if (flags & HDB_F_GET_KRBTGT) {
		ret = LDB_fetch_krbtgt(context, db, lp_ctx, mem_ctx, principal, flags, entry_ex);
		if (ret != HDB_ERR_NOENTRY) goto done;
	}

done:
	talloc_free(mem_ctx);
	return ret;
}

static krb5_error_code LDB_store(krb5_context context, HDB *db, unsigned flags, hdb_entry_ex *entry)
{
	return HDB_ERR_DB_INUSE;
}

static krb5_error_code LDB_remove(krb5_context context, HDB *db, krb5_const_principal principal)
{
	return HDB_ERR_DB_INUSE;
}

struct hdb_ldb_seq {
	struct ldb_context *ctx;
	struct loadparm_context *lp_ctx;
	int index;
	int count;
	struct ldb_message **msgs;
	struct ldb_dn *realm_dn;
};

static krb5_error_code LDB_seq(krb5_context context, HDB *db, unsigned flags, hdb_entry_ex *entry)
{
	krb5_error_code ret;
	struct hdb_ldb_seq *priv = (struct hdb_ldb_seq *)db->hdb_dbc;
	TALLOC_CTX *mem_ctx;
	hdb_entry_ex entry_ex;
	memset(&entry_ex, '\0', sizeof(entry_ex));

	if (!priv) {
		return HDB_ERR_NOENTRY;
	}

	mem_ctx = talloc_named(priv, 0, "LDB_seq context");

	if (!mem_ctx) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, "LDB_seq: talloc_named() failed!");
		return ret;
	}

	if (priv->index < priv->count) {
		ret = LDB_message2entry(context, db, priv->lp_ctx, 
					mem_ctx, 
					NULL, HDB_SAMBA4_ENT_TYPE_ANY, 
					priv->realm_dn, priv->msgs[priv->index++], entry);
	} else {
		ret = HDB_ERR_NOENTRY;
	}

	if (ret != 0) {
		talloc_free(priv);
		db->hdb_dbc = NULL;
	} else {
		talloc_free(mem_ctx);
	}

	return ret;
}

static krb5_error_code LDB_firstkey(krb5_context context, HDB *db, unsigned flags,
					hdb_entry_ex *entry)
{
	struct ldb_context *ldb_ctx = (struct ldb_context *)db->hdb_db;
	struct loadparm_context *lp_ctx = talloc_get_type(ldb_get_opaque(ldb_ctx, "loadparm"), 
							  struct loadparm_context);
	struct hdb_ldb_seq *priv = (struct hdb_ldb_seq *)db->hdb_dbc;
	char *realm;
	struct ldb_result *res = NULL;
	krb5_error_code ret;
	TALLOC_CTX *mem_ctx;
	int lret;

	if (priv) {
		talloc_free(priv);
		db->hdb_dbc = NULL;
	}

	priv = (struct hdb_ldb_seq *) talloc(db, struct hdb_ldb_seq);
	if (!priv) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, "talloc: out of memory");
		return ret;
	}

	priv->ctx = ldb_ctx;
	priv->lp_ctx = lp_ctx;
	priv->index = 0;
	priv->msgs = NULL;
	priv->realm_dn = ldb_get_default_basedn(ldb_ctx);
	priv->count = 0;

	mem_ctx = talloc_named(priv, 0, "LDB_firstkey context");

	if (!mem_ctx) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, "LDB_firstkey: talloc_named() failed!");
		return ret;
	}

	ret = krb5_get_default_realm(context, &realm);
	if (ret != 0) {
		talloc_free(priv);
		return ret;
	}
		
	lret = ldb_search(ldb_ctx, priv, &res,
			  priv->realm_dn, LDB_SCOPE_SUBTREE, user_attrs,
			  "(objectClass=user)");

	if (lret != LDB_SUCCESS) {
		talloc_free(priv);
		return HDB_ERR_NOENTRY;
	}

	priv->count = res->count;
	priv->msgs = talloc_steal(priv, res->msgs);
	talloc_free(res);

	db->hdb_dbc = priv;

	ret = LDB_seq(context, db, flags, entry);

	if (ret != 0) {
    		talloc_free(priv);
		db->hdb_dbc = NULL;
	} else {
		talloc_free(mem_ctx);
	}
	return ret;
}

static krb5_error_code LDB_nextkey(krb5_context context, HDB *db, unsigned flags,
				   hdb_entry_ex *entry)
{
	return LDB_seq(context, db, flags, entry);
}

static krb5_error_code LDB_destroy(krb5_context context, HDB *db)
{
	talloc_free(db);
	return 0;
}

/* This interface is to be called by the KDC, which is expecting Samba
 * calling conventions.  It is also called by a wrapper
 * (hdb_ldb_create) from the kpasswdd -> krb5 -> keytab_hdb -> hdb
 * code */

NTSTATUS kdc_hdb_samba4_create(TALLOC_CTX *mem_ctx, 
			    struct tevent_context *ev_ctx, 
			    struct loadparm_context *lp_ctx,
			    krb5_context context, struct HDB **db, const char *arg)
{
	NTSTATUS nt_status;
	struct auth_session_info *session_info;
	*db = talloc(mem_ctx, HDB);
	if (!*db) {
		krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
		return NT_STATUS_NO_MEMORY;
	}

	(*db)->hdb_master_key_set = 0;
	(*db)->hdb_db = NULL;
	(*db)->hdb_capability_flags = 0;

	nt_status = auth_system_session_info(*db, lp_ctx, &session_info);
	if (!NT_STATUS_IS_OK(nt_status)) {
		return nt_status;
	}
	
	/* The idea here is very simple.  Using Kerberos to
	 * authenticate the KDC to the LDAP server is higly likely to
	 * be circular.
	 *
	 * In future we may set this up to use EXERNAL and SSL
	 * certificates, for now it will almost certainly be NTLMSSP
	*/
	
	cli_credentials_set_kerberos_state(session_info->credentials, 
					   CRED_DONT_USE_KERBEROS);

	/* Setup the link to LDB */
	(*db)->hdb_db = samdb_connect(*db, ev_ctx, lp_ctx, session_info);
	if ((*db)->hdb_db == NULL) {
		DEBUG(1, ("hdb_ldb_create: Cannot open samdb for KDC backend!"));
		return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
	}

	(*db)->hdb_dbc = NULL;
	(*db)->hdb_open = LDB_open;
	(*db)->hdb_close = LDB_close;
	(*db)->hdb_fetch = LDB_fetch;
	(*db)->hdb_store = LDB_store;
	(*db)->hdb_remove = LDB_remove;
	(*db)->hdb_firstkey = LDB_firstkey;
	(*db)->hdb_nextkey = LDB_nextkey;
	(*db)->hdb_lock = LDB_lock;
	(*db)->hdb_unlock = LDB_unlock;
	(*db)->hdb_rename = LDB_rename;
	/* we don't implement these, as we are not a lockable database */
	(*db)->hdb__get = NULL;
	(*db)->hdb__put = NULL;
	/* kadmin should not be used for deletes - use other tools instead */
	(*db)->hdb__del = NULL;
	(*db)->hdb_destroy = LDB_destroy;

	return NT_STATUS_OK;
}

krb5_error_code hdb_samba4_create(krb5_context context, struct HDB **db, const char *arg)
{
	NTSTATUS nt_status;
	/* The global kdc_mem_ctx and kdc_lp_ctx, Disgusting, ugly hack, but it means one less private hook */
	nt_status = kdc_hdb_samba4_create(kdc_mem_ctx, kdc_ev_ctx, kdc_lp_ctx,
					  context, db, arg);

	if (NT_STATUS_IS_OK(nt_status)) {
		return 0;
	}
	return EINVAL;
}
