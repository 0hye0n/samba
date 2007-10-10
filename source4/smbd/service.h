/* 
   Unix SMB/CIFS implementation.

   SERVER SERVICE code

   Copyright (C) Stefan (metze) Metzmacher	2004
   
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

#ifndef _SERVER_SERVICE_H
#define _SERVER_SERVICE_H

struct event_context;
struct model_ops;
struct server_context;

struct server_connection;
struct server_service;

/* modules can use the following to determine if the interface has changed
 * please increment the version number after each interface change
 * with a comment and maybe update struct process_model_critical_sizes.
 */
/* version 1 - initial version - metze */
#define SERVER_SERVICE_VERSION 1

struct server_service_ops {
	/* the name of the server_service */
	const char *name;

	/* called at startup when the server_service is selected */
	void (*service_init)(struct server_service *service, const struct model_ops *ops);

	/* function to accept new connection */
	void (*accept_connection)(struct server_connection *);

	void (*recv_handler)(struct server_connection *, struct timeval, uint16_t);

	void (*send_handler)(struct server_connection *, struct timeval, uint16_t);

	/* function to be called when the server is idle */
	void (*idle_handler)(struct server_connection *, struct timeval);

	/* function to close a connection */
	void (*close_connection)(struct server_connection *, const char *reason);

	/* function to exit server */
	void (*service_exit)(struct server_service *srv_ctx, const char *reason);	
};

struct socket_context;

struct server_socket {
	struct server_socket *next,*prev;
	void *private_data;

	struct {
		struct event_context *ctx;
		struct fd_event *fde;
	} event;

	struct socket_context *socket;

	struct server_service *service;

	struct server_connection *connection_list;
};

struct server_service {
	struct server_service *next,*prev;
	void *private_data;
	const struct server_service_ops *ops;

	const struct model_ops *model_ops;

	struct server_socket *socket_list;

	struct server_context *srv_ctx;
};

/* the concept of whether two operations are on the same server
   connection or different connections is an important one in SMB, especially
   for locking and share modes. We will use a servid_t to distinguish different
   connections 

   this means that (for example) a unique open file is distinguished by the triple
   of 
      servid_t server;
      uint16   tid;
      uint16   fnum;
*/
typedef uint32_t servid_t;

struct server_connection {
	struct server_connection *next,*prev;
	void *private_data;

	struct {
		struct event_context *ctx;
		struct fd_event *fde;
		struct timed_event *idle;
		struct timeval idle_time;
	} event;

	servid_t server_id;

	struct socket_context *socket;

	struct server_socket *server_socket;

	struct server_service *service;

	struct messaging_context *messaging_ctx;
};

#endif /* _SERVER_SERVICE_H */
