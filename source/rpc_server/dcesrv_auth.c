/* 
   Unix SMB/CIFS implementation.

   server side dcerpc authentication code

   Copyright (C) Andrew Tridgell 2003

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
  parse any auth information from a dcerpc bind request
  return False if we can't handle the auth request for some 
  reason (in which case we send a bind_nak)
*/
BOOL dcesrv_auth_bind(struct dcesrv_call_state *call)
{
	struct dcerpc_packet *pkt = &call->pkt;
	struct dcesrv_connection *dce_conn = call->conn;
	NTSTATUS status;

	if (pkt->u.bind.auth_info.length == 0) {
		dce_conn->auth_state.auth_info = NULL;
		return True;
	}

	dce_conn->auth_state.auth_info = talloc_p(dce_conn->mem_ctx, struct dcerpc_auth);
	if (!dce_conn->auth_state.auth_info) {
		return False;
	}

	status = ndr_pull_struct_blob(&pkt->u.bind.auth_info,
				      call->mem_ctx,
				      dce_conn->auth_state.auth_info,
				      (ndr_pull_flags_fn_t)ndr_pull_dcerpc_auth);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	if (dce_conn->auth_state.auth_info->auth_type != DCERPC_AUTH_TYPE_NTLMSSP) {
		/* only do NTLMSSP for now */
		DEBUG(2,("auth_type %d not supported\n", dce_conn->auth_state.auth_info->auth_type));
		return False;
	}

	if (dce_conn->auth_state.auth_info->auth_level != DCERPC_AUTH_LEVEL_INTEGRITY &&
	    dce_conn->auth_state.auth_info->auth_level != DCERPC_AUTH_LEVEL_PRIVACY) {
		DEBUG(2,("auth_level %d not supported\n", dce_conn->auth_state.auth_info->auth_level));
		return False;
	}

	status = auth_ntlmssp_start(&dce_conn->auth_state.ntlmssp_state);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	return True;
}

/*
  add any auth information needed in a bind ack
*/
BOOL dcesrv_auth_bind_ack(struct dcesrv_call_state *call, struct dcerpc_packet *pkt)
{
	struct dcesrv_connection *dce_conn = call->conn;
	NTSTATUS status;

	if (!call->conn->auth_state.ntlmssp_state) {
		return True;
	}

	status = auth_ntlmssp_update(dce_conn->auth_state.ntlmssp_state,
				     dce_conn->auth_state.auth_info->credentials, 
				     &dce_conn->auth_state.auth_info->credentials);
	if (!NT_STATUS_IS_OK(status) && 
	    !NT_STATUS_EQUAL(status, NT_STATUS_MORE_PROCESSING_REQUIRED)) {
		return False;
	}

	dce_conn->auth_state.auth_info->auth_pad_length = 0;
	dce_conn->auth_state.auth_info->auth_reserved = 0;
				     
	return True;
}


/*
  process the final stage of a NTLMSSP auth request
*/
BOOL dcesrv_auth_auth3(struct dcesrv_call_state *call)
{
	struct dcerpc_packet *pkt = &call->pkt;
	struct dcesrv_connection *dce_conn = call->conn;
	NTSTATUS status;

	if (!dce_conn->auth_state.auth_info ||
	    !dce_conn->auth_state.ntlmssp_state ||
	    pkt->u.auth.auth_info.length == 0) {
		return False;
	}

	status = ndr_pull_struct_blob(&pkt->u.auth.auth_info,
				      call->mem_ctx,
				      dce_conn->auth_state.auth_info,
				      (ndr_pull_flags_fn_t)ndr_pull_dcerpc_auth);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	if (dce_conn->auth_state.auth_info->auth_type != DCERPC_AUTH_TYPE_NTLMSSP) {
		return False;
	}
	if (dce_conn->auth_state.auth_info->auth_level != DCERPC_AUTH_LEVEL_INTEGRITY &&
	    dce_conn->auth_state.auth_info->auth_level != DCERPC_AUTH_LEVEL_PRIVACY) {
		return False;
	}

	status = auth_ntlmssp_update(dce_conn->auth_state.ntlmssp_state,
				     dce_conn->auth_state.auth_info->credentials, 
				     &dce_conn->auth_state.auth_info->credentials);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	switch (dce_conn->auth_state.auth_info->auth_level) {
	case DCERPC_AUTH_LEVEL_PRIVACY:
	case DCERPC_AUTH_LEVEL_INTEGRITY:
		/* setup for signing */
		status = ntlmssp_sign_init(dce_conn->auth_state.ntlmssp_state->ntlmssp_state);
		break;
	}

	return True;
}


