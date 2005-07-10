/* 
   Unix SMB/CIFS implementation.

   NBT server task

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

#include "includes.h"
#include "lib/events/events.h"
#include "smbd/service_task.h"
#include "nbt_server/nbt_server.h"


/*
  serve out the nbt statistics
*/
static NTSTATUS nbtd_information(struct irpc_message *msg, 
				 struct nbtd_information *r)
{
	struct nbtd_server *server = talloc_get_type(msg->private, struct nbtd_server);

	switch (r->in.level) {
	case NBTD_INFO_STATISTICS:
		r->out.info.stats = &server->stats;
		break;
	}

	return NT_STATUS_OK;
}



/*
  startup the nbtd task
*/
static void nbtd_task_init(struct task_server *task)
{
	struct nbtd_server *nbtsrv;
	NTSTATUS status;

	if (iface_count() == 0) {
		task_server_terminate(task, "nbtd: no network interfaces configured");
		return;
	}

	nbtsrv = talloc(task, struct nbtd_server);
	if (nbtsrv == NULL) {
		task_server_terminate(task, "nbtd: out of memory");
		return;
	}

	nbtsrv->task            = task;
	nbtsrv->interfaces      = NULL;
	nbtsrv->bcast_interface = NULL;
	nbtsrv->wins_interface  = NULL;

	/* start listening on the configured network interfaces */
	status = nbtd_startup_interfaces(nbtsrv);
	if (!NT_STATUS_IS_OK(status)) {
		task_server_terminate(task, "nbtd failed to setup interfaces");
		return;
	}

	/* start the WINS server, if appropriate */
	status = nbtd_winsserver_init(nbtsrv);
	if (!NT_STATUS_IS_OK(status)) {
		task_server_terminate(task, "nbtd failed to start WINS server");
		return;
	}

	/* setup monitoring */
	status = IRPC_REGISTER(task->msg_ctx, irpc, NBTD_INFORMATION, 
			       nbtd_information, nbtsrv);
	if (!NT_STATUS_IS_OK(status)) {
		task_server_terminate(task, "nbtd failed to setup monitoring");
		return;
	}

	/* start the process of registering our names on all interfaces */
	nbtd_register_names(nbtsrv);

	irpc_add_name(task->msg_ctx, "nbt_server");
}


/*
  initialise the nbt server
 */
static NTSTATUS nbtd_init(struct event_context *event_ctx, const struct model_ops *model_ops)
{
	return task_server_startup(event_ctx, model_ops, nbtd_task_init);
}


/*
  register ourselves as a available server
*/
NTSTATUS server_service_nbtd_init(void)
{
	return register_server_service("nbt", nbtd_init);
}
