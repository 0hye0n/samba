/* 
   Unix SMB/CIFS mplementation.
   LDAP protocol helper functions for SAMBA
   
   Copyright (C) Simo Sorce 2005
    
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
#include "libcli/util/asn_1.h"
#include "libcli/ldap/ldap.h"
#include "lib/ldb/include/ldb.h"

struct control_handler {
	const char *oid;
	bool (*decode)(void *mem_ctx, DATA_BLOB in, void **out);
	bool (*encode)(void *mem_ctx, void *in, DATA_BLOB *out);
};

static bool decode_server_sort_response(void *mem_ctx, DATA_BLOB in, void **out)
{
	DATA_BLOB attr;
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_sort_resp_control *lsrc;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	lsrc = talloc(mem_ctx, struct ldb_sort_resp_control);
	if (!lsrc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_enumerated(data, &(lsrc->result))) {
		return false;
	}

	lsrc->attr_desc = NULL;
	if (asn1_peek_tag(data, ASN1_OCTET_STRING)) {
		if (!asn1_read_OctetString(data, mem_ctx, &attr)) {
			return false;
		}
		lsrc->attr_desc = talloc_strndup(lsrc, (const char *)attr.data, attr.length);
		if (!lsrc->attr_desc) {
			return false;
		}
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lsrc;

	return true;
}

static bool decode_server_sort_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	DATA_BLOB attr;
	DATA_BLOB rule;
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_server_sort_control **lssc;
	int num;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	lssc = NULL;

	for (num = 0; asn1_peek_tag(data, ASN1_SEQUENCE(0)); num++) {
		lssc = talloc_realloc(mem_ctx, lssc, struct ldb_server_sort_control *, num + 2);
		if (!lssc) {
			return false;
		}
		lssc[num] = talloc_zero(lssc, struct ldb_server_sort_control);
		if (!lssc[num]) {
			return false;
		}

		if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
			return false;
		}

		if (!asn1_read_OctetString(data, mem_ctx, &attr)) {
			return false;
		}

		lssc[num]->attributeName = talloc_strndup(lssc[num], (const char *)attr.data, attr.length);
		if (!lssc [num]->attributeName) {
			return false;
		}
	
		if (asn1_peek_tag(data, ASN1_OCTET_STRING)) {
			if (!asn1_read_OctetString(data, mem_ctx, &rule)) {
				return false;
			}
			lssc[num]->orderingRule = talloc_strndup(lssc[num], (const char *)rule.data, rule.length);
			if (!lssc[num]->orderingRule) {
				return false;
			}
		}

		if (asn1_peek_tag(data, ASN1_BOOLEAN)) {
			bool reverse;
			if (!asn1_read_BOOLEAN(data, &reverse)) {
			return false;
			}
			lssc[num]->reverse = reverse;
		}
	
		if (!asn1_end_tag(data)) {
			return false;
		}
	}

	if (lssc != NULL) {
		lssc[num] = NULL;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lssc;

	return true;
}

static bool decode_extended_dn_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	struct asn1_data *data;
	struct ldb_extended_dn_control *ledc;

	/* The content of this control is optional */
	if (in.length == 0) {
		*out = NULL;
		return true;
	}

	data = asn1_init(mem_ctx);
	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	ledc = talloc(mem_ctx, struct ldb_extended_dn_control);
	if (!ledc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_Integer(data, &(ledc->type))) {
		return false;
	}
	
	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = ledc;

	return true;
}

static bool decode_sd_flags_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_sd_flags_control *lsdfc;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	lsdfc = talloc(mem_ctx, struct ldb_sd_flags_control);
	if (!lsdfc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_Integer(data, &(lsdfc->secinfo_flags))) {
		return false;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lsdfc;

	return true;
}

static bool decode_search_options_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_search_options_control *lsoc;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	lsoc = talloc(mem_ctx, struct ldb_search_options_control);
	if (!lsoc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_Integer(data, &(lsoc->search_options))) {
		return false;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lsoc;

	return true;
}

static bool decode_paged_results_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	DATA_BLOB cookie;
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_paged_control *lprc;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	lprc = talloc(mem_ctx, struct ldb_paged_control);
	if (!lprc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_Integer(data, &(lprc->size))) {
		return false;
	}
	
	if (!asn1_read_OctetString(data, mem_ctx, &cookie)) {
		return false;
	}
	lprc->cookie_len = cookie.length;
	if (lprc->cookie_len) {
		lprc->cookie = talloc_memdup(lprc, cookie.data, cookie.length);

		if (!(lprc->cookie)) {
			return false;
		}
	} else {
		lprc->cookie = NULL;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lprc;

	return true;
}

