/* 
   Unix SMB/CIFS implementation.

   Kerberos backend for GENSEC
   
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2004-2005
   Copyright (C) Stefan Metzmacher <metze@samba.org> 2004-2005

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
#include "system/network.h"
#include "auth/kerberos/kerberos.h"
#include "librpc/gen_ndr/ndr_krb5pac.h"
#include "auth/auth.h"

struct gensec_gssapi_state {
	gss_ctx_id_t gssapi_context;
	struct gss_channel_bindings_struct *input_chan_bindings;
	gss_name_t server_name;
	gss_name_t client_name;
	OM_uint32 want_flags, got_flags;
	const gss_OID_desc *gss_oid;

	DATA_BLOB session_key;
	DATA_BLOB pac;

	struct smb_krb5_context *smb_krb5_context;
	krb5_ccache ccache;
	const char *ccache_name;
	krb5_keytab keytab;

	gss_cred_id_t cred;
};

static char *gssapi_error_string(TALLOC_CTX *mem_ctx, 
				 OM_uint32 maj_stat, OM_uint32 min_stat)
{
	OM_uint32 disp_min_stat, disp_maj_stat;
	gss_buffer_desc maj_error_message;
	gss_buffer_desc min_error_message;
	OM_uint32 msg_ctx = 0;

	char *ret;

	maj_error_message.value = NULL;
	min_error_message.value = NULL;
	
	disp_maj_stat = gss_display_status(&disp_min_stat, maj_stat, GSS_C_GSS_CODE,
			   GSS_C_NULL_OID, &msg_ctx, &maj_error_message);
	disp_maj_stat = gss_display_status(&disp_min_stat, min_stat, GSS_C_MECH_CODE,
			   GSS_C_NULL_OID, &msg_ctx, &min_error_message);
	ret = talloc_asprintf(mem_ctx, "%s: %s", (char *)maj_error_message.value, (char *)min_error_message.value);

	gss_release_buffer(&disp_min_stat, &maj_error_message);
	gss_release_buffer(&disp_min_stat, &min_error_message);

	return ret;
}


static int gensec_gssapi_destory(void *ptr) 
{
	struct gensec_gssapi_state *gensec_gssapi_state = ptr;
	OM_uint32 maj_stat, min_stat;
	
	if (gensec_gssapi_state->cred != GSS_C_NO_CREDENTIAL) {
		maj_stat = gss_release_cred(&min_stat, 
					    &gensec_gssapi_state->cred);
	}

	if (gensec_gssapi_state->gssapi_context != GSS_C_NO_CONTEXT) {
		maj_stat = gss_delete_sec_context (&min_stat,
						   &gensec_gssapi_state->gssapi_context,
						   GSS_C_NO_BUFFER);
	}

	if (gensec_gssapi_state->server_name != GSS_C_NO_NAME) {
		maj_stat = gss_release_name(&min_stat, &gensec_gssapi_state->server_name);
	}
	if (gensec_gssapi_state->client_name != GSS_C_NO_NAME) {
		maj_stat = gss_release_name(&min_stat, &gensec_gssapi_state->client_name);
	}
	return 0;
}

static NTSTATUS gensec_gssapi_start(struct gensec_security *gensec_security)
{
	struct gensec_gssapi_state *gensec_gssapi_state;
	krb5_error_code ret;
	
	gensec_gssapi_state = talloc(gensec_security, struct gensec_gssapi_state);
	if (!gensec_gssapi_state) {
		return NT_STATUS_NO_MEMORY;
	}

	gensec_security->private_data = gensec_gssapi_state;

	gensec_gssapi_state->gssapi_context = GSS_C_NO_CONTEXT;
	gensec_gssapi_state->server_name = GSS_C_NO_NAME;
	gensec_gssapi_state->client_name = GSS_C_NO_NAME;

	/* TODO: Fill in channel bindings */
	gensec_gssapi_state->input_chan_bindings = GSS_C_NO_CHANNEL_BINDINGS;
	
	gensec_gssapi_state->want_flags = 0;
	gensec_gssapi_state->got_flags = 0;

	gensec_gssapi_state->session_key = data_blob(NULL, 0);
	gensec_gssapi_state->pac = data_blob(NULL, 0);

	gensec_gssapi_state->cred = GSS_C_NO_CREDENTIAL;

	talloc_set_destructor(gensec_gssapi_state, gensec_gssapi_destory); 

	if (gensec_security->want_features & GENSEC_FEATURE_SIGN) {
		gensec_gssapi_state->want_flags |= GSS_C_INTEG_FLAG;
	}
	if (gensec_security->want_features & GENSEC_FEATURE_SEAL) {
		gensec_gssapi_state->want_flags |= GSS_C_CONF_FLAG;
	}
	if (gensec_security->want_features & GENSEC_FEATURE_DCE_STYLE) {
		gensec_gssapi_state->want_flags |= GSS_C_DCE_STYLE;
	}

	gensec_gssapi_state->gss_oid = gss_mech_krb5;
	
	ret = smb_krb5_init_context(gensec_gssapi_state, 
				    &gensec_gssapi_state->smb_krb5_context);
	if (ret) {
		DEBUG(1,("gensec_krb5_start: krb5_init_context failed (%s)\n", 					
			 error_message(ret)));
		return NT_STATUS_INTERNAL_ERROR;
	}
	return NT_STATUS_OK;
}

