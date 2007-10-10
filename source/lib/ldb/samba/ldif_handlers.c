/* 
   ldb database library - ldif handlers for Samba

   Copyright (C) Andrew Tridgell  2005

     ** NOTE! The following LGPL license applies to the ldb
     ** library. This does NOT imply that all of Samba is released
     ** under the LGPL
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "includes.h"
#include "ldb/include/ldb.h"
#include "ldb/include/ldb_private.h"
#include "librpc/gen_ndr/ndr_security.h"

/*
  convert a ldif formatted objectSid to a NDR formatted blob
*/
static int ldif_read_objectSid(struct ldb_context *ldb, void *mem_ctx,
			       const struct ldb_val *in, struct ldb_val *out)
{
	struct dom_sid *sid;
	NTSTATUS status;
	sid = dom_sid_parse_talloc(mem_ctx, (const char *)in->data);
	if (sid == NULL) {
		return -1;
	}
	status = ndr_push_struct_blob(out, mem_ctx, sid, 
				      (ndr_push_flags_fn_t)ndr_push_dom_sid);
	talloc_free(sid);
	if (!NT_STATUS_IS_OK(status)) {
		return -1;
	}
	return 0;
}

/*
  convert a NDR formatted blob to a ldif formatted objectSid
*/
static int ldif_write_objectSid(struct ldb_context *ldb, void *mem_ctx,
				const struct ldb_val *in, struct ldb_val *out)
{
	struct dom_sid *sid;
	NTSTATUS status;
	sid = talloc(mem_ctx, struct dom_sid);
	if (sid == NULL) {
		return -1;
	}
	status = ndr_pull_struct_blob(in, sid, sid, 
				      (ndr_pull_flags_fn_t)ndr_pull_dom_sid);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(sid);
		return -1;
	}
	out->data = dom_sid_string(mem_ctx, sid);
	talloc_free(sid);
	if (out->data == NULL) {
		return -1;
	}
	out->length = strlen(out->data);
	return 0;
}

static BOOL ldb_comparision_objectSid_isString(const struct ldb_val *v)
{
	/* see if the input if null-terninated */
	if (v->data[v->length] != '\0') return False;
	
	if (strncmp("S-", v->data, 2) != 0) return False;
	return True;
}

/*
  compare two objectSids
*/
static int ldb_comparison_objectSid(struct ldb_context *ldb, void *mem_ctx,
				    const struct ldb_val *v1, const struct ldb_val *v2)
{
	if (ldb_comparision_objectSid_isString(v1)) {
		if (ldb_comparision_objectSid_isString(v1)) {
			return strcmp(v1->data, v2->data);
		} else {
			struct ldb_val v;
			int ret;
			if (ldif_read_objectSid(ldb, mem_ctx, v1, &v) != 0) {
				return -1;
			}
			ret = ldb_comparison_binary(ldb, mem_ctx, &v, v2);
			talloc_free(v.data);
			return ret;
		}
	}
	return ldb_comparison_binary(ldb, mem_ctx, v1, v2);
}

/*
  canonicalise a objectSid
*/
static int ldb_canonicalise_objectSid(struct ldb_context *ldb, void *mem_ctx,
				      const struct ldb_val *in, struct ldb_val *out)
{
	if (ldb_comparision_objectSid_isString(in)) {
		return ldif_read_objectSid(ldb, mem_ctx, in, out);
	}
	return ldb_handler_copy(ldb, mem_ctx, in, out);
}

/*
  convert a ldif formatted objectGUID to a NDR formatted blob
*/
static int ldif_read_objectGUID(struct ldb_context *ldb, void *mem_ctx,
			        const struct ldb_val *in, struct ldb_val *out)
{
	struct GUID guid;
	NTSTATUS status;

	status = GUID_from_string(in->data, &guid);
	if (!NT_STATUS_IS_OK(status)) {
		return -1;
	}

	status = ndr_push_struct_blob(out, mem_ctx, &guid,
				      (ndr_push_flags_fn_t)ndr_push_GUID);
	if (!NT_STATUS_IS_OK(status)) {
		return -1;
	}
	return 0;
}

