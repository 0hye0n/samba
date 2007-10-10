/* 
   Unix SMB/CIFS implementation.

   provide interfaces to libnet calls from ejs scripts

   Copyright (C) Rafal Szczesniak  2005
   
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
#include "scripting/ejs/smbcalls.h"
#include "scripting/ejs/ejsnet.h"
#include "libnet/libnet.h"


static int ejs_net_context(MprVarHandle eid, int argc, struct MprVar **argv)
{
	struct cli_credentials *creds;
	struct libnet_context *ctx;
	struct MprVar *obj;

	ctx = libnet_context_init(NULL);
	creds = talloc(ctx, struct cli_credentials);
	ctx->cred = creds;

	if (argc == 0) {
		cli_credentials_set_anonymous(creds);

	} else if (argc == 2 || argc == 4 ) {

		if (!mprVarIsString(argv[0]->type)) {
			ejsSetErrorMsg(eid, "argument 1 must be a string");
			goto done;
		}
		cli_credentials_set_username(creds, argv[0]->string, CRED_SPECIFIED);

		if (!mprVarIsString(argv[1]->type)) {
			ejsSetErrorMsg(eid, "argument 2 must be a string");
			goto done;
		}
		cli_credentials_set_password(creds, argv[1]->string, CRED_SPECIFIED);

	} else {
		ejsSetErrorMsg(eid, "invalid number of arguments");
		goto done;
	}

		
	if (argc == 4) {

		if (!mprVarIsString(argv[2]->type)) {
			ejsSetErrorMsg(eid, "argument 3 must be a string");
			goto done;
		}
		cli_credentials_set_domain(creds, argv[2]->string, CRED_SPECIFIED);
		
		if (!mprVarIsString(argv[3]->type)) {
			ejsSetErrorMsg(eid, "argument 4 must be a string");
			goto done;
		}
		cli_credentials_set_realm(creds, argv[3]->string, CRED_SPECIFIED);
	}

	obj = mprInitObject(eid, "NetCtx", argc, argv);

	mprSetStringCFunction(obj, "CreateUser", ejs_net_createuser);
	mprSetPtrChild(obj, "ctx", ctx);

	return 0;
done:
	talloc_free(ctx);
	return -1;
}


static int ejs_net_createuser(MprVarHandle eid, int argc, char **argv)
{
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;
	TALLOC_CTX *mem_ctx;
	struct libnet_context *ctx;
	struct libnet_CreateUser req;

	if (argc != 1) {
		ejsSetErrorMsg(eid, "argument 1 must be a string");
		goto done;
	}

	ctx = mprGetThisPtr(eid, "ctx");
	mem_ctx = talloc_init(NULL);
	
	req.in.domain_name = cli_credentials_get_domain(ctx->cred);
	req.in.user_name   = argv[0];

	status = libnet_CreateUser(ctx, mem_ctx, &req);
	if (!NT_STATUS_IS_OK(status)) {
		ejsSetErrorMsg(eid, "error when creating user: %s", nt_errstr(status));
	}

	talloc_free(mem_ctx);
	mpr_Return(eid, mprNTSTATUS(status));
	return 0;

done:
	talloc_free(mem_ctx);
	return -1;
}


void ejsnet_setup(void)
{
	ejsDefineCFunction(-1, "NetContext", ejs_net_context, NULL, MPR_VAR_SCRIPT_HANDLE);
}
