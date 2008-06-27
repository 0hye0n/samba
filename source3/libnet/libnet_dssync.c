/*
   Unix SMB/CIFS implementation.

   Copyright (C) Stefan (metze) Metzmacher 2005
   Copyright (C) Guenther Deschner 2008

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
#include "libnet/libnet_dssync.h"

/****************************************************************
****************************************************************/

static int libnet_dssync_free_context(struct dssync_context *ctx)
{
	if (!ctx) {
		return 0;
	}

	if (is_valid_policy_hnd(&ctx->bind_handle) && ctx->cli) {
		rpccli_drsuapi_DsUnbind(ctx->cli, ctx, &ctx->bind_handle, NULL);
	}

	return 0;
}

/****************************************************************
****************************************************************/

NTSTATUS libnet_dssync_init_context(TALLOC_CTX *mem_ctx,
				    struct dssync_context **ctx_p)
{
	struct dssync_context *ctx;

	ctx = TALLOC_ZERO_P(mem_ctx, struct dssync_context);
	NT_STATUS_HAVE_NO_MEMORY(ctx);

	talloc_set_destructor(ctx, libnet_dssync_free_context);

	*ctx_p = ctx;

	return NT_STATUS_OK;
}

/****************************************************************
****************************************************************/

static DATA_BLOB *decrypt_attr_val(TALLOC_CTX *mem_ctx,
				   DATA_BLOB *session_key,
				   uint32_t rid,
				   enum drsuapi_DsAttributeId id,
				   DATA_BLOB *raw_data)
{
	bool rcrypt = false;
	DATA_BLOB out_data;

	ZERO_STRUCT(out_data);

	switch (id) {
		case DRSUAPI_ATTRIBUTE_dBCSPwd:
		case DRSUAPI_ATTRIBUTE_unicodePwd:
		case DRSUAPI_ATTRIBUTE_ntPwdHistory:
		case DRSUAPI_ATTRIBUTE_lmPwdHistory:
			rcrypt	= true;
			break;
		case DRSUAPI_ATTRIBUTE_supplementalCredentials:
		case DRSUAPI_ATTRIBUTE_priorValue:
		case DRSUAPI_ATTRIBUTE_currentValue:
		case DRSUAPI_ATTRIBUTE_trustAuthOutgoing:
		case DRSUAPI_ATTRIBUTE_trustAuthIncoming:
		case DRSUAPI_ATTRIBUTE_initialAuthOutgoing:
		case DRSUAPI_ATTRIBUTE_initialAuthIncoming:
			break;
		default:
			return raw_data;
	}

	out_data = decrypt_drsuapi_blob(mem_ctx, session_key, rcrypt,
					  rid, raw_data);

	if (out_data.length) {
		return (DATA_BLOB *)talloc_memdup(mem_ctx, &out_data, sizeof(DATA_BLOB));
	}

	return raw_data;
}

/****************************************************************
****************************************************************/

static void parse_obj_identifier(struct drsuapi_DsReplicaObjectIdentifier *id,
				 uint32_t *rid)
{
	if (!id || !rid) {
		return;
	}

	*rid = 0;

	if (id->sid.num_auths > 0) {
		*rid = id->sid.sub_auths[id->sid.num_auths - 1];
	}
}

/****************************************************************
****************************************************************/

static void parse_obj_attribute(TALLOC_CTX *mem_ctx,
				DATA_BLOB *session_key,
				uint32_t rid,
				struct drsuapi_DsReplicaAttribute *attr)
{
	int i = 0;

	for (i=0; i<attr->value_ctr.num_values; i++) {

		DATA_BLOB *plain_data = NULL;

		plain_data = decrypt_attr_val(mem_ctx,
					      session_key,
					      rid,
					      attr->attid,
					      attr->value_ctr.values[i].blob);

		attr->value_ctr.values[i].blob = plain_data;
	}
}

/****************************************************************
****************************************************************/

