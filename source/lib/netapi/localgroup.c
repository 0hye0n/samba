/*
 *  Unix SMB/CIFS implementation.
 *  NetApi LocalGroup Support
 *  Copyright (C) Guenther Deschner 2008
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"

#include "librpc/gen_ndr/libnetapi.h"
#include "lib/netapi/netapi.h"
#include "lib/netapi/netapi_private.h"
#include "lib/netapi/libnetapi.h"

static WERROR libnetapi_samr_lookup_and_open_alias(TALLOC_CTX *mem_ctx,
						   struct rpc_pipe_client *pipe_cli,
						   struct policy_handle *domain_handle,
						   struct lsa_String *lsa_account_name,
						   uint32_t access_rights,
						   struct policy_handle *alias_handle)
{
	NTSTATUS status;
	WERROR werr;
	struct samr_Ids user_rids, name_types;

	status = rpccli_samr_LookupNames(pipe_cli, mem_ctx,
					 domain_handle,
					 1,
					 lsa_account_name,
					 &user_rids,
					 &name_types);
	if (!NT_STATUS_IS_OK(status)) {
		werr = ntstatus_to_werror(status);
		goto done;
	}

	switch (name_types.ids[0]) {
		case SID_NAME_ALIAS:
		case SID_NAME_WKN_GRP:
			break;
		default:
			return WERR_INVALID_DATATYPE;
	}

	status = rpccli_samr_OpenAlias(pipe_cli, mem_ctx,
				       domain_handle,
				       access_rights,
				       user_rids.ids[0],
				       alias_handle);
	if (NT_STATUS_IS_OK(status)) {
		werr = ntstatus_to_werror(status);
		goto done;
	}

	werr = WERR_OK;

 done:
	return werr;
}

/****************************************************************
****************************************************************/

WERROR NetLocalGroupAdd_r(struct libnetapi_ctx *ctx,
			  struct NetLocalGroupAdd *r)
{
	struct cli_state *cli = NULL;
	struct rpc_pipe_client *pipe_cli = NULL;
	NTSTATUS status;
	WERROR werr;
	struct lsa_String lsa_account_name;
	struct policy_handle connect_handle, domain_handle, builtin_handle, alias_handle;
	struct dom_sid2 *domain_sid = NULL;
	uint32_t rid;

	struct LOCALGROUP_INFO_0 *info0;
	struct LOCALGROUP_INFO_1 *info1;

	const char *alias_name = NULL;

	if (!r->in.buf) {
		return WERR_INVALID_PARAM;
	}

	switch (r->in.level) {
		case 0:
			info0 = (struct LOCALGROUP_INFO_0 *)r->in.buf;
			alias_name = info0->lgrpi0_name;
			break;
		case 1:
			info1 = (struct LOCALGROUP_INFO_1 *)r->in.buf;
			alias_name = info1->lgrpi1_name;
			break;
		default:
			werr = WERR_UNKNOWN_LEVEL;
			goto done;
	}

	ZERO_STRUCT(connect_handle);
	ZERO_STRUCT(builtin_handle);
	ZERO_STRUCT(domain_handle);
	ZERO_STRUCT(alias_handle);

	werr = libnetapi_open_ipc_connection(ctx, r->in.server_name, &cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_open_pipe(ctx, cli, PI_SAMR, &pipe_cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_samr_open_builtin_domain(ctx, pipe_cli,
						  SAMR_ACCESS_OPEN_DOMAIN |
						  SAMR_ACCESS_ENUM_DOMAINS,
						  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
						  &connect_handle,
						  &builtin_handle);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	init_lsa_String(&lsa_account_name, alias_name);

	werr = libnetapi_samr_lookup_and_open_alias(ctx, pipe_cli,
						    &builtin_handle,
						    &lsa_account_name,
						    SAMR_ALIAS_ACCESS_LOOKUP_INFO,
						    &alias_handle);

	rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);

	if (W_ERROR_IS_OK(werr)) {
		werr = WERR_ALIAS_EXISTS;
		goto done;
	}

	werr = libnetapi_samr_open_domain(ctx, pipe_cli,
					  SAMR_ACCESS_ENUM_DOMAINS |
					  SAMR_ACCESS_OPEN_DOMAIN,
					  SAMR_DOMAIN_ACCESS_CREATE_ALIAS |
					  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
					  &connect_handle,
					  &domain_handle,
					  &domain_sid);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	status = rpccli_samr_CreateDomAlias(pipe_cli, ctx,
					    &domain_handle,
					    &lsa_account_name,
					    SEC_STD_DELETE |
					    SAMR_ALIAS_ACCESS_SET_INFO,
					    &alias_handle,
					    &rid);
	if (!NT_STATUS_IS_OK(status)) {
		werr = ntstatus_to_werror(status);
		goto done;
	}

	if (r->in.level == 1) {

		union samr_AliasInfo alias_info;

		init_lsa_String(&alias_info.description, info1->lgrpi1_comment);

		status = rpccli_samr_SetAliasInfo(pipe_cli, ctx,
						  &alias_handle,
						  ALIASINFODESCRIPTION,
						  &alias_info);
		if (!NT_STATUS_IS_OK(status)) {
			werr = ntstatus_to_werror(status);
			goto done;
		}
	}

	werr = WERR_OK;

 done:
	if (!cli) {
		return werr;
	}

	if (is_valid_policy_hnd(&alias_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &alias_handle);
	}
	if (is_valid_policy_hnd(&domain_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &domain_handle);
	}
	if (is_valid_policy_hnd(&builtin_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);
	}
	if (is_valid_policy_hnd(&connect_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &connect_handle);
	}

	return werr;
}

/****************************************************************
****************************************************************/

WERROR NetLocalGroupAdd_l(struct libnetapi_ctx *ctx,
			  struct NetLocalGroupAdd *r)
{
	return NetLocalGroupAdd_r(ctx, r);
}

/****************************************************************
****************************************************************/


WERROR NetLocalGroupDel_r(struct libnetapi_ctx *ctx,
			  struct NetLocalGroupDel *r)
{
	struct cli_state *cli = NULL;
	struct rpc_pipe_client *pipe_cli = NULL;
	NTSTATUS status;
	WERROR werr;
	struct lsa_String lsa_account_name;
	struct policy_handle connect_handle, domain_handle, builtin_handle, alias_handle;
	struct dom_sid2 *domain_sid = NULL;

