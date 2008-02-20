/* 
   ldb database module

   LDAP semantics mapping module

   Copyright (C) Jelmer Vernooij 2005
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2006

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

/* 
   This module relies on ldb_map to do all the real work, but performs
   some of the trivial mappings between AD semantics and that provided
   by OpenLDAP and similar servers.
*/

#include "includes.h"
#include "ldb/include/ldb.h"
#include "ldb/include/ldb_private.h"
#include "ldb/include/ldb_errors.h"
#include "ldb/ldb_map/ldb_map.h"

#include "librpc/gen_ndr/ndr_misc.h"
#include "librpc/ndr/libndr.h"

struct entryuuid_private {
	struct ldb_dn **base_dns;
};

static struct ldb_val encode_guid(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct GUID guid;
	NTSTATUS status = GUID_from_string((char *)val->data, &guid);
	enum ndr_err_code ndr_err;
	struct ldb_val out = data_blob(NULL, 0);

	if (!NT_STATUS_IS_OK(status)) {
		return out;
	}
	ndr_err = ndr_push_struct_blob(&out, ctx, NULL, &guid,
				       (ndr_push_flags_fn_t)ndr_push_GUID);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		return out;
	}

	return out;
}

static struct ldb_val guid_always_string(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct GUID *guid;
	struct ldb_val out = data_blob(NULL, 0);
	if (val->length >= 32 && val->data[val->length] == '\0') {
		ldb_handler_copy(module->ldb, ctx, val, &out);
	} else {
		enum ndr_err_code ndr_err;

		guid = talloc(ctx, struct GUID);
		if (guid == NULL) {
			return out;
		}
		ndr_err = ndr_pull_struct_blob(val, guid, NULL, guid,
					       (ndr_pull_flags_fn_t)ndr_pull_GUID);
		if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
			talloc_free(guid);
			return out;
		}
		out = data_blob_string_const(GUID_string(ctx, guid));
		talloc_free(guid);
	}
	return out;
}

static struct ldb_val encode_ns_guid(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct GUID guid;
	NTSTATUS status = NS_GUID_from_string((char *)val->data, &guid);
	enum ndr_err_code ndr_err;
	struct ldb_val out = data_blob(NULL, 0);

	if (!NT_STATUS_IS_OK(status)) {
		return out;
	}
	ndr_err = ndr_push_struct_blob(&out, ctx, NULL, &guid,
				       (ndr_push_flags_fn_t)ndr_push_GUID);
	if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
		return out;
	}

	return out;
}

static struct ldb_val guid_ns_string(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out = data_blob(NULL, 0);
	if (val->length >= 32 && val->data[val->length] == '\0') {
		struct GUID guid;
		GUID_from_string((char *)val->data, &guid);
		out = data_blob_string_const(NS_GUID_string(ctx, &guid));
	} else {
		enum ndr_err_code ndr_err;
		struct GUID *guid_p;
		guid_p = talloc(ctx, struct GUID);
		if (guid_p == NULL) {
			return out;
		}
		ndr_err = ndr_pull_struct_blob(val, guid_p, NULL, guid_p,
					       (ndr_pull_flags_fn_t)ndr_pull_GUID);
		if (!NDR_ERR_CODE_IS_SUCCESS(ndr_err)) {
			talloc_free(guid_p);
			return out;
		}
		out = data_blob_string_const(NS_GUID_string(ctx, guid_p));
		talloc_free(guid_p);
	}
	return out;
}

/* The backend holds binary sids, so just copy them back */
static struct ldb_val val_copy(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out = data_blob(NULL, 0);
	ldb_handler_copy(module->ldb, ctx, val, &out);

	return out;
}

/* Ensure we always convert sids into binary, so the backend doesn't have to know about both forms */
static struct ldb_val sid_always_binary(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out = data_blob(NULL, 0);
	const struct ldb_schema_attribute *a = ldb_schema_attribute_by_name(module->ldb, "objectSid");

	if (a->syntax->canonicalise_fn(module->ldb, ctx, val, &out) != LDB_SUCCESS) {
		return data_blob(NULL, 0);
	}

	return out;
}

