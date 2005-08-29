/* 
   samba -- Unix SMB/CIFS implementation.

   Client credentials structure

   Copyright (C) Jelmer Vernooij 2004-2005
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

/* In order of priority */
enum credentials_obtained { 
	CRED_UNINITIALISED = 0,  /* We don't even have a guess yet */
	CRED_GUESS_ENV,	         /* Current value should be used, which was guessed */
	CRED_CALLBACK, 		 /* Callback should be used to obtain value */
	CRED_GUESS_FILE,	 /* A guess from a file (or file pointed at in env variable) */
	CRED_SPECIFIED		 /* Was explicitly specified on the command-line */
};

struct cli_credentials {
	/* Preferred methods, NULL means default */
	const char **preferred_methods;

	enum credentials_obtained workstation_obtained;
	enum credentials_obtained username_obtained;
	enum credentials_obtained password_obtained;
	enum credentials_obtained domain_obtained;
	enum credentials_obtained realm_obtained;
	enum credentials_obtained ccache_obtained;
	enum credentials_obtained principal_obtained;

	const char *workstation;
	const char *username;
	const char *password;
	const char *domain;
	const char *realm;
	const char *principal;

	struct samr_Password *nt_hash;

	struct ccache_container *ccache;

	const char *(*workstation_cb) (struct cli_credentials *);
	const char *(*password_cb) (struct cli_credentials *);
	const char *(*username_cb) (struct cli_credentials *);
	const char *(*domain_cb) (struct cli_credentials *);
	const char *(*realm_cb) (struct cli_credentials *);
	const char *(*principal_cb) (struct cli_credentials *);

	/* Private handle for the callback routines to use */
	void *priv_data;

	struct creds_CredentialState *netlogon_creds;
	enum netr_SchannelType secure_channel_type;
	int kvno;

	/* We are flagged to get machine account details from the
	 * secrets.ldb when we are asked for a username or password */

	BOOL machine_account_pending;
};
