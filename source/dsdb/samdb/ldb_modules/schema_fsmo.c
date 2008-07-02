/* 
   Unix SMB/CIFS mplementation.

   The module that handles the Schema FSMO Role Owner
   checkings, it also loads the dsdb_schema.
   
   Copyright (C) Stefan Metzmacher <metze@samba.org> 2007
    
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
#include "lib/ldb/include/ldb.h"
#include "lib/ldb/include/ldb_errors.h"
#include "lib/ldb/include/ldb_private.h"
#include "dsdb/samdb/samdb.h"
#include "librpc/gen_ndr/ndr_misc.h"
#include "librpc/gen_ndr/ndr_drsuapi.h"
#include "librpc/gen_ndr/ndr_drsblobs.h"
#include "lib/util/dlinklist.h"
#include "param/param.h"

static int schema_fsmo_init(struct ldb_module *module)
{
	TALLOC_CTX *mem_ctx;
	struct ldb_dn *schema_dn;
	struct dsdb_schema *schema;
	struct ldb_result *schema_res;
	struct ldb_result *a_res;
	struct ldb_result *c_res;
	char *error_string = NULL;
	int ret;
	static const char *schema_attrs[] = {
		"prefixMap",
		"schemaInfo",
		"fSMORoleOwner",
		NULL
	};

	if (dsdb_get_schema(module->ldb)) {
		return ldb_next_init(module);
	}

	schema_dn = samdb_schema_dn(module->ldb);
	if (!schema_dn) {
		ldb_reset_err_string(module->ldb);
		ldb_debug(module->ldb, LDB_DEBUG_WARNING,
			  "schema_fsmo_init: no schema dn present: (skip schema loading)\n");
		return ldb_next_init(module);
	}

	mem_ctx = talloc_new(module);
	if (!mem_ctx) {
		ldb_oom(module->ldb);
		return LDB_ERR_OPERATIONS_ERROR;
	}

	/*
	 * setup the prefix mappings and schema info
	 */
	ret = ldb_search(module->ldb, schema_dn,
			 LDB_SCOPE_BASE,
			 NULL, schema_attrs,
			 &schema_res);
	if (ret == LDB_ERR_NO_SUCH_OBJECT) {
		ldb_reset_err_string(module->ldb);
		ldb_debug(module->ldb, LDB_DEBUG_WARNING,
			  "schema_fsmo_init: no schema head present: (skip schema loading)\n");
		talloc_free(mem_ctx);
		return ldb_next_init(module);
	} else if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(module->ldb, 
				       "schema_fsmo_init: failed to search the schema head: %s",
				       ldb_errstring(module->ldb));
		talloc_free(mem_ctx);
		return ret;
	}
	talloc_steal(mem_ctx, schema_res);
	if (schema_res->count == 0) {
		ldb_debug(module->ldb, LDB_DEBUG_WARNING,
			  "schema_fsmo_init: no schema head present: (skip schema loading)\n");
		talloc_free(mem_ctx);
		return ldb_next_init(module);
	} else if (schema_res->count > 1) {
		ldb_debug_set(module->ldb, LDB_DEBUG_FATAL,
			      "schema_fsmo_init: [%u] schema heads found on a base search",
			      schema_res->count);
		talloc_free(mem_ctx);
		return LDB_ERR_CONSTRAINT_VIOLATION;
	}

	/*
	 * load the attribute definitions
	 */
	ret = ldb_search(module->ldb, schema_dn,
			 LDB_SCOPE_ONELEVEL,
			 "(objectClass=attributeSchema)", NULL,
			 &a_res);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(module->ldb, 
				       "schema_fsmo_init: failed to search attributeSchema objects: %s",
				       ldb_errstring(module->ldb));
		talloc_free(mem_ctx);
		return ret;
	}
	talloc_steal(mem_ctx, a_res);

	/*
	 * load the objectClass definitions
	 */
	ret = ldb_search(module->ldb, schema_dn,
			 LDB_SCOPE_ONELEVEL,
			 "(objectClass=classSchema)", NULL,
			 &c_res);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(module->ldb, 
				       "schema_fsmo_init: failed to search classSchema objects: %s",
				       ldb_errstring(module->ldb));
		talloc_free(mem_ctx);
		return ret;
	}
	talloc_steal(mem_ctx, c_res);

	ret = dsdb_schema_from_ldb_results(mem_ctx, module->ldb,
					   lp_iconv_convenience(ldb_get_opaque(module->ldb, "loadparm")),
					   schema_res, a_res, c_res, &schema, &error_string);
	if (ret != LDB_SUCCESS) {
		ldb_asprintf_errstring(module->ldb, 
				       "schema_fsmo_init: dsdb_schema load failed: %s",
				       error_string);
		talloc_free(mem_ctx);
		return ret;
	}
	
	/* dsdb_set_schema() steal schema into the ldb_context */
	ret = dsdb_set_schema(module->ldb, schema);
	if (ret != LDB_SUCCESS) {
		ldb_debug_set(module->ldb, LDB_DEBUG_FATAL,
			      "schema_fsmo_init: dsdb_set_schema() failed: %d:%s",
			      ret, ldb_strerror(ret));
		talloc_free(mem_ctx);
		return ret;
	}

	talloc_free(mem_ctx);
	return ldb_next_init(module);
}

static int schema_fsmo_add(struct ldb_module *module, struct ldb_request *req)
{
	struct dsdb_schema *schema;
	const char *attributeID = NULL;
	const char *governsID = NULL;
	const char *oid_attr = NULL;
	const char *oid = NULL;
	uint32_t id32;
	WERROR status;

	schema = dsdb_get_schema(module->ldb);
	if (!schema) {
		return ldb_next_request(module, req);
	}

	if (!schema->fsmo.we_are_master) {
		ldb_debug_set(module->ldb, LDB_DEBUG_ERROR,
			  "schema_fsmo_add: we are not master: reject request\n");
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	attributeID = samdb_result_string(req->op.add.message, "attributeID", NULL);
	governsID = samdb_result_string(req->op.add.message, "governsID", NULL);

	if (attributeID) {
		oid_attr = "attributeID";
		oid = attributeID;
	} else if (governsID) {
		oid_attr = "governsID";
		oid = governsID;
	}

	if (!oid) {
		return ldb_next_request(module, req);
	}

	status = dsdb_map_oid2int(schema, oid, &id32);
	if (W_ERROR_IS_OK(status)) {
		return ldb_next_request(module, req);
	} else if (!W_ERROR_EQUAL(WERR_DS_NO_MSDS_INTID, status)) {
		ldb_debug_set(module->ldb, LDB_DEBUG_ERROR,
			  "schema_fsmo_add: failed to map %s[%s]: %s\n",
			  oid_attr, oid, win_errstr(status));
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	status = dsdb_create_prefix_mapping(module->ldb, schema, oid);
	if (!W_ERROR_IS_OK(status)) {
		ldb_debug_set(module->ldb, LDB_DEBUG_ERROR,
			  "schema_fsmo_add: failed to create prefix mapping for %s[%s]: %s\n",
			  oid_attr, oid, win_errstr(status));
		return LDB_ERR_UNWILLING_TO_PERFORM;
	}

	return ldb_next_request(module, req);
}

_PUBLIC_ const struct ldb_module_ops ldb_schema_fsmo_module_ops = {
	.name		= "schema_fsmo",
	.init_context	= schema_fsmo_init,
	.add		= schema_fsmo_add
};