/* Ensure we always convert objectCategory into a DN */
static struct ldb_val objectCategory_always_dn(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out = data_blob(NULL, 0);
	const struct ldb_schema_attribute *a = ldb_schema_attribute_by_name(module->ldb, "objectCategory");

	if (a->syntax->canonicalise_fn(module->ldb, ctx, val, &out) != LDB_SUCCESS) {
		return data_blob(NULL, 0);
	}

	return out;
}

static struct ldb_val normalise_to_signed32(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	long long int signed_ll = strtoll((const char *)val->data, NULL, 10);
	if (signed_ll >= 0x80000000LL) {
		union {
			int32_t signed_int;
			uint32_t unsigned_int;
		} u = {
			.unsigned_int = strtoul((const char *)val->data, NULL, 10)
		};

		struct ldb_val out = data_blob_string_const(talloc_asprintf(ctx, "%d", u.signed_int));
		return out;
	}
	return val_copy(module, ctx, val);
}

static struct ldb_val usn_to_entryCSN(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out;
	unsigned long long usn = strtoull((const char *)val->data, NULL, 10);
	time_t t = (usn >> 24);
	out = data_blob_string_const(talloc_asprintf(ctx, "%s#%06x#00#000000", ldb_timestring(ctx, t), (unsigned int)(usn & 0xFFFFFF)));
	return out;
}

static unsigned long long entryCSN_to_usn_int(TALLOC_CTX *ctx, const struct ldb_val *val) 
{
	char *entryCSN = talloc_strdup(ctx, (const char *)val->data);
	char *mod_per_sec;
	time_t t;
	unsigned long long usn;
	char *p;
	if (!entryCSN) {
		return 0;
	}
	p = strchr(entryCSN, '#');
	if (!p) {
		return 0;
	}
	p[0] = '\0';
	p++;
	mod_per_sec = p;

	p = strchr(p, '#');
	if (!p) {
		return 0;
	}
	p[0] = '\0';
	p++;

	usn = strtol(mod_per_sec, NULL, 16);

	t = ldb_string_to_time(entryCSN);
	
	usn = usn | ((unsigned long long)t <<24);
	return usn;
}

static struct ldb_val entryCSN_to_usn(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out;
	unsigned long long usn = entryCSN_to_usn_int(ctx, val);
	out = data_blob_string_const(talloc_asprintf(ctx, "%lld", usn));
	return out;
}

static struct ldb_val usn_to_timestamp(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out;
	unsigned long long usn = strtoull((const char *)val->data, NULL, 10);
	time_t t = (usn >> 24);
	out = data_blob_string_const(ldb_timestring(ctx, t));
	return out;
}

static struct ldb_val timestamp_to_usn(struct ldb_module *module, TALLOC_CTX *ctx, const struct ldb_val *val)
{
	struct ldb_val out;
	time_t t;
	unsigned long long usn;

	t = ldb_string_to_time((const char *)val->data);
	
	usn = ((unsigned long long)t <<24);

	out = data_blob_string_const(talloc_asprintf(ctx, "%lld", usn));
	return out;
}