static void libnet_dssync_decrypt_attributes(TALLOC_CTX *mem_ctx,
					     DATA_BLOB *session_key,
					     struct drsuapi_DsReplicaObjectListItemEx *cur)
{
	for (; cur; cur = cur->next_object) {

		uint32_t i;
		uint32_t rid = 0;

		parse_obj_identifier(cur->object.identifier, &rid);

		for (i=0; i < cur->object.attribute_ctr.num_attributes; i++) {

			struct drsuapi_DsReplicaAttribute *attr;

			attr = &cur->object.attribute_ctr.attributes[i];

			if (attr->value_ctr.num_values < 1) {
				continue;
			}

			if (!attr->value_ctr.values[0].blob) {
				continue;
			}

			parse_obj_attribute(mem_ctx,
					    session_key,
					    rid,
					    attr);
		}
	}
}
/****************************************************************
****************************************************************/

static NTSTATUS libnet_dssync_bind(TALLOC_CTX *mem_ctx,
				   struct dssync_context *ctx)
{
	NTSTATUS status;
	WERROR werr;

	struct GUID bind_guid;
	struct drsuapi_DsBindInfoCtr bind_info;
	struct drsuapi_DsBindInfo28 info28;

	ZERO_STRUCT(info28);

	GUID_from_string(DRSUAPI_DS_BIND_GUID, &bind_guid);

	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_BASE;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_ASYNC_REPLICATION;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_REMOVEAPI;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_MOVEREQ_V2;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GETCHG_COMPRESS;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_DCINFO_V1;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_RESTORE_USN_OPTIMIZATION;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_KCC_EXECUTE;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_ADDENTRY_V2;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_LINKED_VALUE_REPLICATION;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_DCINFO_V2;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_INSTANCE_TYPE_NOT_REQ_ON_MOD;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_CRYPTO_BIND;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GET_REPL_INFO;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_STRONG_ENCRYPTION;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_DCINFO_V01;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_TRANSITIVE_MEMBERSHIP;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_ADD_SID_HISTORY;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_POST_BETA3;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GET_MEMBERSHIPS2;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GETCHGREQ_V6;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_NONDOMAIN_NCS;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GETCHGREQ_V8;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GETCHGREPLY_V5;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GETCHGREPLY_V6;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_ADDENTRYREPLY_V3;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_GETCHGREPLY_V7;
	info28.supported_extensions	|= DRSUAPI_SUPPORTED_EXTENSION_VERIFY_OBJECT;
	info28.site_guid		= GUID_zero();
	info28.u1			= 508;
	info28.repl_epoch		= 0;

	bind_info.length = 28;
	bind_info.info.info28 = info28;

	status = rpccli_drsuapi_DsBind(ctx->cli, mem_ctx,
				       &bind_guid,
				       &bind_info,
				       &ctx->bind_handle,
				       &werr);

	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	if (!W_ERROR_IS_OK(werr)) {
		return werror_to_ntstatus(werr);
	}

	return status;
}

/****************************************************************
****************************************************************/

static NTSTATUS libnet_dssync_lookup_nc(TALLOC_CTX *mem_ctx,
					struct dssync_context *ctx)
{
	NTSTATUS status;
	WERROR werr;
	int32_t level = 1;
	union drsuapi_DsNameRequest req;
	int32_t level_out;
	struct drsuapi_DsNameString names[1];
	union drsuapi_DsNameCtr ctr;

	names[0].str = talloc_asprintf(mem_ctx, "%s\\", ctx->domain_name);
	NT_STATUS_HAVE_NO_MEMORY(names[0].str);

	req.req1.codepage	= 1252; /* german */
	req.req1.language	= 0x00000407; /* german */
	req.req1.count		= 1;
	req.req1.names		= names;
	req.req1.format_flags	= DRSUAPI_DS_NAME_FLAG_NO_FLAGS;
	req.req1.format_offered	= DRSUAPI_DS_NAME_FORMAT_UKNOWN;
	req.req1.format_desired	= DRSUAPI_DS_NAME_FORMAT_FQDN_1779;

