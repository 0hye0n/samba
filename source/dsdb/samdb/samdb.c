/* 
   Unix SMB/CIFS implementation.

   interface functions for the sam database

   Copyright (C) Andrew Tridgell 2004
   
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
#include "librpc/gen_ndr/ndr_netlogon.h"
#include "lib/ldb/include/ldb.h"

/*
  connect to the SAM database
  return an opaque context pointer on success, or NULL on failure
 */
void *samdb_connect(TALLOC_CTX *mem_ctx)
{
	return ldb_wrap_connect(mem_ctx, lp_sam_url(), 0, NULL);
}

/*
  search the sam for the specified attributes - varargs variant
*/
int samdb_search(void *ctx,
		 TALLOC_CTX *mem_ctx, 
		 const char *basedn,
		 struct ldb_message ***res,
		 const char * const *attrs,
		 const char *format, ...) _PRINTF_ATTRIBUTE(6,7)
{
	struct ldb_wrap *sam_ctx = ctx;
	va_list ap;
	int count;

	va_start(ap, format);
	count = gendb_search_v(sam_ctx->ldb, mem_ctx, basedn, res, attrs, format, ap);
	va_end(ap);

	return count;
}

/*
  search the sam for the specified attributes in a specific domain, filter on
  objectSid being in domain_sid.
*/
int samdb_search_domain(void *ctx,
			TALLOC_CTX *mem_ctx, 
			const char *basedn,
			struct ldb_message ***res,
			const char * const *attrs,
			const struct dom_sid *domain_sid,
			const char *format, ...)  _PRINTF_ATTRIBUTE(7,8)
{
	struct ldb_wrap *sam_ctx = ctx;
	va_list ap;
	int i, count;

	va_start(ap, format);
	count = gendb_search_v(sam_ctx->ldb, mem_ctx, basedn, res, attrs,
			       format, ap);
	va_end(ap);

	i=0;

	while (i<count) {
		struct dom_sid *entry_sid;

		entry_sid = samdb_result_dom_sid(mem_ctx, (*res)[i],
						 "objectSid");

		if ((entry_sid == NULL) ||
		    (!dom_sid_in_domain(domain_sid, entry_sid))) {

			/* Delete that entry from the result set */
			(*res)[i] = (*res)[count-1];
			count -= 1;
			continue;
		}
		i += 1;
	}

	return count;
}

/*
  free up a search result
*/
int samdb_search_free(void *ctx,
		      TALLOC_CTX *mem_ctx, struct ldb_message **res)
{
	struct ldb_wrap *sam_ctx = ctx;
	return ldb_search_free(sam_ctx->ldb, res);
}

/*
  search the sam for a single string attribute in exactly 1 record
*/
const char *samdb_search_string_v(void *ctx,
				  TALLOC_CTX *mem_ctx,
				  const char *basedn,
				  const char *attr_name,
				  const char *format, va_list ap) _PRINTF_ATTRIBUTE(5,0)
{
	struct ldb_wrap *sam_ctx = ctx;
	int count;
	const char * const attrs[2] = { attr_name, NULL };
	struct ldb_message **res = NULL;

	count = gendb_search_v(sam_ctx->ldb, mem_ctx, basedn, &res, attrs, format, ap);
	if (count > 1) {		
		DEBUG(1,("samdb: search for %s %s not single valued (count=%d)\n", 
			 attr_name, format, count));
	}
	if (count != 1) {
		samdb_search_free(ctx, mem_ctx, res);
		return NULL;
	}

	return samdb_result_string(res[0], attr_name, NULL);
}
				 

/*
  search the sam for a single string attribute in exactly 1 record
*/
const char *samdb_search_string(void *ctx,
				TALLOC_CTX *mem_ctx,
				const char *basedn,
				const char *attr_name,
				const char *format, ...) _PRINTF_ATTRIBUTE(5,6)
{
	va_list ap;
	const char *str;

	va_start(ap, format);
	str = samdb_search_string_v(ctx, mem_ctx, basedn, attr_name, format, ap);
	va_end(ap);

	return str;
}