static bool decode_dirsync_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	DATA_BLOB cookie;
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_dirsync_control *ldc;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	ldc = talloc(mem_ctx, struct ldb_dirsync_control);
	if (!ldc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_Integer(data, &(ldc->flags))) {
		return false;
	}
	
	if (!asn1_read_Integer(data, &(ldc->max_attributes))) {
		return false;
	}
	
	if (!asn1_read_OctetString(data, mem_ctx, &cookie)) {
		return false;
	}
	ldc->cookie_len = cookie.length;
	if (ldc->cookie_len) {
		ldc->cookie = talloc_memdup(ldc, cookie.data, cookie.length);

		if (!(ldc->cookie)) {
			return false;
		}
	} else {
		ldc->cookie = NULL;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = ldc;

	return true;
}

/* seem that this controls has 2 forms one in case it is used with
 * a Search Request and another when used ina Search Response
 */
static bool decode_asq_control(void *mem_ctx, DATA_BLOB in, void **out)
{
	DATA_BLOB source_attribute;
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_asq_control *lac;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	lac = talloc(mem_ctx, struct ldb_asq_control);
	if (!lac) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (asn1_peek_tag(data, ASN1_OCTET_STRING)) {

		if (!asn1_read_OctetString(data, mem_ctx, &source_attribute)) {
			return false;
		}
		lac->src_attr_len = source_attribute.length;
		if (lac->src_attr_len) {
			lac->source_attribute = talloc_strndup(lac, (const char *)source_attribute.data, source_attribute.length);

			if (!(lac->source_attribute)) {
				return false;
			}
		} else {
			lac->source_attribute = NULL;
		}

		lac->request = 1;

	} else if (asn1_peek_tag(data, ASN1_ENUMERATED)) {

		if (!asn1_read_enumerated(data, &(lac->result))) {
			return false;
		}

		lac->request = 0;

	} else {
		return false;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lac;

	return true;
}

static bool decode_domain_scope_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	if (in.length != 0) {
		return false;
	}

	return true;
}

static bool decode_notification_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	if (in.length != 0) {
		return false;
	}

	return true;
}

static bool decode_show_deleted_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	if (in.length != 0) {
		return false;
	}

	return true;
}

static bool decode_permissive_modify_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	if (in.length != 0) {
		return false;
	}

	return true;
}

static bool decode_manageDSAIT_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	if (in.length != 0) {
		return false;
	}

	return true;
}

static bool decode_vlv_request(void *mem_ctx, DATA_BLOB in, void **out)
{
	DATA_BLOB assertion_value, context_id;
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_vlv_req_control *lvrc;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	lvrc = talloc(mem_ctx, struct ldb_vlv_req_control);
	if (!lvrc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_Integer(data, &(lvrc->beforeCount))) {
		return false;
	}
	
	if (!asn1_read_Integer(data, &(lvrc->afterCount))) {
		return false;
	}

	if (asn1_peek_tag(data, ASN1_CONTEXT(0))) {

		lvrc->type = 0;
		
		if (!asn1_start_tag(data, ASN1_CONTEXT(0))) {
			return false;
		}

		if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
			return false;
		}

		if (!asn1_read_Integer(data, &(lvrc->match.byOffset.offset))) {
			return false;
		}

		if (!asn1_read_Integer(data, &(lvrc->match.byOffset.contentCount))) {
			return false;
		}

		if (!asn1_end_tag(data)) { /*SEQUENCE*/
			return false;
		}

		if (!asn1_end_tag(data)) { /*CONTEXT*/
			return false;
		}

	} else {

		lvrc->type = 1;

		if (!asn1_start_tag(data, ASN1_CONTEXT(1))) {
			return false;
		}

		if (!asn1_read_OctetString(data, mem_ctx, &assertion_value)) {
			return false;
		}
		lvrc->match.gtOrEq.value_len = assertion_value.length;
		if (lvrc->match.gtOrEq.value_len) {
			lvrc->match.gtOrEq.value = talloc_memdup(lvrc, assertion_value.data, assertion_value.length);

			if (!(lvrc->match.gtOrEq.value)) {
				return false;
			}
		} else {
			lvrc->match.gtOrEq.value = NULL;
		}

		if (!asn1_end_tag(data)) { /*CONTEXT*/
			return false;
		}
	}

	if (asn1_peek_tag(data, ASN1_OCTET_STRING)) {
		if (!asn1_read_OctetString(data, mem_ctx, &context_id)) {
			return false;
		}
		lvrc->ctxid_len = context_id.length;
		if (lvrc->ctxid_len) {
			lvrc->contextId = talloc_memdup(lvrc, context_id.data, context_id.length);

			if (!(lvrc->contextId)) {
				return false;
			}
		} else {
			lvrc->contextId = NULL;
		}
	} else {
		lvrc->contextId = NULL;
		lvrc->ctxid_len = 0;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lvrc;

	return true;
}

