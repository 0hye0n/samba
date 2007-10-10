/* 
   Unix SMB/CIFS implementation.

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
/*
  a composite API for making a full SMB connection
*/

#include "includes.h"
#include "libcli/raw/libcliraw.h"
#include "libcli/composite/composite.h"

/* the stages of this call */
enum connect_stage {CONNECT_RESOLVE, 
		    CONNECT_SOCKET, 
		    CONNECT_SESSION_REQUEST, 
		    CONNECT_NEGPROT,
		    CONNECT_SESSION_SETUP,
		    CONNECT_TCON};

struct connect_state {
	struct smbcli_socket *sock;
	struct smbcli_transport *transport;
	struct smbcli_session *session;
	struct smb_composite_connect *io;
	union smb_tcon *io_tcon;
	struct smb_composite_sesssetup *io_setup;
	struct smbcli_request *req;
	struct smbcli_composite *creq;
};


static void request_handler(struct smbcli_request *);
static void composite_handler(struct smbcli_composite *);

/*
  setup a negprot send 
*/
static NTSTATUS connect_send_negprot(struct smbcli_composite *c, 
				     struct smb_composite_connect *io)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);

	state->req = smb_raw_negotiate_send(state->transport, lp_maxprotocol());
	NT_STATUS_HAVE_NO_MEMORY(state->req);

	state->req->async.fn = request_handler;
	state->req->async.private = c;
	c->stage = CONNECT_NEGPROT;
	
	return NT_STATUS_OK;
}


/*
  a tree connect request has competed
*/
static NTSTATUS connect_tcon(struct smbcli_composite *c, 
			     struct smb_composite_connect *io)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);
	NTSTATUS status;

	status = smb_tree_connect_recv(state->req, c, state->io_tcon);
	NT_STATUS_NOT_OK_RETURN(status);

	io->out.tree->tid = state->io_tcon->tconx.out.tid;
	if (state->io_tcon->tconx.out.dev_type) {
		io->out.tree->device = talloc_strdup(io->out.tree, 
						     state->io_tcon->tconx.out.dev_type);
	}
	if (state->io_tcon->tconx.out.fs_type) {
		io->out.tree->fs_type = talloc_strdup(io->out.tree, 
						      state->io_tcon->tconx.out.fs_type);
	}

	/* all done! */
	c->state = SMBCLI_REQUEST_DONE;
	if (c->async.fn) {
		c->async.fn(c);
	}

	return NT_STATUS_OK;
}


/*
  a session setup request has competed
*/
static NTSTATUS connect_session_setup(struct smbcli_composite *c, 
				      struct smb_composite_connect *io)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);
	NTSTATUS status;

	status = smb_composite_sesssetup_recv(state->creq);
	NT_STATUS_NOT_OK_RETURN(status);
	
	state->session->vuid = state->io_setup->out.vuid;
	
	/* setup for a tconx */
	io->out.tree = smbcli_tree_init(state->session);
	NT_STATUS_HAVE_NO_MEMORY(io->out.tree);

	state->io_tcon = talloc(c, union smb_tcon);
	NT_STATUS_HAVE_NO_MEMORY(state->io_tcon);

	/* connect to a share using a tree connect */
	state->io_tcon->generic.level = RAW_TCON_TCONX;
	state->io_tcon->tconx.in.flags = 0;
	state->io_tcon->tconx.in.password = data_blob(NULL, 0);	
	
	state->io_tcon->tconx.in.path = talloc_asprintf(state->io_tcon, 
						 "\\\\%s\\%s", 
						 io->in.called_name, 
						 io->in.service);
	NT_STATUS_HAVE_NO_MEMORY(state->io_tcon->tconx.in.path);
	if (!io->in.service_type) {
		state->io_tcon->tconx.in.device = "?????";
	} else {
		state->io_tcon->tconx.in.device = io->in.service_type;
	}

	state->req = smb_tree_connect_send(io->out.tree, state->io_tcon);
	NT_STATUS_HAVE_NO_MEMORY(state->req);

	state->req->async.fn = request_handler;
	state->req->async.private = c;
	c->stage = CONNECT_TCON;

	return NT_STATUS_OK;
}

/*
  a negprot request has competed
*/
static NTSTATUS connect_negprot(struct smbcli_composite *c, 
				struct smb_composite_connect *io)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);
	NTSTATUS status;

	status = smb_raw_negotiate_recv(state->req);
	NT_STATUS_NOT_OK_RETURN(status);

	/* next step is a session setup */
	state->session = smbcli_session_init(state->transport);
	NT_STATUS_HAVE_NO_MEMORY(state->session);

	/* get rid of the extra reference to the transport */
	talloc_free(state->transport);

	state->io_setup = talloc(c, struct smb_composite_sesssetup);
	NT_STATUS_HAVE_NO_MEMORY(state->io_setup);

	/* prepare a session setup to establish a security context */
	state->io_setup->in.sesskey      = state->transport->negotiate.sesskey;
	state->io_setup->in.capabilities = state->transport->negotiate.capabilities;
	state->io_setup->in.domain       = io->in.domain;
	state->io_setup->in.user         = io->in.user;
	state->io_setup->in.password     = io->in.password;

	state->creq = smb_composite_sesssetup_send(state->session, state->io_setup);
	NT_STATUS_HAVE_NO_MEMORY(state->creq);

	state->creq->async.fn = composite_handler;
	state->creq->async.private = c;
	c->stage = CONNECT_SESSION_SETUP;
	
	return NT_STATUS_OK;
}


