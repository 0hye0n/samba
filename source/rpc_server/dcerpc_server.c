/* 
   Unix SMB/CIFS implementation.

   server side dcerpc core code

   Copyright (C) Andrew Tridgell 2003-2005
   Copyright (C) Stefan (metze) Metzmacher 2004-2005
   
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
#include "librpc/gen_ndr/ndr_epmapper.h"
#include "librpc/gen_ndr/ndr_oxidresolver.h"
#include "auth/auth.h"
#include "dlinklist.h"
#include "rpc_server/dcerpc_server.h"
#include "events.h"

/*
  see if two endpoints match
*/
static BOOL endpoints_match(const struct dcerpc_binding *ep1,
			    const struct dcerpc_binding *ep2)
{
	if (ep1->transport != ep2->transport) {
		return False;
	}

	if (!ep1->endpoint || !ep2->endpoint) {
		return ep1->endpoint == ep2->endpoint;
	}

	if (strcasecmp(ep1->endpoint, ep2->endpoint) != 0) 
		return False;

	return True;
}

/*
  find an endpoint in the dcesrv_context
*/
static struct dcesrv_endpoint *find_endpoint(struct dcesrv_context *dce_ctx,
					     const struct dcerpc_binding *ep_description)
{
	struct dcesrv_endpoint *ep;
	for (ep=dce_ctx->endpoint_list; ep; ep=ep->next) {
		if (endpoints_match(&ep->ep_description, ep_description)) {
			return ep;
		}
	}
	return NULL;
}

/*
  find a registered context_id from a bind or alter_context
*/
static struct dcesrv_connection_context *dcesrv_find_context(struct dcesrv_connection *conn, 
								   uint32_t context_id)
{
	struct dcesrv_connection_context *c;
	for (c=conn->contexts;c;c=c->next) {
		if (c->context_id == context_id) return c;
	}
	return NULL;
}

/*
  see if a uuid and if_version match to an interface
*/
static BOOL interface_match(const struct dcesrv_interface *if1,
							const struct dcesrv_interface *if2)
{
	if (if1->if_version != if2->if_version) {
		return False;
	}

	if (strcmp(if1->uuid, if2->uuid)==0) {
		return True;
	}			

	return False;
}

/*
  find the interface operations on an endpoint
*/
static const struct dcesrv_interface *find_interface(const struct dcesrv_endpoint *endpoint,
						     const struct dcesrv_interface *iface)
{
	struct dcesrv_if_list *ifl;
	for (ifl=endpoint->interface_list; ifl; ifl=ifl->next) {
		if (interface_match(&(ifl->iface), iface)) {
			return &(ifl->iface);
		}
	}
	return NULL;
}

/*
  see if a uuid and if_version match to an interface
*/
static BOOL interface_match_by_uuid(const struct dcesrv_interface *iface,
				    const char *uuid, uint32_t if_version)
{
	if (iface->if_version != if_version) {
		return False;
	}

	if (strcmp(iface->uuid, uuid)==0) {
		return True;
	}			

	return False;
}

/*
  find the interface operations on an endpoint by uuid
*/
static const struct dcesrv_interface *find_interface_by_uuid(const struct dcesrv_endpoint *endpoint,
							     const char *uuid, uint32_t if_version)
{
	struct dcesrv_if_list *ifl;
	for (ifl=endpoint->interface_list; ifl; ifl=ifl->next) {
		if (interface_match_by_uuid(&(ifl->iface), uuid, if_version)) {
			return &(ifl->iface);
		}
	}
	return NULL;
}

/*
  find a call that is pending in our call list
*/
static struct dcesrv_call_state *dcesrv_find_call(struct dcesrv_connection *dce_conn, uint16_t call_id)
{
	struct dcesrv_call_state *c;
	for (c=dce_conn->call_list;c;c=c->next) {
		if (c->pkt.call_id == call_id) {
			return c;
		}
	}
	return NULL;
}

/*
  register an interface on an endpoint
*/
NTSTATUS dcesrv_interface_register(struct dcesrv_context *dce_ctx,
				   const char *ep_name,
				   const struct dcesrv_interface *iface,
				   const struct security_descriptor *sd)
{
	struct dcesrv_endpoint *ep;
	struct dcesrv_if_list *ifl;
	struct dcerpc_binding binding;
	BOOL add_ep = False;
	NTSTATUS status;
	
	status = dcerpc_parse_binding(dce_ctx, ep_name, &binding);

	if (NT_STATUS_IS_ERR(status)) {
		DEBUG(0, ("Trouble parsing binding string '%s'\n", ep_name));
		return status;
	}

	/* check if this endpoint exists
	 */
	if ((ep=find_endpoint(dce_ctx, &binding))==NULL) {
		ep = talloc_p(dce_ctx, struct dcesrv_endpoint);
		if (!ep) {
			return NT_STATUS_NO_MEMORY;
		}
		ZERO_STRUCTP(ep);
		ep->ep_description = binding;
		add_ep = True;
	}

	/* see if the interface is already registered on te endpoint */
	if (find_interface(ep, iface)!=NULL) {
		DEBUG(0,("dcesrv_interface_register: interface '%s' already registered on endpoint '%s'\n",
			iface->name, ep_name));
		return NT_STATUS_OBJECT_NAME_COLLISION;
	}

	/* talloc a new interface list element */
	ifl = talloc_p(dce_ctx, struct dcesrv_if_list);
	if (!ifl) {
		return NT_STATUS_NO_MEMORY;
	}

	/* copy the given interface struct to the one on the endpoints interface list */
	memcpy(&(ifl->iface),iface, sizeof(struct dcesrv_interface));

	/* if we have a security descriptor given,
	 * we should see if we can set it up on the endpoint
	 */
	if (sd != NULL) {
		/* if there's currently no security descriptor given on the endpoint
		 * we try to set it
		 */
		if (ep->sd == NULL) {
			ep->sd = security_descriptor_copy(dce_ctx, sd);
		}

		/* if now there's no security descriptor given on the endpoint
		 * something goes wrong, either we failed to copy the security descriptor
		 * or there was already one on the endpoint
		 */
		if (ep->sd != NULL) {
			DEBUG(0,("dcesrv_interface_register: interface '%s' failed to setup a security descriptor\n"
			         "                           on endpoint '%s'\n",
				iface->name, ep_name));
			if (add_ep) free(ep);
			free(ifl);
			return NT_STATUS_OBJECT_NAME_COLLISION;
		}
	}

	/* finally add the interface on the endpoint */
	DLIST_ADD(ep->interface_list, ifl);

	/* if it's a new endpoint add it to the dcesrv_context */
	if (add_ep) {
		DLIST_ADD(dce_ctx->endpoint_list, ep);
	}

	DEBUG(4,("dcesrv_interface_register: interface '%s' registered on endpoint '%s'\n",
		iface->name, ep_name));

	return NT_STATUS_OK;
}

