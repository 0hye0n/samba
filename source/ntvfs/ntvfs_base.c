/* 
   Unix SMB/CIFS implementation.
   NTVFS base code

   Copyright (C) Andrew Tridgell 2003
   Copyright (C) Stefan (metze) Metzmacher 2004

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
/*
  this implements the core code for all NTVFS modules. Backends register themselves here.
*/

#include "includes.h"
#include "dlinklist.h"
#include "smb_server/smb_server.h"
#include "smb_build.h"
#include "ntvfs/ntvfs.h"

/* the list of currently registered NTVFS backends, note that there
 * can be more than one backend with the same name, as long as they
 * have different typesx */
static struct ntvfs_backend {
	const struct ntvfs_ops *ops;
} *backends = NULL;
static int num_backends;

/*
  register a NTVFS backend. 

  The 'name' can be later used by other backends to find the operations
  structure for this backend.  

  The 'type' is used to specify whether this is for a disk, printer or IPC$ share
*/
_PUBLIC_ NTSTATUS ntvfs_register(const void *_ops)
{
	const struct ntvfs_ops *ops = _ops;
	struct ntvfs_ops *new_ops;
	
	if (ntvfs_backend_byname(ops->name, ops->type) != NULL) {
		/* its already registered! */
		DEBUG(0,("NTVFS backend '%s' for type %d already registered\n", 
			 ops->name, (int)ops->type));
		return NT_STATUS_OBJECT_NAME_COLLISION;
	}

	backends = realloc_p(backends, struct ntvfs_backend, num_backends+1);
	if (!backends) {
		smb_panic("out of memory in ntvfs_register");
	}

	new_ops = smb_xmemdup(ops, sizeof(*ops));
	new_ops->name = smb_xstrdup(ops->name);

	backends[num_backends].ops = new_ops;

	num_backends++;

	DEBUG(3,("NTVFS backend '%s' for type %d registered\n", 
		 ops->name,ops->type));

	return NT_STATUS_OK;
}


/*
  return the operations structure for a named backend of the specified type
*/
_PUBLIC_ const struct ntvfs_ops *ntvfs_backend_byname(const char *name, enum ntvfs_type type)
{
	int i;

	for (i=0;i<num_backends;i++) {
		if (backends[i].ops->type == type && 
		    strcmp(backends[i].ops->name, name) == 0) {
			return backends[i].ops;
		}
	}

	return NULL;
}


/*
  return the NTVFS interface version, and the size of some critical types
  This can be used by backends to either detect compilation errors, or provide
  multiple implementations for different smbd compilation options in one module
*/
_PUBLIC_ const struct ntvfs_critical_sizes *ntvfs_interface_version(void)
{
	static const struct ntvfs_critical_sizes critical_sizes = {
		NTVFS_INTERFACE_VERSION,
		sizeof(struct ntvfs_critical_sizes),
		sizeof(struct ntvfs_context),
		sizeof(struct ntvfs_module_context),
		sizeof(struct ntvfs_ops),
		sizeof(struct ntvfs_async_state),
		sizeof(struct smbsrv_tcon),
		sizeof(struct smbsrv_request),
	};

	return &critical_sizes;
}


/*
  initialise a connection structure to point at a NTVFS backend
*/
NTSTATUS ntvfs_init_connection(struct smbsrv_request *req, enum ntvfs_type type)
{
	const char **handlers = lp_ntvfs_handler(req->tcon->service);
	int i;
	struct ntvfs_context *ctx;

	if (!handlers) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	ctx = talloc(req->tcon, struct ntvfs_context);
	NT_STATUS_HAVE_NO_MEMORY(ctx);
	ctx->type = type;
	ctx->modules = NULL;

	for (i=0; handlers[i]; i++) {
		struct ntvfs_module_context *ntvfs;

		ntvfs = talloc(ctx, struct ntvfs_module_context);
		NT_STATUS_HAVE_NO_MEMORY(ntvfs);

		ntvfs->private_data = NULL;

		ntvfs->ops = ntvfs_backend_byname(handlers[i], ctx->type);
		if (!ntvfs->ops) {
			DEBUG(1,("ntvfs_init_connection: failed to find backend=%s, type=%d\n",
				handlers[i], ctx->type));
			return NT_STATUS_INTERNAL_ERROR;
		}
		ntvfs->depth = i;
		DLIST_ADD_END(ctx->modules, ntvfs, struct ntvfs_module_context *);
	}

	if (!ctx->modules) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	req->tcon->ntvfs_ctx = ctx;

	return NT_STATUS_OK;
}

NTSTATUS ntvfs_init(void)
{
	init_module_fn static_init[] = STATIC_NTVFS_MODULES;
	init_module_fn *shared_init = load_samba_modules(NULL, "ntvfs");

	run_init_functions(static_init);
	run_init_functions(shared_init);

	talloc_free(shared_init);
	
	return NT_STATUS_OK;
}
