/* 
   Unix SMB/CIFS implementation.
   handle SMBsessionsetup
   Copyright (C) Andrew Tridgell 1998-2001
   Copyright (C) Andrew Bartlett      2001
   Copyright (C) Jim McDonough        2002
   Copyright (C) Luke Howard          2003

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
#include "version.h"
#include "auth/auth.h"
#include "smb_server/smb_server.h"
#include "smbd/service_stream.h"


/*
  setup the OS, Lanman and domain portions of a session setup reply
*/
static void sesssetup_common_strings(struct smbsrv_request *req,
				     char **os, char **lanman, char **domain)
{
	(*os) = talloc_asprintf(req, "Unix");
	(*lanman) = talloc_asprintf(req, "Samba %s", SAMBA_VERSION_STRING);
	(*domain) = talloc_asprintf(req, "%s", lp_workgroup());
}


/*
  handler for old style session setup
*/
static NTSTATUS sesssetup_old(struct smbsrv_request *req, union smb_sesssetup *sess)
{
	NTSTATUS status;
	struct auth_usersupplied_info *user_info = NULL;
	struct auth_serversupplied_info *server_info = NULL;
	struct auth_session_info *session_info;
	char *remote_machine;
	TALLOC_CTX *mem_ctx;

	mem_ctx = talloc_named(req, 0, "OLD session setup");
	NT_STATUS_HAVE_NO_MEMORY(mem_ctx);

	if (!req->smb_conn->negotiate.done_sesssetup) {
		req->smb_conn->negotiate.max_send = sess->old.in.bufsize;
	}
	
	remote_machine = socket_get_peer_addr(req->smb_conn->connection->socket, mem_ctx);
	status = make_user_info_for_reply_enc(req->smb_conn, 
					      sess->old.in.user, sess->old.in.domain,
					      remote_machine,
					      sess->old.in.password,
					      data_blob(NULL, 0),
					      &user_info);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(mem_ctx);
		return NT_STATUS_ACCESS_DENIED;
	}

	status = auth_check_password(req->smb_conn->negotiate.auth_context,
				     mem_ctx, user_info, &server_info);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(mem_ctx);
		return auth_nt_status_squash(status);
	}

	/* This references server_info into session_info */
	status = auth_generate_session_info(req, server_info, &session_info);
	talloc_free(mem_ctx);
	if (!NT_STATUS_IS_OK(status)) {
		return auth_nt_status_squash(status);
	}

	sess->old.out.action = 0;
	sess->old.out.vuid = smbsrv_register_session(req->smb_conn, session_info, NULL);
	if (sess->old.out.vuid == UID_FIELD_INVALID) {
		return NT_STATUS_ACCESS_DENIED;
	}
	sesssetup_common_strings(req, 
				 &sess->old.out.os,
				 &sess->old.out.lanman,
				 &sess->old.out.domain);

	req->session = smbsrv_session_find(req->smb_conn, sess->old.out.vuid);

	return NT_STATUS_OK;
}


/*
  handler for NT1 style session setup
*/
static NTSTATUS sesssetup_nt1(struct smbsrv_request *req, union smb_sesssetup *sess)
{
	NTSTATUS status;
	struct auth_usersupplied_info *user_info = NULL;
	struct auth_serversupplied_info *server_info = NULL;
	struct auth_session_info *session_info;
	TALLOC_CTX *mem_ctx;
	
	mem_ctx = talloc_named(req, 0, "NT1 session setup");
	NT_STATUS_HAVE_NO_MEMORY(mem_ctx);

	if (!req->smb_conn->negotiate.done_sesssetup) {
		req->smb_conn->negotiate.max_send = sess->nt1.in.bufsize;
		req->smb_conn->negotiate.client_caps = sess->nt1.in.capabilities;
	}

	if (req->smb_conn->negotiate.spnego_negotiated) {
		struct auth_context *auth_context;

		if (sess->nt1.in.user && *sess->nt1.in.user) {
			return NT_STATUS_ACCESS_DENIED;
		}

		status = make_user_info_anonymous(req->smb_conn, &user_info);
		if (!NT_STATUS_IS_OK(status)) {
			talloc_free(mem_ctx);
			return status;
		}

		/* TODO: should we use just "anonymous" here? */
		status = auth_context_create(req->smb_conn, lp_auth_methods(), &auth_context);
		if (!NT_STATUS_IS_OK(status)) {
			talloc_free(mem_ctx);
			return status;
		}

		status = auth_check_password(auth_context, mem_ctx,
					     user_info, &server_info);
	} else {
		char *remote_machine;

		remote_machine = socket_get_peer_addr(req->smb_conn->connection->socket, mem_ctx);

		status = make_user_info_for_reply_enc(req->smb_conn, 
						      sess->nt1.in.user, sess->nt1.in.domain,
						      remote_machine,
						      sess->nt1.in.password1,
						      sess->nt1.in.password2,
						      &user_info);
		if (!NT_STATUS_IS_OK(status)) {
			talloc_free(mem_ctx);
			return NT_STATUS_ACCESS_DENIED;
		}
		
		status = auth_check_password(req->smb_conn->negotiate.auth_context, 
					     req, user_info, &server_info);
	}

	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(mem_ctx);
		return auth_nt_status_squash(status);
	}

	/* This references server_info into session_info */
	status = auth_generate_session_info(mem_ctx, server_info, &session_info);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(mem_ctx);
		return auth_nt_status_squash(status);
	}

	sess->nt1.out.action = 0;
	sess->nt1.out.vuid = smbsrv_register_session(req->smb_conn, session_info, NULL);
	talloc_free(mem_ctx);
	if (sess->nt1.out.vuid == UID_FIELD_INVALID) {
		return NT_STATUS_ACCESS_DENIED;
	}
	sesssetup_common_strings(req, 
				 &sess->nt1.out.os,
				 &sess->nt1.out.lanman,
				 &sess->nt1.out.domain);
	
	req->session = smbsrv_session_find(req->smb_conn, sess->nt1.out.vuid);
	if (!session_info->server_info->authenticated) {
		return NT_STATUS_OK;
	}

	if (!srv_setup_signing(req->smb_conn, &session_info->session_key, &sess->nt1.in.password2)) {
		/* Already signing, or disabled */
		return NT_STATUS_OK;
	}

	/* Force check of the request packet, now we know the session key */
	req_signing_check_incoming(req);

	srv_signing_restart(req->smb_conn,  &session_info->session_key, &sess->nt1.in.password2);

	return NT_STATUS_OK;
}