static const struct ldb_map_attribute entryuuid_attributes[] = 
{
	/* objectGUID */
	{
		.local_name = "objectGUID",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				.remote_name = "entryUUID", 
				.convert_local = guid_always_string,
				.convert_remote = encode_guid,
			},
		},
	},
	/* invocationId */
	{
		.local_name = "invocationId",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				.remote_name = "invocationId", 
				.convert_local = guid_always_string,
				.convert_remote = encode_guid,
			},
		},
	},
	/* objectSid */
	{
		.local_name = "objectSid",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				.remote_name = "objectSid", 
				.convert_local = sid_always_binary,
				.convert_remote = val_copy,
			},
		},
	},
	{
		.local_name = "name",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "samba4RDN"
			 }
		}
	},
	{
		.local_name = "whenCreated",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "createTimestamp"
			 }
		}
	},
	{
		.local_name = "whenChanged",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "modifyTimestamp"
			 }
		}
	},
	{
		.local_name = "objectClasses",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "samba4ObjectClasses"
			 }
		}
	},
	{
		.local_name = "dITContentRules",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "samba4DITContentRules"
			 }
		}
	},
	{
		.local_name = "attributeTypes",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "samba4AttributeTypes"
			 }
		}
	},
	{
		.local_name = "sambaPassword",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "userPassword"
			 }
		}
	},
	{
		.local_name = "objectCategory",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				.remote_name = "objectCategory", 
				.convert_local = objectCategory_always_dn,
				.convert_remote = val_copy,
			},
		},
	},
	{
		.local_name = "distinguishedName",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "entryDN"
			 }
		}
	},
	{
		.local_name = "groupType",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "groupType",
				 .convert_local = normalise_to_signed32,
				 .convert_remote = val_copy,
			 },
		}
	},
	{
		.local_name = "sAMAccountType",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "sAMAccountType",
				 .convert_local = normalise_to_signed32,
				 .convert_remote = val_copy,
			 },
		}
	},
	{
		.local_name = "usnChanged",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "entryCSN",
				 .convert_local = usn_to_entryCSN,
				 .convert_remote = entryCSN_to_usn
			 },
		},
	},
	{
		.local_name = "usnCreated",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "createTimestamp",
				 .convert_local = usn_to_timestamp,
				 .convert_remote = timestamp_to_usn,
			 },
		},
	},
	{
		.local_name = "*",
		.type = MAP_KEEP,
	},
	{
		.local_name = NULL,
	}
};

/* This objectClass conflicts with builtin classes on OpenLDAP */
const struct ldb_map_objectclass entryuuid_objectclasses[] =
{
	{
		.local_name = "subSchema",
		.remote_name = "samba4SubSchema"
	},
	{
		.local_name = NULL
	}
};

/* These things do not show up in wildcard searches in OpenLDAP, but
 * we need them to show up in the AD-like view */
static const char * const entryuuid_wildcard_attributes[] = {
	"objectGUID", 
	"whenCreated", 
	"whenChanged",
	"usnCreated",
	"usnChanged",
	"memberOf",
	NULL
};

static const struct ldb_map_attribute nsuniqueid_attributes[] = 
{
	/* objectGUID */
	{
		.local_name = "objectGUID",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				.remote_name = "nsuniqueid", 
				.convert_local = guid_ns_string,
				.convert_remote = encode_ns_guid,
			},
		},
	},
	/* objectSid */	
	{
		.local_name = "objectSid",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				.remote_name = "objectSid", 
				.convert_local = sid_always_binary,
				.convert_remote = val_copy,
			},
		},
	},
	{
		.local_name = "whenCreated",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "createTimestamp"
			 }
		}
	},
	{
		.local_name = "whenChanged",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "modifyTimestamp"
			 }
		}
	},
	{
		.local_name = "sambaPassword",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "userPassword"
			 }
		}
	},
	{
		.local_name = "objectCategory",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				.remote_name = "objectCategory", 
				.convert_local = objectCategory_always_dn,
				.convert_remote = val_copy,
			},
		},
	},
	{
		.local_name = "distinguishedName",
		.type = MAP_RENAME,
		.u = {
			.rename = {
				 .remote_name = "entryDN"
			 }
		}
	},
	{
		.local_name = "groupType",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "groupType",
				 .convert_local = normalise_to_signed32,
				 .convert_remote = val_copy,
			 },
		}
	},
	{
		.local_name = "sAMAccountType",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "sAMAccountType",
				 .convert_local = normalise_to_signed32,
				 .convert_remote = val_copy,
			 },
		}
	},
	{
		.local_name = "usnChanged",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "modifyTimestamp",
				 .convert_local = usn_to_timestamp,
				 .convert_remote = timestamp_to_usn,
			 },
		},
	},
	{
		.local_name = "usnCreated",
		.type = MAP_CONVERT,
		.u = {
			.convert = {
				 .remote_name = "createTimestamp",
				 .convert_local = usn_to_timestamp,
				 .convert_remote = timestamp_to_usn,
			 },
		},
	},
	{
		.local_name = "*",
		.type = MAP_KEEP,
	},
	{
		.local_name = NULL,
	}
};

