/* 
   Unix SMB/CIFS implementation.
   
   Copyright (C) Stefan Metzmacher	2004
   
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
#include "libcli/nbt/libnbt.h"
#include "libnet/libnet.h"

/**
 * Finds a domain pdc (generic part)
 * 
 * @param ctx initialised libnet context
 * @param mem_ctx memory context of this call
 * @param r data structure containing necessary parameters and return values
 * @return nt status of the call
 **/

static NTSTATUS libnet_find_pdc_generic(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, 
					union libnet_find_pdc *r)
{
	const char *address;
	NTSTATUS status;
	struct nbt_name name;

	if (is_ipaddress(r->generic.in.domain_name)) {
		r->generic.out.pdc_name = r->generic.in.domain_name;
		return NT_STATUS_OK;
	}

	name.name = r->generic.in.domain_name;
	name.type = NBT_NAME_PDC;
	name.scope = NULL;

	status = resolve_name(&name, mem_ctx, &address);
	if (!NT_STATUS_IS_OK(status)) {
		name.type = NBT_NAME_SERVER;
		status = resolve_name(&name, mem_ctx, &address);
	}
	NT_STATUS_NOT_OK_RETURN(status);

	r->generic.out.pdc_name = talloc_strdup(mem_ctx, address);

	return NT_STATUS_OK;
}


/**
 * Finds a domain pdc function
 * 
 * @param ctx initialised libnet context
 * @param mem_ctx memory context of this call
 * @param r data structure containing necessary parameters and return values
 * @return nt status of the call
 **/

NTSTATUS libnet_find_pdc(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, union libnet_find_pdc *r)
{
	switch (r->generic.level) {
		case LIBNET_FIND_PDC_GENERIC:
			return libnet_find_pdc_generic(ctx, mem_ctx, r);
	}

	return NT_STATUS_INVALID_LEVEL;
}


/**
 * Connects rpc pipe on remote server
 * 
 * @param ctx initialised libnet context
 * @param mem_ctx memory context of this call
 * @param r data structure containing necessary parameters and return values
 * @return nt status of the call
 **/

static NTSTATUS libnet_rpc_connect_standard(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, union libnet_rpc_connect *r)
{
	NTSTATUS status;
	const char *binding = NULL;

	binding = talloc_asprintf(mem_ctx, "ncacn_np:%s",
					r->standard.in.server_name);

	status = dcerpc_pipe_connect(&r->standard.out.dcerpc_pipe,
					binding,
					r->standard.in.dcerpc_iface_uuid,
					r->standard.in.dcerpc_iface_version,
					ctx->user.domain_name,
					ctx->user.account_name,
					ctx->user.password); 
	if (!NT_STATUS_IS_OK(status)) {
		r->standard.out.error_string = talloc_asprintf(mem_ctx, 
						"dcerpc_pipe_connect to pipe %s failed with %s\n",
						r->standard.in.dcerpc_iface_name, binding);
		return status;
	}

	r->standard.out.error_string = NULL;

	return status;
}


/**
 * Connects rpc pipe on domain pdc
 * 
 * @param ctx initialised libnet context
 * @param mem_ctx memory context of this call
 * @param r data structure containing necessary parameters and return values
 * @return nt status of the call
 **/

static NTSTATUS libnet_rpc_connect_pdc(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, union libnet_rpc_connect *r)
{
	NTSTATUS status;
	union libnet_rpc_connect r2;
	union libnet_find_pdc f;

	f.generic.level			= LIBNET_FIND_PDC_GENERIC;
	f.generic.in.domain_name	= r->pdc.in.domain_name;

	status = libnet_find_pdc(ctx, mem_ctx, &f);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	r2.standard.level			= LIBNET_RPC_CONNECT_STANDARD;
	r2.standard.in.server_name		= f.generic.out.pdc_name;
	r2.standard.in.dcerpc_iface_name	= r->standard.in.dcerpc_iface_name;
	r2.standard.in.dcerpc_iface_uuid	= r->standard.in.dcerpc_iface_uuid;
	r2.standard.in.dcerpc_iface_version	= r->standard.in.dcerpc_iface_version;
	
	status = libnet_rpc_connect(ctx, mem_ctx, &r2);

	r->pdc.out.dcerpc_pipe		= r2.standard.out.dcerpc_pipe;
	r->pdc.out.error_string		= r2.standard.out.error_string;

	return status;
}


/**
 * Connects to rpc pipe on remote server or pdc
 * 
 * @param ctx initialised libnet context
 * @param mem_ctx memory context of this call
 * @param r data structure containing necessary parameters and return values
 * @return nt status of the call
 **/

NTSTATUS libnet_rpc_connect(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, union libnet_rpc_connect *r)
{
	switch (r->standard.level) {
		case LIBNET_RPC_CONNECT_STANDARD:
			return libnet_rpc_connect_standard(ctx, mem_ctx, r);
		case LIBNET_RPC_CONNECT_PDC:
			return libnet_rpc_connect_pdc(ctx, mem_ctx, r);
	}

	return NT_STATUS_INVALID_LEVEL;
}