	status = rpccli_drsuapi_DsCrackNames(ctx->cli, mem_ctx,
					     &ctx->bind_handle,
					     level,
					     &req,
					     &level_out,
					     &ctr,
					     &werr);
	if (!NT_STATUS_IS_OK(status)) {
		ctx->error_message = talloc_asprintf(mem_ctx,
			"Failed to lookup DN for domain name: %s",
			get_friendly_werror_msg(werr));
		return status;
	}

	if (!W_ERROR_IS_OK(werr)) {
		return werror_to_ntstatus(werr);
	}

	if (ctr.ctr1->count != 1) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	if (ctr.ctr1->array[0].status != DRSUAPI_DS_NAME_STATUS_OK) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	ctx->nc_dn = talloc_strdup(mem_ctx, ctr.ctr1->array[0].result_name);
	NT_STATUS_HAVE_NO_MEMORY(ctx->nc_dn);

	if (!ctx->dns_domain_name) {
		ctx->dns_domain_name = talloc_strdup_upper(mem_ctx,
			ctr.ctr1->array[0].dns_domain_name);
		NT_STATUS_HAVE_NO_MEMORY(ctx->dns_domain_name);
	}

	return NT_STATUS_OK;
}

/****************************************************************
****************************************************************/

static NTSTATUS libnet_dssync_init(TALLOC_CTX *mem_ctx,
				   struct dssync_context *ctx)
{
	NTSTATUS status;

	status = libnet_dssync_bind(mem_ctx, ctx);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	if (!ctx->nc_dn) {
		status = libnet_dssync_lookup_nc(mem_ctx, ctx);
	}

	return status;
}

/****************************************************************
****************************************************************/

static NTSTATUS libnet_dssync_process(TALLOC_CTX *mem_ctx,
				      struct dssync_context *ctx)
{
	NTSTATUS status;
	WERROR werr;

	int32_t level = 8;
	int32_t level_out = 0;
	union drsuapi_DsGetNCChangesRequest req;
	union drsuapi_DsGetNCChangesCtr ctr;
	struct drsuapi_DsReplicaObjectIdentifier nc;
	struct dom_sid null_sid;

	struct drsuapi_DsGetNCChangesCtr1 *ctr1 = NULL;
	struct drsuapi_DsGetNCChangesCtr6 *ctr6 = NULL;
	int32_t out_level = 0;
	int y;

	ZERO_STRUCT(null_sid);
	ZERO_STRUCT(req);

	nc.dn = ctx->nc_dn;
	nc.guid = GUID_zero();
	nc.sid = null_sid;

	req.req8.naming_context		= &nc;
	req.req8.replica_flags		= DRSUAPI_DS_REPLICA_NEIGHBOUR_WRITEABLE |
					  DRSUAPI_DS_REPLICA_NEIGHBOUR_SYNC_ON_STARTUP |
					  DRSUAPI_DS_REPLICA_NEIGHBOUR_DO_SCHEDULED_SYNCS |
					  DRSUAPI_DS_REPLICA_NEIGHBOUR_RETURN_OBJECT_PARENTS |
					  DRSUAPI_DS_REPLICA_NEIGHBOUR_NEVER_SYNCED;
	req.req8.max_object_count	= 402;
	req.req8.max_ndr_size		= 402116;