static NTSTATUS gensec_gssapi_server_start(struct gensec_security *gensec_security)
{
	NTSTATUS nt_status;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc name_token;
	struct gensec_gssapi_state *gensec_gssapi_state;
	struct cli_credentials *machine_account;

	nt_status = gensec_gssapi_start(gensec_security);
	if (!NT_STATUS_IS_OK(nt_status)) {
		return nt_status;
	}

	gensec_gssapi_state = gensec_security->private_data;

	machine_account = cli_credentials_init(gensec_gssapi_state);
	cli_credentials_set_conf(machine_account);
	nt_status = cli_credentials_set_machine_account(machine_account);
	
	if (!NT_STATUS_IS_OK(nt_status)) {
		DEBUG(3, ("Could not obtain machine account credentials from the local database\n"));
		talloc_free(machine_account);
		return nt_status;
	} else {
		nt_status = create_memory_keytab(gensec_gssapi_state,
						 machine_account, 
						 gensec_gssapi_state->smb_krb5_context,
						 &gensec_gssapi_state->keytab);
		if (!NT_STATUS_IS_OK(nt_status)) {
			DEBUG(3, ("Could not create memory keytab!\n"));
			talloc_free(machine_account);
			return nt_status;
		}
	}

	name_token.value = cli_credentials_get_principal(machine_account, 
							 machine_account);
	name_token.length = strlen(name_token.value);

	maj_stat = gss_import_name (&min_stat,
				    &name_token,
				    GSS_C_NT_USER_NAME,
				    &gensec_gssapi_state->server_name);
	talloc_free(machine_account);

	if (maj_stat) {
		DEBUG(2, ("GSS Import name of %s failed: %s\n",
			  (char *)name_token.value,
			  gssapi_error_string(gensec_gssapi_state, maj_stat, min_stat)));
		return NT_STATUS_UNSUCCESSFUL;
	}

	maj_stat = gsskrb5_acquire_cred(&min_stat, 
					gensec_gssapi_state->keytab, NULL,
					gensec_gssapi_state->server_name,
					GSS_C_INDEFINITE,
					GSS_C_NULL_OID_SET,
					GSS_C_ACCEPT,
					&gensec_gssapi_state->cred,
					NULL, 
					NULL);
	if (maj_stat) {
		DEBUG(1, ("Aquiring acceptor credentails failed: %s\n", 
			  gssapi_error_string(gensec_gssapi_state, maj_stat, min_stat)));
		return NT_STATUS_CANT_ACCESS_DOMAIN_INFO;
	}

	return NT_STATUS_OK;

}

