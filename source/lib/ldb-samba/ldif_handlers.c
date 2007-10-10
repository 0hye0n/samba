/* 
   ldb database library - ldif handlers for Samba

   Copyright (C) Andrew Tridgell 2005
   Copyright (C) Andrew Bartlett 2006-2007
     ** NOTE! The following LGPL license applies to the ldb
     ** library. This does NOT imply that all of Samba is released
     ** under the LGPL
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "ldb_includes.h"
#include "ldb_handlers.h"

#include "librpc/gen_ndr/ndr_security.h"
#include "librpc/gen_ndr/ndr_misc.h"
#include "dsdb/samdb/samdb.h"
#include "libcli/security/security.h"

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
	out->data = (uint8_t *)dom_sid_string(mem_ctx, sid);
	talloc_free(sid);
	if (out->data == NULL) {
		return -1;
	}
	out->length = strlen((const char *)out->data);
	return 0;
}

static bool ldb_comparision_objectSid_isString(const struct ldb_val *v)
{
	if (v->length < 3) {
		return false;
	}

	if (strncmp("S-", (const char *)v->data, 2) != 0) return false;
	
	return true;
}

/*
  compare two objectSids
*/
static int ldb_comparison_objectSid(struct ldb_context *ldb, void *mem_ctx,
				    const struct ldb_val *v1, const struct ldb_val *v2)
{
	if (ldb_comparision_objectSid_isString(v1) && ldb_comparision_objectSid_isString(v2)) {
		return strcmp((const char *)v1->data, (const char *)v2->data);
	} else if (ldb_comparision_objectSid_isString(v1)
		   && !ldb_comparision_objectSid_isString(v2)) {
		struct ldb_val v;
		int ret;
		if (ldif_read_objectSid(ldb, mem_ctx, v1, &v) != 0) {
			return -1;
		}
		ret = ldb_comparison_binary(ldb, mem_ctx, &v, v2);
		talloc_free(v.data);
		return ret;
	} else if (!ldb_comparision_objectSid_isString(v1)
		   && ldb_comparision_objectSid_isString(v2)) {
		struct ldb_val v;
		int ret;
		if (ldif_read_objectSid(ldb, mem_ctx, v2, &v) != 0) {
			return -1;
		}
		ret = ldb_comparison_binary(ldb, mem_ctx, v1, &v);
		talloc_free(v.data);
		return ret;
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

	status = GUID_from_string((const char *)in->data, &guid);
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
	out->data = (uint8_t *)GUID_string(mem_ctx, &guid);
	if (out->data == NULL) {
		return -1;
	}
	out->length = strlen((const char *)out->data);
	return 0;
}

static bool ldb_comparision_objectGUID_isString(const struct ldb_val *v)
{
	struct GUID guid;
	NTSTATUS status;

	if (v->length < 33) return false;

	/* see if the input if null-terninated (safety check for the below) */
	if (v->data[v->length] != '\0') return false;

	status = GUID_from_string((const char *)v->data, &guid);
	if (!NT_STATUS_IS_OK(status)) {
		return false;
	}

	return true;
}

/*
  compare two objectGUIDs
*/
static int ldb_comparison_objectGUID(struct ldb_context *ldb, void *mem_ctx,
				     const struct ldb_val *v1, const struct ldb_val *v2)
{
	if (ldb_comparision_objectGUID_isString(v1) && ldb_comparision_objectGUID_isString(v2)) {
		return strcmp((const char *)v1->data, (const char *)v2->data);
	} else if (ldb_comparision_objectGUID_isString(v1)
		   && !ldb_comparision_objectGUID_isString(v2)) {
		struct ldb_val v;
		int ret;
		if (ldif_read_objectGUID(ldb, mem_ctx, v1, &v) != 0) {
			return -1;
		}
		ret = ldb_comparison_binary(ldb, mem_ctx, &v, v2);
		talloc_free(v.data);
		return ret;
	} else if (!ldb_comparision_objectGUID_isString(v1)
		   && ldb_comparision_objectGUID_isString(v2)) {
		struct ldb_val v;
		int ret;
		if (ldif_read_objectGUID(ldb, mem_ctx, v2, &v) != 0) {
			return -1;
		}
		ret = ldb_comparison_binary(ldb, mem_ctx, v1, &v);
		talloc_free(v.data);
		return ret;
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


/*
  convert a ldif (SDDL) formatted ntSecurityDescriptor to a NDR formatted blob
*/
static int ldif_read_ntSecurityDescriptor(struct ldb_context *ldb, void *mem_ctx,
					  const struct ldb_val *in, struct ldb_val *out)
{
	struct security_descriptor *sd;
	NTSTATUS status;

	sd = sddl_decode(mem_ctx, (const char *)in->data, NULL);
	if (sd == NULL) {
		return -1;
	}
	status = ndr_push_struct_blob(out, mem_ctx, sd, 
				      (ndr_push_flags_fn_t)ndr_push_security_descriptor);
	talloc_free(sd);
	if (!NT_STATUS_IS_OK(status)) {
		return -1;
	}
	return 0;
}

/*
  convert a NDR formatted blob to a ldif formatted ntSecurityDescriptor (SDDL format)
*/
static int ldif_write_ntSecurityDescriptor(struct ldb_context *ldb, void *mem_ctx,
					   const struct ldb_val *in, struct ldb_val *out)
{
	struct security_descriptor *sd;
	NTSTATUS status;

	sd = talloc(mem_ctx, struct security_descriptor);
	if (sd == NULL) {
		return -1;
	}
	status = ndr_pull_struct_blob(in, sd, sd, 
				      (ndr_pull_flags_fn_t)ndr_pull_security_descriptor);
	if (!NT_STATUS_IS_OK(status)) {
		talloc_free(sd);
		return -1;
	}
	out->data = (uint8_t *)sddl_encode(mem_ctx, sd, NULL);
	talloc_free(sd);
	if (out->data == NULL) {
		return -1;
	}
	out->length = strlen((const char *)out->data);
	return 0;
}

/* 
   canonicolise an objectCategory.  We use the short form as the cannoical form:
   cn=Person,cn=Schema,cn=Configuration,<basedn> becomes 'person'
*/

static int ldif_canonicalise_objectCategory(struct ldb_context *ldb, void *mem_ctx,
					    const struct ldb_val *in, struct ldb_val *out)
{
	struct ldb_dn *dn1 = NULL;
	const struct dsdb_schema *schema = dsdb_get_schema(ldb);
	const struct dsdb_class *class;
	TALLOC_CTX *tmp_ctx = talloc_new(mem_ctx);
	if (!tmp_ctx) {
		return LDB_ERR_OPERATIONS_ERROR;
	}

	if (!schema) {
		*out = data_blob_talloc(mem_ctx, in->data, in->length);
		if (in->data && !out->data) {
			return LDB_ERR_OPERATIONS_ERROR;
		}
		return LDB_SUCCESS;
	}
	dn1 = ldb_dn_new(tmp_ctx, ldb, (char *)in->data);
	if ( ! ldb_dn_validate(dn1)) {
		const char *lDAPDisplayName = talloc_strndup(tmp_ctx, (char *)in->data, in->length);
		class = dsdb_class_by_lDAPDisplayName(schema, lDAPDisplayName);
		if (class) {
			struct ldb_dn *dn = ldb_dn_new(mem_ctx, ldb,  
						       class->defaultObjectCategory);
			*out = data_blob_string_const(ldb_dn_alloc_casefold(mem_ctx, dn));
			talloc_free(tmp_ctx);

			if (!out->data) {
				return LDB_ERR_OPERATIONS_ERROR;
			}
			return LDB_SUCCESS;
		} else {
			*out = data_blob_talloc(mem_ctx, in->data, in->length);
			talloc_free(tmp_ctx);

			if (in->data && !out->data) {
				return LDB_ERR_OPERATIONS_ERROR;
			}
			return LDB_SUCCESS;
		}
	}
	*out = data_blob_string_const(ldb_dn_alloc_casefold(mem_ctx, dn1));
	talloc_free(tmp_ctx);

	if (!out->data) {
		return LDB_ERR_OPERATIONS_ERROR;
	}
	return LDB_SUCCESS;
}

static int ldif_comparison_objectCategory(struct ldb_context *ldb, void *mem_ctx,
					  const struct ldb_val *v1,
					  const struct ldb_val *v2)
{

	int ret, ret1, ret2;
	struct ldb_val v1_canon, v2_canon;
	TALLOC_CTX *tmp_ctx = talloc_new(mem_ctx);

	/* I could try and bail if tmp_ctx was NULL, but what return
	 * value would I use?
	 *
	 * It seems easier to continue on the NULL context 
	 */
	ret1 = ldif_canonicalise_objectCategory(ldb, tmp_ctx, v1, &v1_canon);
	ret2 = ldif_canonicalise_objectCategory(ldb, tmp_ctx, v2, &v2_canon);

	if (ret1 == LDB_SUCCESS && ret2 == LDB_SUCCESS) {
		ret = data_blob_cmp(&v1_canon, &v2_canon);
	} else {
		ret = data_blob_cmp(v1, v2);
	}
	talloc_free(tmp_ctx);
	return ret;
}

#define LDB_SYNTAX_SAMBA_SID			"LDB_SYNTAX_SAMBA_SID"
#define LDB_SYNTAX_SAMBA_SECURITY_DESCRIPTOR	"LDB_SYNTAX_SAMBA_SECURITY_DESCRIPTOR"
#define LDB_SYNTAX_SAMBA_GUID			"LDB_SYNTAX_SAMBA_GUID"
#define LDB_SYNTAX_SAMBA_OBJECT_CATEGORY	"LDB_SYNTAX_SAMBA_OBJECT_CATEGORY"

static const struct ldb_schema_syntax samba_syntaxes[] = {
	{
		.name		= LDB_SYNTAX_SAMBA_SID,
		.ldif_read_fn	= ldif_read_objectSid,
		.ldif_write_fn	= ldif_write_objectSid,
		.canonicalise_fn= ldb_canonicalise_objectSid,
		.comparison_fn	= ldb_comparison_objectSid
	},{
		.name		= LDB_SYNTAX_SAMBA_SECURITY_DESCRIPTOR,
		.ldif_read_fn	= ldif_read_ntSecurityDescriptor,
		.ldif_write_fn	= ldif_write_ntSecurityDescriptor,
		.canonicalise_fn= ldb_handler_copy,
		.comparison_fn	= ldb_comparison_binary
	},{
		.name		= LDB_SYNTAX_SAMBA_GUID,
		.ldif_read_fn	= ldif_read_objectGUID,
		.ldif_write_fn	= ldif_write_objectGUID,
		.canonicalise_fn= ldb_canonicalise_objectGUID,
		.comparison_fn	= ldb_comparison_objectGUID
	},{
		.name		= LDB_SYNTAX_SAMBA_OBJECT_CATEGORY,
		.ldif_read_fn	= ldb_handler_copy,
		.ldif_write_fn	= ldb_handler_copy,
		.canonicalise_fn= ldif_canonicalise_objectCategory,
		.comparison_fn	= ldif_comparison_objectCategory
	}
};

static const struct {
	const char *name;
	const char *syntax;
} samba_attributes[] = {
	{ "objectSid",			LDB_SYNTAX_SAMBA_SID },
	{ "securityIdentifier", 	LDB_SYNTAX_SAMBA_SID },
	{ "ntSecurityDescriptor",	LDB_SYNTAX_SAMBA_SECURITY_DESCRIPTOR },
	{ "objectGUID",			LDB_SYNTAX_SAMBA_GUID },
	{ "invocationId",		LDB_SYNTAX_SAMBA_GUID },
	{ "schemaIDGUID",		LDB_SYNTAX_SAMBA_GUID },
	{ "attributeSecurityGUID",	LDB_SYNTAX_SAMBA_GUID },
	{ "parentGUID",			LDB_SYNTAX_SAMBA_GUID },
	{ "siteGUID",			LDB_SYNTAX_SAMBA_GUID },
	{ "pKTGUID",			LDB_SYNTAX_SAMBA_GUID },
	{ "fRSVersionGUID",		LDB_SYNTAX_SAMBA_GUID },
	{ "fRSReplicaSetGUID",		LDB_SYNTAX_SAMBA_GUID },
	{ "netbootGUID",		LDB_SYNTAX_SAMBA_GUID },
	{ "objectCategory",		LDB_SYNTAX_SAMBA_OBJECT_CATEGORY },
	{ "member",			LDB_SYNTAX_DN },
	{ "memberOf",			LDB_SYNTAX_DN },
	{ "nCName",			LDB_SYNTAX_DN },
	{ "schemaNamingContext",	LDB_SYNTAX_DN },
	{ "configurationNamingContext",	LDB_SYNTAX_DN },
	{ "rootDomainNamingContext",	LDB_SYNTAX_DN },
	{ "defaultNamingContext",	LDB_SYNTAX_DN },
	{ "subRefs",			LDB_SYNTAX_DN },
	{ "dMDLocation",		LDB_SYNTAX_DN },
	{ "serverReference",		LDB_SYNTAX_DN },
	{ "masteredBy",			LDB_SYNTAX_DN },
	{ "msDs-masteredBy",		LDB_SYNTAX_DN },
	{ "fSMORoleOwner",		LDB_SYNTAX_DN },
};

/*
  register the samba ldif handlers
*/
int ldb_register_samba_handlers(struct ldb_context *ldb)
{
	uint32_t i;

	for (i=0; i < ARRAY_SIZE(samba_attributes); i++) {
		int ret;
		uint32_t j;
		const struct ldb_schema_syntax *s = NULL;

		for (j=0; j < ARRAY_SIZE(samba_syntaxes); j++) {
			if (strcmp(samba_attributes[i].syntax, samba_syntaxes[j].name) == 0) {
				s = &samba_syntaxes[j];
				break;
			}
		}

		if (!s) {
			s = ldb_standard_syntax_by_name(ldb, samba_attributes[i].syntax);
		}

		if (!s) {
			return -1;
		}

		ret = ldb_schema_attribute_add_with_syntax(ldb, samba_attributes[i].name, 0, s);
		if (ret != LDB_SUCCESS) {
			return ret;
		}
	}

	return LDB_SUCCESS;
}