static NTSTATUS dcesrv_inherited_session_key(struct dcesrv_connection *p,
					      DATA_BLOB *session_key)
{
	if (p->auth_state.session_info->session_key.length) {
		*session_key = p->auth_state.session_info->session_key;
		return NT_STATUS_OK;
	}
	return NT_STATUS_NO_USER_SESSION_KEY;
}

NTSTATUS dcesrv_generic_session_key(struct dcesrv_connection *p,
				    DATA_BLOB *session_key)
{
	/* this took quite a few CPU cycles to find ... */
	session_key->data = discard_const_p(uint8_t, "SystemLibraryDTC");
	session_key->length = 16;
	return NT_STATUS_OK;
}

/*
  fetch the user session key - may be default (above) or the SMB session key
*/
NTSTATUS dcesrv_fetch_session_key(struct dcesrv_connection *p,
				  DATA_BLOB *session_key)
{
	return p->auth_state.session_key(p, session_key);
}


/*
  destroy a link to an endpoint
*/
static int dcesrv_endpoint_destructor(void *ptr)
{
	struct dcesrv_connection *p = ptr;

	while (p->contexts) {
		struct dcesrv_connection_context *c = p->contexts;

		DLIST_REMOVE(p->contexts, c);

		if (c->iface) {
			c->iface->unbind(c, c->iface);
		}
	}

	return 0;
}


/*
  connect to a dcerpc endpoint
*/
NTSTATUS dcesrv_endpoint_connect(struct dcesrv_context *dce_ctx,
				 TALLOC_CTX *mem_ctx,
				 const struct dcesrv_endpoint *ep,
				 struct server_connection *srv_conn,
				 struct dcesrv_connection **_p)
{
	struct dcesrv_connection *p;

	p = talloc(mem_ctx, struct dcesrv_connection);
	NT_STATUS_HAVE_NO_MEMORY(p);

	p->dce_ctx = dce_ctx;
	p->endpoint = ep;
	p->contexts = NULL;
	p->call_list = NULL;
	p->pending_call_list = NULL;
	p->cli_max_recv_frag = 0;
	p->partial_input = data_blob(NULL, 0);
	p->auth_state.auth_info = NULL;
	p->auth_state.gensec_security = NULL;
	p->auth_state.session_info = NULL;
	p->auth_state.session_key = dcesrv_generic_session_key;
	p->srv_conn = srv_conn;

	talloc_set_destructor(p, dcesrv_endpoint_destructor);

	*_p = p;
	return NT_STATUS_OK;
}

/*
  search and connect to a dcerpc endpoint
*/
NTSTATUS dcesrv_endpoint_search_connect(struct dcesrv_context *dce_ctx,
					TALLOC_CTX *mem_ctx,
					const struct dcerpc_binding *ep_description,
					struct auth_session_info *session_info,
					struct server_connection *srv_conn,
					struct dcesrv_connection **dce_conn_p)
{
	NTSTATUS status;
	const struct dcesrv_endpoint *ep;

	/* make sure this endpoint exists */
	ep = find_endpoint(dce_ctx, ep_description);
	if (!ep) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	status = dcesrv_endpoint_connect(dce_ctx, mem_ctx, ep, srv_conn, dce_conn_p);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	(*dce_conn_p)->auth_state.session_info = talloc_reference((*dce_conn_p), session_info);
	(*dce_conn_p)->auth_state.session_key = dcesrv_inherited_session_key;

	/* TODO: check security descriptor of the endpoint here 
	 *       if it's a smb named pipe
	 *	 if it's failed free dce_conn_p
	 */

	return NT_STATUS_OK;
}


static void dcesrv_init_hdr(struct dcerpc_packet *pkt)
{
	pkt->rpc_vers = 5;
	pkt->rpc_vers_minor = 0;
	if (lp_rpc_big_endian()) {
		pkt->drep[0] = 0;
	} else {
		pkt->drep[0] = DCERPC_DREP_LE;
	}
	pkt->drep[1] = 0;
	pkt->drep[2] = 0;
	pkt->drep[3] = 0;
}