static NTSTATUS gensec_gssapi_client_start(struct gensec_security *gensec_security)
{
	struct gensec_gssapi_state *gensec_gssapi_state;
	struct cli_credentials *creds = gensec_get_credentials(gensec_security);
	struct ccache_container *ccache;
	krb5_error_code ret;
	NTSTATUS nt_status;
	gss_buffer_desc name_token;
	OM_uint32 maj_stat, min_stat;
	const char *hostname = gensec_get_target_hostname(gensec_security);

	if (!hostname) {
		DEBUG(1, ("Could not determine hostname for target computer, cannot use kerberos\n"));
		return NT_STATUS_INVALID_PARAMETER;
	}
	if (is_ipaddress(hostname)) {
		DEBUG(2, ("Cannot do GSSAPI to a IP address"));
		return NT_STATUS_INVALID_PARAMETER;
	}

	nt_status = gensec_gssapi_start(gensec_security);
	if (!NT_STATUS_IS_OK(nt_status)) {
		return nt_status;
	}

	gensec_gssapi_state = gensec_security->private_data;

	name_token.value = talloc_asprintf(gensec_gssapi_state, "%s@%s", 
					   gensec_get_target_service(gensec_security), 
					   hostname);
	name_token.length = strlen(name_token.value);

	maj_stat = gss_import_name (&min_stat,
				    &name_token,
				    GSS_C_NT_HOSTBASED_SERVICE,
				    &gensec_gssapi_state->server_name);
	if (maj_stat) {
		DEBUG(2, ("GSS Import name of %s failed: %s\n",
			  (char *)name_token.value,
			  gssapi_error_string(gensec_gssapi_state, maj_stat, min_stat)));
		return NT_STATUS_INVALID_PARAMETER;
	}

	ret = cli_credentials_get_ccache(creds, 
					 &ccache);
	if (ret) {
		DEBUG(1, ("Failed to get CCACHE for gensec_gssapi: %s\n", error_message(ret)));
		return NT_STATUS_UNSUCCESSFUL;
	}

	name_token.value = cli_credentials_get_principal(creds, 
							 gensec_gssapi_state);
	name_token.length = strlen(name_token.value);

	maj_stat = gss_import_name (&min_stat,
				    &name_token,
				    GSS_C_NT_USER_NAME,
				    &gensec_gssapi_state->client_name);
	if (maj_stat) {
		DEBUG(2, ("GSS Import name of %s failed: %s\n",
			  (char *)name_token.value,
			  gssapi_error_string(gensec_gssapi_state, maj_stat, min_stat)));
		return NT_STATUS_UNSUCCESSFUL;
	}

	maj_stat = gsskrb5_acquire_cred(&min_stat, 
					NULL, ccache->ccache,
					gensec_gssapi_state->client_name,
					GSS_C_INDEFINITE,
					GSS_C_NULL_OID_SET,
					GSS_C_INITIATE,
					&gensec_gssapi_state->cred,
					NULL, 
					NULL);
	if (maj_stat) {
		DEBUG(1, ("Aquiring initiator credentails failed: %s\n", 
			  gssapi_error_string(gensec_gssapi_state, maj_stat, min_stat)));
		return NT_STATUS_UNSUCCESSFUL;
	}

	return NT_STATUS_OK;
}


/**
 * Check if the packet is one for this mechansim
 * 
 * @param gensec_security GENSEC state
 * @param in The request, as a DATA_BLOB
 * @return Error, INVALID_PARAMETER if it's not a packet for us
 *                or NT_STATUS_OK if the packet is ok. 
 */

static NTSTATUS gensec_gssapi_magic(struct gensec_security *gensec_security, 
				    const DATA_BLOB *in) 
{
	if (gensec_gssapi_check_oid(in, GENSEC_OID_KERBEROS5)) {
		return NT_STATUS_OK;
	} else {
		return NT_STATUS_INVALID_PARAMETER;
	}
}


