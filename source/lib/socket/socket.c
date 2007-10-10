/* 
   Unix SMB/CIFS implementation.
   Socket functions
   Copyright (C) Stefan Metzmacher 2004
   
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
#include "lib/socket/socket.h"
#include "system/filesys.h"

/*
  auto-close sockets on free
*/
static int socket_destructor(void *ptr)
{
	struct socket_context *sock = ptr;
	if (sock->ops->fn_close) {
		sock->ops->fn_close(sock);
	}
	return 0;
}

static NTSTATUS socket_create_with_ops(TALLOC_CTX *mem_ctx, const struct socket_ops *ops,
				       struct socket_context **new_sock, 
				       enum socket_type type, uint32_t flags)
{
	NTSTATUS status;

	(*new_sock) = talloc(mem_ctx, struct socket_context);
	if (!(*new_sock)) {
		return NT_STATUS_NO_MEMORY;
	}

	(*new_sock)->type = type;
	(*new_sock)->state = SOCKET_STATE_UNDEFINED;
	(*new_sock)->flags = flags;

	(*new_sock)->fd = -1;

	(*new_sock)->private_data = NULL;
	(*new_sock)->ops = ops;

	status = (*new_sock)->ops->fn_init((*new_sock));
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(*new_sock);
		return status;
	}

	/* by enabling "testnonblock" mode, all socket receive and
	   send calls on non-blocking sockets will randomly recv/send
	   less data than requested */
	if (!(flags & SOCKET_FLAG_BLOCK) &&
	    type == SOCKET_TYPE_STREAM &&
	    lp_parm_bool(-1, "socket", "testnonblock", False)) {
		(*new_sock)->flags |= SOCKET_FLAG_TESTNONBLOCK;
	}

	talloc_set_destructor(*new_sock, socket_destructor);

	return NT_STATUS_OK;
}

NTSTATUS socket_create(const char *name, enum socket_type type, 
		       struct socket_context **new_sock, uint32_t flags)
{
	const struct socket_ops *ops;

	ops = socket_getops_byname(name, type);
	if (!ops) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	return socket_create_with_ops(NULL, ops, new_sock, type, flags);
}

void socket_destroy(struct socket_context *sock)
{
	/* the close is handled by the destructor */
	talloc_free(sock);
}

NTSTATUS socket_connect(struct socket_context *sock,
			const char *my_address, int my_port,
			const char *server_address, int server_port,
			uint32_t flags)
{
	if (sock->state != SOCKET_STATE_UNDEFINED) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!sock->ops->fn_connect) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	return sock->ops->fn_connect(sock, my_address, my_port, server_address, server_port, flags);
}

NTSTATUS socket_connect_complete(struct socket_context *sock, uint32_t flags)
{
	if (!sock->ops->fn_connect_complete) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}
	return sock->ops->fn_connect_complete(sock, flags);
}

NTSTATUS socket_listen(struct socket_context *sock, const char *my_address, int port, int queue_size, uint32_t flags)
{
	if (sock->state != SOCKET_STATE_UNDEFINED) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!sock->ops->fn_listen) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	return sock->ops->fn_listen(sock, my_address, port, queue_size, flags);
}

NTSTATUS socket_accept(struct socket_context *sock, struct socket_context **new_sock)
{
	NTSTATUS status;

	if (sock->type != SOCKET_TYPE_STREAM) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (sock->state != SOCKET_STATE_SERVER_LISTEN) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!sock->ops->fn_accept) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	status = sock->ops->fn_accept(sock, new_sock);

	if (NT_STATUS_IS_OK(status)) {
		talloc_set_destructor(*new_sock, socket_destructor);
	}

	return status;
}

NTSTATUS socket_recv(struct socket_context *sock, void *buf, 
		     size_t wantlen, size_t *nread, uint32_t flags)
{
	if (sock->state != SOCKET_STATE_CLIENT_CONNECTED &&
	    sock->state != SOCKET_STATE_SERVER_CONNECTED) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!sock->ops->fn_recv) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	if ((sock->flags & SOCKET_FLAG_TESTNONBLOCK) && wantlen > 1) {
		if (random() % 10 == 0) {
			*nread = 0;
			return STATUS_MORE_ENTRIES;
		}
		return sock->ops->fn_recv(sock, buf, 1+(random() % wantlen), nread, flags);
	}

	return sock->ops->fn_recv(sock, buf, wantlen, nread, flags);
}

NTSTATUS socket_recvfrom(struct socket_context *sock, void *buf, 
			 size_t wantlen, size_t *nread, uint32_t flags,
			 const char **src_addr, int *src_port)
{
	if (sock->type != SOCKET_TYPE_DGRAM) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!sock->ops->fn_recvfrom) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	return sock->ops->fn_recvfrom(sock, buf, wantlen, nread, flags, 
				      src_addr, src_port);
}