	if (!r->in.group_name) {
		return WERR_INVALID_PARAM;
	}

	ZERO_STRUCT(connect_handle);
	ZERO_STRUCT(builtin_handle);
	ZERO_STRUCT(domain_handle);
	ZERO_STRUCT(alias_handle);

	werr = libnetapi_open_ipc_connection(ctx, r->in.server_name, &cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_open_pipe(ctx, cli, PI_SAMR, &pipe_cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_samr_open_builtin_domain(ctx, pipe_cli,
						  SAMR_ACCESS_OPEN_DOMAIN |
						  SAMR_ACCESS_ENUM_DOMAINS,
						  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
						  &connect_handle,
						  &builtin_handle);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	init_lsa_String(&lsa_account_name, r->in.group_name);

	werr = libnetapi_samr_lookup_and_open_alias(ctx, pipe_cli,
						    &builtin_handle,
						    &lsa_account_name,
						    SEC_STD_DELETE,
						    &alias_handle);

	rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);

	if (W_ERROR_IS_OK(werr)) {
		goto delete_alias;
	}

	werr = libnetapi_samr_open_domain(ctx, pipe_cli,
					  SAMR_ACCESS_ENUM_DOMAINS |
					  SAMR_ACCESS_OPEN_DOMAIN,
					  SAMR_DOMAIN_ACCESS_CREATE_ALIAS |
					  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
					  &connect_handle,
					  &domain_handle,
					  &domain_sid);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_samr_lookup_and_open_alias(ctx, pipe_cli,
						    &domain_handle,
						    &lsa_account_name,
						    SEC_STD_DELETE,
						    &alias_handle);

	rpccli_samr_Close(pipe_cli, ctx, &domain_handle);

	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}


 delete_alias:
	status = rpccli_samr_DeleteDomAlias(pipe_cli, ctx,
					    &alias_handle);
	if (!NT_STATUS_IS_OK(status)) {
		werr = ntstatus_to_werror(status);
		goto done;
	}

	ZERO_STRUCT(alias_handle);

	werr = WERR_OK;

 done:
	if (!cli) {
		return werr;
	}

	if (is_valid_policy_hnd(&alias_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &alias_handle);
	}
	if (is_valid_policy_hnd(&domain_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &domain_handle);
	}
	if (is_valid_policy_hnd(&builtin_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);
	}
	if (is_valid_policy_hnd(&connect_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &connect_handle);
	}

