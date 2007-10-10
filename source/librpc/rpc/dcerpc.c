/* 
   Unix SMB/CIFS implementation.
   raw dcerpc operations

   Copyright (C) Tim Potter 2003
   Copyright (C) Andrew Tridgell 2003-2005
   Copyright (C) Jelmer Vernooij 2004
   
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
#include "dlinklist.h"
#include "lib/events/events.h"
#include "librpc/gen_ndr/ndr_epmapper.h"
#include "librpc/gen_ndr/ndr_dcerpc.h"

static struct dcerpc_interface_list *dcerpc_pipes = NULL;

/*
  register a dcerpc client interface
*/
NTSTATUS librpc_register_interface(const struct dcerpc_interface_table *interface)
{
	struct dcerpc_interface_list *l = talloc(talloc_autofree_context(),
						   struct dcerpc_interface_list);
		
	if (idl_iface_by_name (interface->name) != NULL) {
		DEBUG(0, ("Attempt to register interface %s twice\n", interface->name));
		return NT_STATUS_OBJECT_NAME_COLLISION;
	}
	l->table = interface;

	DLIST_ADD(dcerpc_pipes, l);
	
  	return NT_STATUS_OK;
}

/*
  return the list of registered dcerpc_pipes
*/
const struct dcerpc_interface_list *librpc_dcerpc_pipes(void)
{
	return dcerpc_pipes;
}

/* destroy a dcerpc connection */
static int dcerpc_connection_destructor(void *ptr)
{
	struct dcerpc_connection *c = ptr;
	if (c->transport.shutdown_pipe) {
		c->transport.shutdown_pipe(c);
	}
	return 0;
}


/* initialise a dcerpc connection. */
struct dcerpc_connection *dcerpc_connection_init(TALLOC_CTX *mem_ctx)
{
	struct dcerpc_connection *c;

	c = talloc_zero(mem_ctx, struct dcerpc_connection);
	if (!c) {
		return NULL;
	}

	c->call_id = 1;
	c->security_state.auth_info = NULL;
	c->security_state.session_key = dcerpc_generic_session_key;
	c->security_state.generic_state = NULL;
	c->binding_string = NULL;
	c->flags = 0;
	c->srv_max_xmit_frag = 0;
	c->srv_max_recv_frag = 0;
	c->pending = NULL;

	talloc_set_destructor(c, dcerpc_connection_destructor);

	return c;
}

/* initialise a dcerpc pipe. */
struct dcerpc_pipe *dcerpc_pipe_init(TALLOC_CTX *mem_ctx)
{
	struct dcerpc_pipe *p;

	p = talloc(mem_ctx, struct dcerpc_pipe);
	if (!p) {
		return NULL;
	}

	p->conn = dcerpc_connection_init(p);
	if (p->conn == NULL) {
		talloc_free(p);
		return NULL;
	}

	p->last_fault_code = 0;
	p->context_id = 0;

	ZERO_STRUCT(p->syntax);
	ZERO_STRUCT(p->transfer_syntax);

	return p;
}


/* 
   choose the next call id to use
*/
static uint32_t next_call_id(struct dcerpc_connection *c)
{
	c->call_id++;
	if (c->call_id == 0) {
		c->call_id++;
	}
	return c->call_id;
}

/* we need to be able to get/set the fragment length without doing a full
   decode */
void dcerpc_set_frag_length(DATA_BLOB *blob, uint16_t v)
{
	if (CVAL(blob->data,DCERPC_DREP_OFFSET) & DCERPC_DREP_LE) {
		SSVAL(blob->data, DCERPC_FRAG_LEN_OFFSET, v);
	} else {
		RSSVAL(blob->data, DCERPC_FRAG_LEN_OFFSET, v);
	}
}

uint16_t dcerpc_get_frag_length(const DATA_BLOB *blob)
{
	if (CVAL(blob->data,DCERPC_DREP_OFFSET) & DCERPC_DREP_LE) {
		return SVAL(blob->data, DCERPC_FRAG_LEN_OFFSET);
	} else {
		return RSVAL(blob->data, DCERPC_FRAG_LEN_OFFSET);
	}
}

void dcerpc_set_auth_length(DATA_BLOB *blob, uint16_t v)
{
	if (CVAL(blob->data,DCERPC_DREP_OFFSET) & DCERPC_DREP_LE) {
		SSVAL(blob->data, DCERPC_AUTH_LEN_OFFSET, v);
	} else {
		RSSVAL(blob->data, DCERPC_AUTH_LEN_OFFSET, v);
	}
}


/*
  setup for a ndr pull, also setting up any flags from the binding string
*/
static struct ndr_pull *ndr_pull_init_flags(struct dcerpc_connection *c, 
					    DATA_BLOB *blob, TALLOC_CTX *mem_ctx)
{
	struct ndr_pull *ndr = ndr_pull_init_blob(blob, mem_ctx);

	if (ndr == NULL) return ndr;

	if (c->flags & DCERPC_DEBUG_PAD_CHECK) {
		ndr->flags |= LIBNDR_FLAG_PAD_CHECK;
	}

	if (c->flags & DCERPC_NDR_REF_ALLOC) {
		ndr->flags |= LIBNDR_FLAG_REF_ALLOC;
	}

	return ndr;
}

/* 
   parse a data blob into a ncacn_packet structure. This handles both
   input and output packets
*/
static NTSTATUS dcerpc_pull(struct dcerpc_connection *c, DATA_BLOB *blob, TALLOC_CTX *mem_ctx, 
			    struct ncacn_packet *pkt)
{
	struct ndr_pull *ndr;

	ndr = ndr_pull_init_flags(c, blob, mem_ctx);
	if (!ndr) {
		return NT_STATUS_NO_MEMORY;
	}