/*
  handler for SPNEGO style session setup
*/
static NTSTATUS sesssetup_spnego(struct smbsrv_request *req, union smb_sesssetup *sess)
{
	NTSTATUS status = NT_STATUS_ACCESS_DENIED;
	struct smbsrv_session *smb_sess;
	struct gensec_security *gensec_ctx = NULL;
	struct auth_session_info *session_info = NULL;
	uint16_t vuid;

	if (!req->smb_conn->negotiate.done_sesssetup) {
		req->smb_conn->negotiate.max_send = sess->nt1.in.bufsize;
		req->smb_conn->negotiate.client_caps = sess->nt1.in.capabilities;
	}

	vuid = SVAL(req->in.hdr,HDR_UID);
	smb_sess = smbsrv_session_find(req->smb_conn, vuid);
	if (smb_sess && !smb_sess->session_info) {
		if (!smb_sess->gensec_ctx) {
			return NT_STATUS_INVALID_HANDLE;
		}

		status = gensec_update(smb_sess->gensec_ctx, req, sess->spnego.in.secblob, &sess->spnego.out.secblob);
	} else {
		smb_sess = NULL;

		status = gensec_server_start(req->smb_conn, &gensec_ctx);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(1, ("Failed to start GENSEC server code: %s\n", nt_errstr(status)));
			return status;
		}

		gensec_set_target_service(gensec_ctx, "cifs");

		gensec_want_feature(gensec_ctx, GENSEC_FEATURE_SESSION_KEY);

		status = gensec_start_mech_by_oid(gensec_ctx, GENSEC_OID_SPNEGO);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(1, ("Failed to start GENSEC SPNEGO server code: %s\n", nt_errstr(status)));
			return status;
		}

		status = gensec_update(gensec_ctx, req, sess->spnego.in.secblob, &sess->spnego.out.secblob);

	}

	if (!smb_sess) {
		vuid = smbsrv_register_session(req->smb_conn, 
					       session_info, gensec_ctx);
		if (vuid == UID_FIELD_INVALID) {
			return NT_STATUS_ACCESS_DENIED;
		}
		smb_sess = smbsrv_session_find(req->smb_conn, vuid);
		if (!smb_sess) {
			return NT_STATUS_FOOBAR;
		}
	}

	if (NT_STATUS_IS_OK(status)) {
		DATA_BLOB session_key;
		
		status = gensec_session_info(smb_sess->gensec_ctx, &smb_sess->session_info);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
		
		status = gensec_session_key(smb_sess->gensec_ctx, 
					    &session_key);
		if (NT_STATUS_IS_OK(status) 
		    && smb_sess->session_info->server_info->authenticated
		    && srv_setup_signing(req->smb_conn, &session_key, NULL)) {
			/* Force check of the request packet, now we know the session key */
			req_signing_check_incoming(req);

			srv_signing_restart(req->smb_conn, &session_key, NULL);

		}
	} else {
		status = auth_nt_status_squash(status);
		if (smb_sess->gensec_ctx && 
		    !NT_STATUS_EQUAL(status, NT_STATUS_MORE_PROCESSING_REQUIRED)) {
			talloc_free(smb_sess->gensec_ctx);
			smb_sess->gensec_ctx = NULL;
		}
	}

	sess->spnego.out.action = 0;
	sess->spnego.out.vuid = vuid;
	sesssetup_common_strings(req, 
				 &sess->spnego.out.os,
				 &sess->spnego.out.lanman,
				 &sess->spnego.out.workgroup);

	return status;
}

/*
  backend for sessionsetup call - this takes all 3 variants of the call
*/
NTSTATUS sesssetup_backend(struct smbsrv_request *req, 
			   union smb_sesssetup *sess)
{
	NTSTATUS status = NT_STATUS_INVALID_LEVEL;

	switch (sess->old.level) {
		case RAW_SESSSETUP_OLD:
			status = sesssetup_old(req, sess);
			break;
		case RAW_SESSSETUP_NT1:
			status = sesssetup_nt1(req, sess);
			break;
		case RAW_SESSSETUP_SPNEGO:
			status = sesssetup_spnego(req, sess);
			break;
	}

	if (NT_STATUS_IS_OK(status)) {
		req->smb_conn->negotiate.done_sesssetup = True;
	}

	return status;
}