/*
  return the count of the number of records in the sam matching the query
*/
int samdb_search_count(void *ctx,
		       TALLOC_CTX *mem_ctx,
		       const char *basedn,
		       const char *format, ...) _PRINTF_ATTRIBUTE(4,5)
{
	struct ldb_wrap *samdb_ctx = ctx;
	va_list ap;
	struct ldb_message **res;
	const char * const attrs[] = { NULL };
	int ret;

	va_start(ap, format);
	ret = gendb_search_v(samdb_ctx->ldb, mem_ctx, basedn, &res, attrs, format, ap);
	va_end(ap);

	return ret;
}


/*
  search the sam for a single integer attribute in exactly 1 record
*/
uint_t samdb_search_uint(void *ctx,
			 TALLOC_CTX *mem_ctx,
			 uint_t default_value,
			 const char *basedn,
			 const char *attr_name,
			 const char *format, ...) _PRINTF_ATTRIBUTE(6,7)
{
	struct ldb_wrap *samdb_ctx = ctx;
	va_list ap;
	int count;
	struct ldb_message **res;
	const char * const attrs[2] = { attr_name, NULL };

	va_start(ap, format);
	count = gendb_search_v(samdb_ctx->ldb, mem_ctx, basedn, &res, attrs, format, ap);
	va_end(ap);

	if (count != 1) {
		return default_value;
	}

	return samdb_result_uint(res[0], attr_name, default_value);
}

/*
  search the sam for a single signed 64 bit integer attribute in exactly 1 record
*/
int64_t samdb_search_int64(void *ctx,
			   TALLOC_CTX *mem_ctx,
			   int64_t default_value,
			   const char *basedn,
			   const char *attr_name,
			   const char *format, ...) _PRINTF_ATTRIBUTE(6,7)
{
	struct ldb_wrap *samdb_ctx = ctx;
	va_list ap;
	int count;
	struct ldb_message **res;
	const char * const attrs[2] = { attr_name, NULL };

	va_start(ap, format);
	count = gendb_search_v(samdb_ctx->ldb, mem_ctx, basedn, &res, attrs, format, ap);
	va_end(ap);

	if (count != 1) {
		return default_value;
	}

	return samdb_result_int64(res[0], attr_name, default_value);
}

/*
  search the sam for multipe records each giving a single string attribute
  return the number of matches, or -1 on error
*/
int samdb_search_string_multiple(void *ctx,
				 TALLOC_CTX *mem_ctx,
				 const char *basedn,
				 const char ***strs,
				 const char *attr_name,
				 const char *format, ...) _PRINTF_ATTRIBUTE(6,7)
{
	struct ldb_wrap *samdb_ctx = ctx;
	va_list ap;
	int count, i;
	const char * const attrs[2] = { attr_name, NULL };
	struct ldb_message **res = NULL;

	va_start(ap, format);
	count = gendb_search_v(samdb_ctx->ldb, mem_ctx, basedn, &res, attrs, format, ap);
	va_end(ap);

	if (count <= 0) {
		return count;
	}

	/* make sure its single valued */
	for (i=0;i<count;i++) {
		if (res[i]->num_elements != 1) {
			DEBUG(1,("samdb: search for %s %s not single valued\n", 
				 attr_name, format));
			samdb_search_free(ctx, mem_ctx, res);
			return -1;
		}
	}

	*strs = talloc_array_p(mem_ctx, const char *, count+1);
	if (! *strs) {
		samdb_search_free(ctx, mem_ctx, res);
		return -1;
	}

	for (i=0;i<count;i++) {
		(*strs)[i] = samdb_result_string(res[i], attr_name, NULL);
	}
	(*strs)[count] = NULL;

	return count;
}

/*
  pull a uint from a result set. 
*/
uint_t samdb_result_uint(struct ldb_message *msg, const char *attr, uint_t default_value)
{
	return ldb_msg_find_uint(msg, attr, default_value);
}

/*
  pull a (signed) int64 from a result set. 
*/
int64_t samdb_result_int64(struct ldb_message *msg, const char *attr, int64_t default_value)
{
	return ldb_msg_find_int64(msg, attr, default_value);
}

/*
  pull a string from a result set. 
*/
const char *samdb_result_string(struct ldb_message *msg, const char *attr, 
				const char *default_value)
{
	return ldb_msg_find_string(msg, attr, default_value);
}

