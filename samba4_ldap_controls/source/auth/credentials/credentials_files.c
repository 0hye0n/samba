/* 
   Unix SMB/CIFS implementation.

   User credentials handling (as regards on-disk files)

   Copyright (C) Jelmer Vernooij 2005
   Copyright (C) Tim Potter 2001
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2005
   
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
#include "lib/ldb/include/ldb.h"
#include "librpc/gen_ndr/ndr_samr.h" /* for struct samrPassword */
#include "include/secrets.h"
#include "system/filesys.h"

/**
 * Read a file descriptor, and parse it for a password (eg from a file or stdin)
 *
 * @param credentials Credentials structure on which to set the password
 * @param fd open file descriptor to read the password from 
 * @param obtained This enum describes how 'specified' this password is
 */

BOOL cli_credentials_parse_password_fd(struct cli_credentials *credentials, 
				       int fd, enum credentials_obtained obtained)
{
	char *p;
	char pass[128];

	for(p = pass, *p = '\0'; /* ensure that pass is null-terminated */
		p && p - pass < sizeof(pass);) {
		switch (read(fd, p, 1)) {
		case 1:
			if (*p != '\n' && *p != '\0') {
				*++p = '\0'; /* advance p, and null-terminate pass */
				break;
			}
		case 0:
			if (p - pass) {
				*p = '\0'; /* null-terminate it, just in case... */
				p = NULL; /* then force the loop condition to become false */
				break;
			} else {
				fprintf(stderr, "Error reading password from file descriptor %d: %s\n", fd, "empty password\n");
				return False;
			}

		default:
			fprintf(stderr, "Error reading password from file descriptor %d: %s\n",
					fd, strerror(errno));
			return False;
		}
	}

	cli_credentials_set_password(credentials, pass, obtained);
	return True;
}

/**
 * Read a named file, and parse it for a password
 *
 * @param credentials Credentials structure on which to set the password
 * @param file a named file to read the password from 
 * @param obtained This enum describes how 'specified' this password is
 */

BOOL cli_credentials_parse_password_file(struct cli_credentials *credentials, const char *file, enum credentials_obtained obtained)
{
	int fd = open(file, O_RDONLY, 0);
	BOOL ret;

	if (fd < 0) {
		fprintf(stderr, "Error opening PASSWD_FILE %s: %s\n",
				file, strerror(errno));
		return False;
	}

	ret = cli_credentials_parse_password_fd(credentials, fd, obtained);

	close(fd);
	
	return ret;
}

/**
 * Read a named file, and parse it for username, domain, realm and password
 *
 * @param credentials Credentials structure on which to set the password
 * @param file a named file to read the details from 
 * @param obtained This enum describes how 'specified' this password is
 */

BOOL cli_credentials_parse_file(struct cli_credentials *cred, const char *file, enum credentials_obtained obtained) 
{
	uint16_t len = 0;
	char *ptr, *val, *param;
	char **lines;
	int i, numlines;

	lines = file_lines_load(file, &numlines, NULL);

	if (lines == NULL)
	{
		/* fail if we can't open the credentials file */
		d_printf("ERROR: Unable to open credentials file!\n");
		return False;
	}

	for (i = 0; i < numlines; i++) {
		len = strlen(lines[i]);

		if (len == 0)
			continue;

		/* break up the line into parameter & value.
		 * will need to eat a little whitespace possibly */
		param = lines[i];
		if (!(ptr = strchr_m (lines[i], '=')))
			continue;

		val = ptr+1;
		*ptr = '\0';

		/* eat leading white space */
		while ((*val!='\0') && ((*val==' ') || (*val=='\t')))
			val++;

		if (strwicmp("password", param) == 0) {
			cli_credentials_set_password(cred, val, obtained);
		} else if (strwicmp("username", param) == 0) {
			cli_credentials_set_username(cred, val, obtained);
		} else if (strwicmp("domain", param) == 0) {
			cli_credentials_set_domain(cred, val, obtained);
		} else if (strwicmp("realm", param) == 0) {
			cli_credentials_set_realm(cred, val, obtained);
		}
		memset(lines[i], 0, len);
	}

	talloc_free(lines);

	return True;
}


/**
 * Fill in credentials for the machine trust account, from the secrets database.
 * 
 * @param cred Credentials structure to fill in
 * @retval NTSTATUS error detailing any failure
 */
