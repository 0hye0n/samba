/*
   Unix SMB/CIFS implementation.

   Command backend for wbinfo -G

   Copyright (C) Kai Blin 2007

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "libcli/composite/composite.h"
#include "winbind/wb_server.h"
#include "smbd/service_task.h"
#include "winbind/wb_helper.h"
#include "libcli/security/proto.h"
#include "winbind/idmap.h"

struct gid2sid_state {
	struct composite_context *ctx;
	struct wbsrv_service *service;
	struct dom_sid *sid;
};

struct composite_context *wb_gid2sid_send(TALLOC_CTX *mem_ctx,
		struct wbsrv_service *service, gid_t gid)
{
	struct composite_context *result;
	struct gid2sid_state *state;

	DEBUG(5, ("wb_gid2sid_send called\n"));

	result = composite_create(mem_ctx, service->task->event_ctx);
	if (!result) return NULL;

	state = talloc(mem_ctx, struct gid2sid_state);
	if (composite_nomem(state, result)) return result;

	state->ctx = result;
	result->private_data = state;
	state->service = service;

	state->ctx->status = idmap_gid_to_sid(service->idmap_ctx, mem_ctx, gid,
					      &state->sid);
	if (NT_STATUS_EQUAL(state->ctx->status, NT_STATUS_RETRY)) {
		state->ctx->status = idmap_gid_to_sid(service->idmap_ctx,
						      mem_ctx, gid,
						      &state->sid);
	}
	if (!composite_is_ok(state->ctx)) return result;

	composite_done(state->ctx);
	return result;
}

NTSTATUS wb_gid2sid_recv(struct composite_context *ctx, TALLOC_CTX *mem_ctx,
		struct dom_sid **sid)
{
	NTSTATUS status = composite_wait(ctx);

	DEBUG(5, ("wb_gid2sid_recv called.\n"));

	if (NT_STATUS_IS_OK(status)) {
		struct gid2sid_state *state =
			talloc_get_type(ctx->private_data,
				struct gid2sid_state);
		*sid = talloc_steal(mem_ctx, state->sid);
	}
	talloc_free(ctx);
	return status;
}