/*
  pull a rid from a objectSid in a result set. 
*/
uint32_t samdb_result_rid_from_sid(TALLOC_CTX *mem_ctx, struct ldb_message *msg, 
				 const char *attr, uint32_t default_value)
{
	struct dom_sid *sid;
	const char *sidstr = ldb_msg_find_string(msg, attr, NULL);
	if (!sidstr) return default_value;

	sid = dom_sid_parse_talloc(mem_ctx, sidstr);
	if (!sid) return default_value;

	return sid->sub_auths[sid->num_auths-1];
}

/*
  pull a dom_sid structure from a objectSid in a result set. 
*/
struct dom_sid *samdb_result_dom_sid(TALLOC_CTX *mem_ctx, struct ldb_message *msg, 
				     const char *attr)
{
	const char *sidstr = ldb_msg_find_string(msg, attr, NULL);
	if (!sidstr) return NULL;

	return dom_sid_parse_talloc(mem_ctx, sidstr);
}

/*
  pull a guid structure from a objectGUID in a result set. 
*/
struct GUID samdb_result_guid(struct ldb_message *msg, const char *attr)
{
	NTSTATUS status;
	struct GUID guid;
	const char *guidstr = ldb_msg_find_string(msg, attr, NULL);

	ZERO_STRUCT(guid);

	if (!guidstr) return guid;

	status = GUID_from_string(guidstr, &guid);
	if (!NT_STATUS_IS_OK(status)) {
		ZERO_STRUCT(guid);
		return guid;
	}

	return guid;
}

/*
  pull a sid prefix from a objectSid in a result set. 
  this is used to find the domain sid for a user
*/
const char *samdb_result_sid_prefix(TALLOC_CTX *mem_ctx, struct ldb_message *msg, 
				    const char *attr)
{
	struct dom_sid *sid = samdb_result_dom_sid(mem_ctx, msg, attr);
	if (!sid || sid->num_auths < 1) return NULL;

	sid->num_auths--;

	return dom_sid_string(mem_ctx, sid);
}

/*
  pull a NTTIME in a result set. 
*/
NTTIME samdb_result_nttime(struct ldb_message *msg, const char *attr, const char *default_value)
{
	const char *str = ldb_msg_find_string(msg, attr, default_value);
	return nttime_from_string(str);
}

/*
  pull a uint64_t from a result set. 
*/
uint64_t samdb_result_uint64(struct ldb_message *msg, const char *attr, uint64_t default_value)
{
	return ldb_msg_find_uint64(msg, attr, default_value);
}


/*
  construct the allow_password_change field from the PwdLastSet attribute and the 
  domain password settings
*/
NTTIME samdb_result_allow_password_change(void *ctx, TALLOC_CTX *mem_ctx, 
					  const char *domain_dn, 
					  struct ldb_message *msg, 
					  const char *attr)
{
	uint64_t attr_time = samdb_result_uint64(msg, attr, 0);
	int64_t minPwdAge;

	if (attr_time == 0) {
		return 0;
	}

	minPwdAge = samdb_search_int64(ctx, mem_ctx, 0, NULL, 
				       "minPwdAge", "dn=%s", domain_dn);

	/* yes, this is a -= not a += as minPwdAge is stored as the negative
	   of the number of 100-nano-seconds */
	attr_time -= minPwdAge;

	return attr_time;
}

/*
  construct the force_password_change field from the PwdLastSet attribute and the 
  domain password settings
*/
NTTIME samdb_result_force_password_change(void *ctx, TALLOC_CTX *mem_ctx, 
					  const char *domain_dn, 
					  struct ldb_message *msg, 
					  const char *attr)
{
	uint64_t attr_time = samdb_result_uint64(msg, attr, 0);
	int64_t maxPwdAge;

	if (attr_time == 0) {
		return 0;
	}

	maxPwdAge = samdb_search_int64(ctx, mem_ctx, 0, NULL, "maxPwdAge", "dn=%s", domain_dn);
	if (maxPwdAge == 0) {
		return 0;
	} else {
		attr_time -= maxPwdAge;
	}

	return attr_time;
}