static bool decode_vlv_response(void *mem_ctx, DATA_BLOB in, void **out)
{
	DATA_BLOB context_id;
	struct asn1_data *data = asn1_init(mem_ctx);
	struct ldb_vlv_resp_control *lvrc;

	if (!data) return false;

	if (!asn1_load(data, in)) {
		return false;
	}

	lvrc = talloc(mem_ctx, struct ldb_vlv_resp_control);
	if (!lvrc) {
		return false;
	}

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_Integer(data, &(lvrc->targetPosition))) {
		return false;
	}
	
	if (!asn1_read_Integer(data, &(lvrc->contentCount))) {
		return false;
	}
	
	if (!asn1_read_enumerated(data, &(lvrc->vlv_result))) {
		return false;
	}

	if (asn1_peek_tag(data, ASN1_OCTET_STRING)) {
		if (!asn1_read_OctetString(data, mem_ctx, &context_id)) {
			return false;
		}
		lvrc->contextId = talloc_strndup(lvrc, (const char *)context_id.data, context_id.length);
		if (!lvrc->contextId) {
			return false;
		}
		lvrc->ctxid_len = context_id.length;
	} else {
		lvrc->contextId = NULL;
		lvrc->ctxid_len = 0;
	}

	if (!asn1_end_tag(data)) {
		return false;
	}

	*out = lvrc;

	return true;
}