/**
 * Next state function for the GSSAPI GENSEC mechanism
 * 
 * @param gensec_gssapi_state GSSAPI State
 * @param out_mem_ctx The TALLOC_CTX for *out to be allocated on
 * @param in The request, as a DATA_BLOB
 * @param out The reply, as an talloc()ed DATA_BLOB, on *out_mem_ctx
 * @return Error, MORE_PROCESSING_REQUIRED if a reply is sent, 
 *                or NT_STATUS_OK if the user is authenticated. 
 */

static NTSTATUS gensec_gssapi_update(struct gensec_security *gensec_security, 
				   TALLOC_CTX *out_mem_ctx, 
				   const DATA_BLOB in, DATA_BLOB *out) 
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	NTSTATUS nt_status = NT_STATUS_UNSUCCESSFUL;
	OM_uint32 maj_stat, min_stat;
	OM_uint32 min_stat2;
	gss_buffer_desc input_token, output_token;
	gss_OID gss_oid_p;
	input_token.length = in.length;
	input_token.value = in.data;

	switch (gensec_security->gensec_role) {
	case GENSEC_CLIENT:
	{
		maj_stat = gss_init_sec_context(&min_stat, 
						gensec_gssapi_state->cred,
						&gensec_gssapi_state->gssapi_context, 
						gensec_gssapi_state->server_name, 
						discard_const_p(gss_OID_desc, gensec_gssapi_state->gss_oid),
						gensec_gssapi_state->want_flags, 
						0, 
						gensec_gssapi_state->input_chan_bindings,
						&input_token, 
						NULL, 
						&output_token, 
						&gensec_gssapi_state->got_flags, /* ret flags */
						NULL);
		break;
	}
	case GENSEC_SERVER:
	{
		maj_stat = gss_accept_sec_context(&min_stat, 
						  &gensec_gssapi_state->gssapi_context, 
						  gensec_gssapi_state->cred,
						  &input_token, 
						  gensec_gssapi_state->input_chan_bindings,
						  &gensec_gssapi_state->client_name, 
						  &gss_oid_p,
						  &output_token, 
						  &gensec_gssapi_state->got_flags, 
						  NULL, 
						  NULL);
		gensec_gssapi_state->gss_oid = gss_oid_p;
		break;
	}
	default:
		return NT_STATUS_INVALID_PARAMETER;
		
	}

	*out = data_blob_talloc(out_mem_ctx, output_token.value, output_token.length);
	gss_release_buffer(&min_stat2, &output_token);

	if (maj_stat == GSS_S_COMPLETE) {
		return NT_STATUS_OK;
	} else if (maj_stat == GSS_S_CONTINUE_NEEDED) {
		return NT_STATUS_MORE_PROCESSING_REQUIRED;
	} else {
		if (maj_stat == GSS_S_FAILURE
		    && (min_stat == KRB5KRB_AP_ERR_BADVERSION || min_stat == KRB5KRB_AP_ERR_MSG_TYPE)) {
			/* garbage input, possibly from the auto-mech detection */
			return NT_STATUS_INVALID_PARAMETER;
		}
		DEBUG(1, ("GSS Update failed: %s\n", 
			  gssapi_error_string(out_mem_ctx, maj_stat, min_stat)));
		return nt_status;
	}
}

static NTSTATUS gensec_gssapi_wrap(struct gensec_security *gensec_security, 
				   TALLOC_CTX *mem_ctx, 
				   const DATA_BLOB *in, 
				   DATA_BLOB *out)
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc input_token, output_token;
	int conf_state;
	input_token.length = in->length;
	input_token.value = in->data;
	
	maj_stat = gss_wrap(&min_stat, 
			    gensec_gssapi_state->gssapi_context, 
			    gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL),
			    GSS_C_QOP_DEFAULT,
			    &input_token,
			    &conf_state,
			    &output_token);
	if (GSS_ERROR(maj_stat)) {
		DEBUG(1, ("GSS Wrap failed: %s\n", 
			  gssapi_error_string(mem_ctx, maj_stat, min_stat)));
		return NT_STATUS_ACCESS_DENIED;
	}
	*out = data_blob_talloc(mem_ctx, output_token.value, output_token.length);

	gss_release_buffer(&min_stat, &output_token);

	if (gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL)
	    && !conf_state) {
		return NT_STATUS_ACCESS_DENIED;
	}
	return NT_STATUS_OK;
}

