/* 
   Unix SMB/CIFS implementation.

   Kerberos utility functions for GENSEC
   
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2004-2005

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "includes.h"
#include "system/kerberos.h"
#include "system/time.h"
#include "system/network.h"
#include "auth/kerberos/kerberos.h"
#include "auth/auth.h"

struct principal_container {
	struct smb_krb5_context *smb_krb5_context;
	krb5_principal principal;
};

static int free_principal(void *ptr) {
	struct principal_container *pc = ptr;
	/* current heimdal - 0.6.3, which we need anyway, fixes segfaults here */
	krb5_free_principal(pc->smb_krb5_context->krb5_context, pc->principal);

	return 0;
}

krb5_error_code salt_principal_from_credentials(TALLOC_CTX *parent_ctx, 
						struct cli_credentials *machine_account, 
						struct smb_krb5_context *smb_krb5_context,
						krb5_principal *salt_princ)
{
	krb5_error_code ret;
	char *machine_username;
	char *salt_body;
	char *lower_realm;
	char *salt_principal;
	struct principal_container *mem_ctx = talloc(parent_ctx, struct principal_container);
	if (!mem_ctx) {
		return ENOMEM;
	}

	salt_principal = cli_credentials_get_salt_principal(machine_account);
	if (salt_principal) {
		ret = krb5_parse_name(smb_krb5_context->krb5_context, salt_principal, salt_princ); 
	} else {
		machine_username = talloc_strdup(mem_ctx, cli_credentials_get_username(machine_account));
		
		if (!machine_username) {
			talloc_free(mem_ctx);
			return ENOMEM;
		}
		
		if (machine_username[strlen(machine_username)-1] == '$') {
			machine_username[strlen(machine_username)-1] = '\0';
		}
		lower_realm = strlower_talloc(mem_ctx, cli_credentials_get_realm(machine_account));
		if (!lower_realm) {
			talloc_free(mem_ctx);
			return ENOMEM;
		}
		
		salt_body = talloc_asprintf(mem_ctx, "%s.%s", machine_username, 
					    lower_realm);
		if (!salt_body) {
			talloc_free(mem_ctx);
		return ENOMEM;
		}
		
		ret = krb5_make_principal(smb_krb5_context->krb5_context, salt_princ, 
					  cli_credentials_get_realm(machine_account), 
					  "host", salt_body, NULL);
	} 

	if (ret == 0) {
		mem_ctx->smb_krb5_context = talloc_reference(mem_ctx, smb_krb5_context);
		mem_ctx->principal = *salt_princ;
		talloc_set_destructor(mem_ctx, free_principal);
	}
	return ret;
}

krb5_error_code principal_from_credentials(TALLOC_CTX *parent_ctx, 
					   struct cli_credentials *credentials, 
					   struct smb_krb5_context *smb_krb5_context,
					   krb5_principal *princ)
{
	krb5_error_code ret;
	const char *princ_string;
	struct principal_container *mem_ctx = talloc(parent_ctx, struct principal_container);
	if (!mem_ctx) {
		return ENOMEM;
	}
	
	princ_string = cli_credentials_get_principal(credentials, mem_ctx);

	if (!princ_string) {
		talloc_free(mem_ctx);
		princ = NULL;
		return 0;
	}

	ret = krb5_parse_name(smb_krb5_context->krb5_context,
			      princ_string, princ);

	if (ret == 0) {
		mem_ctx->smb_krb5_context = talloc_reference(mem_ctx, smb_krb5_context);
		mem_ctx->principal = *princ;
		talloc_set_destructor(mem_ctx, free_principal);
	}
	return ret;
}