/*
  pull a samr_Password structutre from a result set. 
*/
struct samr_Password samdb_result_hash(struct ldb_message *msg, const char *attr)
{
	struct samr_Password hash;
	const struct ldb_val *val = ldb_msg_find_ldb_val(msg, attr);
	ZERO_STRUCT(hash);
	if (val) {
		memcpy(hash.hash, val->data, MIN(val->length, sizeof(hash.hash)));
	}
	return hash;
}

/*
  pull an array of samr_Password structutres from a result set. 
*/
uint_t samdb_result_hashes(TALLOC_CTX *mem_ctx, struct ldb_message *msg, 
			   const char *attr, struct samr_Password **hashes)
{
	uint_t count = 0;
	const struct ldb_val *val = ldb_msg_find_ldb_val(msg, attr);
	int i;

	*hashes = NULL;
	if (!val) {
		return 0;
	}
	count = val->length / 16;
	if (count == 0) {
		return 0;
	}

	*hashes = talloc_array_p(mem_ctx, struct samr_Password, count);
	if (! *hashes) {
		return 0;
	}

	for (i=0;i<count;i++) {
		memcpy((*hashes)[i].hash, (i*16)+(char *)val->data, 16);
	}

	return count;
}

NTSTATUS samdb_result_passwords(TALLOC_CTX *mem_ctx, struct ldb_message *msg, 
				struct samr_Password **lm_pwd, struct samr_Password **nt_pwd) 
{

	const char *unicodePwd = samdb_result_string(msg, "unicodePwd", NULL);
	
	struct samr_Password *lmPwdHash, *ntPwdHash;
	if (unicodePwd) {
		if (nt_pwd) {
			ntPwdHash = talloc_p(mem_ctx, struct samr_Password);
			if (!ntPwdHash) {
				return NT_STATUS_NO_MEMORY;
			}
			
			E_md4hash(unicodePwd, ntPwdHash->hash);
			*nt_pwd = ntPwdHash;
		}

		if (lm_pwd) {
			BOOL lm_hash_ok;
		
			lmPwdHash = talloc_p(mem_ctx, struct samr_Password);
			if (!lmPwdHash) {
				return NT_STATUS_NO_MEMORY;
			}
			
			/* compute the new nt and lm hashes */
			lm_hash_ok = E_deshash(unicodePwd, lmPwdHash->hash);
			
			if (lm_hash_ok) {
				*lm_pwd = lmPwdHash;
			} else {
				*lm_pwd = NULL;
			}
		}
	} else {
		if (nt_pwd) {
			int num_nt;
			num_nt = samdb_result_hashes(mem_ctx, msg, "ntPwdHash", &ntPwdHash);
			if (num_nt == 0) {
				*nt_pwd = NULL;
			} else if (num_nt > 1) {
				return NT_STATUS_INTERNAL_DB_CORRUPTION;
			} else {
				*nt_pwd = &ntPwdHash[0];
			}
		}
		if (lm_pwd) {
			int num_lm;
			num_lm = samdb_result_hashes(mem_ctx, msg, "lmPwdHash", &lmPwdHash);
			if (num_lm == 0) {
				*lm_pwd = NULL;
			} else if (num_lm > 1) {
				return NT_STATUS_INTERNAL_DB_CORRUPTION;
			} else {
				*lm_pwd = &lmPwdHash[0];
			}
		}
		
	}
	return NT_STATUS_OK;
}

/*
  pull a samr_LogonHours structutre from a result set. 
*/
struct samr_LogonHours samdb_result_logon_hours(TALLOC_CTX *mem_ctx, struct ldb_message *msg, const char *attr)
{
	struct samr_LogonHours hours;
	const int units_per_week = 168;
	const struct ldb_val *val = ldb_msg_find_ldb_val(msg, attr);
	ZERO_STRUCT(hours);
	hours.bits = talloc_array_p(mem_ctx, uint8, units_per_week);
	if (!hours.bits) {
		return hours;
	}
	hours.units_per_week = units_per_week;
	memset(hours.bits, 0xFF, units_per_week);
	if (val) {
		memcpy(hours.bits, val->data, MIN(val->length, units_per_week));
	}
	return hours;
}