/*
  a session request operation has competed
*/
static NTSTATUS connect_session_request(struct smbcli_composite *c, 
					struct smb_composite_connect *io)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);
	NTSTATUS status;

	status = smbcli_transport_connect_recv(state->req);
	NT_STATUS_NOT_OK_RETURN(status);

	/* next step is a negprot */
	return connect_send_negprot(c, io);
}

/*
  a socket connection operation has competed
*/
static NTSTATUS connect_socket(struct smbcli_composite *c, 
			       struct smb_composite_connect *io)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);
	NTSTATUS status;
	struct nbt_name calling, called;

	status = smbcli_sock_connect_recv(state->creq);
	NT_STATUS_NOT_OK_RETURN(status);

	/* the socket is up - we can initialise the smbcli transport layer */
	state->transport = smbcli_transport_init(state->sock);
	NT_STATUS_HAVE_NO_MEMORY(state->transport);

	/* we have a connected socket - next step is a session
	   request, if needed. Port 445 doesn't need it, so it goes
	   straight to the negprot */
	if (state->sock->port == 445) {
		return connect_send_negprot(c, io);
	}

	calling.name = io->in.calling_name;
	calling.type = NBT_NAME_CLIENT;
	calling.scope = NULL;

	nbt_choose_called_name(state, &called, io->in.called_name, NBT_NAME_SERVER);

	state->req = smbcli_transport_connect_send(state->transport, &calling, &called);
	NT_STATUS_HAVE_NO_MEMORY(state->req);

	state->req->async.fn = request_handler;
	state->req->async.private = c;
	c->stage = CONNECT_SESSION_REQUEST;

	return NT_STATUS_OK;
}


/*
  called when name resolution is finished
*/
static NTSTATUS connect_resolve(struct smbcli_composite *c, 
				struct smb_composite_connect *io)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);
	NTSTATUS status;
	const char *address;

	status = resolve_name_recv(state->creq, state, &address);
	NT_STATUS_NOT_OK_RETURN(status);

	state->creq = smbcli_sock_connect_send(state->sock, address, state->io->in.port);
	NT_STATUS_HAVE_NO_MEMORY(state->creq);

	c->stage = CONNECT_SOCKET;
	state->creq->async.private = c;
	state->creq->async.fn = composite_handler;

	return NT_STATUS_OK;
}


/*
  handle and dispatch state transitions
*/
static void state_handler(struct smbcli_composite *c)
{
	struct connect_state *state = talloc_get_type(c->private, struct connect_state);

	switch (c->stage) {
	case CONNECT_RESOLVE:
		c->status = connect_resolve(c, state->io);
		break;
	case CONNECT_SOCKET:
		c->status = connect_socket(c, state->io);
		break;
	case CONNECT_SESSION_REQUEST:
		c->status = connect_session_request(c, state->io);
		break;
	case CONNECT_NEGPROT:
		c->status = connect_negprot(c, state->io);
		break;
	case CONNECT_SESSION_SETUP:
		c->status = connect_session_setup(c, state->io);
		break;
	case CONNECT_TCON:
		c->status = connect_tcon(c, state->io);
		break;
	}

	if (!NT_STATUS_IS_OK(c->status)) {
		c->state = SMBCLI_REQUEST_ERROR;
		if (c->async.fn) {
			c->async.fn(c);
		}
	}
}


/*
  handler for completion of a smbcli_request sub-request
*/
static void request_handler(struct smbcli_request *req)
{
	struct smbcli_composite *c = talloc_get_type(req->async.private, 
						     struct smbcli_composite);
	return state_handler(c);
}

/*
  handler for completion of a smbcli_composite sub-request
*/
static void composite_handler(struct smbcli_composite *req)
{
	struct smbcli_composite *c = talloc_get_type(req->async.private, 
						     struct smbcli_composite);
	return state_handler(c);
}

/*
  a function to establish a smbcli_tree from scratch
*/
struct smbcli_composite *smb_composite_connect_send(struct smb_composite_connect *io)
{
	struct smbcli_composite *c;
	struct connect_state *state;
	struct nbt_name name;

	c = talloc_zero(NULL, struct smbcli_composite);
	if (c == NULL) goto failed;

	state = talloc(c, struct connect_state);
	if (state == NULL) goto failed;

	state->sock = smbcli_sock_init(state);
	if (state->sock == NULL) goto failed;

	state->io = io;

	c->state = SMBCLI_REQUEST_SEND;
	c->stage = CONNECT_RESOLVE;
	c->event_ctx = state->sock->event.ctx;
	c->private = state;

	name.name = io->in.dest_host;
	name.type = NBT_NAME_SERVER;
	name.scope = NULL;

	state->creq = resolve_name_send(&name, c->event_ctx);
	if (state->creq == NULL) goto failed;

	state->creq->async.private = c;
	state->creq->async.fn = composite_handler;

	return c;
failed:
	talloc_free(c);
	return NULL;
}

/*
  recv half of async composite connect code
*/
NTSTATUS smb_composite_connect_recv(struct smbcli_composite *c, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;

	status = smb_composite_wait(c);

	if (NT_STATUS_IS_OK(status)) {
		struct connect_state *state = talloc_get_type(c->private, struct connect_state);
		talloc_steal(mem_ctx, state->io->out.tree);
	}

	talloc_free(c);
	return status;
}

/*
  sync version of smb_composite_connect 
*/
NTSTATUS smb_composite_connect(struct smb_composite_connect *io, TALLOC_CTX *mem_ctx)
{
	struct smbcli_composite *c = smb_composite_connect_send(io);
	return smb_composite_connect_recv(c, mem_ctx);
}
