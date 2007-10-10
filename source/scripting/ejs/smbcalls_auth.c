/* 
   Unix SMB/CIFS implementation.

   ejs auth functions

   Copyright (C) Simo Sorce 2005
   Copyright (C) Andrew Tridgell 2005
   
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
#include "lib/appweb/ejs/ejs.h"
#include "auth/auth.h"
#include "scripting/ejs/smbcalls.h"

static int ejs_doauth(TALLOC_CTX *tmp_ctx, struct MprVar *auth, const char *username, 
		      const char *password, const char *domain, const char *remote_host,
		      const char *authtype)
{
	struct auth_usersupplied_info *user_info = NULL;
	struct auth_serversupplied_info *server_info = NULL;
	struct auth_context *auth_context;
	const char *auth_types[] = { authtype, NULL };
	NTSTATUS nt_status;
	DATA_BLOB pw_blob;

	/*
	  darn, we need some way to get the right event_context here
	*/
	nt_status = auth_context_create(tmp_ctx, auth_types, &auth_context, NULL);
	if (!NT_STATUS_IS_OK(nt_status)) {
		mprSetPropertyValue(auth, "result", mprCreateBoolVar(False));
		mprSetPropertyValue(auth, "report", mprString("Auth System Failure"));
		goto done;
	}

	pw_blob = data_blob(password, strlen(password)+1);
	
	user_info = talloc(tmp_ctx, struct auth_usersupplied_info);
	if (!user_info) {
		mprSetPropertyValue(auth, "result", mprCreateBoolVar(False));
		mprSetPropertyValue(auth, "report", mprString("talloc failed"));
		goto done;
	}

	user_info->mapped_state = True;
	user_info->client.account_name = username;
	user_info->mapped.account_name = username;
	user_info->client.domain_name = domain;
	user_info->mapped.domain_name = domain;

	user_info->workstation_name = remote_host;

	user_info->remote_host = remote_host;

	user_info->password_state = AUTH_PASSWORD_PLAIN;
	user_info->password.plaintext = talloc_strdup(user_info, password);

	user_info->flags = USER_INFO_CASE_INSENSITIVE_USERNAME |
		USER_INFO_DONT_CHECK_UNIX_ACCOUNT;

	nt_status = auth_check_password(auth_context, tmp_ctx, user_info, &server_info);
	if (!NT_STATUS_IS_OK(nt_status)) {
		mprSetPropertyValue(auth, "result", mprCreateBoolVar(False));
		mprSetPropertyValue(auth, "report", mprString("Login Failed"));
		goto done;
	}

	mprSetPropertyValue(auth, "result", mprCreateBoolVar(server_info->authenticated));
	mprSetPropertyValue(auth, "username", mprString(server_info->account_name));
	mprSetPropertyValue(auth, "domain", mprString(server_info->domain_name));

done:
	return 0;
}

/*
  perform user authentication, returning an array of results

  syntax:
    var authinfo = new Object();
    authinfo.username = myname;
    authinfo.password = mypass;
    authinfo.domain = mydom;
    authinfo.rhost = request['REMOTE_HOST'];
    auth = userAuth(authinfo);
*/
static int ejs_userAuth(MprVarHandle eid, int argc, struct MprVar **argv)
{
	TALLOC_CTX *tmp_ctx;
	const char *username;
	const char *password;
	const char *domain;
	const char *remote_host;
	struct MprVar auth;
	struct cli_credentials *creds;

	if (argc != 1 || argv[0]->type != MPR_TYPE_OBJECT) {
		ejsSetErrorMsg(eid, "userAuth invalid arguments, this function requires an object.");
		return -1;
	}

	/* get credential values from credentials object */
	creds = mprGetPtr(argv[0], "creds");
	if (creds == NULL) {
		ejsSetErrorMsg(eid, "userAuth requires a 'creds' element");
		return -1;
	}
	username    = cli_credentials_get_username(creds, tmp_ctx);
	password    = cli_credentials_get_password(creds);
	domain      = cli_credentials_get_domain(creds);
	remote_host = cli_credentials_get_workstation(creds);

	if (username == NULL || password == NULL || domain == NULL) {
		mpr_Return(eid, mprCreateUndefinedVar());
		return 0;
	}

 	tmp_ctx = talloc_new(mprMemCtx());	
	auth = mprObject("auth");

	if (domain && strcmp("System User", domain) == 0) {
		ejs_doauth(tmp_ctx, &auth, username, password, domain, remote_host, "unix");
	} else {
		ejs_doauth(tmp_ctx, &auth, username, password, domain, remote_host, "sam");
	}

	mpr_Return(eid, auth);
	talloc_free(tmp_ctx);
	return 0;
}

/*
  setup C functions that be called from ejs
*/
void smb_setup_ejs_auth(void)
{
	ejsDefineCFunction(-1, "userAuth", ejs_userAuth, NULL, MPR_VAR_SCRIPT_HANDLE);
}