/*
  pull a set of account_flags from a result set. 
*/
uint16_t samdb_result_acct_flags(struct ldb_message *msg, const char *attr)
{
	uint_t userAccountControl = ldb_msg_find_uint(msg, attr, 0);
	return samdb_uf2acb(userAccountControl);
}

/*
  copy from a template record to a message
*/
int samdb_copy_template(void *ctx, TALLOC_CTX *mem_ctx, 
			struct ldb_message *msg, const char *expression)
{
	struct ldb_message **res, *t;
	int ret, i, j;
	

	/* pull the template record */
	ret = samdb_search(ctx, mem_ctx, NULL, &res, NULL, "%s", expression);
	if (ret != 1) {
		DEBUG(1,("samdb: ERROR: template '%s' matched %d records\n", 
			 expression, ret));
		return -1;
	}
	t = res[0];

	for (i=0;i<t->num_elements;i++) {
		struct ldb_message_element *el = &t->elements[i];
		/* some elements should not be copied from the template */
		if (strcasecmp(el->name, "cn") == 0 ||
		    strcasecmp(el->name, "name") == 0 ||
		    strcasecmp(el->name, "sAMAccountName") == 0) {
			continue;
		}
		for (j=0;j<el->num_values;j++) {
			if (strcasecmp(el->name, "objectClass") == 0 &&
			    (strcasecmp((char *)el->values[j].data, "Template") == 0 ||
			     strcasecmp((char *)el->values[j].data, "userTemplate") == 0 ||
			     strcasecmp((char *)el->values[j].data, "groupTemplate") == 0 ||
			     strcasecmp((char *)el->values[j].data, "foreignSecurityTemplate") == 0 ||
			     strcasecmp((char *)el->values[j].data, "aliasTemplate") == 0)) {
				continue;
			}
			samdb_msg_add_string(ctx, mem_ctx, msg, el->name, 
					     (char *)el->values[j].data);
		}
	}

	return 0;
}


/*
  allocate a new id, attempting to do it atomically
  return 0 on failure, the id on success
*/
static NTSTATUS _samdb_allocate_next_id(void *ctx, TALLOC_CTX *mem_ctx, const char *dn, 
					const char *attr, uint32_t *id)
{
	struct ldb_wrap *sam_ctx = ctx;
	struct ldb_message msg;
	int ret;
	const char *str;
	struct ldb_val vals[2];
	struct ldb_message_element els[2];

	str = samdb_search_string(ctx, mem_ctx, NULL, attr, "dn=%s", dn);
	if (!str) {
		DEBUG(1,("id not found at %s %s\n", dn, attr));
		return NT_STATUS_OBJECT_NAME_INVALID;
	}

	*id = strtol(str, NULL, 0);
	if ((*id)+1 == 0) {
		/* out of IDs ! */
		return NT_STATUS_INSUFFICIENT_RESOURCES;
	}

	/* we do a delete and add as a single operation. That prevents
	   a race */
	ZERO_STRUCT(msg);
	msg.dn = talloc_strdup(mem_ctx, dn);
	if (!msg.dn) {
		return NT_STATUS_NO_MEMORY;
	}
	msg.num_elements = 2;
	msg.elements = els;

	els[0].num_values = 1;
	els[0].values = &vals[0];
	els[0].flags = LDB_FLAG_MOD_DELETE;
	els[0].name = talloc_strdup(mem_ctx, attr);
	if (!els[0].name) {
		return NT_STATUS_NO_MEMORY;
	}

	els[1].num_values = 1;
	els[1].values = &vals[1];
	els[1].flags = LDB_FLAG_MOD_ADD;
	els[1].name = els[0].name;

	vals[0].data = talloc_asprintf(mem_ctx, "%u", *id);
	if (!vals[0].data) {
		return NT_STATUS_NO_MEMORY;
	}
	vals[0].length = strlen(vals[0].data);

	vals[1].data = talloc_asprintf(mem_ctx, "%u", (*id)+1);
	if (!vals[1].data) {
		return NT_STATUS_NO_MEMORY;
	}
	vals[1].length = strlen(vals[1].data);

	ret = ldb_modify(sam_ctx->ldb, &msg);
	if (ret != 0) {
		return NT_STATUS_UNEXPECTED_IO_ERROR;
	}

	(*id)++;

	return NT_STATUS_OK;
}