	return werr;
}

/****************************************************************
****************************************************************/

WERROR NetLocalGroupDel_l(struct libnetapi_ctx *ctx,
			  struct NetLocalGroupDel *r)
{
	return NetLocalGroupDel_r(ctx, r);
}

/****************************************************************
****************************************************************/

static WERROR map_alias_info_to_buffer(TALLOC_CTX *mem_ctx,
				       struct samr_AliasInfoAll *info,
				       uint32_t level,
				       uint8_t **buffer)
{
	struct LOCALGROUP_INFO_0 g0;
	struct LOCALGROUP_INFO_1 g1;
	struct LOCALGROUP_INFO_1002 g1002;

	switch (level) {
		case 0:
			g0.lgrpi0_name		= info->name.string;

			*buffer = (uint8_t *)talloc_memdup(mem_ctx, &g0, sizeof(g0));

			break;
		case 1:
			g1.lgrpi1_name		= info->name.string;
			g1.lgrpi1_comment	= info->description.string;

			*buffer = (uint8_t *)talloc_memdup(mem_ctx, &g1, sizeof(g1));

			break;
		case 1002:
			g1002.lgrpi1002_comment	= info->description.string;

			*buffer = (uint8_t *)talloc_memdup(mem_ctx, &g1002, sizeof(g1002));

			break;
		default:
			return WERR_UNKNOWN_LEVEL;
	}

	W_ERROR_HAVE_NO_MEMORY(*buffer);

	return WERR_OK;
}

/****************************************************************
****************************************************************/

WERROR NetLocalGroupGetInfo_r(struct libnetapi_ctx *ctx,
			      struct NetLocalGroupGetInfo *r)
{
	struct cli_state *cli = NULL;
	struct rpc_pipe_client *pipe_cli = NULL;
	NTSTATUS status;
	WERROR werr;
	struct lsa_String lsa_account_name;
	struct policy_handle connect_handle, domain_handle, builtin_handle, alias_handle;
	struct dom_sid2 *domain_sid = NULL;
	union samr_AliasInfo *alias_info = NULL;

	if (!r->in.group_name) {
		return WERR_INVALID_PARAM;
	}

	switch (r->in.level) {
		case 0:
		case 1:
		case 1002:
			break;
		default:
			return WERR_UNKNOWN_LEVEL;
	}

	ZERO_STRUCT(connect_handle);
	ZERO_STRUCT(builtin_handle);
	ZERO_STRUCT(domain_handle);
	ZERO_STRUCT(alias_handle);

	werr = libnetapi_open_ipc_connection(ctx, r->in.server_name, &cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_open_pipe(ctx, cli, PI_SAMR, &pipe_cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_samr_open_builtin_domain(ctx, pipe_cli,
						  SAMR_ACCESS_OPEN_DOMAIN |
						  SAMR_ACCESS_ENUM_DOMAINS,
						  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
						  &connect_handle,
						  &builtin_handle);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	init_lsa_String(&lsa_account_name, r->in.group_name);

	werr = libnetapi_samr_lookup_and_open_alias(ctx, pipe_cli,
						    &builtin_handle,
						    &lsa_account_name,
						    SAMR_ALIAS_ACCESS_LOOKUP_INFO,
						    &alias_handle);

	rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);

	if (W_ERROR_IS_OK(werr)) {
		goto query_alias;
	}

	werr = libnetapi_samr_open_domain(ctx, pipe_cli,
					  SAMR_ACCESS_ENUM_DOMAINS |
					  SAMR_ACCESS_OPEN_DOMAIN,
					  SAMR_DOMAIN_ACCESS_CREATE_ALIAS |
					  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
					  &connect_handle,
					  &domain_handle,
					  &domain_sid);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_samr_lookup_and_open_alias(ctx, pipe_cli,
						    &domain_handle,
						    &lsa_account_name,
						    SAMR_ALIAS_ACCESS_LOOKUP_INFO,
						    &alias_handle);

	rpccli_samr_Close(pipe_cli, ctx, &domain_handle);

	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

 query_alias:
	status = rpccli_samr_QueryAliasInfo(pipe_cli, ctx,
					    &alias_handle,
					    ALIASINFOALL,
					    &alias_info);
	if (!NT_STATUS_IS_OK(status)) {
		werr = ntstatus_to_werror(status);
		goto done;
	}

	werr = map_alias_info_to_buffer(ctx, &alias_info->all,
					r->in.level, r->out.buf);

 done:
	if (!cli) {
		return werr;
	}

	if (is_valid_policy_hnd(&alias_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &alias_handle);
	}
	if (is_valid_policy_hnd(&domain_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &domain_handle);
	}
	if (is_valid_policy_hnd(&builtin_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);
	}
	if (is_valid_policy_hnd(&connect_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &connect_handle);
	}

	return werr;
}

/****************************************************************
****************************************************************/

WERROR NetLocalGroupGetInfo_l(struct libnetapi_ctx *ctx,
			      struct NetLocalGroupGetInfo *r)
{
	return NetLocalGroupGetInfo_r(ctx, r);
}

/****************************************************************
****************************************************************/

static WERROR map_buffer_to_alias_info(TALLOC_CTX *mem_ctx,
				       uint32_t level,
				       uint8_t *buffer,
				       enum samr_AliasInfoEnum *alias_level,
				       union samr_AliasInfo **alias_info)
{
	struct LOCALGROUP_INFO_0 *info0;
	struct LOCALGROUP_INFO_1 *info1;
	struct LOCALGROUP_INFO_1002 *info1002;
	union samr_AliasInfo *info = NULL;