NTSTATUS cli_credentials_set_machine_account(struct cli_credentials *cred)
{
	TALLOC_CTX *mem_ctx;
	
	struct ldb_context *ldb;
	int ldb_ret;
	struct ldb_message **msgs;
	const char *attrs[] = {
		"secret",
		"samAccountName",
		"flatname",
		"realm",
		"secureChannelType",
		"ntPwdHash",
		"msDS-KeyVersionNumber",
		NULL
	};
	
	const char *machine_account;
	const char *password;
	const char *domain;
	const char *realm;
	enum netr_SchannelType sct;
	
	/* ok, we are going to get it now, don't recurse back here */
	cred->machine_account_pending = False;

	mem_ctx = talloc_named(cred, 0, "cli_credentials fetch machine password");
	/* Local secrets are stored in secrets.ldb */
	ldb = secrets_db_connect(mem_ctx);
	if (!ldb) {
		DEBUG(1, ("Could not open secrets.ldb\n"));
		return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
	}

	/* search for the secret record */
	ldb_ret = gendb_search(ldb,
			       mem_ctx, ldb_dn_explode(mem_ctx, SECRETS_PRIMARY_DOMAIN_DN), 
			       &msgs, attrs,
			       SECRETS_PRIMARY_DOMAIN_FILTER, 
			       cli_credentials_get_domain(cred));
	if (ldb_ret == 0) {
		DEBUG(1, ("Could not find join record to domain: %s\n",
			  cli_credentials_get_domain(cred)));
		talloc_free(mem_ctx);
		return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
	} else if (ldb_ret != 1) {
		DEBUG(1, ("Found more than one (%d) join records to domain: %s\n",
			  ldb_ret, cli_credentials_get_domain(cred)));
		talloc_free(mem_ctx);
		return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
	}
	
	password = ldb_msg_find_string(msgs[0], "secret", NULL);

	machine_account = ldb_msg_find_string(msgs[0], "samAccountName", NULL);

	if (!machine_account) {
		DEBUG(1, ("Could not find 'samAccountName' in join record to domain: %s\n",
			  cli_credentials_get_domain(cred)));
		talloc_free(mem_ctx);
		return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
	}
	
	sct = ldb_msg_find_int(msgs[0], "secureChannelType", 0);
	if (!sct) { 
		DEBUG(1, ("Domain join for acocunt %s did not have a secureChannelType set!\n",
			  machine_account));
		return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
	}
	
	if (!password) {
		const struct ldb_val *nt_password_hash = ldb_msg_find_ldb_val(msgs[0], "ntPwdHash");
		struct samr_Password hash;
		ZERO_STRUCT(hash);
		if (nt_password_hash) {
			memcpy(hash.hash, nt_password_hash->data, 
			       MIN(nt_password_hash->length, sizeof(hash.hash)));
		
			cli_credentials_set_nt_hash(cred, &hash, CRED_SPECIFIED);
		} else {
		
			DEBUG(1, ("Could not find 'secret' in join record to domain: %s\n",
				  cli_credentials_get_domain(cred)));
			talloc_free(mem_ctx);
			return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
		}
	}
	
	cli_credentials_set_secure_channel_type(cred, sct);

	domain = ldb_msg_find_string(msgs[0], "flatname", NULL);
	if (domain) {
		cli_credentials_set_domain(cred, domain, CRED_SPECIFIED);
	}

	realm = ldb_msg_find_string(msgs[0], "realm", NULL);
	if (realm) {
		cli_credentials_set_realm(cred, realm, CRED_SPECIFIED);
	}

	cli_credentials_set_username(cred, machine_account, CRED_SPECIFIED);
	if (password) {
		cli_credentials_set_password(cred, password, CRED_SPECIFIED);
	}

	cli_credentials_set_kvno(cred, ldb_msg_find_int(msgs[0], "msDS-KeyVersionNumber", 0));
	
	talloc_free(mem_ctx);
	
	return NT_STATUS_OK;
}

/**
 * Ask that when required, the credentials system will be filled with
 * machine trust account, from the secrets database.
 * 
 * @param cred Credentials structure to fill in
 * @note This function is used to call the above function after, rather 
 *       than during, popt processing.
 *
 */
void cli_credentials_set_machine_account_pending(struct cli_credentials *cred)
{
	cred->machine_account_pending = True;
}