/**
 * Return a freshly allocated ccache (destroyed by destructor on child
 * of parent_ctx), for a given set of client credentials 
 */

 krb5_error_code kinit_to_ccache(TALLOC_CTX *parent_ctx,
				 struct cli_credentials *credentials,
				 struct smb_krb5_context *smb_krb5_context,
				 krb5_ccache ccache) 
{
	krb5_error_code ret;
	const char *password;
	time_t kdc_time = 0;
	krb5_principal princ;

	TALLOC_CTX *mem_ctx = talloc_new(parent_ctx);

	if (!mem_ctx) {
		return ENOMEM;
	}

	ret = principal_from_credentials(mem_ctx, credentials, smb_krb5_context, &princ);
	if (ret) {
		talloc_free(mem_ctx);
		return ret;
	}

	password = cli_credentials_get_password(credentials);
	
	if (password) {
		ret = kerberos_kinit_password_cc(smb_krb5_context->krb5_context, ccache, 
						 princ, 
						 password, NULL, &kdc_time);
	} else {
		/* No password available, try to use a keyblock instead */

		krb5_keyblock keyblock;
		const struct samr_Password *mach_pwd;
		mach_pwd = cli_credentials_get_nt_hash(credentials, mem_ctx);
		if (!mach_pwd) {
			talloc_free(mem_ctx);
			DEBUG(1, ("kinit_to_ccache: No password available for kinit\n"));
			return EINVAL;
		}
		ret = krb5_keyblock_init(smb_krb5_context->krb5_context,
					 ENCTYPE_ARCFOUR_HMAC,
					 mach_pwd->hash, sizeof(mach_pwd->hash), 
					 &keyblock);
		
		if (ret == 0) {
			ret = kerberos_kinit_keyblock_cc(smb_krb5_context->krb5_context, ccache, 
							 princ,
							 &keyblock, NULL, &kdc_time);
			krb5_free_keyblock_contents(smb_krb5_context->krb5_context, &keyblock);
		}
	}

	/* cope with ticket being in the future due to clock skew */
	if ((unsigned)kdc_time > time(NULL)) {
		time_t t = time(NULL);
		int time_offset =(unsigned)kdc_time-t;
		DEBUG(4,("Advancing clock by %d seconds to cope with clock skew\n", time_offset));
		krb5_set_real_time(smb_krb5_context->krb5_context, t + time_offset + 1, 0);
	}
	
	if (ret == KRB5KRB_AP_ERR_SKEW || ret == KRB5_KDCREP_SKEW) {
		DEBUG(1,("kinit for %s failed (%s)\n", 
			 cli_credentials_get_principal(credentials, mem_ctx), 
			 smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
						    ret, mem_ctx)));
		talloc_free(mem_ctx);
		return ret;
	}
	if (ret) {
		DEBUG(1,("kinit for %s failed (%s)\n", 
			 cli_credentials_get_principal(credentials, mem_ctx), 
			 smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
						    ret, mem_ctx)));
		talloc_free(mem_ctx);
		return ret;
	} 
	return 0;
}

static int free_keytab(void *ptr) {
	struct keytab_container *ktc = ptr;
	krb5_kt_close(ktc->smb_krb5_context->krb5_context, ktc->keytab);

	return 0;
}

 int create_memory_keytab(TALLOC_CTX *parent_ctx,
			  struct cli_credentials *machine_account,
			  struct smb_krb5_context *smb_krb5_context,
			  struct keytab_container **keytab_container) 
{
	krb5_error_code ret;
	const char *password_s;
	char *old_secret;
	krb5_data password;
	int i, kvno;
	krb5_enctype *enctypes;
	krb5_principal salt_princ;
	krb5_principal princ;
	krb5_keytab keytab;

	TALLOC_CTX *mem_ctx = talloc_new(parent_ctx);
	if (!mem_ctx) {
		return ENOMEM;
	}
	
	*keytab_container = talloc(mem_ctx, struct keytab_container);

	ret = krb5_kt_resolve(smb_krb5_context->krb5_context, "MEMORY:", &keytab);
	if (ret) {
		DEBUG(1,("failed to generate a new krb5 keytab: %s\n", 
			 error_message(ret)));
		talloc_free(mem_ctx);
		return ret;
	}

	(*keytab_container)->smb_krb5_context = talloc_reference(*keytab_container, smb_krb5_context);
	(*keytab_container)->keytab = keytab;

	talloc_set_destructor(*keytab_container, free_keytab);