/*
  convert a NDR formatted blob to a ldif formatted objectGUID
*/
static int ldif_write_objectGUID(struct ldb_context *ldb, void *mem_ctx,
				 const struct ldb_val *in, struct ldb_val *out)
{
	struct GUID guid;
	NTSTATUS status;
	status = ndr_pull_struct_blob(in, mem_ctx, &guid,
				      (ndr_pull_flags_fn_t)ndr_pull_GUID);
	if (!NT_STATUS_IS_OK(status)) {
		return -1;
	}
	out->data = GUID_string(mem_ctx, &guid);
	if (out->data == NULL) {
		return -1;
	}
	out->length = strlen(out->data);
	return 0;
}

static BOOL ldb_comparision_objectGUID_isString(const struct ldb_val *v)
{
	struct GUID guid;
	NTSTATUS status;

	/* see if the input if null-terninated */
	if (v->data[v->length] != '\0') return False;

	status = GUID_from_string(v->data, &guid);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	return True;
}

/*
  compare two objectGUIDs
*/
static int ldb_comparison_objectGUID(struct ldb_context *ldb, void *mem_ctx,
				     const struct ldb_val *v1, const struct ldb_val *v2)
{
	if (ldb_comparision_objectGUID_isString(v1)) {
		if (ldb_comparision_objectGUID_isString(v2)) {
			return strcmp(v1->data, v2->data);
		} else {
			struct ldb_val v;
			int ret;
			if (ldif_read_objectGUID(ldb, mem_ctx, v1, &v) != 0) {
				return -1;
			}
			ret = ldb_comparison_binary(ldb, mem_ctx, &v, v2);
			talloc_free(v.data);
			return ret;
		}
	}
	return ldb_comparison_binary(ldb, mem_ctx, v1, v2);
}

/*
  canonicalise a objectGUID
*/
static int ldb_canonicalise_objectGUID(struct ldb_context *ldb, void *mem_ctx,
				       const struct ldb_val *in, struct ldb_val *out)
{
	if (ldb_comparision_objectGUID_isString(in)) {
		return ldif_read_objectGUID(ldb, mem_ctx, in, out);
	}
	return ldb_handler_copy(ldb, mem_ctx, in, out);
}

static const struct ldb_attrib_handler samba_handlers[] = {
	{ 
		.attr            = "objectSid",
		.flags           = 0,
		.ldif_read_fn    = ldif_read_objectSid,
		.ldif_write_fn   = ldif_write_objectSid,
		.canonicalise_fn = ldb_canonicalise_objectSid,
		.comparison_fn   = ldb_comparison_objectSid
	},
	{ 
		.attr            = "securityIdentifier",
		.flags           = 0,
		.ldif_read_fn    = ldif_read_objectSid,
		.ldif_write_fn   = ldif_write_objectSid,
		.canonicalise_fn = ldb_canonicalise_objectSid,
		.comparison_fn   = ldb_comparison_objectSid
	},
	{ 
		.attr            = "objectGUID",
		.flags           = 0,
		.ldif_read_fn    = ldif_read_objectGUID,
		.ldif_write_fn   = ldif_write_objectGUID,
		.canonicalise_fn = ldb_canonicalise_objectGUID,
		.comparison_fn   = ldb_comparison_objectGUID
	},
	{ 
		.attr            = "invocationId",
		.flags           = 0,
		.ldif_read_fn    = ldif_read_objectGUID,
		.ldif_write_fn   = ldif_write_objectGUID,
		.canonicalise_fn = ldb_canonicalise_objectGUID,
		.comparison_fn   = ldb_comparison_objectGUID
	}
};

/*
  register the samba ldif handlers
*/
int ldb_register_samba_handlers(struct ldb_context *ldb)
{
	return ldb_set_attrib_handlers(ldb, samba_handlers, ARRAY_SIZE(samba_handlers));
}
