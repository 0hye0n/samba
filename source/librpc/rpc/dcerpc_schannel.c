/* 
   Unix SMB/CIFS implementation.

   dcerpc schannel operations

   Copyright (C) Andrew Tridgell 2004
   
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

/*
  wrappers for the schannel_*() functions
*/
static NTSTATUS schan_unseal_packet(struct dcerpc_security *dcerpc_security, 
				  uchar *data, size_t length, DATA_BLOB *sig)
{
	struct schannel_state *schannel_state = dcerpc_security->private;
	return schannel_unseal_packet(schannel_state, data, length, sig);
}

static NTSTATUS schan_check_packet(struct dcerpc_security *dcerpc_security, 
				  const uchar *data, size_t length, 
				  const DATA_BLOB *sig)
{
	struct schannel_state *schannel_state = dcerpc_security->private;
	return schannel_check_packet(schannel_state, data, length, sig);
}

static NTSTATUS schan_seal_packet(struct dcerpc_security *dcerpc_security, 
				 uchar *data, size_t length, 
				 DATA_BLOB *sig)
{
	struct schannel_state *schannel_state = dcerpc_security->private;
	return schannel_seal_packet(schannel_state, data, length, sig);
}

static NTSTATUS schan_sign_packet(struct dcerpc_security *dcerpc_security, 
				 const uchar *data, size_t length, 
				 DATA_BLOB *sig)
{
	struct schannel_state *schannel_state = dcerpc_security->private;
	return schannel_sign_packet(schannel_state, data, length, sig);
}

static void schan_security_end(struct dcerpc_security *dcerpc_security)
{
	struct schannel_state *schannel_state = dcerpc_security->private;
	return schannel_end(&schannel_state);
}


/*
  do a schannel style bind on a dcerpc pipe. The username is usually
  of the form HOSTNAME$ and the password is the domain trust password
*/
NTSTATUS dcerpc_bind_auth_schannel(struct dcerpc_pipe *p,
				   const char *uuid, unsigned version,
				   const char *domain,
				   const char *username,
				   const char *password)
{
	NTSTATUS status;
	struct dcerpc_pipe *p2;
	struct netr_ServerReqChallenge r;
	struct netr_ServerAuthenticate2 a;
	uint8 mach_pwd[16];
	uint8 session_key[16];
	struct netr_CredentialState creds;
	struct schannel_state *schannel_state;
	const char *workgroup, *workstation;
	uint32 negotiate_flags = 0;

	workstation = username;
	workgroup = domain;

	/*
	  step 1 - establish a netlogon connection, with no authentication
	*/
	status = dcerpc_secondary_smb(p, &p2, 
				      DCERPC_NETLOGON_NAME, 
				      DCERPC_NETLOGON_UUID, 
				      DCERPC_NETLOGON_VERSION);


	/*
	  step 2 - request a netlogon challenge
	*/
	r.in.server_name = talloc_asprintf(p->mem_ctx, "\\\\%s", dcerpc_server_name(p));
	r.in.computer_name = workstation;
	generate_random_buffer(r.in.credentials.data, sizeof(r.in.credentials.data), False);

	status = dcerpc_netr_ServerReqChallenge(p2, p->mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/*
	  step 3 - authenticate on the netlogon pipe
	*/
	E_md4hash(password, mach_pwd);
	creds_client_init(&creds, &r.in.credentials, &r.out.credentials, mach_pwd,
			  &a.in.credentials);

	a.in.server_name = r.in.server_name;
	a.in.username = talloc_asprintf(p->mem_ctx, "%s$", workstation);
	if (lp_server_role() == ROLE_DOMAIN_BDC) {
		a.in.secure_channel_type = SEC_CHAN_BDC;
	} else {
		a.in.secure_channel_type = SEC_CHAN_WKSTA;
	}
	a.in.computer_name = workstation;
	a.in.negotiate_flags = &negotiate_flags;
	a.out.negotiate_flags = &negotiate_flags;

	status = dcerpc_netr_ServerAuthenticate2(p2, p->mem_ctx, &a);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	if (!creds_client_check(&creds, &a.out.credentials)) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	/*
	  the schannel session key is now in creds.session_key
	*/


	/*
	  step 4 - perform a bind with security type schannel
	*/
	p->auth_info = talloc(p->mem_ctx, sizeof(*p->auth_info));
	if (!p->auth_info) {
		status = NT_STATUS_NO_MEMORY;
		goto done;
	}

	p->auth_info->auth_type = DCERPC_AUTH_TYPE_SCHANNEL;
	
	if (p->flags & DCERPC_SEAL) {
		p->auth_info->auth_level = DCERPC_AUTH_LEVEL_PRIVACY;
	} else {
		/* note that DCERPC_AUTH_LEVEL_NONE does not make any 
		   sense, and would be rejected by the server */
		p->auth_info->auth_level = DCERPC_AUTH_LEVEL_INTEGRITY;
	}
	p->auth_info->auth_pad_length = 0;
	p->auth_info->auth_reserved = 0;
	p->auth_info->auth_context_id = 1;
	p->security_state = NULL;

	p->auth_info->credentials = data_blob_talloc(p->mem_ctx, 
						     NULL,
						     8 +
						     strlen(workgroup)+1 +
						     strlen(workstation)+1);
	if (!p->auth_info->credentials.data) {
		return NT_STATUS_NO_MEMORY;
	}

	/* oh, this is ugly! */
	SIVAL(p->auth_info->credentials.data, 0, 0);
	SIVAL(p->auth_info->credentials.data, 4, 3);
	memcpy(p->auth_info->credentials.data+8, workgroup, strlen(workgroup)+1);
	memcpy(p->auth_info->credentials.data+8+strlen(workgroup)+1, 
	       workstation, strlen(workstation)+1);

	/* send the authenticated bind request */
	status = dcerpc_bind_byuuid(p, p->mem_ctx, uuid, version);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	p->security_state = talloc_p(p->mem_ctx, struct dcerpc_security);
	if (!p->security_state) {
		status = NT_STATUS_NO_MEMORY;
		goto done;
	}

	schannel_state = talloc_p(p->mem_ctx, struct schannel_state);
	if (!schannel_state) {
		status = NT_STATUS_NO_MEMORY;
		goto done;
	}

	memcpy(session_key, creds.session_key, 8);
	memset(session_key+8, 0, 8);

	status = schannel_start(&schannel_state, session_key, True);
	if (!NT_STATUS_IS_OK(status)) {
		goto done;
	}

	dump_data_pw("session key:\n", schannel_state->session_key, 16);

	p->security_state->private = schannel_state;
	p->security_state->unseal_packet = schan_unseal_packet;
	p->security_state->check_packet = schan_check_packet;
	p->security_state->seal_packet = schan_seal_packet;
	p->security_state->sign_packet = schan_sign_packet;
	p->security_state->security_end = schan_security_end;

done:
	return status;
}