NTSTATUS socket_send(struct socket_context *sock, 
		     const DATA_BLOB *blob, size_t *sendlen, uint32_t flags)
{
	if (sock->state != SOCKET_STATE_CLIENT_CONNECTED &&
	    sock->state != SOCKET_STATE_SERVER_CONNECTED) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!sock->ops->fn_send) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	if ((sock->flags & SOCKET_FLAG_TESTNONBLOCK) && blob->length > 1) {
		DATA_BLOB blob2 = *blob;
		if (random() % 10 == 0) {
			*sendlen = 0;
			return STATUS_MORE_ENTRIES;
		}
		blob2.length = 1+(random() % blob2.length);
		return sock->ops->fn_send(sock, &blob2, sendlen, flags);
	}

	return sock->ops->fn_send(sock, blob, sendlen, flags);
}


NTSTATUS socket_sendto(struct socket_context *sock, 
		       const DATA_BLOB *blob, size_t *sendlen, uint32_t flags,
		       const char *dest_addr, int dest_port)
{
	if (sock->type != SOCKET_TYPE_DGRAM) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (sock->state == SOCKET_STATE_CLIENT_CONNECTED ||
	    sock->state == SOCKET_STATE_SERVER_CONNECTED) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (!sock->ops->fn_sendto) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	return sock->ops->fn_sendto(sock, blob, sendlen, flags, dest_addr, dest_port);
}

NTSTATUS socket_set_option(struct socket_context *sock, const char *option, const char *val)
{
	if (!sock->ops->fn_set_option) {
		return NT_STATUS_NOT_IMPLEMENTED;
	}

	return sock->ops->fn_set_option(sock, option, val);
}

char *socket_get_peer_name(struct socket_context *sock, TALLOC_CTX *mem_ctx)
{
	if (!sock->ops->fn_get_peer_name) {
		return NULL;
	}

	return sock->ops->fn_get_peer_name(sock, mem_ctx);
}

char *socket_get_peer_addr(struct socket_context *sock, TALLOC_CTX *mem_ctx)
{
	if (!sock->ops->fn_get_peer_addr) {
		return NULL;
	}

	return sock->ops->fn_get_peer_addr(sock, mem_ctx);
}

int socket_get_peer_port(struct socket_context *sock)
{
	if (!sock->ops->fn_get_peer_port) {
		return -1;
	}

	return sock->ops->fn_get_peer_port(sock);
}

char *socket_get_my_addr(struct socket_context *sock, TALLOC_CTX *mem_ctx)
{
	if (!sock->ops->fn_get_my_addr) {
		return NULL;
	}

	return sock->ops->fn_get_my_addr(sock, mem_ctx);
}

int socket_get_my_port(struct socket_context *sock)
{
	if (!sock->ops->fn_get_my_port) {
		return -1;
	}

	return sock->ops->fn_get_my_port(sock);
}

int socket_get_fd(struct socket_context *sock)
{
	if (!sock->ops->fn_get_fd) {
		return -1;
	}

	return sock->ops->fn_get_fd(sock);
}

/*
  call dup() on a socket, and close the old fd. This is used to change
  the fd to the lowest available number, to make select() more
  efficient (select speed depends on the maxiumum fd number passed to
  it)
*/
NTSTATUS socket_dup(struct socket_context *sock)
{
	int fd;
	if (sock->fd == -1) {
		return NT_STATUS_INVALID_HANDLE;
	}
	fd = dup(sock->fd);
	if (fd == -1) {
		return map_nt_error_from_unix(errno);
	}
	close(sock->fd);
	sock->fd = fd;
	return NT_STATUS_OK;
	
}

const struct socket_ops *socket_getops_byname(const char *name, enum socket_type type)
{
	extern const struct socket_ops *socket_ipv4_ops(enum socket_type );
	extern const struct socket_ops *socket_ipv6_ops(enum socket_type );
	extern const struct socket_ops *socket_unixdom_ops(enum socket_type );

	if (strcmp("ip", name) == 0 || 
	    strcmp("ipv4", name) == 0) {
		return socket_ipv4_ops(type);
	}

#if HAVE_SOCKET_IPV6
	if (strcmp("ipv6", name) == 0) {
		if (lp_parm_bool(-1, "socket", "noipv6", False)) {
			DEBUG(3, ("IPv6 support was disabled in smb.conf"));
			return NULL;
		}
		return socket_ipv6_ops(type);
	}
#endif

	if (strcmp("unix", name) == 0) {
		return socket_unixdom_ops(type);
	}

	return NULL;
}