/*
  return a dcerpc fault
*/
static NTSTATUS dcesrv_fault(struct dcesrv_call_state *call, uint32_t fault_code)
{
	struct dcerpc_packet pkt;
	struct dcesrv_call_reply *rep;
	NTSTATUS status;

	/* setup a bind_ack */
	dcesrv_init_hdr(&pkt);
	pkt.auth_length = 0;
	pkt.call_id = call->pkt.call_id;
	pkt.ptype = DCERPC_PKT_FAULT;
	pkt.pfc_flags = DCERPC_PFC_FLAG_FIRST | DCERPC_PFC_FLAG_LAST;
	pkt.u.fault.alloc_hint = 0;
	pkt.u.fault.context_id = 0;
	pkt.u.fault.cancel_count = 0;
	pkt.u.fault.status = fault_code;

	rep = talloc_p(call, struct dcesrv_call_reply);
	if (!rep) {
		return NT_STATUS_NO_MEMORY;
	}

	status = dcerpc_push_auth(&rep->data, call, &pkt, NULL);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	dcerpc_set_frag_length(&rep->data, rep->data.length);

	DLIST_ADD_END(call->replies, rep, struct dcesrv_call_reply *);
	DLIST_ADD_END(call->conn->call_list, call, struct dcesrv_call_state *);

	return NT_STATUS_OK;	
}


/*
  return a dcerpc bind_nak
*/
static NTSTATUS dcesrv_bind_nak(struct dcesrv_call_state *call, uint32_t reason)
{
	struct dcerpc_packet pkt;
	struct dcesrv_call_reply *rep;
	NTSTATUS status;

	/* setup a bind_nak */
	dcesrv_init_hdr(&pkt);
	pkt.auth_length = 0;
	pkt.call_id = call->pkt.call_id;
	pkt.ptype = DCERPC_PKT_BIND_NAK;
	pkt.pfc_flags = DCERPC_PFC_FLAG_FIRST | DCERPC_PFC_FLAG_LAST;
	pkt.u.bind_nak.reject_reason = reason;
	pkt.u.bind_nak.num_versions = 0;

	rep = talloc_p(call, struct dcesrv_call_reply);
	if (!rep) {
		return NT_STATUS_NO_MEMORY;
	}

	status = dcerpc_push_auth(&rep->data, call, &pkt, NULL);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	dcerpc_set_frag_length(&rep->data, rep->data.length);

	DLIST_ADD_END(call->replies, rep, struct dcesrv_call_reply *);
	DLIST_ADD_END(call->conn->call_list, call, struct dcesrv_call_state *);

	return NT_STATUS_OK;	
}


/*
  handle a bind request
*/
static NTSTATUS dcesrv_bind(struct dcesrv_call_state *call)
{
	const char *uuid, *transfer_syntax;
	uint32_t if_version, transfer_syntax_version;
	struct dcerpc_packet pkt;
	struct dcesrv_call_reply *rep;
	NTSTATUS status;
	uint32_t result=0, reason=0;
	uint32_t context_id;
	const struct dcesrv_interface *iface;

	if (call->pkt.u.bind.num_contexts != 1 ||
	    call->pkt.u.bind.ctx_list[0].num_transfer_syntaxes < 1) {
		return dcesrv_bind_nak(call, 0);
	}

	context_id = call->pkt.u.bind.ctx_list[0].context_id;

	/* you can't bind twice on one context */
	if (dcesrv_find_context(call->conn, context_id) != NULL) {
		return dcesrv_bind_nak(call, 0);
	}

	if_version = call->pkt.u.bind.ctx_list[0].abstract_syntax.if_version;
	uuid = GUID_string(call, &call->pkt.u.bind.ctx_list[0].abstract_syntax.uuid);
	if (!uuid) {
		return dcesrv_bind_nak(call, 0);
	}

	transfer_syntax_version = call->pkt.u.bind.ctx_list[0].transfer_syntaxes[0].if_version;
	transfer_syntax = GUID_string(call, 
				      &call->pkt.u.bind.ctx_list[0].transfer_syntaxes[0].uuid);
	if (!transfer_syntax ||
	    strcasecmp(NDR_GUID, transfer_syntax) != 0 ||
	    NDR_GUID_VERSION != transfer_syntax_version) {
		/* we only do NDR encoded dcerpc */
		return dcesrv_bind_nak(call, 0);
	}

	iface = find_interface_by_uuid(call->conn->endpoint, uuid, if_version);
	if (iface == NULL) {
		DEBUG(2,("Request for unknown dcerpc interface %s/%d\n", uuid, if_version));
		/* we don't know about that interface */
		result = DCERPC_BIND_PROVIDER_REJECT;
		reason = DCERPC_BIND_REASON_ASYNTAX;		
	}

	if (iface) {
		/* add this context to the list of available context_ids */
		struct dcesrv_connection_context *context = talloc(call->conn, 
								   struct dcesrv_connection_context);
		if (context == NULL) {
			return dcesrv_bind_nak(call, 0);
		}
		context->conn = call->conn;
		context->iface = iface;
		context->context_id = context_id;
		context->private = NULL;
		context->handles = NULL;
		DLIST_ADD(call->conn->contexts, context);
		call->context = context;
	}

	if (call->conn->cli_max_recv_frag == 0) {
		call->conn->cli_max_recv_frag = call->pkt.u.bind.max_recv_frag;
	}

	/* handle any authentication that is being requested */
	if (!dcesrv_auth_bind(call)) {
		/* TODO: work out the right reject code */
		return dcesrv_bind_nak(call, 0);
	}

	/* setup a bind_ack */
	dcesrv_init_hdr(&pkt);
	pkt.auth_length = 0;
	pkt.call_id = call->pkt.call_id;
	pkt.ptype = DCERPC_PKT_BIND_ACK;
	pkt.pfc_flags = DCERPC_PFC_FLAG_FIRST | DCERPC_PFC_FLAG_LAST;
	pkt.u.bind_ack.max_xmit_frag = 0x2000;
	pkt.u.bind_ack.max_recv_frag = 0x2000;
	pkt.u.bind_ack.assoc_group_id = call->pkt.u.bind.assoc_group_id;
	if (iface) {
		/* FIXME: Use pipe name as specified by endpoint instead of interface name */
		pkt.u.bind_ack.secondary_address = talloc_asprintf(call, "\\PIPE\\%s", iface->name);
	} else {
		pkt.u.bind_ack.secondary_address = "";
	}
	pkt.u.bind_ack.num_results = 1;
	pkt.u.bind_ack.ctx_list = talloc_p(call, struct dcerpc_ack_ctx);
	if (!pkt.u.bind_ack.ctx_list) {
		return NT_STATUS_NO_MEMORY;
	}
	pkt.u.bind_ack.ctx_list[0].result = result;
	pkt.u.bind_ack.ctx_list[0].reason = reason;
	GUID_from_string(NDR_GUID, &pkt.u.bind_ack.ctx_list[0].syntax.uuid);
	pkt.u.bind_ack.ctx_list[0].syntax.if_version = NDR_GUID_VERSION;
	pkt.u.bind_ack.auth_info = data_blob(NULL, 0);

	if (!dcesrv_auth_bind_ack(call, &pkt)) {
		return dcesrv_bind_nak(call, 0);
	}

	if (iface) {
		status = iface->bind(call, iface);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(2,("Request for dcerpc interface %s/%d rejected: %s\n", 
				 uuid, if_version, nt_errstr(status)));
			return dcesrv_bind_nak(call, 0);
		}
	}

	rep = talloc(call, struct dcesrv_call_reply);
	if (!rep) {
		return NT_STATUS_NO_MEMORY;
	}

	status = dcerpc_push_auth(&rep->data, call, &pkt, 
				  call->conn->auth_state.auth_info);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	dcerpc_set_frag_length(&rep->data, rep->data.length);

	DLIST_ADD_END(call->replies, rep, struct dcesrv_call_reply *);
	DLIST_ADD_END(call->conn->call_list, call, struct dcesrv_call_state *);

	return NT_STATUS_OK;
}


