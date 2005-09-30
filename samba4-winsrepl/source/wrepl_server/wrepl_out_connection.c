/* 
   Unix SMB/CIFS implementation.
   
   WINS Replication server
   
   Copyright (C) Stefan Metzmacher	2005
   
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
#include "lib/socket/socket.h"
#include "smbd/service_task.h"
#include "smbd/service_stream.h"
#include "lib/messaging/irpc.h"
#include "librpc/gen_ndr/ndr_winsrepl.h"
#include "wrepl_server/wrepl_server.h"
#include "nbt_server/wins/winsdb.h"
#include "ldb/include/ldb.h"
#include "libcli/composite/composite.h"
#include "libcli/wrepl/winsrepl.h"
#include "wrepl_server/wrepl_out_helpers.h"


static void wreplsrv_pull_handler_te(struct event_context *ev, struct timed_event *te,
				     struct timeval t, void *ptr);

static void wreplsrv_pull_handler_creq(struct composite_context *creq)
{
	struct wreplsrv_partner *partner = talloc_get_type(creq->async.private_data, struct wreplsrv_partner);
	uint32_t interval;

	partner->pull.last_status = wreplsrv_pull_cycle_recv(partner->pull.creq);
	partner->pull.creq = NULL;
	talloc_free(partner->pull.cycle_io);
	partner->pull.cycle_io = NULL;

	if (!NT_STATUS_IS_OK(partner->pull.last_status)) {
		interval = partner->pull.error_count * partner->pull.retry_interval;
		interval = MIN(interval, partner->pull.interval);
		partner->pull.error_count++;

		DEBUG(1,("wreplsrv_pull_cycle(%s): %s: next: %us\n",
			 partner->address, nt_errstr(partner->pull.last_status),
			 interval));
	} else {
		interval = partner->pull.interval;
		partner->pull.error_count = 0;

		DEBUG(2,("wreplsrv_pull_cycle(%s): %s: next: %us\n",
			 partner->address, nt_errstr(partner->pull.last_status),
			 interval));
	}

	partner->pull.te = event_add_timed(partner->service->task->event_ctx, partner,
					   timeval_current_ofs(interval, 0),
					   wreplsrv_pull_handler_te, partner);
	if (!partner->pull.te) {
		DEBUG(0,("wreplsrv_pull_handler_creq: event_add_timed() failed! no memory!\n"));
	}
}

static void wreplsrv_pull_handler_te(struct event_context *ev, struct timed_event *te,
				     struct timeval t, void *ptr)
{
	struct wreplsrv_partner *partner = talloc_get_type(ptr, struct wreplsrv_partner);

	partner->pull.cycle_io = talloc(partner, struct wreplsrv_pull_cycle_io);
	if (!partner->pull.cycle_io) {
		goto requeue;
	}

	
	partner->pull.cycle_io->in.partner	= partner;
	partner->pull.cycle_io->in.num_owners	= 0;
	partner->pull.cycle_io->in.owners	= NULL;
	partner->pull.cycle_io->in.wreplconn	= NULL;
	partner->pull.creq = wreplsrv_pull_cycle_send(partner, partner->pull.cycle_io);
	if (!partner->pull.creq) {
		DEBUG(1,("wreplsrv_pull_cycle_send(%s) failed\n",
			 partner->address));
		goto requeue;
	}

	partner->pull.creq->async.fn		= wreplsrv_pull_handler_creq;
	partner->pull.creq->async.private_data	= partner;

	return;
requeue:
	/* retry later */
	partner->pull.te = event_add_timed(partner->service->task->event_ctx, partner,
					   timeval_add(&t, partner->pull.retry_interval, 0),
					   wreplsrv_pull_handler_te, partner);
	if (!partner->pull.te) {
		DEBUG(0,("wreplsrv_pull_handler_te: event_add_timed() failed! no memory!\n"));
	}
}

NTSTATUS wreplsrv_setup_out_connections(struct wreplsrv_service *service)
{
	struct wreplsrv_partner *cur;

	for (cur = service->partners; cur; cur = cur->next) {
		if (cur->type & WINSREPL_PARTNER_PULL) {
			cur->pull.te = event_add_timed(service->task->event_ctx, cur,
						       timeval_zero(), wreplsrv_pull_handler_te, cur);
			NT_STATUS_HAVE_NO_MEMORY(cur->pull.te);
		}
	}

	return NT_STATUS_OK;
}
