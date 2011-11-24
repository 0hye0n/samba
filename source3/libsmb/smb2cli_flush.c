/*
   Unix SMB/CIFS implementation.
   smb2 lib
   Copyright (C) Volker Lendecke 2011

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
#include "client.h"
#include "async_smb.h"
#include "smb2cli_base.h"
#include "smb2cli.h"
#include "libsmb/proto.h"
#include "lib/util/tevent_ntstatus.h"

struct smb2cli_flush_state {
	uint8_t fixed[24];
};

static void smb2cli_flush_done(struct tevent_req *subreq);

struct tevent_req *smb2cli_flush_send(TALLOC_CTX *mem_ctx,
				       struct tevent_context *ev,
				       struct cli_state *cli,
				       uint64_t fid_persistent,
				       uint64_t fid_volatile)
{
	struct tevent_req *req, *subreq;
	struct smb2cli_flush_state *state;
	uint8_t *fixed;

	req = tevent_req_create(mem_ctx, &state,
				struct smb2cli_flush_state);
	if (req == NULL) {
		return NULL;
	}
	fixed = state->fixed;
	SSVAL(fixed, 0, 24);
	SBVAL(fixed, 8, fid_persistent);
	SBVAL(fixed, 16, fid_volatile);

	subreq = smb2cli_req_send(state, ev, cli, SMB2_OP_FLUSH,
				  0, 0, /* flags */
				  cli->timeout,
				  cli->smb2.pid,
				  cli->smb2.tid,
				  cli->smb2.session,
				  state->fixed, sizeof(state->fixed),
				  NULL, 0);
	if (tevent_req_nomem(subreq, req)) {
		return tevent_req_post(req, ev);
	}
	tevent_req_set_callback(subreq, smb2cli_flush_done, req);
	return req;
}

static void smb2cli_flush_done(struct tevent_req *subreq)
{
	struct tevent_req *req =
		tevent_req_callback_data(subreq,
		struct tevent_req);
	NTSTATUS status;
	struct iovec *iov;
	static const struct smb2cli_req_expected_response expected[] = {
	{
		.status = NT_STATUS_OK,
		.body_size = 0x04
	}
	};

	status = smb2cli_req_recv(subreq, talloc_tos(), &iov,
				  expected, ARRAY_SIZE(expected));
	if (tevent_req_nterror(req, status)) {
		return;
	}
	tevent_req_done(req);
}

NTSTATUS smb2cli_flush_recv(struct tevent_req *req)
{
	return tevent_req_simple_recv_ntstatus(req);
}

NTSTATUS smb2cli_flush(struct cli_state *cli,
		       uint64_t fid_persistent,
		       uint64_t fid_volatile)
{
	TALLOC_CTX *frame = talloc_stackframe();
	struct event_context *ev;
	struct tevent_req *req;
	NTSTATUS status = NT_STATUS_NO_MEMORY;

	if (cli_has_async_calls(cli)) {
		/*
		 * Can't use sync call while an async call is in flight
		 */
		status = NT_STATUS_INVALID_PARAMETER;
		goto fail;
	}
	ev = event_context_init(frame);
	if (ev == NULL) {
		goto fail;
	}
	req = smb2cli_flush_send(frame, ev, cli,
				 fid_persistent, fid_volatile);
	if (req == NULL) {
		goto fail;
	}
	if (!tevent_req_poll_ntstatus(req, ev, &status)) {
		goto fail;
	}
	status = smb2cli_flush_recv(req);
 fail:
	TALLOC_FREE(frame);
	return status;
}
