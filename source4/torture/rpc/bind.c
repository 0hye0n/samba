/* 
   Unix SMB/CIFS implementation.

   dcerpc torture tests

   Copyright (C) Andrew Tridgell 2003
   Copyright (C) Andrew Bartlett <abartlet@samba.org 2004

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
#include "librpc/gen_ndr/ndr_lsa.h"

/*
  This test is 'bogus' in that it doesn't actually perform to the
  spec.  We need to deal with other things inside the DCERPC layer,
  before we could have multiple binds.

  We should never pass this test, until such details are fixed in our
  client, and it looks like multible binds are never used anyway.

*/

BOOL torture_multi_bind(void) 
{
	struct dcerpc_pipe *p;
	const char *domain = lp_parm_string(-1, "torture", "userdomain");
	const char *username = lp_parm_string(-1, "torture", "username");
	const char *password = lp_parm_string(-1, "torture", "password");
	const char *pipe_uuid = DCERPC_LSARPC_UUID;
	uint32_t pipe_version = DCERPC_LSARPC_VERSION;
	struct dcerpc_binding b;
	struct dcerpc_binding *binding;
	const char *binding_string = lp_parm_string(-1, "torture", "binding");
	TALLOC_CTX *mem_ctx;
	NTSTATUS status;
	BOOL ret;

	mem_ctx = talloc_init("torture_multi_bind");

	status = dcerpc_parse_binding(mem_ctx, binding_string, &b);
	if (!NT_STATUS_IS_OK(status)) {
		printf("Failed to parse dcerpc binding '%s'\n", binding_string);
		talloc_free(mem_ctx);
		return False;
	}

	binding = &b;

	status = torture_rpc_connection(&p, 
					NULL,
					pipe_uuid,
					pipe_version);
	
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	if (username && username[0] && (binding->flags & DCERPC_SCHANNEL_ANY)) {
		status = dcerpc_bind_auth_schannel(p, pipe_uuid, pipe_version, 
						   domain, username, password);
	} else if (username && username[0]) {
		uint8_t auth_type;
		if (binding->flags & DCERPC_AUTH_SPNEGO) {
			auth_type = DCERPC_AUTH_TYPE_SPNEGO;
		} else {
			auth_type = DCERPC_AUTH_TYPE_NTLMSSP;
		}

		status = dcerpc_bind_auth_password(p, pipe_uuid, pipe_version, 
						   domain, username, password, 
						   auth_type,
						   binding->authservice);
	} else {
		status = dcerpc_bind_auth_none(p, pipe_uuid, pipe_version);
	}

	if (NT_STATUS_IS_OK(status)) {
		printf("(incorrectly) allowed re-bind to uuid %s - %s\n", 
			pipe_uuid, nt_errstr(status));
		ret = False;
	} else {
		printf("\n");
		ret = True;
	}

	talloc_free(mem_ctx);
	torture_rpc_close(p);

	return ret;
}