	for (y=0; ;y++) {

		bool last_query = true;

		if (level == 8) {
			DEBUG(1,("start[%d] tmp_higest_usn: %llu , highest_usn: %llu\n",y,
				(long long)req.req8.highwatermark.tmp_highest_usn,
				(long long)req.req8.highwatermark.highest_usn));
		}

		status = rpccli_drsuapi_DsGetNCChanges(ctx->cli, mem_ctx,
						       &ctx->bind_handle,
						       level,
						       &req,
						       &level_out,
						       &ctr,
						       &werr);
		if (!NT_STATUS_IS_OK(status)) {
			ctx->error_message = talloc_asprintf(mem_ctx,
				"Failed to get NC Changes: %s",
				get_friendly_werror_msg(werr));
			goto out;
		}

		if (!W_ERROR_IS_OK(werr)) {
			status = werror_to_ntstatus(werr);
			goto out;
		}

		if (level_out == 1) {
			out_level = 1;
			ctr1 = &ctr.ctr1;
		} else if (level_out == 2) {
			out_level = 1;
			ctr1 = ctr.ctr2.ctr.mszip1.ctr1;
		}

		status = cli_get_session_key(mem_ctx, ctx->cli, &ctx->session_key);
		if (!NT_STATUS_IS_OK(status)) {
			ctx->error_message = talloc_asprintf(mem_ctx,
				"Failed to get Session Key: %s",
				nt_errstr(status));
			return status;
		}

		if (out_level == 1) {
			DEBUG(1,("end[%d] tmp_highest_usn: %llu , highest_usn: %llu\n",y,
				(long long)ctr1->new_highwatermark.tmp_highest_usn,
				(long long)ctr1->new_highwatermark.highest_usn));

			libnet_dssync_decrypt_attributes(mem_ctx,
							 &ctx->session_key,
							 ctr1->first_object);

			if (ctr1->new_highwatermark.tmp_highest_usn > ctr1->new_highwatermark.highest_usn) {
				req.req5.highwatermark = ctr1->new_highwatermark;
				last_query = false;
			}

			if (ctx->processing_fn) {
				status = ctx->processing_fn(mem_ctx,
							    ctr1->first_object,
							    &ctr1->mapping_ctr,
							    last_query,
							    ctx);
				if (!NT_STATUS_IS_OK(status)) {
					ctx->error_message = talloc_asprintf(mem_ctx,
						"Failed to call processing function: %s",
						nt_errstr(status));
					goto out;
				}
			}

			if (!last_query) {
				continue;
			}
		}

		if (level_out == 6) {
			out_level = 6;
			ctr6 = &ctr.ctr6;
		} else if (level_out == 7
			   && ctr.ctr7.level == 6
			   && ctr.ctr7.type == DRSUAPI_COMPRESSION_TYPE_MSZIP) {
			out_level = 6;
			ctr6 = ctr.ctr7.ctr.mszip6.ctr6;
		}

		if (out_level == 6) {
			DEBUG(1,("end[%d] tmp_highest_usn: %llu , highest_usn: %llu\n",y,
				(long long)ctr6->new_highwatermark.tmp_highest_usn,
				(long long)ctr6->new_highwatermark.highest_usn));

			libnet_dssync_decrypt_attributes(mem_ctx,
							 &ctx->session_key,
							 ctr6->first_object);

			if (ctr6->new_highwatermark.tmp_highest_usn > ctr6->new_highwatermark.highest_usn) {
				req.req8.highwatermark = ctr6->new_highwatermark;
				last_query = false;
			}

			if (ctx->processing_fn) {
				status = ctx->processing_fn(mem_ctx,
							    ctr6->first_object,
							    &ctr6->mapping_ctr,
							    last_query,
							    ctx);
				if (!NT_STATUS_IS_OK(status)) {
					ctx->error_message = talloc_asprintf(mem_ctx,
						"Failed to call processing function: %s",
						nt_errstr(status));
					goto out;
				}
			}

			if (!last_query) {
				continue;
			}
		}

		break;
	}

 out:
	return status;
}

/****************************************************************
****************************************************************/

NTSTATUS libnet_dssync(TALLOC_CTX *mem_ctx,
		       struct dssync_context *ctx)
{
	NTSTATUS status;

	status = libnet_dssync_init(mem_ctx, ctx);
	if (!NT_STATUS_IS_OK(status)) {
		goto out;
	}

	status = libnet_dssync_process(mem_ctx, ctx);
	if (!NT_STATUS_IS_OK(status)) {
		goto out;
	}

 out:
	return status;
}
