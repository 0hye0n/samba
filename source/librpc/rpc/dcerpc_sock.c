/* 
   Unix SMB/CIFS implementation.

   dcerpc over standard sockets transport

   Copyright (C) Andrew Tridgell 2003
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
#include "lib/socket/socket.h"

#define MIN_HDR_SIZE 16

struct sock_blob {
	struct sock_blob *next, *prev;
	DATA_BLOB data;
};

/* transport private information used by general socket pipe transports */
struct sock_private {
	struct event_context *event_ctx;
	struct fd_event *fde;
	struct socket_context *sock;
	char *server_name;

	struct sock_blob *pending_send;

	struct {
		size_t received;
		DATA_BLOB data;
		uint_t pending_count;
	} recv;
};


/*
  mark the socket dead
*/
static void sock_dead(struct dcerpc_connection *p, NTSTATUS status)
{
	struct sock_private *sock = p->transport.private;

	if (sock && sock->sock != NULL) {
		talloc_free(sock->sock);
		sock->sock = NULL;
	}

	/* wipe any pending sends */
	while (sock->pending_send) {
		struct sock_blob *blob = sock->pending_send;
		DLIST_REMOVE(sock->pending_send, blob);
		talloc_free(blob);
	}

	if (!NT_STATUS_IS_OK(status)) {
		p->transport.recv_data(p, NULL, status);
	}

	talloc_free(sock->fde);
}

/*
  process send requests
*/
static void sock_process_send(struct dcerpc_connection *p)
{
	struct sock_private *sock = p->transport.private;

	while (sock->pending_send) {
		struct sock_blob *blob = sock->pending_send;
		NTSTATUS status;
		size_t sent;
		status = socket_send(sock->sock, &blob->data, &sent, 0);
		if (NT_STATUS_IS_ERR(status)) {
			sock_dead(p, NT_STATUS_NET_WRITE_FAULT);
			break;
		}
		if (sent == 0) {
			break;
		}

		blob->data.data += sent;
		blob->data.length -= sent;

		if (blob->data.length != 0) {
			break;
		}

		DLIST_REMOVE(sock->pending_send, blob);
		talloc_free(blob);
	}

	if (sock->pending_send == NULL) {
		EVENT_FD_NOT_WRITEABLE(sock->fde);
	}
}


/*
  process recv requests
*/
static void sock_process_recv(struct dcerpc_connection *p)
{
	struct sock_private *sock = p->transport.private;
	NTSTATUS status;
	size_t nread;

	if (sock->recv.data.data == NULL) {
		sock->recv.data = data_blob_talloc(sock, NULL, MIN_HDR_SIZE);
	}

	/* read in the base header to get the fragment length */
	if (sock->recv.received < MIN_HDR_SIZE) {
		uint32_t frag_length;

		status = socket_recv(sock->sock, 
				     sock->recv.data.data + sock->recv.received, 
				     MIN_HDR_SIZE - sock->recv.received, 
				     &nread, 0);
		if (NT_STATUS_IS_ERR(status)) {
			sock_dead(p, NT_STATUS_NET_WRITE_FAULT);
			return;
		}
		if (nread == 0) {
			return;
		}
		
		sock->recv.received += nread;

		if (sock->recv.received != MIN_HDR_SIZE) {
			return;
		}
		frag_length = dcerpc_get_frag_length(&sock->recv.data);

		sock->recv.data.data = talloc_realloc(sock, sock->recv.data.data,
						      uint8_t, frag_length);
		if (sock->recv.data.data == NULL) {
			sock_dead(p, NT_STATUS_NO_MEMORY);
			return;
		}
		sock->recv.data.length = frag_length;
	}

	/* read in the rest of the packet */
	status = socket_recv(sock->sock, 
			     sock->recv.data.data + sock->recv.received, 
			     sock->recv.data.length - sock->recv.received, 
			     &nread, 0);
	if (NT_STATUS_IS_ERR(status)) {
		sock_dead(p, NT_STATUS_NET_WRITE_FAULT);
		return;
	}
	if (nread == 0) {
		return;
	}
	sock->recv.received += nread;

	if (sock->recv.received != sock->recv.data.length) {
		return;
	}

	/* we have a full packet */
	p->transport.recv_data(p, &sock->recv.data, NT_STATUS_OK);
	talloc_free(sock->recv.data.data);
	sock->recv.data = data_blob(NULL, 0);
	sock->recv.received = 0;
	sock->recv.pending_count--;
	if (sock->recv.pending_count == 0) {
		EVENT_FD_NOT_READABLE(sock->fde);
	}
}

/*
  called when a IO is triggered by the events system
*/
static void sock_io_handler(struct event_context *ev, struct fd_event *fde, 
			    uint16_t flags, void *private)
{
	struct dcerpc_connection *p = talloc_get_type(private, struct dcerpc_connection);
	struct sock_private *sock = p->transport.private;

	if (flags & EVENT_FD_WRITE) {
		sock_process_send(p);
		return;
	}

	if (sock->sock == NULL) {
		return;
	}

	if (flags & EVENT_FD_READ) {
		sock_process_recv(p);
	}
}