static NTSTATUS gensec_gssapi_unwrap(struct gensec_security *gensec_security, 
				     TALLOC_CTX *mem_ctx, 
				     const DATA_BLOB *in, 
				     DATA_BLOB *out)
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc input_token, output_token;
	int conf_state;
	gss_qop_t qop_state;
	input_token.length = in->length;
	input_token.value = in->data;
	
	maj_stat = gss_unwrap(&min_stat, 
			      gensec_gssapi_state->gssapi_context, 
			      &input_token,
			      &output_token, 
			      &conf_state,
			      &qop_state);
	if (GSS_ERROR(maj_stat)) {
		DEBUG(1, ("GSS UnWrap failed: %s\n", 
			  gssapi_error_string(mem_ctx, maj_stat, min_stat)));
		return NT_STATUS_ACCESS_DENIED;
	}
	*out = data_blob_talloc(mem_ctx, output_token.value, output_token.length);

	gss_release_buffer(&min_stat, &output_token);
	
	if (gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL)
	    && !conf_state) {
		return NT_STATUS_ACCESS_DENIED;
	}
	return NT_STATUS_OK;
}

static size_t gensec_gssapi_sig_size(struct gensec_security *gensec_security) 
{
	/* not const but work for DCERPC packets and arcfour */
	return 45;
}

static NTSTATUS gensec_gssapi_seal_packet(struct gensec_security *gensec_security, 
					  TALLOC_CTX *mem_ctx, 
					  uint8_t *data, size_t length, 
					  const uint8_t *whole_pdu, size_t pdu_length, 
					  DATA_BLOB *sig)
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc input_token, output_token;
	int conf_state;
	ssize_t sig_length = 0;

	input_token.length = length;
	input_token.value = data;
	
	maj_stat = gss_wrap(&min_stat, 
			    gensec_gssapi_state->gssapi_context,
			    gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL),
			    GSS_C_QOP_DEFAULT,
			    &input_token,
			    &conf_state,
			    &output_token);
	if (GSS_ERROR(maj_stat)) {
		DEBUG(1, ("GSS Wrap failed: %s\n", 
			  gssapi_error_string(mem_ctx, maj_stat, min_stat)));
		return NT_STATUS_ACCESS_DENIED;
	}

	if (output_token.length < length) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	sig_length = 45;

	memcpy(data, ((uint8_t *)output_token.value) + sig_length, length);
	*sig = data_blob_talloc(mem_ctx, (uint8_t *)output_token.value, sig_length);

	dump_data_pw("gensec_gssapi_seal_packet: sig\n", sig->data, sig->length);
	dump_data_pw("gensec_gssapi_seal_packet: clear\n", data, length);
	dump_data_pw("gensec_gssapi_seal_packet: sealed\n", ((uint8_t *)output_token.value) + sig_length, output_token.length - sig_length);

	gss_release_buffer(&min_stat, &output_token);

	if (gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL)
	    && !conf_state) {
		return NT_STATUS_ACCESS_DENIED;
	}
	return NT_STATUS_OK;
}