	ret = salt_principal_from_credentials(mem_ctx, machine_account, 
					      smb_krb5_context, 
					      &salt_princ);
	if (ret) {
		DEBUG(1,("create_memory_keytab: maksing salt principal failed (%s)\n",
			 smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
						    ret, mem_ctx)));
		talloc_free(mem_ctx);
		return ret;
	}

	ret = principal_from_credentials(mem_ctx, machine_account, smb_krb5_context, &princ);
	if (ret) {
		DEBUG(1,("create_memory_keytab: maksing krb5 principal failed (%s)\n",
			 smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
						    ret, mem_ctx)));
		talloc_free(mem_ctx);
		return ret;
	}

	password_s = cli_credentials_get_password(machine_account);
	if (!password_s) {
		/* If we don't have the plaintext password, try for
		 * the MD4 password hash */

		krb5_keytab_entry entry;
		const struct samr_Password *mach_pwd;
		mach_pwd = cli_credentials_get_nt_hash(machine_account, mem_ctx);
		if (!mach_pwd) {
			talloc_free(mem_ctx);
			DEBUG(1, ("create_memory_keytab: Domain trust informaton for account %s not available\n",
				  cli_credentials_get_principal(machine_account, mem_ctx)));
			return EINVAL;
		}
		ret = krb5_keyblock_init(smb_krb5_context->krb5_context,
					 ENCTYPE_ARCFOUR_HMAC,
					 mach_pwd->hash, sizeof(mach_pwd->hash), 
					 &entry.keyblock);
		if (ret) {
			DEBUG(1, ("create_memory_keytab: krb5_keyblock_init failed: %s\n",
				  smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
							     ret, mem_ctx)));
			return ret;
		}

		entry.principal = princ;
		entry.vno       = cli_credentials_get_kvno(machine_account);
		ret = krb5_kt_add_entry(smb_krb5_context->krb5_context, keytab, &entry);
		if (ret) {
			DEBUG(1, ("Failed to add ARCFOUR_HMAC (only) entry for %s to keytab: %s",
				  cli_credentials_get_principal(machine_account, mem_ctx), 
				  smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
							     ret, mem_ctx)));
			talloc_free(mem_ctx);
			krb5_free_keyblock_contents(smb_krb5_context->krb5_context, &entry.keyblock);
			return ret;
		}
		
		krb5_free_keyblock_contents(smb_krb5_context->krb5_context, &entry.keyblock);

		talloc_steal(parent_ctx, *keytab_container);
		talloc_free(mem_ctx);
		return 0;
	}
		
	/* good, we actually have the real plaintext */

	ret = get_kerberos_allowed_etypes(smb_krb5_context->krb5_context, 
					  &enctypes);
	if (ret) {
		DEBUG(1,("create_memory_keytab: getting encrption types failed (%s)\n",
			 error_message(ret)));
		talloc_free(mem_ctx);
		return ret;
	}

	password.data = discard_const_p(char *, password_s);
	password.length = strlen(password_s);
	kvno = cli_credentials_get_kvno(machine_account);

	for (i=0; enctypes[i]; i++) {
		krb5_keytab_entry entry;
		ret = create_kerberos_key_from_string(smb_krb5_context->krb5_context, 
						      salt_princ, &password, &entry.keyblock, enctypes[i]);
		if (ret) {
			talloc_free(mem_ctx);
			return ret;
		}

                entry.principal = princ;
                entry.vno       = kvno;
		ret = krb5_kt_add_entry(smb_krb5_context->krb5_context, keytab, &entry);
		if (ret) {
			DEBUG(1, ("Failed to add entry for %s to keytab: %s",
				  cli_credentials_get_principal(machine_account, mem_ctx), 
				  smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
							     ret, mem_ctx)));
			talloc_free(mem_ctx);
			krb5_free_keyblock_contents(smb_krb5_context->krb5_context, &entry.keyblock);
			return ret;
		}
		
		krb5_free_keyblock_contents(smb_krb5_context->krb5_context, &entry.keyblock);
	}

	old_secret = cli_credentials_get_old_password(machine_account);
	if (kvno != 0 && old_secret) {
		password.data = discard_const_p(char *, old_secret);
		password.length = strlen(old_secret);
		
		for (i=0; enctypes[i]; i++) {
			krb5_keytab_entry entry;
			ret = create_kerberos_key_from_string(smb_krb5_context->krb5_context, 
							      salt_princ, &password, &entry.keyblock, enctypes[i]);
			if (ret) {
				talloc_free(mem_ctx);
				return ret;
			}
			
			entry.principal = princ;
			entry.vno       = kvno - 1;
			ret = krb5_kt_add_entry(smb_krb5_context->krb5_context, keytab, &entry);
			if (ret) {
				DEBUG(1, ("Failed to add 'old password' entry for %s to keytab: %s",
					  cli_credentials_get_principal(machine_account, mem_ctx), 
					  smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
								     ret, mem_ctx)));
				talloc_free(mem_ctx);
				krb5_free_keyblock_contents(smb_krb5_context->krb5_context, &entry.keyblock);
				return ret;
			}
			
			krb5_free_keyblock_contents(smb_krb5_context->krb5_context, &entry.keyblock);
		}
	}

	free_kerberos_etypes(smb_krb5_context->krb5_context, enctypes);

	talloc_steal(parent_ctx, *keytab_container);
	talloc_free(mem_ctx);
	return 0;
}


