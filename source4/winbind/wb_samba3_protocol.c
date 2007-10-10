/* 
   Unix SMB/CIFS implementation.
   Main winbindd samba3 server routines

   Copyright (C) Stefan Metzmacher	2005
   Copyright (C) Volker Lendecke	2005

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
#include "smbd/service_stream.h"
#include "nsswitch/winbind_nss_config.h"
#include "nsswitch/winbindd_nss.h"
#include "winbind/wb_server.h"
#include "winbind/wb_samba3_protocol.h"

uint32_t wbsrv_samba3_packet_length(DATA_BLOB blob)
{
	uint32_t *len = (uint32_t *)blob.data;
	return *len;
}

NTSTATUS wbsrv_samba3_pull_request(DATA_BLOB blob, TALLOC_CTX *mem_ctx, struct wbsrv_call **_call)
{
	struct wbsrv_call *call;
	struct wbsrv_samba3_call *s3_call;

	if (blob.length != sizeof(s3_call->request)) {
		DEBUG(0,("wbsrv_samba3_pull_request: invalid blob length %u should be %u\n"
			 " make sure you use the correct winbind client tools!\n",
			 blob.length, sizeof(s3_call->request)));
		return NT_STATUS_INVALID_PARAMETER;
	}

	call = talloc_zero(mem_ctx, struct wbsrv_call);
	NT_STATUS_HAVE_NO_MEMORY(call);

	s3_call = talloc_zero(call, struct wbsrv_samba3_call);
	NT_STATUS_HAVE_NO_MEMORY(s3_call);
	s3_call->call = call;

	/* the packet layout is the same as the in memory layout of the request, so just copy it */
	memcpy(&s3_call->request, blob.data, sizeof(s3_call->request));

	call->private_data = s3_call;

	*_call = call;
	return NT_STATUS_OK;
}

NTSTATUS wbsrv_samba3_handle_call(struct wbsrv_call *call)
{
	struct wbsrv_samba3_call *s3call = talloc_get_type(call->private_data,
							   struct wbsrv_samba3_call);

	DEBUG(10, ("Got winbind samba3 request %d\n", s3call->request.cmd));

	switch(s3call->request.cmd) {
	case WINBINDD_INTERFACE_VERSION:
		return wbsrv_samba3_interface_version(s3call);

	case WINBINDD_CHECK_MACHACC:
		return wbsrv_samba3_check_machacc(s3call);

	case WINBINDD_PING:
		return wbsrv_samba3_ping(s3call);

	case WINBINDD_INFO:
		return wbsrv_samba3_domain_name(s3call);

	case WINBINDD_DOMAIN_NAME:
		return wbsrv_samba3_domain_name(s3call);

	case WINBINDD_NETBIOS_NAME:
		return wbsrv_samba3_netbios_name(s3call);

	case WINBINDD_PRIV_PIPE_DIR:
		return wbsrv_samba3_priv_pipe_dir(s3call);
	}

	s3call->response.result = WINBINDD_ERROR;
	return NT_STATUS_OK;
}

NTSTATUS wbsrv_samba3_push_reply(struct wbsrv_call *call, TALLOC_CTX *mem_ctx, DATA_BLOB *_blob)
{
	struct wbsrv_samba3_call *s3call = talloc_get_type(call->private_data,
							   struct wbsrv_samba3_call);
	DATA_BLOB blob;
	uint8_t *extra_data;
	size_t extra_data_len = 0;

	extra_data = s3call->response.extra_data;
	if (extra_data) {
		extra_data_len = strlen((char *)s3call->response.extra_data) + 1;
	}

	blob = data_blob_talloc(mem_ctx, NULL, sizeof(s3call->response) + extra_data_len);
	NT_STATUS_HAVE_NO_MEMORY(blob.data);

	/* don't push real pointer values into sockets */
	if (extra_data) {
		s3call->response.extra_data = (void *)0xFFFFFFFF;
	}
	s3call->response.length = sizeof(s3call->response) + extra_data_len;
	memcpy(blob.data, &s3call->response, sizeof(s3call->response));
	/* set back the pointer */
	s3call->response.extra_data = extra_data;

	if (extra_data) {
		memcpy(blob.data + sizeof(s3call->response), extra_data, extra_data_len);
	}

	*_blob = blob;
	return NT_STATUS_OK;
}