static NTSTATUS gensec_gssapi_unseal_packet(struct gensec_security *gensec_security, 
					    TALLOC_CTX *mem_ctx, 
					    uint8_t *data, size_t length, 
					    const uint8_t *whole_pdu, size_t pdu_length,
					    const DATA_BLOB *sig)
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc input_token, output_token;
	int conf_state;
	gss_qop_t qop_state;
	DATA_BLOB in;

	dump_data_pw("gensec_gssapi_seal_packet: sig\n", sig->data, sig->length);

	in = data_blob_talloc(mem_ctx, NULL, sig->length + length);

	memcpy(in.data, sig->data, sig->length);
	memcpy(in.data + sig->length, data, length);

	input_token.length = in.length;
	input_token.value = in.data;
	
	maj_stat = gss_unwrap(&min_stat, 
			      gensec_gssapi_state->gssapi_context, 
			      &input_token,
			      &output_token, 
			      &conf_state,
			      &qop_state);
	if (GSS_ERROR(maj_stat)) {
		DEBUG(1, ("GSS UnWrap failed: %s\n", 
			  gssapi_error_string(mem_ctx, maj_stat, min_stat)));
		return NT_STATUS_ACCESS_DENIED;
	}

	if (output_token.length != length) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	memcpy(data, output_token.value, length);

	gss_release_buffer(&min_stat, &output_token);
	
	if (gensec_have_feature(gensec_security, GENSEC_FEATURE_SEAL)
	    && !conf_state) {
		return NT_STATUS_ACCESS_DENIED;
	}
	return NT_STATUS_OK;
}

static NTSTATUS gensec_gssapi_sign_packet(struct gensec_security *gensec_security, 
					  TALLOC_CTX *mem_ctx, 
					  const uint8_t *data, size_t length, 
					  const uint8_t *whole_pdu, size_t pdu_length, 
					  DATA_BLOB *sig)
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc input_token, output_token;
	int conf_state;
	ssize_t sig_length = 0;

	input_token.length = length;
	input_token.value = discard_const_p(uint8_t *, data);

	maj_stat = gss_wrap(&min_stat, 
			    gensec_gssapi_state->gssapi_context,
			    0,
			    GSS_C_QOP_DEFAULT,
			    &input_token,
			    &conf_state,
			    &output_token);
	if (GSS_ERROR(maj_stat)) {
		DEBUG(1, ("GSS Wrap failed: %s\n", 
			  gssapi_error_string(mem_ctx, maj_stat, min_stat)));
		return NT_STATUS_ACCESS_DENIED;
	}

	if (output_token.length < length) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	sig_length = 45;

	/*memcpy(data, ((uint8_t *)output_token.value) + sig_length, length);*/
	*sig = data_blob_talloc(mem_ctx, (uint8_t *)output_token.value, sig_length);

	dump_data_pw("gensec_gssapi_seal_packet: sig\n", sig->data, sig->length);

	gss_release_buffer(&min_stat, &output_token);

	return NT_STATUS_OK;
}

static NTSTATUS gensec_gssapi_check_packet(struct gensec_security *gensec_security, 
					   TALLOC_CTX *mem_ctx, 
					   const uint8_t *data, size_t length, 
					   const uint8_t *whole_pdu, size_t pdu_length, 
					   const DATA_BLOB *sig)
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc input_token, output_token;
	int conf_state;
	gss_qop_t qop_state;
	DATA_BLOB in;

	dump_data_pw("gensec_gssapi_seal_packet: sig\n", sig->data, sig->length);

	in = data_blob_talloc(mem_ctx, NULL, sig->length + length);

	memcpy(in.data, sig->data, sig->length);
	memcpy(in.data + sig->length, data, length);

	input_token.length = in.length;
	input_token.value = in.data;
	
	maj_stat = gss_unwrap(&min_stat, 
			      gensec_gssapi_state->gssapi_context, 
			      &input_token,
			      &output_token, 
			      &conf_state,
			      &qop_state);
	if (GSS_ERROR(maj_stat)) {
		DEBUG(1, ("GSS UnWrap failed: %s\n", 
			  gssapi_error_string(mem_ctx, maj_stat, min_stat)));
		return NT_STATUS_ACCESS_DENIED;
	}

	if (output_token.length != length) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	gss_release_buffer(&min_stat, &output_token);

	return NT_STATUS_OK;
}