/*
  allocate a new id, attempting to do it atomically
  return 0 on failure, the id on success
*/
NTSTATUS samdb_allocate_next_id(void *ctx, TALLOC_CTX *mem_ctx, const char *dn, const char *attr,
				uint32_t *id)
{
	int tries = 10;
	NTSTATUS status;

	/* we need to try multiple times to cope with two account
	   creations at the same time */
	while (tries--) {
		status = _samdb_allocate_next_id(ctx, mem_ctx, dn, attr, id);
		if (!NT_STATUS_EQUAL(NT_STATUS_UNEXPECTED_IO_ERROR, status)) {
			break;
		}
	}

	if (NT_STATUS_EQUAL(NT_STATUS_UNEXPECTED_IO_ERROR, status)) {
		DEBUG(1,("Failed to increment id %s at %s\n", attr, dn));
	}

	return status;
}


/*
  add a string element to a message
*/
int samdb_msg_add_string(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			 const char *attr_name, const char *str)
{
	struct ldb_wrap *sam_ctx = ctx;
	char *s = talloc_strdup(mem_ctx, str);
	char *a = talloc_strdup(mem_ctx, attr_name);
	if (s == NULL || a == NULL) {
		return -1;
	}
	return ldb_msg_add_string(sam_ctx->ldb, msg, a, s);
}

/*
  add a delete element operation to a message
*/
int samdb_msg_add_delete(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			 const char *attr_name)
{
	struct ldb_wrap *sam_ctx = ctx;
	char *a = talloc_strdup(mem_ctx, attr_name);
	if (a == NULL) {
		return -1;
	}
	/* we use an empty replace rather than a delete, as it allows for 
	   samdb_replace() to be used everywhere */
	return ldb_msg_add_empty(sam_ctx->ldb, msg, a, LDB_FLAG_MOD_REPLACE);
}

/*
  add a add attribute value to a message
*/
int samdb_msg_add_addval(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			 const char *attr_name, const char *value)
{
	struct ldb_wrap *sam_ctx = ctx;
	struct ldb_message_element *el;
	char *a, *v;
	int ret;
	a = talloc_strdup(mem_ctx, attr_name);
	if (a == NULL)
		return -1;
	v = talloc_strdup(mem_ctx, value);
	if (v == NULL)
		return -1;
	ret = ldb_msg_add_string(sam_ctx->ldb, msg, a, v);
	if (ret != 0)
		return ret;
	el = ldb_msg_find_element(msg, a);
	if (el == NULL)
		return -1;
	el->flags = LDB_FLAG_MOD_ADD;
	return 0;
}

/*
  add a delete attribute value to a message
*/
int samdb_msg_add_delval(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			 const char *attr_name, const char *value)
{
	struct ldb_wrap *sam_ctx = ctx;
	struct ldb_message_element *el;
	char *a, *v;
	int ret;
	a = talloc_strdup(mem_ctx, attr_name);
	if (a == NULL)
		return -1;
	v = talloc_strdup(mem_ctx, value);
	if (v == NULL)
		return -1;
	ret = ldb_msg_add_string(sam_ctx->ldb, msg, a, v);
	if (ret != 0)
		return ret;
	el = ldb_msg_find_element(msg, a);
	if (el == NULL)
		return -1;
	el->flags = LDB_FLAG_MOD_DELETE;
	return 0;
}

/*
  add a uint_t element to a message
*/
int samdb_msg_add_uint(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
		       const char *attr_name, uint_t v)
{
	const char *s = talloc_asprintf(mem_ctx, "%u", v);
	return samdb_msg_add_string(ctx, mem_ctx, msg, attr_name, s);
}

/*
  add a (signed) int64_t element to a message
*/
int samdb_msg_add_int64(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			const char *attr_name, int64_t v)
{
	const char *s = talloc_asprintf(mem_ctx, "%lld", v);
	return samdb_msg_add_string(ctx, mem_ctx, msg, attr_name, s);
}