	info = TALLOC_ZERO_P(mem_ctx, union samr_AliasInfo);
	W_ERROR_HAVE_NO_MEMORY(info);

	switch (level) {
		case 0:
			info0 = (struct LOCALGROUP_INFO_0 *)buffer;
			init_lsa_String(&info->name, info0->lgrpi0_name);
			*alias_level = ALIASINFONAME;
			break;
		case 1:
			info1 = (struct LOCALGROUP_INFO_1 *)buffer;
			/* group name will be ignored */
			init_lsa_String(&info->description, info1->lgrpi1_comment);
			*alias_level = ALIASINFODESCRIPTION;
			break;
		case 1002:
			info1002 = (struct LOCALGROUP_INFO_1002 *)buffer;
			init_lsa_String(&info->description, info1002->lgrpi1002_comment);
			*alias_level = ALIASINFODESCRIPTION;
			break;
	}

	*alias_info = info;

	return WERR_OK;
}

/****************************************************************
****************************************************************/

WERROR NetLocalGroupSetInfo_r(struct libnetapi_ctx *ctx,
			      struct NetLocalGroupSetInfo *r)
{
	struct cli_state *cli = NULL;
	struct rpc_pipe_client *pipe_cli = NULL;
	NTSTATUS status;
	WERROR werr;
	struct lsa_String lsa_account_name;
	struct policy_handle connect_handle, domain_handle, builtin_handle, alias_handle;
	struct dom_sid2 *domain_sid = NULL;
	enum samr_AliasInfoEnum alias_level;
	union samr_AliasInfo *alias_info = NULL;

	if (!r->in.group_name) {
		return WERR_INVALID_PARAM;
	}

	switch (r->in.level) {
		case 0:
		case 1:
		case 1002:
			break;
		default:
			return WERR_UNKNOWN_LEVEL;
	}

	ZERO_STRUCT(connect_handle);
	ZERO_STRUCT(builtin_handle);
	ZERO_STRUCT(domain_handle);
	ZERO_STRUCT(alias_handle);

	werr = libnetapi_open_ipc_connection(ctx, r->in.server_name, &cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_open_pipe(ctx, cli, PI_SAMR, &pipe_cli);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_samr_open_builtin_domain(ctx, pipe_cli,
						  SAMR_ACCESS_OPEN_DOMAIN |
						  SAMR_ACCESS_ENUM_DOMAINS,
						  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
						  &connect_handle,
						  &builtin_handle);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	init_lsa_String(&lsa_account_name, r->in.group_name);

	werr = libnetapi_samr_lookup_and_open_alias(ctx, pipe_cli,
						    &builtin_handle,
						    &lsa_account_name,
						    SAMR_ALIAS_ACCESS_SET_INFO,
						    &alias_handle);

	rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);

	if (W_ERROR_IS_OK(werr)) {
		goto set_alias;
	}

	werr = libnetapi_samr_open_domain(ctx, pipe_cli,
					  SAMR_ACCESS_ENUM_DOMAINS |
					  SAMR_ACCESS_OPEN_DOMAIN,
					  SAMR_DOMAIN_ACCESS_OPEN_ACCOUNT,
					  &connect_handle,
					  &domain_handle,
					  &domain_sid);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	werr = libnetapi_samr_lookup_and_open_alias(ctx, pipe_cli,
						    &domain_handle,
						    &lsa_account_name,
						    SAMR_ALIAS_ACCESS_SET_INFO,
						    &alias_handle);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	rpccli_samr_Close(pipe_cli, ctx, &domain_handle);

 set_alias:

	werr = map_buffer_to_alias_info(ctx, r->in.level, r->in.buf,
					&alias_level, &alias_info);
	if (!W_ERROR_IS_OK(werr)) {
		goto done;
	}

	status = rpccli_samr_SetAliasInfo(pipe_cli, ctx,
					  &alias_handle,
					  alias_level,
					  alias_info);
	if (!NT_STATUS_IS_OK(status)) {
		werr = ntstatus_to_werror(status);
		goto done;
	}

	werr = WERR_OK;

 done:
	if (!cli) {
		return werr;
	}

	if (is_valid_policy_hnd(&alias_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &alias_handle);
	}
	if (is_valid_policy_hnd(&domain_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &domain_handle);
	}
	if (is_valid_policy_hnd(&builtin_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &builtin_handle);
	}
	if (is_valid_policy_hnd(&connect_handle)) {
		rpccli_samr_Close(pipe_cli, ctx, &connect_handle);
	}

	return werr;
}

/****************************************************************
****************************************************************/

WERROR NetLocalGroupSetInfo_l(struct libnetapi_ctx *ctx,
			      struct NetLocalGroupSetInfo *r)
{
	return NetLocalGroupSetInfo_r(ctx, r);
}