static BOOL gensec_gssapi_have_feature(struct gensec_security *gensec_security, 
				       uint32_t feature) 
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	if (feature & GENSEC_FEATURE_SIGN) {
		return gensec_gssapi_state->got_flags & GSS_C_INTEG_FLAG;
	}
	if (feature & GENSEC_FEATURE_SEAL) {
		return gensec_gssapi_state->got_flags & GSS_C_CONF_FLAG;
	}
	if (feature & GENSEC_FEATURE_SESSION_KEY) {
		if ((gensec_gssapi_state->gss_oid->length == gss_mech_krb5->length)
		    && (memcmp(gensec_gssapi_state->gss_oid->elements, gss_mech_krb5->elements, gensec_gssapi_state->gss_oid->length) == 0)) {
			return True;
		}
	}
	if (feature & GENSEC_FEATURE_DCE_STYLE) {
		return True;
	}
	if (feature & GENSEC_FEATURE_ASYNC_REPLIES) {
		return True;
	}
	return False;
}

static NTSTATUS gensec_gssapi_session_key(struct gensec_security *gensec_security, 
					  DATA_BLOB *session_key) 
{
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	
	if (gensec_gssapi_state->session_key.data) {
		*session_key = gensec_gssapi_state->session_key;
		return NT_STATUS_OK;
	}

	/* Ensure we only call this for GSSAPI/krb5, otherwise things could get very ugly */
	if ((gensec_gssapi_state->gss_oid->length == gss_mech_krb5->length)
	    && (memcmp(gensec_gssapi_state->gss_oid->elements, gss_mech_krb5->elements, 
		       gensec_gssapi_state->gss_oid->length) == 0)) {
		OM_uint32 maj_stat, min_stat;
		gss_buffer_desc skey;
		
		maj_stat = gsskrb5_get_initiator_subkey(&min_stat, 
							gensec_gssapi_state->gssapi_context, 
							&skey);
		
		if (maj_stat == 0) {
			DEBUG(10, ("Got KRB5 session key of length %d\n",  
				   (int)skey.length));
			gensec_gssapi_state->session_key = data_blob_talloc(gensec_gssapi_state, 
									    skey.value, skey.length);
			*session_key = gensec_gssapi_state->session_key;
			dump_data_pw("KRB5 Session Key:\n", session_key->data, session_key->length);
			
			gss_release_buffer(&min_stat, &skey);
			return NT_STATUS_OK;
		}
		return NT_STATUS_NO_USER_SESSION_KEY;
	}
	
	DEBUG(1, ("NO session key for this mech\n"));
	return NT_STATUS_NO_USER_SESSION_KEY;
}

static NTSTATUS gensec_gssapi_session_info(struct gensec_security *gensec_security,
					 struct auth_session_info **_session_info) 
{
	NTSTATUS nt_status;
	TALLOC_CTX *mem_ctx;
	struct gensec_gssapi_state *gensec_gssapi_state = gensec_security->private_data;
	struct auth_serversupplied_info *server_info = NULL;
	struct auth_session_info *session_info = NULL;
	struct PAC_LOGON_INFO *logon_info;
	char *p;
	char *principal;
	const char *account_name;
	const char *realm;
	OM_uint32 maj_stat, min_stat;
	gss_buffer_desc name_token;
	gss_buffer_desc pac;
	krb5_keyblock *keyblock;
	
	mem_ctx = talloc_named(gensec_gssapi_state, 0, "gensec_gssapi_session_info context"); 
	NT_STATUS_HAVE_NO_MEMORY(mem_ctx);

	maj_stat = gss_display_name (&min_stat,
				     gensec_gssapi_state->client_name,
				     &name_token,
				     NULL);
	if (maj_stat) {
		return NT_STATUS_FOOBAR;
	}

	principal = talloc_strndup(mem_ctx, name_token.value, name_token.length);

	gss_release_buffer(&min_stat, &name_token);

	if (!principal) {
		talloc_free(mem_ctx);
		return NT_STATUS_NO_MEMORY;
	}

	p = strchr(principal, '@');
	if (p) {
		*p = '\0';
		p++;
		realm = p;
	} else {
		realm = lp_realm();
	}
	account_name = principal;
	
	maj_stat = gss_krb5_copy_service_keyblock(&min_stat, 
						  gensec_gssapi_state->gssapi_context, 
						  &keyblock);