	if (! (CVAL(blob->data, DCERPC_DREP_OFFSET) & DCERPC_DREP_LE)) {
		ndr->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	return ndr_pull_ncacn_packet(ndr, NDR_SCALARS|NDR_BUFFERS, pkt);
}

/*
  generate a CONNECT level verifier
*/
static NTSTATUS dcerpc_connect_verifier(TALLOC_CTX *mem_ctx, DATA_BLOB *blob)
{
	*blob = data_blob_talloc(mem_ctx, NULL, 16);
	if (blob->data == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	SIVAL(blob->data, 0, 1);
	memset(blob->data+4, 0, 12);
	return NT_STATUS_OK;
}

/*
  check a CONNECT level verifier
*/
static NTSTATUS dcerpc_check_connect_verifier(DATA_BLOB *blob)
{
	if (blob->length != 16 ||
	    IVAL(blob->data, 0) != 1) {
		return NT_STATUS_ACCESS_DENIED;
	}
	return NT_STATUS_OK;
}

/* 
   parse a possibly signed blob into a dcerpc request packet structure
*/
static NTSTATUS dcerpc_pull_request_sign(struct dcerpc_connection *c, 
					 DATA_BLOB *blob, TALLOC_CTX *mem_ctx, 
					 struct ncacn_packet *pkt)
{
	struct ndr_pull *ndr;
	NTSTATUS status;
	struct dcerpc_auth auth;
	DATA_BLOB auth_blob;

	/* non-signed packets are simpler */
	if (!c->security_state.auth_info || 
	    !c->security_state.generic_state) {
		return dcerpc_pull(c, blob, mem_ctx, pkt);
	}

	ndr = ndr_pull_init_flags(c, blob, mem_ctx);
	if (!ndr) {
		return NT_STATUS_NO_MEMORY;
	}

	if (! (CVAL(blob->data, DCERPC_DREP_OFFSET) & DCERPC_DREP_LE)) {
		ndr->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	/* pull the basic packet */
	status = ndr_pull_ncacn_packet(ndr, NDR_SCALARS|NDR_BUFFERS, pkt);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	if (pkt->ptype != DCERPC_PKT_RESPONSE) {
		return status;
	}

	if (pkt->auth_length == 0 &&
	    c->security_state.auth_info->auth_level == DCERPC_AUTH_LEVEL_CONNECT) {
		return NT_STATUS_OK;
	}

	auth_blob.length = 8 + pkt->auth_length;

	/* check for a valid length */
	if (pkt->u.response.stub_and_verifier.length < auth_blob.length) {
		return NT_STATUS_INFO_LENGTH_MISMATCH;
	}

	auth_blob.data = 
		pkt->u.response.stub_and_verifier.data + 
		pkt->u.response.stub_and_verifier.length - auth_blob.length;
	pkt->u.response.stub_and_verifier.length -= auth_blob.length;

	/* pull the auth structure */
	ndr = ndr_pull_init_flags(c, &auth_blob, mem_ctx);
	if (!ndr) {
		return NT_STATUS_NO_MEMORY;
	}

	if (! (CVAL(blob->data, DCERPC_DREP_OFFSET) & DCERPC_DREP_LE)) {
		ndr->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	status = ndr_pull_dcerpc_auth(ndr, NDR_SCALARS|NDR_BUFFERS, &auth);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	
	
	/* check signature or unseal the packet */
	switch (c->security_state.auth_info->auth_level) {
	case DCERPC_AUTH_LEVEL_PRIVACY:
		status = gensec_unseal_packet(c->security_state.generic_state, 
					      mem_ctx, 
					      blob->data + DCERPC_REQUEST_LENGTH,
					      pkt->u.response.stub_and_verifier.length, 
					      blob->data,
					      blob->length - auth.credentials.length,
					      &auth.credentials);
		memcpy(pkt->u.response.stub_and_verifier.data,
		       blob->data + DCERPC_REQUEST_LENGTH,
		       pkt->u.response.stub_and_verifier.length);
		break;
		
	case DCERPC_AUTH_LEVEL_INTEGRITY:
		status = gensec_check_packet(c->security_state.generic_state, 
					     mem_ctx, 
					     pkt->u.response.stub_and_verifier.data, 
					     pkt->u.response.stub_and_verifier.length, 
					     blob->data,
					     blob->length - auth.credentials.length,
					     &auth.credentials);
		break;

	case DCERPC_AUTH_LEVEL_CONNECT:
		status = dcerpc_check_connect_verifier(&auth.credentials);
		break;

	case DCERPC_AUTH_LEVEL_NONE:
		break;

	default:
		status = NT_STATUS_INVALID_LEVEL;
		break;
	}
	
	/* remove the indicated amount of paddiing */
	if (pkt->u.response.stub_and_verifier.length < auth.auth_pad_length) {
		return NT_STATUS_INFO_LENGTH_MISMATCH;
	}
	pkt->u.response.stub_and_verifier.length -= auth.auth_pad_length;

	return status;
}


/* 
   push a dcerpc request packet into a blob, possibly signing it.
*/
static NTSTATUS dcerpc_push_request_sign(struct dcerpc_connection *c, 
					 DATA_BLOB *blob, TALLOC_CTX *mem_ctx, 
					 struct ncacn_packet *pkt)
{
	NTSTATUS status;
	struct ndr_push *ndr;
	DATA_BLOB creds2;

	/* non-signed packets are simpler */
	if (!c->security_state.auth_info || 
	    !c->security_state.generic_state) {
		return dcerpc_push_auth(blob, mem_ctx, pkt, c->security_state.auth_info);
	}

	ndr = ndr_push_init_ctx(mem_ctx);
	if (!ndr) {
		return NT_STATUS_NO_MEMORY;
	}

	if (c->flags & DCERPC_PUSH_BIGENDIAN) {
		ndr->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	if (pkt->pfc_flags & DCERPC_PFC_FLAG_ORPC) {
		ndr->flags |= LIBNDR_FLAG_OBJECT_PRESENT;
	}

	status = ndr_push_ncacn_packet(ndr, NDR_SCALARS|NDR_BUFFERS, pkt);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* pad to 16 byte multiple in the payload portion of the
	   packet. This matches what w2k3 does */
	c->security_state.auth_info->auth_pad_length = 
		(16 - (pkt->u.request.stub_and_verifier.length & 15)) & 15;
	ndr_push_zero(ndr, c->security_state.auth_info->auth_pad_length);

	/* sign or seal the packet */
	switch (c->security_state.auth_info->auth_level) {
	case DCERPC_AUTH_LEVEL_PRIVACY:
	case DCERPC_AUTH_LEVEL_INTEGRITY:
		c->security_state.auth_info->credentials
			= data_blob_talloc(mem_ctx, NULL, gensec_sig_size(c->security_state.generic_state));
		data_blob_clear(&c->security_state.auth_info->credentials);
		break;

	case DCERPC_AUTH_LEVEL_CONNECT:
		status = dcerpc_connect_verifier(mem_ctx, &c->security_state.auth_info->credentials);
		break;
		
	case DCERPC_AUTH_LEVEL_NONE:
		c->security_state.auth_info->credentials = data_blob(NULL, 0);
		break;
		
	default:
		status = NT_STATUS_INVALID_LEVEL;
		break;
	}
	
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}	

	/* add the auth verifier */
	status = ndr_push_dcerpc_auth(ndr, NDR_SCALARS|NDR_BUFFERS, c->security_state.auth_info);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* extract the whole packet as a blob */
	*blob = ndr_push_blob(ndr);

	/* fill in the fragment length and auth_length, we can't fill
	   in these earlier as we don't know the signature length (it
	   could be variable length) */
	dcerpc_set_frag_length(blob, blob->length);
	dcerpc_set_auth_length(blob, c->security_state.auth_info->credentials.length);

	/* sign or seal the packet */
	switch (c->security_state.auth_info->auth_level) {
	case DCERPC_AUTH_LEVEL_PRIVACY:
		status = gensec_seal_packet(c->security_state.generic_state, 
					    mem_ctx, 
					    blob->data + DCERPC_REQUEST_LENGTH, 
					    pkt->u.request.stub_and_verifier.length + 
					    c->security_state.auth_info->auth_pad_length,
					    blob->data,
					    blob->length - 
					    c->security_state.auth_info->credentials.length,
					    &creds2);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
		memcpy(blob->data + blob->length - creds2.length, creds2.data, creds2.length);
		break;

	case DCERPC_AUTH_LEVEL_INTEGRITY:
		status = gensec_sign_packet(c->security_state.generic_state, 
					    mem_ctx, 
					    blob->data + DCERPC_REQUEST_LENGTH, 
					    pkt->u.request.stub_and_verifier.length + 
					    c->security_state.auth_info->auth_pad_length,
					    blob->data,
					    blob->length - 
					    c->security_state.auth_info->credentials.length,
					    &creds2);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
		memcpy(blob->data + blob->length - creds2.length, creds2.data, creds2.length);
		break;

	case DCERPC_AUTH_LEVEL_CONNECT:
		break;

	case DCERPC_AUTH_LEVEL_NONE:
		c->security_state.auth_info->credentials = data_blob(NULL, 0);
		break;

	default:
		status = NT_STATUS_INVALID_LEVEL;
		break;
	}

	data_blob_free(&c->security_state.auth_info->credentials);

	return NT_STATUS_OK;
}


/* 
   fill in the fixed values in a dcerpc header 
*/
static void init_dcerpc_hdr(struct dcerpc_connection *c, struct ncacn_packet *pkt)
{
	pkt->rpc_vers = 5;
	pkt->rpc_vers_minor = 0;
	if (c->flags & DCERPC_PUSH_BIGENDIAN) {
		pkt->drep[0] = 0;
	} else {
		pkt->drep[0] = DCERPC_DREP_LE;
	}
	pkt->drep[1] = 0;
	pkt->drep[2] = 0;
	pkt->drep[3] = 0;
}

/*
  hold the state of pending full requests
*/
struct full_request_state {
	DATA_BLOB *reply_blob;
	NTSTATUS status;
};

/*
  receive a reply to a full request
 */
static void full_request_recv(struct dcerpc_connection *c, DATA_BLOB *blob, 
			      NTSTATUS status)
{
	struct full_request_state *state = c->full_request_private;

	if (!NT_STATUS_IS_OK(status)) {
		state->status = status;
		return;
	}
	state->reply_blob[0] = data_blob_talloc(state, blob->data, blob->length);
	state->reply_blob = NULL;
}

/*
  perform a single pdu synchronous request - used for the bind code
  this cannot be mixed with normal async requests
*/
static NTSTATUS full_request(struct dcerpc_connection *c, 
			     TALLOC_CTX *mem_ctx,
			     DATA_BLOB *request_blob,
			     DATA_BLOB *reply_blob)
{
	struct full_request_state *state = talloc(mem_ctx, struct full_request_state);
	NTSTATUS status;

	if (state == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	state->reply_blob = reply_blob;
	state->status = NT_STATUS_OK;

	c->transport.recv_data = full_request_recv;
	c->full_request_private = state;

	status = c->transport.send_request(c, request_blob, True);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	while (NT_STATUS_IS_OK(state->status) && state->reply_blob) {
		struct event_context *ctx = c->transport.event_context(c);
		if (event_loop_once(ctx) != 0) {
			return NT_STATUS_CONNECTION_DISCONNECTED;
		}
	}

	return state->status;
}

/*
  map a bind nak reason to a NTSTATUS
*/
static NTSTATUS dcerpc_map_reason(uint16_t reason)
{
	switch (reason) {
	case DCERPC_BIND_REASON_ASYNTAX:
		return NT_STATUS_RPC_UNSUPPORTED_NAME_SYNTAX;
	}
	return NT_STATUS_UNSUCCESSFUL;
}


/* 
   perform a bind using the given syntax 

   the auth_info structure is updated with the reply authentication info
   on success
*/
NTSTATUS dcerpc_bind(struct dcerpc_pipe *p, 
		     TALLOC_CTX *mem_ctx,
		     const struct dcerpc_syntax_id *syntax,
		     const struct dcerpc_syntax_id *transfer_syntax)
{
	struct ncacn_packet pkt;
	NTSTATUS status;
	DATA_BLOB blob;

	p->syntax = *syntax;
	p->transfer_syntax = *transfer_syntax;

	init_dcerpc_hdr(p->conn, &pkt);

	pkt.ptype = DCERPC_PKT_BIND;
	pkt.pfc_flags = DCERPC_PFC_FLAG_FIRST | DCERPC_PFC_FLAG_LAST;
	pkt.call_id = p->conn->call_id;
	pkt.auth_length = 0;

	pkt.u.bind.max_xmit_frag = 5840;
	pkt.u.bind.max_recv_frag = 5840;
	pkt.u.bind.assoc_group_id = 0;
	pkt.u.bind.num_contexts = 1;
	pkt.u.bind.ctx_list = talloc_array(mem_ctx, struct dcerpc_ctx_list, 1);
	if (!pkt.u.bind.ctx_list) {
		return NT_STATUS_NO_MEMORY;
	}
	pkt.u.bind.ctx_list[0].context_id = p->context_id;
	pkt.u.bind.ctx_list[0].num_transfer_syntaxes = 1;
	pkt.u.bind.ctx_list[0].abstract_syntax = p->syntax;
	pkt.u.bind.ctx_list[0].transfer_syntaxes = &p->transfer_syntax;
	pkt.u.bind.auth_info = data_blob(NULL, 0);

	/* construct the NDR form of the packet */
	status = dcerpc_push_auth(&blob, mem_ctx, &pkt, p->conn->security_state.auth_info);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* send it on its way */
	status = full_request(p->conn, mem_ctx, &blob, &blob);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* unmarshall the NDR */
	status = dcerpc_pull(p->conn, &blob, mem_ctx, &pkt);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	if (pkt.ptype == DCERPC_PKT_BIND_NAK) {
		DEBUG(2,("dcerpc: bind_nak reason %d\n", pkt.u.bind_nak.reject_reason));
		return dcerpc_map_reason(pkt.u.bind_nak.reject_reason);
	}

	if ((pkt.ptype != DCERPC_PKT_BIND_ACK) ||
	    pkt.u.bind_ack.num_results == 0 ||
	    pkt.u.bind_ack.ctx_list[0].result != 0) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	p->conn->srv_max_xmit_frag = pkt.u.bind_ack.max_xmit_frag;
	p->conn->srv_max_recv_frag = pkt.u.bind_ack.max_recv_frag;

	/* the bind_ack might contain a reply set of credentials */
	if (p->conn->security_state.auth_info && pkt.u.bind_ack.auth_info.length) {
		status = ndr_pull_struct_blob(&pkt.u.bind_ack.auth_info,
					      mem_ctx,
					      p->conn->security_state.auth_info,
					      (ndr_pull_flags_fn_t)ndr_pull_dcerpc_auth);
	}

	return status;	
}

/* 
   perform a continued bind (and auth3)
*/
NTSTATUS dcerpc_auth3(struct dcerpc_connection *c, 
		      TALLOC_CTX *mem_ctx)
{
	struct ncacn_packet pkt;
	NTSTATUS status;
	DATA_BLOB blob;

	init_dcerpc_hdr(c, &pkt);

	pkt.ptype = DCERPC_PKT_AUTH3;
	pkt.pfc_flags = DCERPC_PFC_FLAG_FIRST | DCERPC_PFC_FLAG_LAST;
	pkt.call_id = next_call_id(c);
	pkt.auth_length = 0;
	pkt.u.auth3._pad = 0;
	pkt.u.auth3.auth_info = data_blob(NULL, 0);

	/* construct the NDR form of the packet */
	status = dcerpc_push_auth(&blob, mem_ctx, &pkt, c->security_state.auth_info);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* send it on its way */
	status = c->transport.send_request(c, &blob, False);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	return status;	
}


/* perform a dcerpc bind, using the uuid as the key */
NTSTATUS dcerpc_bind_byuuid(struct dcerpc_pipe *p, 
			    TALLOC_CTX *mem_ctx,
			    const char *uuid, uint_t version)
{
	struct dcerpc_syntax_id syntax;
	struct dcerpc_syntax_id transfer_syntax;
	NTSTATUS status;

	status = GUID_from_string(uuid, &syntax.uuid);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(2,("Invalid uuid string in dcerpc_bind_byuuid\n"));
		return status;
	}
	syntax.if_version = version;

	status = GUID_from_string(NDR_GUID, &transfer_syntax.uuid);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	transfer_syntax.if_version = NDR_GUID_VERSION;

	return dcerpc_bind(p, mem_ctx, &syntax, &transfer_syntax);
}

/*
  process a fragment received from the transport layer during a
  request
*/
static void dcerpc_request_recv_data(struct dcerpc_connection *c, 
				     DATA_BLOB *data,
				     NTSTATUS status)
{
	struct ncacn_packet pkt;
	struct rpc_request *req;
	uint_t length;
	
	if (!NT_STATUS_IS_OK(status)) {
		/* all pending requests get the error */
		while (c->pending) {
			req = c->pending;
			req->state = RPC_REQUEST_DONE;
			req->status = status;
			DLIST_REMOVE(c->pending, req);
			if (req->async.callback) {
				req->async.callback(req);
			}
		}
		return;
	}

	pkt.call_id = 0;

	status = dcerpc_pull_request_sign(c, data, (TALLOC_CTX *)data->data, &pkt);

	/* find the matching request. Notice we match before we check
	   the status.  this is ok as a pending call_id can never be
	   zero */
	for (req=c->pending;req;req=req->next) {
		if (pkt.call_id == req->call_id) break;
	}

	if (req == NULL) {
		DEBUG(2,("dcerpc_request: unmatched call_id %u in response packet\n", pkt.call_id));
		return;
	}

	if (!NT_STATUS_IS_OK(status)) {
		req->status = status;
		req->state = RPC_REQUEST_DONE;
		DLIST_REMOVE(c->pending, req);
		if (req->async.callback) {
			req->async.callback(req);
		}
		return;
	}

	if (pkt.ptype == DCERPC_PKT_FAULT) {
		DEBUG(5,("rpc fault: %s\n", dcerpc_errstr(c, pkt.u.fault.status)));
		req->fault_code = pkt.u.fault.status;
		req->status = NT_STATUS_NET_WRITE_FAULT;
		req->state = RPC_REQUEST_DONE;
		DLIST_REMOVE(c->pending, req);
		if (req->async.callback) {
			req->async.callback(req);
		}
		return;
	}

	if (pkt.ptype != DCERPC_PKT_RESPONSE) {
		DEBUG(2,("Unexpected packet type %d in dcerpc response\n",
			 (int)pkt.ptype)); 
		req->fault_code = DCERPC_FAULT_OTHER;
		req->status = NT_STATUS_NET_WRITE_FAULT;
		req->state = RPC_REQUEST_DONE;
		DLIST_REMOVE(c->pending, req);
		if (req->async.callback) {
			req->async.callback(req);
		}
		return;
	}

	length = pkt.u.response.stub_and_verifier.length;

	if (length > 0) {
		req->payload.data = talloc_realloc(req, 
						   req->payload.data, 
						   uint8_t,
						   req->payload.length + length);
		if (!req->payload.data) {
			req->status = NT_STATUS_NO_MEMORY;
			req->state = RPC_REQUEST_DONE;
			DLIST_REMOVE(c->pending, req);
			if (req->async.callback) {
				req->async.callback(req);
			}
			return;
		}
		memcpy(req->payload.data+req->payload.length, 
		       pkt.u.response.stub_and_verifier.data, length);
		req->payload.length += length;
	}

	if (!(pkt.pfc_flags & DCERPC_PFC_FLAG_LAST)) {
		c->transport.send_read(c);
		return;
	}

	/* we've got the full payload */
	req->state = RPC_REQUEST_DONE;
	DLIST_REMOVE(c->pending, req);

	if (!(pkt.drep[0] & DCERPC_DREP_LE)) {
		req->flags |= DCERPC_PULL_BIGENDIAN;
	} else {
		req->flags &= ~DCERPC_PULL_BIGENDIAN;
	}

	if (req->async.callback) {
		req->async.callback(req);
	}
}


/*
  make sure requests are cleaned up 
 */
static int dcerpc_req_destructor(void *ptr)
{
	struct rpc_request *req = ptr;
	DLIST_REMOVE(req->p->conn->pending, req);
	return 0;
}

/*
  perform the send side of a async dcerpc request
*/
struct rpc_request *dcerpc_request_send(struct dcerpc_pipe *p, 
					const struct GUID *object,
					uint16_t opnum,
					DATA_BLOB *stub_data)
{
	struct rpc_request *req;
	struct ncacn_packet pkt;
	DATA_BLOB blob;
	uint32_t remaining, chunk_size;
	BOOL first_packet = True;

	p->conn->transport.recv_data = dcerpc_request_recv_data;

	req = talloc(p, struct rpc_request);
	if (req == NULL) {
		return NULL;
	}

	req->p = p;
	req->call_id = next_call_id(p->conn);
	req->status = NT_STATUS_OK;
	req->state = RPC_REQUEST_PENDING;
	req->payload = data_blob(NULL, 0);
	req->flags = 0;
	req->fault_code = 0;
	req->async.callback = NULL;

	init_dcerpc_hdr(p->conn, &pkt);

	remaining = stub_data->length;

	/* we can write a full max_recv_frag size, minus the dcerpc
	   request header size */
	chunk_size = p->conn->srv_max_recv_frag - (DCERPC_MAX_SIGN_SIZE+DCERPC_REQUEST_LENGTH);

	pkt.ptype = DCERPC_PKT_REQUEST;
	pkt.call_id = req->call_id;
	pkt.auth_length = 0;
	pkt.pfc_flags = 0;
	pkt.u.request.alloc_hint = remaining;
	pkt.u.request.context_id = p->context_id;
	pkt.u.request.opnum = opnum;

	if (object) {
		pkt.u.request.object.object = *object;
		pkt.pfc_flags |= DCERPC_PFC_FLAG_ORPC;
		chunk_size -= ndr_size_GUID(object,0);
	}

	DLIST_ADD(p->conn->pending, req);

	/* we send a series of pdus without waiting for a reply */
	while (remaining > 0 || first_packet) {
		uint32_t chunk = MIN(chunk_size, remaining);
		BOOL last_frag = False;

		first_packet = False;
		pkt.pfc_flags &= ~(DCERPC_PFC_FLAG_FIRST |DCERPC_PFC_FLAG_LAST);

		if (remaining == stub_data->length) {
			pkt.pfc_flags |= DCERPC_PFC_FLAG_FIRST;
		}
		if (chunk == remaining) {
			pkt.pfc_flags |= DCERPC_PFC_FLAG_LAST;
			last_frag = True;
		}

		pkt.u.request.stub_and_verifier.data = stub_data->data + 
			(stub_data->length - remaining);
		pkt.u.request.stub_and_verifier.length = chunk;

		req->status = dcerpc_push_request_sign(p->conn, &blob, req, &pkt);
		if (!NT_STATUS_IS_OK(req->status)) {
			req->state = RPC_REQUEST_DONE;
			DLIST_REMOVE(p->conn->pending, req);
			return req;
		}
		
		req->status = p->conn->transport.send_request(p->conn, &blob, last_frag);
		if (!NT_STATUS_IS_OK(req->status)) {
			req->state = RPC_REQUEST_DONE;
			DLIST_REMOVE(p->conn->pending, req);
			return req;
		}		

		remaining -= chunk;
	}

	talloc_set_destructor(req, dcerpc_req_destructor);

	return req;
}

/*
  return the event context for a dcerpc pipe
  used by callers who wish to operate asynchronously
*/
struct event_context *dcerpc_event_context(struct dcerpc_pipe *p)
{
	return p->conn->transport.event_context(p->conn);
}



/*
  perform the receive side of a async dcerpc request
*/
NTSTATUS dcerpc_request_recv(struct rpc_request *req,
			     TALLOC_CTX *mem_ctx,
			     DATA_BLOB *stub_data)
{
	NTSTATUS status;

	while (req->state == RPC_REQUEST_PENDING) {
		struct event_context *ctx = dcerpc_event_context(req->p);
		if (event_loop_once(ctx) != 0) {
			return NT_STATUS_CONNECTION_DISCONNECTED;
		}
	}
	*stub_data = req->payload;
	status = req->status;
	if (stub_data->data) {
		stub_data->data = talloc_steal(mem_ctx, stub_data->data);
	}
	if (NT_STATUS_EQUAL(status, NT_STATUS_NET_WRITE_FAULT)) {
		req->p->last_fault_code = req->fault_code;
	}
	talloc_free(req);
	return status;
}

/*
  perform a full request/response pair on a dcerpc pipe
*/
NTSTATUS dcerpc_request(struct dcerpc_pipe *p, 
			struct GUID *object,
			uint16_t opnum,
			TALLOC_CTX *mem_ctx,
			DATA_BLOB *stub_data_in,
			DATA_BLOB *stub_data_out)
{
	struct rpc_request *req;

	req = dcerpc_request_send(p, object, opnum, stub_data_in);
	if (req == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	return dcerpc_request_recv(req, mem_ctx, stub_data_out);
}


/*
  this is a paranoid NDR validator. For every packet we push onto the wire
  we pull it back again, then push it again. Then we compare the raw NDR data
  for that to the NDR we initially generated. If they don't match then we know
  we must have a bug in either the pull or push side of our code
*/
static NTSTATUS dcerpc_ndr_validate_in(struct dcerpc_connection *c, 
				       TALLOC_CTX *mem_ctx,
				       DATA_BLOB blob,
				       size_t struct_size,
				       NTSTATUS (*ndr_push)(struct ndr_push *, int, void *),
				       NTSTATUS (*ndr_pull)(struct ndr_pull *, int, void *))
{
	void *st;
	struct ndr_pull *pull;
	struct ndr_push *push;
	NTSTATUS status;
	DATA_BLOB blob2;

	st = talloc_size(mem_ctx, struct_size);
	if (!st) {
		return NT_STATUS_NO_MEMORY;
	}

	pull = ndr_pull_init_flags(c, &blob, mem_ctx);
	if (!pull) {
		return NT_STATUS_NO_MEMORY;
	}
	pull->flags |= LIBNDR_FLAG_REF_ALLOC;

	status = ndr_pull(pull, NDR_IN, st);
	if (!NT_STATUS_IS_OK(status)) {
		return ndr_pull_error(pull, NDR_ERR_VALIDATE, 
				      "failed input validation pull - %s",
				      nt_errstr(status));
	}

	push = ndr_push_init_ctx(mem_ctx);
	if (!push) {
		return NT_STATUS_NO_MEMORY;
	}	

	status = ndr_push(push, NDR_IN, st);
	if (!NT_STATUS_IS_OK(status)) {
		return ndr_push_error(push, NDR_ERR_VALIDATE, 
				      "failed input validation push - %s",
				      nt_errstr(status));
	}

	blob2 = ndr_push_blob(push);

	if (!data_blob_equal(&blob, &blob2)) {
		DEBUG(3,("original:\n"));
		dump_data(3, blob.data, blob.length);
		DEBUG(3,("secondary:\n"));
		dump_data(3, blob2.data, blob2.length);
		return ndr_push_error(push, NDR_ERR_VALIDATE, 
				      "failed input validation data - %s",
				      nt_errstr(status));
	}

	return NT_STATUS_OK;
}

/*
  this is a paranoid NDR input validator. For every packet we pull
  from the wire we push it back again then pull and push it
  again. Then we compare the raw NDR data for that to the NDR we
  initially generated. If they don't match then we know we must have a
  bug in either the pull or push side of our code
*/
static NTSTATUS dcerpc_ndr_validate_out(struct dcerpc_connection *c,
					TALLOC_CTX *mem_ctx,
					void *struct_ptr,
					size_t struct_size,
					NTSTATUS (*ndr_push)(struct ndr_push *, int, void *),
					NTSTATUS (*ndr_pull)(struct ndr_pull *, int, void *))
{
	void *st;
	struct ndr_pull *pull;
	struct ndr_push *push;
	NTSTATUS status;
	DATA_BLOB blob, blob2;

	st = talloc_size(mem_ctx, struct_size);
	if (!st) {
		return NT_STATUS_NO_MEMORY;
	}
	memcpy(st, struct_ptr, struct_size);

	push = ndr_push_init_ctx(mem_ctx);
	if (!push) {
		return NT_STATUS_NO_MEMORY;
	}	

	status = ndr_push(push, NDR_OUT, struct_ptr);
	if (!NT_STATUS_IS_OK(status)) {
		return ndr_push_error(push, NDR_ERR_VALIDATE, 
				      "failed output validation push - %s",
				      nt_errstr(status));
	}

	blob = ndr_push_blob(push);

	pull = ndr_pull_init_flags(c, &blob, mem_ctx);
	if (!pull) {
		return NT_STATUS_NO_MEMORY;
	}

	pull->flags |= LIBNDR_FLAG_REF_ALLOC;
	status = ndr_pull(pull, NDR_OUT, st);
	if (!NT_STATUS_IS_OK(status)) {
		return ndr_pull_error(pull, NDR_ERR_VALIDATE, 
				      "failed output validation pull - %s",
				      nt_errstr(status));
	}

	push = ndr_push_init_ctx(mem_ctx);
	if (!push) {
		return NT_STATUS_NO_MEMORY;
	}	

	status = ndr_push(push, NDR_OUT, st);
	if (!NT_STATUS_IS_OK(status)) {
		return ndr_push_error(push, NDR_ERR_VALIDATE, 
				      "failed output validation push2 - %s",
				      nt_errstr(status));
	}

	blob2 = ndr_push_blob(push);

	if (!data_blob_equal(&blob, &blob2)) {
		DEBUG(3,("original:\n"));
		dump_data(3, blob.data, blob.length);
		DEBUG(3,("secondary:\n"));
		dump_data(3, blob2.data, blob2.length);
		return ndr_push_error(push, NDR_ERR_VALIDATE, 
				      "failed output validation data - %s",
				      nt_errstr(status));
	}

	return NT_STATUS_OK;
}


/*
 send a rpc request given a dcerpc_call structure 
 */
struct rpc_request *dcerpc_ndr_request_send(struct dcerpc_pipe *p,
						const struct GUID *object,
						const struct dcerpc_interface_table *table,
						uint32_t opnum, 
						TALLOC_CTX *mem_ctx, 
						void *r)
{
	const struct dcerpc_interface_call *call;
	struct ndr_push *push;
	NTSTATUS status;
	DATA_BLOB request;
	struct rpc_request *req;

	call = &table->calls[opnum];

	/* setup for a ndr_push_* call */
	push = ndr_push_init_ctx(mem_ctx);
	if (!push) {
		return NULL;
	}

	if (p->conn->flags & DCERPC_PUSH_BIGENDIAN) {
		push->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	/* push the structure into a blob */
	status = call->ndr_push(push, NDR_IN, r);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(2,("Unable to ndr_push structure in dcerpc_ndr_request_send - %s\n",
			 nt_errstr(status)));
		talloc_free(push);
		return NULL;
	}

	/* retrieve the blob */
	request = ndr_push_blob(push);

	if (p->conn->flags & DCERPC_DEBUG_VALIDATE_IN) {
		status = dcerpc_ndr_validate_in(p->conn, push, request, call->struct_size, 
						call->ndr_push, call->ndr_pull);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(2,("Validation failed in dcerpc_ndr_request_send - %s\n",
				 nt_errstr(status)));
			talloc_free(push);
			return NULL;
		}
	}

	DEBUG(10,("rpc request data:\n"));
	dump_data(10, request.data, request.length);

	/* make the actual dcerpc request */
	req = dcerpc_request_send(p, object, opnum, &request);

	if (req != NULL) {
		req->ndr.table = table;
		req->ndr.opnum = opnum;
		req->ndr.struct_ptr = r;
		req->ndr.mem_ctx = mem_ctx;
	}

	talloc_free(push);

	return req;
}

/*
  receive the answer from a dcerpc_ndr_request_send()
*/
NTSTATUS dcerpc_ndr_request_recv(struct rpc_request *req)
{
	struct dcerpc_pipe *p = req->p;
	NTSTATUS status;
	DATA_BLOB response;
	struct ndr_pull *pull;
	uint_t flags;
	TALLOC_CTX *mem_ctx = req->ndr.mem_ctx;
	void *r = req->ndr.struct_ptr;
	uint32_t opnum = req->ndr.opnum;
	const struct dcerpc_interface_table *table = req->ndr.table;
	const struct dcerpc_interface_call *call = &table->calls[opnum];

	/* make sure the recv code doesn't free the request, as we
	   need to grab the flags element before it is freed */
	talloc_increase_ref_count(req);

	status = dcerpc_request_recv(req, mem_ctx, &response);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	flags = req->flags;

	/* prepare for ndr_pull_* */
	pull = ndr_pull_init_flags(p->conn, &response, mem_ctx);
	if (!pull) {
		talloc_free(req);
		return NT_STATUS_NO_MEMORY;
	}

	if (pull->data) {
		pull->data = talloc_steal(pull, pull->data);
	}
	talloc_free(req);

	if (flags & DCERPC_PULL_BIGENDIAN) {
		pull->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	DEBUG(10,("rpc reply data:\n"));
	dump_data(10, pull->data, pull->data_size);

	/* pull the structure from the blob */
	status = call->ndr_pull(pull, NDR_OUT, r);
	if (!NT_STATUS_IS_OK(status)) {
		dcerpc_log_packet(table, opnum, NDR_OUT, 
				  &response);
		return status;
	}

	if (p->conn->flags & DCERPC_DEBUG_VALIDATE_OUT) {
		status = dcerpc_ndr_validate_out(p->conn, pull, r, call->struct_size, 
						 call->ndr_push, call->ndr_pull);
		if (!NT_STATUS_IS_OK(status)) {
			dcerpc_log_packet(table, opnum, NDR_OUT, 
				  &response);
			return status;
		}
	}

	if (pull->offset != pull->data_size) {
		DEBUG(0,("Warning! ignoring %d unread bytes in rpc packet!\n", 
			 pull->data_size - pull->offset));
		/* we used to return NT_STATUS_INFO_LENGTH_MISMATCH here,
		   but it turns out that early versions of NT
		   (specifically NT3.1) add junk onto the end of rpc
		   packets, so if we want to interoperate at all with
		   those versions then we need to ignore this error */
	}

	/* TODO: make pull context independent from the output mem_ctx and free the pull context */

	return NT_STATUS_OK;
}


/*
  a useful helper function for synchronous rpc requests 

  this can be used when you have ndr push/pull functions in the
  standard format
*/
NTSTATUS dcerpc_ndr_request(struct dcerpc_pipe *p,
			    const struct GUID *object,
			    const struct dcerpc_interface_table *table,
			    uint32_t opnum, 
			    TALLOC_CTX *mem_ctx, 
			    void *r)
{
	struct rpc_request *req;

	req = dcerpc_ndr_request_send(p, object, table, opnum, mem_ctx, r);
	if (req == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	return dcerpc_ndr_request_recv(req);
}


/*
  a useful function for retrieving the server name we connected to
*/
const char *dcerpc_server_name(struct dcerpc_pipe *p)
{
	if (!p->conn->transport.peer_name) {
		return "";
	}
	return p->conn->transport.peer_name(p->conn);
}


/*
  get the dcerpc auth_level for a open connection
*/
uint32_t dcerpc_auth_level(struct dcerpc_connection *c) 
{
	uint8_t auth_level;

	if (c->flags & DCERPC_SEAL) {
		auth_level = DCERPC_AUTH_LEVEL_PRIVACY;
	} else if (c->flags & DCERPC_SIGN) {
		auth_level = DCERPC_AUTH_LEVEL_INTEGRITY;
	} else if (c->flags & DCERPC_CONNECT) {
		auth_level = DCERPC_AUTH_LEVEL_CONNECT;
	} else {
		auth_level = DCERPC_AUTH_LEVEL_NONE;
	}
	return auth_level;
}


/* 
   send a dcerpc alter_context request
*/
NTSTATUS dcerpc_alter_context(struct dcerpc_pipe *p, 
			      TALLOC_CTX *mem_ctx,
			      const struct dcerpc_syntax_id *syntax,
			      const struct dcerpc_syntax_id *transfer_syntax)
{
	struct ncacn_packet pkt;
	NTSTATUS status;
	DATA_BLOB blob;

	p->syntax = *syntax;
	p->transfer_syntax = *transfer_syntax;

	init_dcerpc_hdr(p->conn, &pkt);

	pkt.ptype = DCERPC_PKT_ALTER;
	pkt.pfc_flags = DCERPC_PFC_FLAG_FIRST | DCERPC_PFC_FLAG_LAST;
	pkt.call_id = p->conn->call_id;
	pkt.auth_length = 0;

	pkt.u.alter.max_xmit_frag = 5840;
	pkt.u.alter.max_recv_frag = 5840;
	pkt.u.alter.assoc_group_id = 0;
	pkt.u.alter.num_contexts = 1;
	pkt.u.alter.ctx_list = talloc_array(mem_ctx, struct dcerpc_ctx_list, 1);
	if (!pkt.u.alter.ctx_list) {
		return NT_STATUS_NO_MEMORY;
	}
	pkt.u.alter.ctx_list[0].context_id = p->context_id;
	pkt.u.alter.ctx_list[0].num_transfer_syntaxes = 1;
	pkt.u.alter.ctx_list[0].abstract_syntax = p->syntax;
	pkt.u.alter.ctx_list[0].transfer_syntaxes = &p->transfer_syntax;
	pkt.u.alter.auth_info = data_blob(NULL, 0);

	/* construct the NDR form of the packet */
	status = dcerpc_push_auth(&blob, mem_ctx, &pkt, p->conn->security_state.auth_info);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* send it on its way */
	status = full_request(p->conn, mem_ctx, &blob, &blob);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	/* unmarshall the NDR */
	status = dcerpc_pull(p->conn, &blob, mem_ctx, &pkt);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	if (pkt.ptype == DCERPC_PKT_ALTER_RESP &&
	    pkt.u.alter_resp.num_results == 1 &&
	    pkt.u.alter_resp.ctx_list[0].result != 0) {
		DEBUG(2,("dcerpc: alter_resp failed - reason %d\n", 
			 pkt.u.alter_resp.ctx_list[0].reason));
		return dcerpc_map_reason(pkt.u.alter_resp.ctx_list[0].reason);
	}

	if (pkt.ptype != DCERPC_PKT_ALTER_RESP ||
	    pkt.u.alter_resp.num_results == 0 ||
	    pkt.u.alter_resp.ctx_list[0].result != 0) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	/* the alter_resp might contain a reply set of credentials */
	if (p->conn->security_state.auth_info && pkt.u.alter_resp.auth_info.length) {
		status = ndr_pull_struct_blob(&pkt.u.alter_resp.auth_info,
					      mem_ctx,
					      p->conn->security_state.auth_info,
					      (ndr_pull_flags_fn_t)ndr_pull_dcerpc_auth);
	}

	return status;	
}