/* 
   initiate a read request 
*/
static NTSTATUS sock_send_read(struct dcerpc_connection *p)
{
	struct sock_private *sock = p->transport.private;

	sock->recv.pending_count++;
	if (sock->recv.pending_count == 1) {
		EVENT_FD_READABLE(sock->fde);
	}
	return NT_STATUS_OK;
}

/* 
   send an initial pdu in a multi-pdu sequence
*/
static NTSTATUS sock_send_request(struct dcerpc_connection *p, DATA_BLOB *data, BOOL trigger_read)
{
	struct sock_private *sock = p->transport.private;
	struct sock_blob *blob;

	if (sock->sock == NULL) {
		return NT_STATUS_CONNECTION_DISCONNECTED;
	}

	blob = talloc(sock, struct sock_blob);
	if (blob == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	blob->data = data_blob_talloc(blob, data->data, data->length);
	if (blob->data.data == NULL) {
		talloc_free(blob);
		return NT_STATUS_NO_MEMORY;
	}

	DLIST_ADD_END(sock->pending_send, blob, struct sock_blob *);

	EVENT_FD_WRITEABLE(sock->fde);

	if (trigger_read) {
		sock_send_read(p);
	}

	return NT_STATUS_OK;
}

/* 
   return the event context so the caller can process asynchronously
*/
static struct event_context *sock_event_context(struct dcerpc_connection *p)
{
	struct sock_private *sock = p->transport.private;

	return sock->event_ctx;
}

/* 
   shutdown sock pipe connection
*/
static NTSTATUS sock_shutdown_pipe(struct dcerpc_connection *p)
{
	struct sock_private *sock = p->transport.private;

	if (sock && sock->sock) {
		sock_dead(p, NT_STATUS_OK);
	}

	return NT_STATUS_OK;
}

/*
  return sock server name
*/
static const char *sock_peer_name(struct dcerpc_connection *p)
{
	struct sock_private *sock = p->transport.private;
	return sock->server_name;
}

/* 
   open a rpc connection using the generic socket library
*/
static NTSTATUS dcerpc_pipe_open_socket(struct dcerpc_connection *c, 
					const char *server,
					uint32_t port, 
					const char *type,
					enum dcerpc_transport_t transport)
{
	struct sock_private *sock;
	struct socket_context *socket_ctx;
	NTSTATUS status;

	sock = talloc(c, struct sock_private);
	if (!sock) {
		return NT_STATUS_NO_MEMORY;
	}

	status = socket_create(type, SOCKET_TYPE_STREAM, &socket_ctx, 0);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(sock);
		return status;
	}
	talloc_steal(sock, socket_ctx);

	status = socket_connect(socket_ctx, NULL, 0, server, port, 0);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(sock);
		return status;
	}

	/*
	  fill in the transport methods
	*/
	c->transport.transport = transport;
	c->transport.private = NULL;

	c->transport.send_request = sock_send_request;
	c->transport.send_read = sock_send_read;
	c->transport.event_context = sock_event_context;
	c->transport.recv_data = NULL;

	c->transport.shutdown_pipe = sock_shutdown_pipe;
	c->transport.peer_name = sock_peer_name;
	
	sock->sock = socket_ctx;
	sock->server_name = strupper_talloc(sock, server);
	sock->event_ctx = event_context_init(sock);
	sock->pending_send = NULL;
	sock->recv.received = 0;
	sock->recv.data = data_blob(NULL, 0);
	sock->recv.pending_count = 0;

	sock->fde = event_add_fd(sock->event_ctx, sock, socket_get_fd(sock->sock), 
				 0, sock_io_handler, c);

	c->transport.private = sock;

	/* ensure we don't get SIGPIPE */
	BlockSignals(True,SIGPIPE);

	return NT_STATUS_OK;
}

/* 
   open a rpc connection using tcp
*/
NTSTATUS dcerpc_pipe_open_tcp(struct dcerpc_connection *c, const char *server, uint32_t port)
{
	NTSTATUS status;
	
	/* Try IPv6 first */
	status = dcerpc_pipe_open_socket(c, server, port, "ipv6", NCACN_IP_TCP);
	if (NT_STATUS_IS_OK(status)) {
		return status;
	}
	
	return dcerpc_pipe_open_socket(c, server, port, "ipv4", NCACN_IP_TCP);
}

/* 
   open a rpc connection to a unix socket 
*/
NTSTATUS dcerpc_pipe_open_unix_stream(struct dcerpc_connection *c, const char *path)
{
	return dcerpc_pipe_open_socket(c, path, 0, "unix", NCACN_UNIX_STREAM);
}

/* 
   open a rpc connection to a named pipe 
*/
NTSTATUS dcerpc_pipe_open_pipe(struct dcerpc_connection *c, const char *identifier)
{
	NTSTATUS status;
	char *canon, *full_path;

	canon = talloc_strdup(NULL, identifier);

	string_replace(canon, '/', '\\');
	full_path = talloc_asprintf(canon, "%s/%s", lp_ncalrpc_dir(), canon);

	status = dcerpc_pipe_open_socket(c, full_path, 0, "unix", NCALRPC);
	talloc_free(canon);

	return status;
}