	maj_stat = gsskrb5_extract_authz_data_from_sec_context(&min_stat, 
							       gensec_gssapi_state->gssapi_context, 
							       KRB5_AUTHDATA_IF_RELEVANT,
							       &pac);
	
	if (maj_stat == 0) {
		DATA_BLOB pac_blob = data_blob_talloc(mem_ctx, pac.value, pac.length);
		pac_blob = unwrap_pac(mem_ctx, &pac_blob);
		gss_release_buffer(&min_stat, &pac);
		
		/* decode and verify the pac */
		nt_status = kerberos_pac_logon_info(mem_ctx, &logon_info, pac_blob,
						    gensec_gssapi_state->smb_krb5_context->krb5_context,
						    NULL, keyblock);

		if (NT_STATUS_IS_OK(nt_status)) {
			union netr_Validation validation;
			validation.sam3 = &logon_info->info3;
			nt_status = make_server_info_netlogon_validation(gensec_gssapi_state, 
									 account_name,
									 3, &validation,
									 &server_info); 
			if (!NT_STATUS_IS_OK(nt_status)) {
				talloc_free(mem_ctx);
				return nt_status;
			}
		} else {
			maj_stat = 1;
		}
	}
	
	if (maj_stat) {
		/* IF we have the PAC - otherwise we need to get this
		 * data from elsewere - local ldb, or (TODO) lookup of some
		 * kind... 
		 *
		 * when heimdal can generate the PAC, we should fail if there's
		 * no PAC present
		 */

		DATA_BLOB user_sess_key = data_blob(NULL, 0);
		DATA_BLOB lm_sess_key = data_blob(NULL, 0);
		/* TODO: should we pass the krb5 session key in here? */
		nt_status = sam_get_server_info(mem_ctx, account_name, realm,
						user_sess_key, lm_sess_key,
						&server_info);
		if (!NT_STATUS_IS_OK(nt_status)) {
			talloc_free(mem_ctx);
			return nt_status;
		}
	}

	/* references the server_info into the session_info */
	nt_status = auth_generate_session_info(gensec_gssapi_state, server_info, &session_info);
	talloc_free(server_info);
	NT_STATUS_NOT_OK_RETURN(nt_status);

	nt_status = gensec_gssapi_session_key(gensec_security, &session_info->session_key);
	NT_STATUS_NOT_OK_RETURN(nt_status);

	*_session_info = session_info;

	return NT_STATUS_OK;
}

static const char *gensec_krb5_oids[] = { 
	GENSEC_OID_KERBEROS5,
	GENSEC_OID_KERBEROS5_OLD,
	NULL 
};

/* As a server, this could in theory accept any GSSAPI mech */
static const struct gensec_security_ops gensec_gssapi_krb5_security_ops = {
	.name		= "gssapi_krb5",
	.auth_type	= DCERPC_AUTH_TYPE_KRB5,
	.oid            = gensec_krb5_oids,
	.client_start   = gensec_gssapi_client_start,
	.server_start   = gensec_gssapi_server_start,
	.magic  	= gensec_gssapi_magic,
	.update 	= gensec_gssapi_update,
	.session_key	= gensec_gssapi_session_key,
	.session_info	= gensec_gssapi_session_info,
	.sig_size	= gensec_gssapi_sig_size,
	.sign_packet	= gensec_gssapi_sign_packet,
	.check_packet	= gensec_gssapi_check_packet,
	.seal_packet	= gensec_gssapi_seal_packet,
	.unseal_packet	= gensec_gssapi_unseal_packet,
	.wrap           = gensec_gssapi_wrap,
	.unwrap         = gensec_gssapi_unwrap,
	.have_feature   = gensec_gssapi_have_feature,
	.enabled        = False
};

NTSTATUS gensec_gssapi_init(void)
{
	NTSTATUS ret;

	ret = gensec_register(&gensec_gssapi_krb5_security_ops);
	if (!NT_STATUS_IS_OK(ret)) {
		DEBUG(0,("Failed to register '%s' gensec backend!\n",
			gensec_gssapi_krb5_security_ops.name));
		return ret;
	}

	return ret;
}