static bool encode_server_sort_response(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_sort_resp_control *lsrc = talloc_get_type(in, struct ldb_sort_resp_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_enumerated(data, lsrc->result)) {
		return false;
	}

	if (lsrc->attr_desc) {
		if (!asn1_write_OctetString(data, lsrc->attr_desc, strlen(lsrc->attr_desc))) {
			return false;
		}
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_server_sort_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_server_sort_control **lssc = talloc_get_type(in, struct ldb_server_sort_control *);
	struct asn1_data *data = asn1_init(mem_ctx);
	int num;

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	for (num = 0; lssc[num]; num++) {
		if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
			return false;
		}
		
		if (!asn1_write_OctetString(data, lssc[num]->attributeName, strlen(lssc[num]->attributeName))) {
			return false;
		}

		if (lssc[num]->orderingRule) {
			if (!asn1_write_OctetString(data, lssc[num]->orderingRule, strlen(lssc[num]->orderingRule))) {
				return false;
			}
		}

		if (lssc[num]->reverse) {
			if (!asn1_write_BOOLEAN(data, lssc[num]->reverse)) {
				return false;
			}
		}

		if (!asn1_pop_tag(data)) {
			return false;
		}
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_extended_dn_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_extended_dn_control *ledc = talloc_get_type(in, struct ldb_extended_dn_control);
	struct asn1_data *data;

	if (!in) {
		*out = data_blob(NULL, 0);
		return true;
	}

	data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_Integer(data, ledc->type)) {
		return false;
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_sd_flags_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_sd_flags_control *lsdfc = talloc_get_type(in, struct ldb_sd_flags_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_Integer(data, lsdfc->secinfo_flags)) {
		return false;
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_search_options_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_search_options_control *lsoc = talloc_get_type(in, struct ldb_search_options_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_Integer(data, lsoc->search_options)) {
		return false;
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_paged_results_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_paged_control *lprc = talloc_get_type(in, struct ldb_paged_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_Integer(data, lprc->size)) {
		return false;
	}

	if (!asn1_write_OctetString(data, lprc->cookie, lprc->cookie_len)) {
		return false;
	}	

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

/* seem that this controls has 2 forms one in case it is used with
 * a Search Request and another when used ina Search Response
 */
static bool encode_asq_control(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_asq_control *lac = talloc_get_type(in, struct ldb_asq_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (lac->request) {

		if (!asn1_write_OctetString(data, lac->source_attribute, lac->src_attr_len)) {
			return false;
		}
	} else {
		if (!asn1_write_enumerated(data, lac->result)) {
			return false;
		}
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_dirsync_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_dirsync_control *ldc = talloc_get_type(in, struct ldb_dirsync_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_Integer(data, ldc->flags)) {
		return false;
	}

	if (!asn1_write_Integer(data, ldc->max_attributes)) {
		return false;
	}

	if (!asn1_write_OctetString(data, ldc->cookie, ldc->cookie_len)) {
		return false;
	}	

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_domain_scope_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	if (in) {
		return false;
	}

	*out = data_blob(NULL, 0);
	return true;
}

static bool encode_notification_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	if (in) {
		return false;
	}

	*out = data_blob(NULL, 0);
	return true;
}

static bool encode_show_deleted_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	if (in) {
		return false;
	}

	*out = data_blob(NULL, 0);
	return true;
}

static bool encode_permissive_modify_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	if (in) {
		return false;
	}

	*out = data_blob(NULL, 0);
	return true;
}

static bool encode_manageDSAIT_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	if (in) {
		return false;
	}

	*out = data_blob(NULL, 0);
	return true;
}

static bool encode_vlv_request(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_vlv_req_control *lvrc = talloc_get_type(in, struct ldb_vlv_req_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_Integer(data, lvrc->beforeCount)) {
		return false;
	}

	if (!asn1_write_Integer(data, lvrc->afterCount)) {
		return false;
	}

	if (lvrc->type == 0) {
		if (!asn1_push_tag(data, ASN1_CONTEXT(0))) {
			return false;
		}
		
		if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
			return false;
		}
		
		if (!asn1_write_Integer(data, lvrc->match.byOffset.offset)) {
			return false;
		}

		if (!asn1_write_Integer(data, lvrc->match.byOffset.contentCount)) {
			return false;
		}

		if (!asn1_pop_tag(data)) { /*SEQUENCE*/
			return false;
		}

		if (!asn1_pop_tag(data)) { /*CONTEXT*/
			return false;
		}
	} else {
		if (!asn1_push_tag(data, ASN1_CONTEXT(1))) {
			return false;
		}
		
		if (!asn1_write_OctetString(data, lvrc->match.gtOrEq.value, lvrc->match.gtOrEq.value_len)) {
			return false;
		}

		if (!asn1_pop_tag(data)) { /*CONTEXT*/
			return false;
		}
	}

	if (lvrc->ctxid_len) {
		if (!asn1_write_OctetString(data, lvrc->contextId, lvrc->ctxid_len)) {
			return false;
		}
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

static bool encode_vlv_response(void *mem_ctx, void *in, DATA_BLOB *out)
{
	struct ldb_vlv_resp_control *lvrc = talloc_get_type(in, struct ldb_vlv_resp_control);
	struct asn1_data *data = asn1_init(mem_ctx);

	if (!data) return false;

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_Integer(data, lvrc->targetPosition)) {
		return false;
	}

	if (!asn1_write_Integer(data, lvrc->contentCount)) {
		return false;
	}

	if (!asn1_write_enumerated(data, lvrc->vlv_result)) {
		return false;
	}

	if (lvrc->ctxid_len) {
		if (!asn1_write_OctetString(data, lvrc->contextId, lvrc->ctxid_len)) {
			return false;
		}
	}

	if (!asn1_pop_tag(data)) {
		return false;
	}

	*out = data_blob_talloc(mem_ctx, data->data, data->length);
	if (out->data == NULL) {
		return false;
	}
	talloc_free(data);

	return true;
}

struct control_handler ldap_known_controls[] = {
	{ "1.2.840.113556.1.4.319", decode_paged_results_request, encode_paged_results_request },
	{ "1.2.840.113556.1.4.529", decode_extended_dn_request, encode_extended_dn_request },
	{ "1.2.840.113556.1.4.473", decode_server_sort_request, encode_server_sort_request },
	{ "1.2.840.113556.1.4.474", decode_server_sort_response, encode_server_sort_response },
	{ "1.2.840.113556.1.4.1504", decode_asq_control, encode_asq_control },
	{ "1.2.840.113556.1.4.841", decode_dirsync_request, encode_dirsync_request },
	{ "1.2.840.113556.1.4.528", decode_notification_request, encode_notification_request },
	{ "1.2.840.113556.1.4.417", decode_show_deleted_request, encode_show_deleted_request },
	{ "1.2.840.113556.1.4.1413", decode_permissive_modify_request, encode_permissive_modify_request },
	{ "1.2.840.113556.1.4.801", decode_sd_flags_request, encode_sd_flags_request },
	{ "1.2.840.113556.1.4.1339", decode_domain_scope_request, encode_domain_scope_request },
	{ "1.2.840.113556.1.4.1340", decode_search_options_request, encode_search_options_request },
	{ "2.16.840.1.113730.3.4.2", decode_manageDSAIT_request, encode_manageDSAIT_request },
	{ "2.16.840.1.113730.3.4.9", decode_vlv_request, encode_vlv_request },
	{ "2.16.840.1.113730.3.4.10", decode_vlv_response, encode_vlv_response },
/* DSDB_CONTROL_CURRENT_PARTITION_OID is internal only, and has no network representation */
	{ "1.3.6.1.4.1.7165.4.3.2", NULL, NULL },
/* DSDB_EXTENDED_REPLICATED_OBJECTS_OID is internal only, and has no network representation */
	{ "1.3.6.1.4.1.7165.4.4.1", NULL, NULL },
	{ NULL, NULL, NULL }
};

bool ldap_decode_control_value(void *mem_ctx, DATA_BLOB value, struct ldb_control *ctrl)
{
	int i;

	for (i = 0; ldap_known_controls[i].oid != NULL; i++) {
		if (strcmp(ldap_known_controls[i].oid, ctrl->oid) == 0) {
			if (!ldap_known_controls[i].decode || !ldap_known_controls[i].decode(mem_ctx, value, &ctrl->data)) {
				return false;
			}
			break;
		}
	}
	if (ldap_known_controls[i].oid == NULL) {
		return false;
	}

	return true;
}

bool ldap_decode_control_wrapper(void *mem_ctx, struct asn1_data *data, struct ldb_control *ctrl, DATA_BLOB *value)
{
	DATA_BLOB oid;

	if (!asn1_start_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_read_OctetString(data, mem_ctx, &oid)) {
		return false;
	}
	ctrl->oid = talloc_strndup(mem_ctx, (char *)oid.data, oid.length);
	if (!ctrl->oid) {
		return false;
	}

	if (asn1_peek_tag(data, ASN1_BOOLEAN)) {
		bool critical;
		if (!asn1_read_BOOLEAN(data, &critical)) {
			return false;
		}
		ctrl->critical = critical;
	} else {
		ctrl->critical = false;
	}

	ctrl->data = NULL;

	if (!asn1_peek_tag(data, ASN1_OCTET_STRING)) {
		*value = data_blob(NULL, 0);
		goto end_tag;
	}

	if (!asn1_read_OctetString(data, mem_ctx, value)) {
		return false;
	}

end_tag:
	if (!asn1_end_tag(data)) {
		return false;
	}

	return true;
}

bool ldap_encode_control(void *mem_ctx, struct asn1_data *data, struct ldb_control *ctrl)
{
	DATA_BLOB value;
	int i;

	for (i = 0; ldap_known_controls[i].oid != NULL; i++) {
		if (strcmp(ldap_known_controls[i].oid, ctrl->oid) == 0) {
			if (!ldap_known_controls[i].encode) {
				if (ctrl->critical) {
					return false;
				} else {
					/* not encoding this control */
					return true;
				}
			}
			if (!ldap_known_controls[i].encode(mem_ctx, ctrl->data, &value)) {
				return false;
			}
			break;
		}
	}
	if (ldap_known_controls[i].oid == NULL) {
		return false;
	}

	if (!asn1_push_tag(data, ASN1_SEQUENCE(0))) {
		return false;
	}

	if (!asn1_write_OctetString(data, ctrl->oid, strlen(ctrl->oid))) {
		return false;
	}

	if (ctrl->critical) {
		if (!asn1_write_BOOLEAN(data, ctrl->critical)) {
			return false;
		}
	}

	if (!ctrl->data) {
		goto pop_tag;
	}

	if (!asn1_write_OctetString(data, value.data, value.length)) {
		return false;
	}

pop_tag:
	if (!asn1_pop_tag(data)) {
		return false;
	}

	return true;
}
