/* 
   Unix SMB/CIFS mplementation.

   LDAP bind calls
   
   Copyright (C) Andrew Tridgell  2005
   Copyright (C) Volker Lendecke  2004
    
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
#include "libcli/ldap/ldap.h"
#include "libcli/ldap/ldap_client.h"
#include "auth/auth.h"

static struct ldap_message *new_ldap_simple_bind_msg(struct ldap_connection *conn, 
						     const char *dn, const char *pw)
{
	struct ldap_message *res;

	res = new_ldap_message(conn);
	if (!res) {
		return NULL;
	}

	res->type = LDAP_TAG_BindRequest;
	res->r.BindRequest.version = 3;
	res->r.BindRequest.dn = talloc_strdup(res, dn);
	res->r.BindRequest.mechanism = LDAP_AUTH_MECH_SIMPLE;
	res->r.BindRequest.creds.password = talloc_strdup(res, pw);

	return res;
}


/*
  perform a simple username/password bind
*/
NTSTATUS ldap_bind_simple(struct ldap_connection *conn, 
			  const char *userdn, const char *password)
{
	struct ldap_request *req;
	struct ldap_message *msg;
	const char *dn, *pw;
	NTSTATUS status;

	if (conn == NULL) {
		return NT_STATUS_INVALID_CONNECTION;
	}

	if (userdn) {
		dn = userdn;
	} else {
		if (conn->auth_dn) {
			dn = conn->auth_dn;
		} else {
			dn = "";
		}
	}

	if (password) {
		pw = password;
	} else {
		if (conn->simple_pw) {
			pw = conn->simple_pw;
		} else {
			pw = "";
		}
	}

	msg = new_ldap_simple_bind_msg(conn, dn, pw);
	NT_STATUS_HAVE_NO_MEMORY(msg);

	/* send the request */
	req = ldap_request_send(conn, msg);
	talloc_free(msg);
	NT_STATUS_HAVE_NO_MEMORY(req);

	/* wait for replies */
	status = ldap_request_wait(req);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(req);
		return status;
	}

	/* check its a valid reply */
	msg = req->replies[0];
	if (msg->type != LDAP_TAG_BindResponse) {
		talloc_free(req);
		return NT_STATUS_UNEXPECTED_NETWORK_ERROR;
	}

	status = ldap_check_response(conn, &msg->r.BindResponse.response);

	talloc_free(req);

	return status;
}


static struct ldap_message *new_ldap_sasl_bind_msg(struct ldap_connection *conn, 
						   const char *sasl_mechanism, 
						   DATA_BLOB *secblob)
{
	struct ldap_message *res;

	res = new_ldap_message(conn);
	if (!res) {
		return NULL;
	}

	res->type = LDAP_TAG_BindRequest;
	res->r.BindRequest.version = 3;
	res->r.BindRequest.dn = "";
	res->r.BindRequest.mechanism = LDAP_AUTH_MECH_SASL;
	res->r.BindRequest.creds.SASL.mechanism = talloc_strdup(res, sasl_mechanism);
	res->r.BindRequest.creds.SASL.secblob = *secblob;

	return res;
}


/*
  perform a sasl bind using the given credentials
*/
NTSTATUS ldap_bind_sasl(struct ldap_connection *conn, struct cli_credentials *creds)
{
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx = NULL;
	DATA_BLOB input = data_blob(NULL, 0);
	DATA_BLOB output = data_blob(NULL, 0);

	status = gensec_client_start(conn, &conn->gensec);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(0, ("Failed to start GENSEC engine (%s)\n", nt_errstr(status)));
		goto failed;
	}

	gensec_want_feature(conn->gensec, 0 | GENSEC_FEATURE_SIGN | GENSEC_FEATURE_SEAL);

	status = gensec_set_credentials(conn->gensec, creds);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(1, ("Failed to start set GENSEC creds: %s\n", 
			  nt_errstr(status)));
		goto failed;
	}

	status = gensec_set_target_hostname(conn->gensec, conn->host);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(1, ("Failed to start set GENSEC target hostname: %s\n", 
			  nt_errstr(status)));
		goto failed;
	}

	status = gensec_set_target_service(conn->gensec, "ldap");
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(1, ("Failed to start set GENSEC target service: %s\n", 
			  nt_errstr(status)));
		goto failed;
	}

	status = gensec_start_mech_by_sasl_name(conn->gensec, "NTLM");
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(1, ("Failed to start set GENSEC client SPNEGO mechanism: %s\n",
			  nt_errstr(status)));
		goto failed;
	}

	tmp_ctx = talloc_new(conn);
	if (tmp_ctx == NULL) goto failed;

	status = gensec_update(conn->gensec, tmp_ctx, input, &output);

	while (1) {
		struct ldap_message *response;
		struct ldap_message *msg;
		struct ldap_request *req;
		int result = LDAP_OTHER;
	
		if (NT_STATUS_IS_OK(status) && output.length == 0) {
			break;
		}
		if (!NT_STATUS_EQUAL(status, NT_STATUS_MORE_PROCESSING_REQUIRED) && 
		    !NT_STATUS_IS_OK(status)) {
			break;
		}

		msg = new_ldap_sasl_bind_msg(tmp_ctx, "GSS-SPNEGO", &output);
		if (msg == NULL) {
			status = NT_STATUS_NO_MEMORY;
			goto failed;
		}

		req = ldap_request_send(conn, msg);
		if (req == NULL) {
			status = NT_STATUS_NO_MEMORY;
			goto failed;
		}
		talloc_steal(tmp_ctx, req);

		status = ldap_result_n(req, 0, &response);
		if (!NT_STATUS_IS_OK(status)) {
			goto failed;
		}
		
		if (response->type != LDAP_TAG_BindResponse) {
			status = NT_STATUS_UNEXPECTED_NETWORK_ERROR;
			goto failed;
		}

		result = response->r.BindResponse.response.resultcode;

		if (result != LDAP_SUCCESS && result != LDAP_SASL_BIND_IN_PROGRESS) {
			break;
		}

		status = gensec_update(conn->gensec, tmp_ctx,
				       response->r.BindResponse.SASL.secblob,
				       &output);
	}

	if (NT_STATUS_IS_OK(status) &&
	    (gensec_have_feature(conn->gensec, GENSEC_FEATURE_SIGN) ||
	     gensec_have_feature(conn->gensec, GENSEC_FEATURE_SIGN))) {
		conn->enable_wrap = True;
	}

	talloc_free(tmp_ctx);
	return status;

failed:
	talloc_free(tmp_ctx);
	talloc_free(conn->gensec);
	conn->gensec = NULL;
	return status;
}