/*
  handle a auth3 request
*/
static NTSTATUS dcesrv_auth3(struct dcesrv_call_state *call)
{
	/* handle the auth3 in the auth code */
	if (!dcesrv_auth_auth3(call)) {
		return dcesrv_fault(call, DCERPC_FAULT_OTHER);
	}

	talloc_free(call);

	/* we don't send a reply to a auth3 request, except by a
	   fault */
	return NT_STATUS_OK;
}


/*
  handle a bind request
*/
static NTSTATUS dcesrv_alter_new_context(struct dcesrv_call_state *call, uint32 context_id)
{
	uint32_t if_version, transfer_syntax_version;
	const char *uuid, *transfer_syntax;
	struct dcesrv_connection_context *context;
	const struct dcesrv_interface *iface;

	if_version = call->pkt.u.alter.ctx_list[0].abstract_syntax.if_version;
	uuid = GUID_string(call, &call->pkt.u.alter.ctx_list[0].abstract_syntax.uuid);
	if (!uuid) {
		return NT_STATUS_NO_MEMORY;
	}

	transfer_syntax_version = call->pkt.u.alter.ctx_list[0].transfer_syntaxes[0].if_version;
	transfer_syntax = GUID_string(call, 
				      &call->pkt.u.alter.ctx_list[0].transfer_syntaxes[0].uuid);
	if (!transfer_syntax ||
	    strcasecmp(NDR_GUID, transfer_syntax) != 0 ||
	    NDR_GUID_VERSION != transfer_syntax_version) {
		/* we only do NDR encoded dcerpc */
		return NT_STATUS_NO_MEMORY;
	}

	iface = find_interface_by_uuid(call->conn->endpoint, uuid, if_version);
	if (iface == NULL) {
		DEBUG(2,("Request for unknown dcerpc interface %s/%d\n", uuid, if_version));
		return NT_STATUS_RPC_PROTSEQ_NOT_SUPPORTED;
	}

	/* add this context to the list of available context_ids */
	context = talloc(call->conn, struct dcesrv_connection_context);
	if (context == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	context->conn = call->conn;
	context->iface = iface;
	context->context_id = context_id;
	context->private = NULL;
	context->handles = NULL;
	DLIST_ADD(call->conn->contexts, context);
	call->context = context;

	return NT_STATUS_OK;
}


/*
  handle a bind request
*/
static NTSTATUS dcesrv_alter(struct dcesrv_call_state *call)
{
	struct dcerpc_packet pkt;
	struct dcesrv_call_reply *rep;
	NTSTATUS status;
	uint32_t result=0, reason=0;
	uint32_t context_id;

	/* handle any authentication that is being requested */
	if (!dcesrv_auth_alter(call)) {
		/* TODO: work out the right reject code */
		result = DCERPC_BIND_PROVIDER_REJECT;
		reason = DCERPC_BIND_REASON_ASYNTAX;		
	}

	context_id = call->pkt.u.alter.ctx_list[0].context_id;

	/* see if they are asking for a new interface */
	if (result == 0 &&
	    dcesrv_find_context(call->conn, context_id) == NULL) {
		status = dcesrv_alter_new_context(call, context_id);
		if (!NT_STATUS_IS_OK(status)) {
			result = DCERPC_BIND_PROVIDER_REJECT;
			reason = DCERPC_BIND_REASON_ASYNTAX;		
		}
	}

	/* setup a alter_resp */
	dcesrv_init_hdr(&pkt);
	pkt.auth_length = 0;
	pkt.call_id = call->pkt.call_id;
	pkt.ptype = DCERPC_PKT_ALTER_RESP;
	pkt.pfc_flags = DCERPC_PFC_FLAG_FIRST | DCERPC_PFC_FLAG_LAST;
	pkt.u.alter_resp.max_xmit_frag = 0x2000;
	pkt.u.alter_resp.max_recv_frag = 0x2000;
	pkt.u.alter_resp.assoc_group_id = call->pkt.u.alter.assoc_group_id;
	pkt.u.alter_resp.num_results = 1;
	pkt.u.alter_resp.ctx_list = talloc_array(call, struct dcerpc_ack_ctx, 1);
	if (!pkt.u.alter_resp.ctx_list) {
		return NT_STATUS_NO_MEMORY;
	}
	pkt.u.alter_resp.ctx_list[0].result = result;
	pkt.u.alter_resp.ctx_list[0].reason = reason;
	GUID_from_string(NDR_GUID, &pkt.u.alter_resp.ctx_list[0].syntax.uuid);
	pkt.u.alter_resp.ctx_list[0].syntax.if_version = NDR_GUID_VERSION;
	pkt.u.alter_resp.auth_info = data_blob(NULL, 0);
	pkt.u.alter_resp.secondary_address = "";

	if (!dcesrv_auth_alter_ack(call, &pkt)) {
		return dcesrv_bind_nak(call, 0);
	}

	rep = talloc_p(call, struct dcesrv_call_reply);
	if (!rep) {
		return NT_STATUS_NO_MEMORY;
	}

	status = dcerpc_push_auth(&rep->data, call, &pkt, 
				  call->conn->auth_state.auth_info);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	dcerpc_set_frag_length(&rep->data, rep->data.length);

	DLIST_ADD_END(call->replies, rep, struct dcesrv_call_reply *);
	DLIST_ADD_END(call->conn->call_list, call, struct dcesrv_call_state *);

	return NT_STATUS_OK;
}

/*
  handle a dcerpc request packet
*/
static NTSTATUS dcesrv_request(struct dcesrv_call_state *call)
{
	struct ndr_pull *pull;
	NTSTATUS status;
	struct dcesrv_connection_context *context;

	call->fault_code	= 0;
	call->state_flags	= call->conn->dce_ctx->state_flags;
	call->time		= timeval_current();

	context = dcesrv_find_context(call->conn, call->pkt.u.request.context_id);
	if (context == NULL) {
		return dcesrv_fault(call, DCERPC_FAULT_UNK_IF);
	}

	pull = ndr_pull_init_blob(&call->pkt.u.request.stub_and_verifier, call);
	NT_STATUS_HAVE_NO_MEMORY(pull);

	call->context	= context;
	call->event_ctx	= context->conn->srv_conn->event.ctx;
	call->ndr_pull	= pull;

	if (call->pkt.pfc_flags & DCERPC_PFC_FLAG_ORPC) {
		pull->flags |= LIBNDR_FLAG_OBJECT_PRESENT;
	}

	if (!(call->pkt.drep[0] & DCERPC_DREP_LE)) {
		pull->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	/* unravel the NDR for the packet */
	status = context->iface->ndr_pull(call, call, pull, &call->r);
	if (!NT_STATUS_IS_OK(status)) {
		return dcesrv_fault(call, call->fault_code);
	}

	if (pull->offset != pull->data_size) {
		DEBUG(3,("Warning: %d extra bytes in incoming RPC request\n", 
			 pull->data_size - pull->offset));
		dump_data(10, pull->data+pull->offset, pull->data_size - pull->offset);
	}

	/* call the dispatch function */
	status = context->iface->dispatch(call, call, call->r);
	if (!NT_STATUS_IS_OK(status)) {
		return dcesrv_fault(call, call->fault_code);
	}

	/* add the call to the pending list */
	DLIST_ADD_END(call->conn->pending_call_list, call, struct dcesrv_call_state *);

	if (call->state_flags & DCESRV_CALL_STATE_FLAG_ASYNC) {
		return NT_STATUS_OK;
	}

	return dcesrv_reply(call);
}

NTSTATUS dcesrv_reply(struct dcesrv_call_state *call)
{
	struct ndr_push *push;
	NTSTATUS status;
	DATA_BLOB stub;
	uint32_t total_length;
	struct dcesrv_connection_context *context = call->context;

	/* call the reply function */
	status = context->iface->reply(call, call, call->r);
	if (!NT_STATUS_IS_OK(status)) {
		return dcesrv_fault(call, call->fault_code);
	}

	/* form the reply NDR */
	push = ndr_push_init_ctx(call);
	NT_STATUS_HAVE_NO_MEMORY(push);

	/* carry over the pointer count to the reply in case we are
	   using full pointer. See NDR specification for full
	   pointers */
	push->ptr_count = call->ndr_pull->ptr_count;

	if (lp_rpc_big_endian()) {
		push->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	status = context->iface->ndr_push(call, call, push, call->r);
	if (!NT_STATUS_IS_OK(status)) {
		return dcesrv_fault(call, call->fault_code);
	}

	stub = ndr_push_blob(push);

	total_length = stub.length;

	do {
		uint32_t length;
		struct dcesrv_call_reply *rep;
		struct dcerpc_packet pkt;

		rep = talloc(call, struct dcesrv_call_reply);
		NT_STATUS_HAVE_NO_MEMORY(rep);

		length = stub.length;
		if (length + DCERPC_RESPONSE_LENGTH > call->conn->cli_max_recv_frag) {
			/* the 32 is to cope with signing data */
			length = call->conn->cli_max_recv_frag - 
				(DCERPC_MAX_SIGN_SIZE+DCERPC_RESPONSE_LENGTH);
		}

		/* form the dcerpc response packet */
		dcesrv_init_hdr(&pkt);
		pkt.auth_length = 0;
		pkt.call_id = call->pkt.call_id;
		pkt.ptype = DCERPC_PKT_RESPONSE;
		pkt.pfc_flags = 0;
		if (stub.length == total_length) {
			pkt.pfc_flags |= DCERPC_PFC_FLAG_FIRST;
		}
		if (length == stub.length) {
			pkt.pfc_flags |= DCERPC_PFC_FLAG_LAST;
		}
		pkt.u.response.alloc_hint = stub.length;
		pkt.u.response.context_id = call->pkt.u.request.context_id;
		pkt.u.response.cancel_count = 0;
		pkt.u.response.stub_and_verifier.data = stub.data;
		pkt.u.response.stub_and_verifier.length = length;

		if (!dcesrv_auth_response(call, &rep->data, &pkt)) {
			return dcesrv_fault(call, DCERPC_FAULT_OTHER);		
		}

		dcerpc_set_frag_length(&rep->data, rep->data.length);

		DLIST_ADD_END(call->replies, rep, struct dcesrv_call_reply *);
		
		stub.data += length;
		stub.length -= length;
	} while (stub.length != 0);

	/* move the call from the pending to the finished calls list */
	DLIST_REMOVE(call->conn->pending_call_list, call);
	DLIST_ADD_END(call->conn->call_list, call, struct dcesrv_call_state *);

	if (call->conn->call_list && call->conn->call_list->replies) {
		if (call->conn->srv_conn &&
		    call->conn->srv_conn->event.fde) {
			call->conn->srv_conn->event.fde->flags |= EVENT_FD_WRITE;
		}
	}

	return NT_STATUS_OK;
}


/*
  work out if we have a full packet yet
*/
static BOOL dce_full_packet(const DATA_BLOB *data)
{
	if (data->length < DCERPC_FRAG_LEN_OFFSET+2) {
		return False;
	}
	if (dcerpc_get_frag_length(data) > data->length) {
		return False;
	}
	return True;
}

/*
  we might have consumed only part of our input - advance past that part
*/
static void dce_partial_advance(struct dcesrv_connection *dce_conn, uint32_t offset)
{
	DATA_BLOB blob;

	if (dce_conn->partial_input.length == offset) {
		data_blob_free(&dce_conn->partial_input);
		return;
	}

	blob = dce_conn->partial_input;
	dce_conn->partial_input = data_blob(blob.data + offset,
					    blob.length - offset);
	data_blob_free(&blob);
}

/*
  process some input to a dcerpc endpoint server.
*/
NTSTATUS dcesrv_input_process(struct dcesrv_connection *dce_conn)
{
	struct ndr_pull *ndr;
	NTSTATUS status;
	struct dcesrv_call_state *call;
	DATA_BLOB blob;

	call = talloc(dce_conn, struct dcesrv_call_state);
	if (!call) {
		talloc_free(dce_conn->partial_input.data);
		return NT_STATUS_NO_MEMORY;
	}
	call->conn = dce_conn;
	call->replies = NULL;
	call->context = NULL;

	blob = dce_conn->partial_input;
	blob.length = dcerpc_get_frag_length(&blob);

	ndr = ndr_pull_init_blob(&blob, call);
	if (!ndr) {
		talloc_free(dce_conn->partial_input.data);
		talloc_free(call);
		return NT_STATUS_NO_MEMORY;
	}

	if (!(CVAL(blob.data, DCERPC_DREP_OFFSET) & DCERPC_DREP_LE)) {
		ndr->flags |= LIBNDR_FLAG_BIGENDIAN;
	}

	status = ndr_pull_dcerpc_packet(ndr, NDR_SCALARS|NDR_BUFFERS, &call->pkt);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(dce_conn->partial_input.data);
		talloc_free(call);
		return status;
	}

	/* we have to check the signing here, before combining the
	   pdus */
	if (call->pkt.ptype == DCERPC_PKT_REQUEST &&
	    !dcesrv_auth_request(call, &blob)) {
		dce_partial_advance(dce_conn, blob.length);
		return dcesrv_fault(call, DCERPC_FAULT_LOGON_FAILURE);		
	}

	dce_partial_advance(dce_conn, blob.length);

	/* see if this is a continued packet */
	if (call->pkt.ptype == DCERPC_PKT_REQUEST &&
	    !(call->pkt.pfc_flags & DCERPC_PFC_FLAG_FIRST)) {
		struct dcesrv_call_state *call2 = call;
		uint32_t alloc_size;

		/* we only allow fragmented requests, no other packet types */
		if (call->pkt.ptype != DCERPC_PKT_REQUEST) {
			return dcesrv_fault(call2, DCERPC_FAULT_OTHER);
		}

		/* this is a continuation of an existing call - find the call then
		   tack it on the end */
		call = dcesrv_find_call(dce_conn, call2->pkt.call_id);
		if (!call) {
			return dcesrv_fault(call2, DCERPC_FAULT_OTHER);
		}

		if (call->pkt.ptype != call2->pkt.ptype) {
			/* trying to play silly buggers are we? */
			return dcesrv_fault(call2, DCERPC_FAULT_OTHER);
		}

		alloc_size = call->pkt.u.request.stub_and_verifier.length +
			call2->pkt.u.request.stub_and_verifier.length;
		if (call->pkt.u.request.alloc_hint > alloc_size) {
			alloc_size = call->pkt.u.request.alloc_hint;
		}

		call->pkt.u.request.stub_and_verifier.data = 
			talloc_realloc(call, 
				       call->pkt.u.request.stub_and_verifier.data, 
				       uint8_t, alloc_size);
		if (!call->pkt.u.request.stub_and_verifier.data) {
			return dcesrv_fault(call2, DCERPC_FAULT_OTHER);
		}
		memcpy(call->pkt.u.request.stub_and_verifier.data +
		       call->pkt.u.request.stub_and_verifier.length,
		       call2->pkt.u.request.stub_and_verifier.data,
		       call2->pkt.u.request.stub_and_verifier.length);
		call->pkt.u.request.stub_and_verifier.length += 
			call2->pkt.u.request.stub_and_verifier.length;

		call->pkt.pfc_flags |= (call2->pkt.pfc_flags & DCERPC_PFC_FLAG_LAST);

		talloc_free(call2);
	}

	/* this may not be the last pdu in the chain - if its isn't then
	   just put it on the call_list and wait for the rest */
	if (call->pkt.ptype == DCERPC_PKT_REQUEST &&
	    !(call->pkt.pfc_flags & DCERPC_PFC_FLAG_LAST)) {
		DLIST_ADD_END(dce_conn->call_list, call, struct dcesrv_call_state *);
		return NT_STATUS_OK;
	}

	switch (call->pkt.ptype) {
	case DCERPC_PKT_BIND:
		status = dcesrv_bind(call);
		break;
	case DCERPC_PKT_AUTH3:
		status = dcesrv_auth3(call);
		break;
	case DCERPC_PKT_ALTER:
		status = dcesrv_alter(call);
		break;
	case DCERPC_PKT_REQUEST:
		status = dcesrv_request(call);
		break;
	default:
		status = NT_STATUS_INVALID_PARAMETER;
		break;
	}

	/* if we are going to be sending a reply then add
	   it to the list of pending calls. We add it to the end to keep the call
	   list in the order we will answer */
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(call);
	}

	return status;
}


/*
  provide some input to a dcerpc endpoint server. This passes data
  from a dcerpc client into the server
*/
NTSTATUS dcesrv_input(struct dcesrv_connection *dce_conn, const DATA_BLOB *data)
{
	NTSTATUS status;

	dce_conn->partial_input.data = talloc_realloc(dce_conn,
						      dce_conn->partial_input.data,
						      uint8_t,
						      dce_conn->partial_input.length + data->length);
	if (!dce_conn->partial_input.data) {
		return NT_STATUS_NO_MEMORY;
	}
	memcpy(dce_conn->partial_input.data + dce_conn->partial_input.length,
	       data->data, data->length);
	dce_conn->partial_input.length += data->length;

	while (dce_full_packet(&dce_conn->partial_input)) {
		status = dcesrv_input_process(dce_conn);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
	}

	return NT_STATUS_OK;
}

/*
  retrieve some output from a dcerpc server
  The caller supplies a function that will be called to do the
  actual output. 

  The first argument to write_fn() will be 'private', the second will
  be a pointer to a buffer containing the data to be sent and the 3rd
  will be the number of bytes to be sent.

  write_fn() should return the number of bytes successfully written.

  this will return STATUS_BUFFER_OVERFLOW if there is more to be read
  from the current fragment
*/
NTSTATUS dcesrv_output(struct dcesrv_connection *dce_conn, 
		       void *private,
		       ssize_t (*write_fn)(void *, DATA_BLOB *))
{
	struct dcesrv_call_state *call;
	struct dcesrv_call_reply *rep;
	ssize_t nwritten;

	call = dce_conn->call_list;
	if (!call || !call->replies) {
		if (dce_conn->pending_call_list) {
			/* TODO: we need to say act async here
			 *       as we know we have pending requests
			 *	 which will be finished at a time
			 */
			return NT_STATUS_FOOBAR;
		}
		return NT_STATUS_FOOBAR;
	}
	rep = call->replies;

	nwritten = write_fn(private, &rep->data);
	if (nwritten == -1) {
		/* TODO: hmm, how do we cope with this? destroy the
		   connection perhaps? */
		return NT_STATUS_UNSUCCESSFUL;
	}

	rep->data.length -= nwritten;
	rep->data.data += nwritten;

	if (rep->data.length == 0) {
		/* we're done with this section of the call */
		DLIST_REMOVE(call->replies, rep);
	} else {
		return STATUS_BUFFER_OVERFLOW;
	}

	if (call->replies == NULL) {
		/* we're done with the whole call */
		DLIST_REMOVE(dce_conn->call_list, call);
		talloc_free(call);
	}

	return NT_STATUS_OK;
}


/*
  write_fn() for dcesrv_output_blob()
*/
static ssize_t dcesrv_output_blob_write_fn(void *private, DATA_BLOB *out)
{
	DATA_BLOB *blob = private;
	if (out->length < blob->length) {
		blob->length = out->length;
	}
	memcpy(blob->data, out->data, blob->length);
	return blob->length;
}

/*
  a simple wrapper for dcesrv_output() for when we want to output
  into a data blob
*/
NTSTATUS dcesrv_output_blob(struct dcesrv_connection *dce_conn, 
			    DATA_BLOB *blob)
{
	return dcesrv_output(dce_conn, blob, dcesrv_output_blob_write_fn);
}

static NTSTATUS dcesrv_init_context(TALLOC_CTX *mem_ctx, const char **endpoint_servers, uint32_t state_flags, struct dcesrv_context **_dce_ctx)
{
	NTSTATUS status;
	struct dcesrv_context *dce_ctx;
	int i;

	if (!endpoint_servers) {
		DEBUG(0,("dcesrv_init_context: no endpoint servers configured\n"));
		return NT_STATUS_INTERNAL_ERROR;
	}

	dce_ctx = talloc(mem_ctx, struct dcesrv_context);
	NT_STATUS_HAVE_NO_MEMORY(dce_ctx);
	dce_ctx->endpoint_list	= NULL;
	dce_ctx->state_flags	= state_flags;

	for (i=0;endpoint_servers[i];i++) {
		const struct dcesrv_endpoint_server *ep_server;

		ep_server = dcesrv_ep_server_byname(endpoint_servers[i]);
		if (!ep_server) {
			DEBUG(0,("dcesrv_init_context: failed to find endpoint server = '%s'\n", endpoint_servers[i]));
			return NT_STATUS_INTERNAL_ERROR;
		}

		status = ep_server->init_server(dce_ctx, ep_server);
		if (!NT_STATUS_IS_OK(status)) {
			DEBUG(0,("dcesrv_init_context: failed to init endpoint server = '%s': %s\n", endpoint_servers[i],
				nt_errstr(status)));
			return status;
		}
	}

	*_dce_ctx = dce_ctx;
	return NT_STATUS_OK;
}

/*
  initialise the dcerpc server context
*/
NTSTATUS dcesrv_init_ipc_context(TALLOC_CTX *mem_ctx, struct dcesrv_context **_dce_ctx)
{
	NTSTATUS status;
	struct dcesrv_context *dce_ctx;

	status = dcesrv_init_context(mem_ctx, lp_dcerpc_endpoint_servers(), 0, &dce_ctx);
	NT_STATUS_NOT_OK_RETURN(status);

	*_dce_ctx = dce_ctx;
	return NT_STATUS_OK;
}

static void dcesrv_init(struct server_service *service)
{
	NTSTATUS status;
	struct dcesrv_context *dce_ctx;

	DEBUG(1,("dcesrv_init\n"));

	status = dcesrv_init_context(service,
				     lp_dcerpc_endpoint_servers(),
				     DCESRV_CALL_STATE_FLAG_MAY_ASYNC,
				     &dce_ctx);
	if (!NT_STATUS_IS_OK(status)) {
		return;
	}

	service->service.private_data = dce_ctx;

	dcesrv_sock_init(service);

	return;	
}

static void dcesrv_accept(struct server_connection *srv_conn)
{
	dcesrv_sock_accept(srv_conn);
}

static void dcesrv_recv(struct server_connection *srv_conn, 
			struct timeval t, uint16_t flags)
{
	dcesrv_sock_recv(srv_conn, t, flags);
}

static void dcesrv_send(struct server_connection *srv_conn, 
			struct timeval t, uint16_t flags)
{
	dcesrv_sock_send(srv_conn, t, flags);
}

/* the list of currently registered DCERPC endpoint servers.
 */
static struct ep_server {
	struct dcesrv_endpoint_server *ep_server;
} *ep_servers = NULL;
static int num_ep_servers;

/*
  register a DCERPC endpoint server. 

  The 'name' can be later used by other backends to find the operations
  structure for this backend.  

  The 'type' is used to specify whether this is for a disk, printer or IPC$ share
*/
NTSTATUS dcerpc_register_ep_server(const void *_ep_server)
{
	const struct dcesrv_endpoint_server *ep_server = _ep_server;
	
	if (dcesrv_ep_server_byname(ep_server->name) != NULL) {
		/* its already registered! */
		DEBUG(0,("DCERPC endpoint server '%s' already registered\n", 
			 ep_server->name));
		return NT_STATUS_OBJECT_NAME_COLLISION;
	}

	ep_servers = realloc_p(ep_servers, struct ep_server, num_ep_servers+1);
	if (!ep_servers) {
		smb_panic("out of memory in dcerpc_register");
	}

	ep_servers[num_ep_servers].ep_server = smb_xmemdup(ep_server, sizeof(*ep_server));
	ep_servers[num_ep_servers].ep_server->name = smb_xstrdup(ep_server->name);

	num_ep_servers++;

	DEBUG(3,("DCERPC endpoint server '%s' registered\n", 
		 ep_server->name));

	return NT_STATUS_OK;
}

/*
  return the operations structure for a named backend of the specified type
*/
const struct dcesrv_endpoint_server *dcesrv_ep_server_byname(const char *name)
{
	int i;

	for (i=0;i<num_ep_servers;i++) {
		if (strcmp(ep_servers[i].ep_server->name, name) == 0) {
			return ep_servers[i].ep_server;
		}
	}

	return NULL;
}

/*
  return the DCERPC module version, and the size of some critical types
  This can be used by endpoint server modules to either detect compilation errors, or provide
  multiple implementations for different smbd compilation options in one module
*/
const struct dcesrv_critical_sizes *dcerpc_module_version(void)
{
	static const struct dcesrv_critical_sizes critical_sizes = {
		DCERPC_MODULE_VERSION,
		sizeof(struct dcesrv_context),
		sizeof(struct dcesrv_endpoint),
		sizeof(struct dcesrv_endpoint_server),
		sizeof(struct dcesrv_interface),
		sizeof(struct dcesrv_if_list),
		sizeof(struct dcesrv_connection),
		sizeof(struct dcesrv_call_state),
		sizeof(struct dcesrv_auth),
		sizeof(struct dcesrv_handle)
	};

	return &critical_sizes;
}

static const struct server_stream_ops dcesrv_stream_ops = {
	.name			= "rpc",
	.socket_init		= NULL,
	.accept_connection	= dcesrv_accept,
	.recv_handler		= dcesrv_recv,
	.send_handler		= dcesrv_send,
	.idle_handler		= NULL,
	.close_connection	= NULL
};

const struct server_stream_ops *dcesrv_get_stream_ops(void)
{
	return &dcesrv_stream_ops;
}

static const struct server_service_ops dcesrv_ops = {
	.name			= "rpc",
	.service_init		= dcesrv_init,
};

const struct server_service_ops *dcesrv_get_ops(void)
{
	return &dcesrv_ops;
}

NTSTATUS server_service_rpc_init(void)
{
	return NT_STATUS_OK;	
}