/* These things do not show up in wildcard searches in OpenLDAP, but
 * we need them to show up in the AD-like view */
static const char * const nsuniqueid_wildcard_attributes[] = {
	"objectGUID", 
	"whenCreated", 
	"whenChanged",
	"usnCreated",
	"usnChanged",
	NULL
};

static int get_remote_rootdse(struct ldb_context *ldb, void *context, 
		       struct ldb_reply *ares) 
{
	struct entryuuid_private *entryuuid_private;
	entryuuid_private = talloc_get_type(context,
					    struct entryuuid_private);
	if (ares->type == LDB_REPLY_ENTRY) {
		int i;
		struct ldb_message_element *el = ldb_msg_find_element(ares->message, "namingContexts");
		entryuuid_private->base_dns = talloc_realloc(entryuuid_private, entryuuid_private->base_dns, struct ldb_dn *, 
							     el->num_values + 1);
		for (i=0; i < el->num_values; i++) {
			if (!entryuuid_private->base_dns) {
				return LDB_ERR_OPERATIONS_ERROR;
			}
			entryuuid_private->base_dns[i] = ldb_dn_new(entryuuid_private->base_dns, ldb, (const char *)el->values[i].data);
			if ( ! ldb_dn_validate(entryuuid_private->base_dns[i])) {
				return LDB_ERR_OPERATIONS_ERROR;
			}
		}
		entryuuid_private->base_dns[i] = NULL;
	}

	return LDB_SUCCESS;
}

static int find_base_dns(struct ldb_module *module, 
			  struct entryuuid_private *entryuuid_private) 
{
	int ret;
	struct ldb_request *req;
	const char *naming_context_attr[] = {
		"namingContexts",
		NULL
	};
	req = talloc(entryuuid_private, struct ldb_request);
	if (req == NULL) {
		ldb_set_errstring(module->ldb, "Out of Memory");
		return LDB_ERR_OPERATIONS_ERROR;
	}

	req->operation = LDB_SEARCH;
	req->op.search.base = ldb_dn_new(req, module->ldb, NULL);
	req->op.search.scope = LDB_SCOPE_BASE;

	req->op.search.tree = ldb_parse_tree(req, "objectClass=*");
	if (req->op.search.tree == NULL) {
		ldb_set_errstring(module->ldb, "Unable to parse search expression");
		talloc_free(req);
		return LDB_ERR_OPERATIONS_ERROR;
	}

	req->op.search.attrs = naming_context_attr;
	req->controls = NULL;
	req->context = entryuuid_private;
	req->callback = get_remote_rootdse;
	ldb_set_timeout(module->ldb, req, 0); /* use default timeout */

	ret = ldb_next_request(module, req);
	
	if (ret == LDB_SUCCESS) {
		ret = ldb_wait(req->handle, LDB_WAIT_ALL);
	}
	
	talloc_free(req);
	if (ret != LDB_SUCCESS) {
		return ret;
	}

	return LDB_SUCCESS;
}

/* the context init function */
static int entryuuid_init(struct ldb_module *module)
{
        int ret;
	struct map_private *map_private;
	struct entryuuid_private *entryuuid_private;

	ret = ldb_map_init(module, entryuuid_attributes, entryuuid_objectclasses, entryuuid_wildcard_attributes, "extensibleObject", NULL);
        if (ret != LDB_SUCCESS)
                return ret;

	map_private = talloc_get_type(module->private_data, struct map_private);

	entryuuid_private = talloc_zero(map_private, struct entryuuid_private);
	map_private->caller_private = entryuuid_private;

	ret = find_base_dns(module, entryuuid_private);

	return ldb_next_init(module);
}

/* the context init function */
static int nsuniqueid_init(struct ldb_module *module)
{
        int ret;
	struct map_private *map_private;
	struct entryuuid_private *entryuuid_private;

	ret = ldb_map_init(module, nsuniqueid_attributes, NULL, nsuniqueid_wildcard_attributes, "extensibleObject", NULL);
        if (ret != LDB_SUCCESS)
                return ret;

	map_private = talloc_get_type(module->private_data, struct map_private);

	entryuuid_private = talloc_zero(map_private, struct entryuuid_private);
	map_private->caller_private = entryuuid_private;

	ret = find_base_dns(module, entryuuid_private);

	return ldb_next_init(module);
}

