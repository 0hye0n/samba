/* 
   Unix SMB/CIFS implementation.

   management calls for smb server

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

#include "includes.h"
#include "smb_server/smb_server.h"
#include "smbd/service_stream.h"
#include "lib/messaging/irpc.h"
#include "librpc/gen_ndr/ndr_irpc.h"
#include "auth/auth.h"

/*
  return a list of open sessions
*/
static NTSTATUS smbsrv_session_information(struct irpc_message *msg, 
					   struct smbsrv_information *r)
{
	struct smbsrv_connection *smb_conn = talloc_get_type(msg->private, struct smbsrv_connection);
	int i=0, count=0;
	struct smbsrv_session *sess;

	/* count the number of sessions */
	for (sess=smb_conn->sessions.list; sess; sess=sess->next) {
		count++;
	}

	r->out.info.sessions.num_sessions = count;
	r->out.info.sessions.sessions = talloc_array(r, struct smbsrv_session_info, count);
	NT_STATUS_HAVE_NO_MEMORY(r->out.info.sessions.sessions);

	for (sess=smb_conn->sessions.list; sess; sess=sess->next) {
		struct smbsrv_session_info *info = &r->out.info.sessions.sessions[i];
		info->vuid         = sess->vuid;
		info->account_name = sess->session_info->server_info->account_name;
		info->domain_name  = sess->session_info->server_info->domain_name;
		info->connect_time = timeval_to_nttime(&sess->connect_time);
		info->client_ip    = socket_get_peer_addr(smb_conn->connection->socket, r);
		i++;
	}	

	return NT_STATUS_OK;
}

/*
  serve smbserver information via irpc
*/
static NTSTATUS smbsrv_information(struct irpc_message *msg, 
				   struct smbsrv_information *r)
{
	switch (r->in.level) {
	case SMBSRV_INFO_SESSIONS:
		return smbsrv_session_information(msg, r);
	}

	return NT_STATUS_OK;
}

/*
  initialise irpc management calls on a connection
*/
void smbsrv_management_init(struct smbsrv_connection *smb_conn)
{
	IRPC_REGISTER(smb_conn->connection->msg_ctx, irpc, SMBSRV_INFORMATION, 
		      smbsrv_information, smb_conn);
}