/*
  check credentials on a request
*/
BOOL dcesrv_auth_request(struct dcesrv_call_state *call)
{
	struct dcerpc_packet *pkt = &call->pkt;
	struct dcesrv_connection *dce_conn = call->conn;
	DATA_BLOB auth_blob;
	struct dcerpc_auth auth;
	struct ndr_pull *ndr;
	NTSTATUS status;

	if (!dce_conn->auth_state.auth_info ||
	    !dce_conn->auth_state.ntlmssp_state) {
		return True;
	}

	auth_blob.length = 8 + pkt->auth_length;

	/* check for a valid length */
	if (pkt->u.request.stub_and_verifier.length < auth_blob.length) {
		return False;
	}

	auth_blob.data = 
		pkt->u.request.stub_and_verifier.data + 
		pkt->u.request.stub_and_verifier.length - auth_blob.length;
	pkt->u.request.stub_and_verifier.length -= auth_blob.length;

	/* pull the auth structure */
	ndr = ndr_pull_init_blob(&auth_blob, call->mem_ctx);
	if (!ndr) {
		return False;
	}

	if (!(pkt->drep[0] & DCERPC_DREP_LE)) {
		ndr->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	status = ndr_pull_dcerpc_auth(ndr, NDR_SCALARS|NDR_BUFFERS, &auth);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	/* check signature or unseal the packet */
	switch (dce_conn->auth_state.auth_info->auth_level) {
	case DCERPC_AUTH_LEVEL_PRIVACY:
		status = ntlmssp_unseal_packet(dce_conn->auth_state.ntlmssp_state->ntlmssp_state, 
					       pkt->u.request.stub_and_verifier.data, 
					       pkt->u.request.stub_and_verifier.length, 
					       &auth.credentials);
		break;

	case DCERPC_AUTH_LEVEL_INTEGRITY:
		status = ntlmssp_check_packet(dce_conn->auth_state.ntlmssp_state->ntlmssp_state, 
					      pkt->u.request.stub_and_verifier.data, 
					      pkt->u.request.stub_and_verifier.length, 
					      &auth.credentials);
		break;

	default:
		status = NT_STATUS_INVALID_LEVEL;
		break;
	}

	/* remove the indicated amount of paddiing */
	if (pkt->u.request.stub_and_verifier.length < auth.auth_pad_length) {
		return False;
	}
	pkt->u.request.stub_and_verifier.length -= auth.auth_pad_length;

	return NT_STATUS_IS_OK(status);
}


/* 
   push a signed or sealed dcerpc request packet into a blob
*/
BOOL dcesrv_auth_response(struct dcesrv_call_state *call,
			  DATA_BLOB *blob, struct dcerpc_packet *pkt)
{
	struct dcesrv_connection *dce_conn = call->conn;
	NTSTATUS status;
	struct ndr_push *ndr;

	/* non-signed packets are simple */
	if (!dce_conn->auth_state.auth_info || !dce_conn->auth_state.ntlmssp_state) {
		status = dcerpc_push_auth(blob, call->mem_ctx, pkt, NULL);
		return NT_STATUS_IS_OK(status);
	}

	ndr = ndr_push_init_ctx(call->mem_ctx);
	if (!ndr) {
		return False;
	}

	if (pkt->drep[0] & DCERPC_DREP_LE) {
		ndr->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	status = ndr_push_dcerpc_packet(ndr, NDR_SCALARS|NDR_BUFFERS, pkt);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	/* pad to 8 byte multiple */
	dce_conn->auth_state.auth_info->auth_pad_length = NDR_ALIGN(ndr, 8);
	ndr_push_zero(ndr, dce_conn->auth_state.auth_info->auth_pad_length);

	/* sign or seal the packet */
	switch (dce_conn->auth_state.auth_info->auth_level) {
	case DCERPC_AUTH_LEVEL_PRIVACY:
		status = ntlmssp_seal_packet(dce_conn->auth_state.ntlmssp_state->ntlmssp_state, 
					     ndr->data + DCERPC_REQUEST_LENGTH, 
					     ndr->offset - DCERPC_REQUEST_LENGTH,
					     &dce_conn->auth_state.auth_info->credentials);
		break;

	case DCERPC_AUTH_LEVEL_INTEGRITY:
		status = ntlmssp_sign_packet(dce_conn->auth_state.ntlmssp_state->ntlmssp_state, 
					     ndr->data + DCERPC_REQUEST_LENGTH, 
					     ndr->offset - DCERPC_REQUEST_LENGTH,
					     &dce_conn->auth_state.auth_info->credentials);
		break;
	default:
		status = NT_STATUS_INVALID_LEVEL;
		break;
	}

	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}	

	/* add the auth verifier */
	status = ndr_push_dcerpc_auth(ndr, NDR_SCALARS|NDR_BUFFERS, dce_conn->auth_state.auth_info);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	/* extract the whole packet as a blob */
	*blob = ndr_push_blob(ndr);

	/* fill in the fragment length and auth_length, we can't fill
	   in these earlier as we don't know the signature length (it
	   could be variable length) */
	dcerpc_set_frag_length(blob, blob->length);
	dcerpc_set_auth_length(blob, dce_conn->auth_state.auth_info->credentials.length);

	data_blob_free(&dce_conn->auth_state.auth_info->credentials);

	return True;
}