static int get_seq(struct ldb_context *ldb, void *context, 
		   struct ldb_reply *ares) 
{
	unsigned long long *max_seq = (unsigned long long *)context;
	unsigned long long seq;
	if (ares->type == LDB_REPLY_ENTRY) {
		struct ldb_message_element *el = ldb_msg_find_element(ares->message, "contextCSN");
		if (el) {
			seq = entryCSN_to_usn_int(ares, &el->values[0]);
			*max_seq = MAX(seq, *max_seq);
		}
	}

	return LDB_SUCCESS;
}

static int entryuuid_sequence_number(struct ldb_module *module, struct ldb_request *req)
{
	int i, ret;
	struct map_private *map_private;
	struct entryuuid_private *entryuuid_private;
	unsigned long long max_seq = 0;
	struct ldb_request *search_req;
	map_private = talloc_get_type(module->private_data, struct map_private);

	entryuuid_private = talloc_get_type(map_private->caller_private, struct entryuuid_private);

	/* Search the baseDNs for a sequence number */
	for (i=0; entryuuid_private && 
		     entryuuid_private->base_dns && 
		     entryuuid_private->base_dns[i];
		i++) {
		static const char *contextCSN_attr[] = {
			"contextCSN", NULL
		};
		search_req = talloc(req, struct ldb_request);
		if (search_req == NULL) {
			ldb_set_errstring(module->ldb, "Out of Memory");
			return LDB_ERR_OPERATIONS_ERROR;
		}
		
		search_req->operation = LDB_SEARCH;
		search_req->op.search.base = entryuuid_private->base_dns[i];
		search_req->op.search.scope = LDB_SCOPE_BASE;
		
		search_req->op.search.tree = ldb_parse_tree(search_req, "objectClass=*");
		if (search_req->op.search.tree == NULL) {
			ldb_set_errstring(module->ldb, "Unable to parse search expression");
			talloc_free(search_req);
			return LDB_ERR_OPERATIONS_ERROR;
		}
		
		search_req->op.search.attrs = contextCSN_attr;
		search_req->controls = NULL;
		search_req->context = &max_seq;
		search_req->callback = get_seq;
		ldb_set_timeout(module->ldb, search_req, 0); /* use default timeout */
		
		ret = ldb_next_request(module, search_req);
		
		if (ret == LDB_SUCCESS) {
			ret = ldb_wait(search_req->handle, LDB_WAIT_ALL);
		}
		
		talloc_free(search_req);
		if (ret != LDB_SUCCESS) {
			return ret;
		}
	}

	switch (req->op.seq_num.type) {
	case LDB_SEQ_HIGHEST_SEQ:
		req->op.seq_num.seq_num = max_seq;
		break;
	case LDB_SEQ_NEXT:
		req->op.seq_num.seq_num = max_seq;
		req->op.seq_num.seq_num++;
		break;
	case LDB_SEQ_HIGHEST_TIMESTAMP:
	{
		req->op.seq_num.seq_num = (max_seq >> 24);
		break;
	}
	}
	req->op.seq_num.flags = 0;
	req->op.seq_num.flags |= LDB_SEQ_TIMESTAMP_SEQUENCE;
	req->op.seq_num.flags |= LDB_SEQ_GLOBAL_SEQUENCE;
	return LDB_SUCCESS;
}

_PUBLIC_ const struct ldb_module_ops ldb_entryuuid_module_ops = {
	.name		   = "entryuuid",
	.init_context	   = entryuuid_init,
	.sequence_number   = entryuuid_sequence_number,
	LDB_MAP_OPS
};

_PUBLIC_ const struct ldb_module_ops ldb_nsuniqueid_module_ops = {
	.name		   = "nsuniqueid",
	.init_context	   = nsuniqueid_init,
	.sequence_number   = entryuuid_sequence_number,
	LDB_MAP_OPS
};