/*
  add a uint64_t element to a message
*/
int samdb_msg_add_uint64(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			const char *attr_name, uint64_t v)
{
	const char *s = talloc_asprintf(mem_ctx, "%llu", v);
	return samdb_msg_add_string(ctx, mem_ctx, msg, attr_name, s);
}

/*
  add a samr_Password element to a message
*/
int samdb_msg_add_hash(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
		       const char *attr_name, struct samr_Password hash)
{
	struct ldb_wrap *sam_ctx = ctx;
	struct ldb_val val;
	val.data = talloc_memdup(mem_ctx, hash.hash, 16);
	if (!val.data) {
		return -1;
	}
	val.length = 16;
	return ldb_msg_add_value(sam_ctx->ldb, msg, attr_name, &val);
}

/*
  add a samr_Password array to a message
*/
int samdb_msg_add_hashes(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			 const char *attr_name, struct samr_Password *hashes, uint_t count)
{
	struct ldb_wrap *sam_ctx = ctx;
	struct ldb_val val;
	int i;
	val.data = talloc_array_size(mem_ctx, 16, count);
	val.length = count*16;
	if (!val.data) {
		return -1;
	}
	for (i=0;i<count;i++) {
		memcpy(i*16 + (char *)val.data, hashes[i].hash, 16);
	}
	return ldb_msg_add_value(sam_ctx->ldb, msg, attr_name, &val);
}

/*
  add a acct_flags element to a message
*/
int samdb_msg_add_acct_flags(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			     const char *attr_name, uint32_t v)
{
	return samdb_msg_add_uint(ctx, mem_ctx, msg, attr_name, samdb_acb2uf(v));
}

/*
  add a logon_hours element to a message
*/
int samdb_msg_add_logon_hours(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			      const char *attr_name, struct samr_LogonHours *hours)
{
	struct ldb_wrap *sam_ctx = ctx;
	struct ldb_val val;
	val.length = hours->units_per_week / 8;
	val.data = hours->bits;
	return ldb_msg_add_value(sam_ctx->ldb, msg, attr_name, &val);
}

/*
  set a string element in a message
*/
int samdb_msg_set_string(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			 const char *attr_name, const char *str)
{
	struct ldb_message_element *el;

	el = ldb_msg_find_element(msg, attr_name);
	if (el) {
		el->num_values = 0;
	}
	return samdb_msg_add_string(ctx, mem_ctx, msg, attr_name, str);
}

/*
  set a ldaptime element in a message
*/
int samdb_msg_set_ldaptime(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg,
			   const char *attr_name, time_t t)
{
	char *str = ldap_timestring(mem_ctx, t);
	if (!str) {
		return -1;
	}
	return samdb_msg_set_string(ctx, mem_ctx, msg, attr_name, str);
}

/*
  add a record
*/
int samdb_add(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg)
{
	struct ldb_wrap *sam_ctx = ctx;

	return ldb_add(sam_ctx->ldb, msg);
}

/*
  delete a record
*/
int samdb_delete(void *ctx, TALLOC_CTX *mem_ctx, const char *dn)
{
	struct ldb_wrap *sam_ctx = ctx;

	return ldb_delete(sam_ctx->ldb, dn);
}

/*
  modify a record
*/
int samdb_modify(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg)
{
	struct ldb_wrap *sam_ctx = ctx;

	return ldb_modify(sam_ctx->ldb, msg);
}

/*
  replace elements in a record
*/
int samdb_replace(void *ctx, TALLOC_CTX *mem_ctx, struct ldb_message *msg)
{
	int i;

	/* mark all the message elements as LDB_FLAG_MOD_REPLACE */
	for (i=0;i<msg->num_elements;i++) {
		msg->elements[i].flags = LDB_FLAG_MOD_REPLACE;
	}

	/* modify the samdb record */
	return samdb_modify(ctx, mem_ctx, msg);
}

/*
  return a default security descriptor
*/
struct security_descriptor *samdb_default_security_descriptor(TALLOC_CTX *mem_ctx)
{
	struct security_descriptor *sd;

	sd = security_descriptor_initialise(mem_ctx);

	return sd;
}
