/* 
   Unix SMB/CIFS implementation.
   Main winbindd server routines

   Copyright (C) Stefan Metzmacher	2005
   Copyright (C) Andrew Tridgell	2005
   
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

#define WINBINDD_DIR "/tmp/.winbindd/"
#define WINBINDD_SOCKET WINBINDD_DIR"socket"
/* the privileged socket is in smbd_tmp_dir() */
#define WINBINDD_PRIVILEGED_SOCKET "winbind_socket"

#define WINBINDD_SAMBA3_SOCKET WINBINDD_DIR"pipe"
/* the privileged socket is in smbd_tmp_dir() */
#define WINBINDD_SAMBA3_PRIVILEGED_SOCKET "winbind_pipe"

/* this struct stores global data for the winbind task */
struct wbsrv_service {
	struct task_server *task;

	struct dcerpc_pipe *netlogon_auth2_pipe;
	struct cli_credentials *schannel_creds;

	struct dcerpc_pipe *lsa_pipe;
	struct dcerpc_pipe *netlogon_pipe;
};

/* 
  this is an abstraction for the actual protocol being used,
  so that we can listen on different sockets with different protocols
  e.g. the old samba3 protocol on one socket and a new protocol on another socket
*/
struct wbsrv_protocol_ops {
	const char *name;
	BOOL allow_pending_calls;
	uint32_t (*packet_length)(DATA_BLOB blob);
	NTSTATUS (*pull_request)(DATA_BLOB blob, TALLOC_CTX *mem_ctx, struct wbsrv_call **call);
	NTSTATUS (*handle_call)(struct wbsrv_call *call);
	NTSTATUS (*push_reply)(struct wbsrv_call *call, TALLOC_CTX *mem_ctx, DATA_BLOB *blob);
};

/*
  state of a listen socket and it's protocol information
*/
struct wbsrv_listen_socket {
	const char *socket_path;
	struct wbsrv_service *service;
	BOOL privileged;
	const struct wbsrv_protocol_ops *ops;
};

/*
  state of an open winbind connection
*/
struct wbsrv_connection {
	/* stream connection we belong to */
	struct stream_connection *conn;

	/* the listening socket we belong to, it holds protocol hooks */
	struct wbsrv_listen_socket *listen_socket;

	/* storage for protocol specific data */
	void *protocol_private_data;

	/* the partial data we've receiced yet */
	DATA_BLOB partial;

	/* the amount that we used yet from the partial buffer */
	uint32_t partial_read;

	/* prevent loops when we use half async code, while processing a requuest */
	BOOL processing;

	/* how many calls are pending */
	uint32_t pending_calls;

	struct data_blob_list_item *send_queue;
};

/*
  state of one request

  NOTE about async replies:
   if the backend wants to reply later:

   - it should set the WBSRV_CALL_FLAGS_REPLY_ASYNC flag, and may set a 
     talloc_destructor on the this structure or on the private_data (if it's a
     talloc child of this structure), so that wbsrv_terminate_connection
     called by another call clean up the whole connection correct.
   - When the backend is ready to reply it should call wbsrv_send_reply(call),
     wbsrv_send_reply implies talloc_free(call), so the backend should use 
     talloc_reference(call), if it needs it later. 
   - If wbsrv_send_reply doesn't return NT_STATUS_OK, the backend function 
     should call, wbsrv_terminate_connection(call->wbconn, nt_errstr(status));
     return;

*/
struct wbsrv_call {
#define WBSRV_CALL_FLAGS_REPLY_ASYNC 0x00000001
	uint32_t flags;

	/* the backend should use this event context */
	struct event_context *event_ctx;

	/* the connection the call belongs to */
	struct wbsrv_connection *wbconn;

	/* storage for protocol specific data */
	void *private_data;
};
