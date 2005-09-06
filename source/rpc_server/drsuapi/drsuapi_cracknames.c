/* 
   Unix SMB/CIFS implementation.

   endpoint server for the drsuapi pipe
   DsCrackNames()

   Copyright (C) Stefan Metzmacher 2004
   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2004-2005
   
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
#include "librpc/gen_ndr/ndr_drsuapi.h"
#include "rpc_server/dcerpc_server.h"
#include "rpc_server/common/common.h"
#include "rpc_server/drsuapi/dcesrv_drsuapi.h"
#include "lib/ldb/include/ldb.h"
#include "system/kerberos.h"
#include "auth/kerberos/kerberos.h"

static WERROR DsCrackNameOneFilter(struct drsuapi_bind_state *b_state, TALLOC_CTX *mem_ctx,
				   struct smb_krb5_context *smb_krb5_context,
				   uint32_t format_flags, uint32_t format_offered, uint32_t format_desired,
				   const struct ldb_dn *name_dn, const char *name, 
				   const char *domain_filter, const char *result_filter, 
				   struct drsuapi_DsNameInfo1 *info1);
static WERROR DsCrackNameOneName(struct drsuapi_bind_state *b_state, TALLOC_CTX *mem_ctx,
				 uint32_t format_flags, uint32_t format_offered, uint32_t format_desired,
				 const char *name, struct drsuapi_DsNameInfo1 *info1);

static enum drsuapi_DsNameStatus LDB_lookup_spn_alias(krb5_context context, struct ldb_context *ldb_ctx, 
				   TALLOC_CTX *mem_ctx,
				   const char *alias_from,
				   char **alias_to)
{
	int i;
	int count;
	struct ldb_message **msg;
	struct ldb_message_element *spnmappings;
	struct ldb_dn *service_dn = ldb_dn_string_compose(mem_ctx, samdb_base_dn(mem_ctx),
						"CN=Directory Service,CN=Windows NT"
						",CN=Services,CN=Configuration");
	char *service_dn_str = ldb_dn_linearize(mem_ctx, service_dn);
	const char *directory_attrs[] = {
		"sPNMappings", 
		NULL
	};

	count = ldb_search(ldb_ctx, service_dn, LDB_SCOPE_BASE, "(objectClass=nTDSService)",
			   directory_attrs, &msg);
	talloc_steal(mem_ctx, msg);

	if (count < 1) {
		DEBUG(1, ("ldb_search: dn: %s not found: %d", service_dn_str, count));
		return DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
	} else if (count > 1) {
		DEBUG(1, ("ldb_search: dn: %s found %d times!", service_dn_str, count));
		return DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
	}
	
	spnmappings = ldb_msg_find_element(msg[0], "sPNMappings");
	if (!spnmappings || spnmappings->num_values == 0) {
		DEBUG(1, ("ldb_search: dn: %s no sPNMappings attribute", service_dn_str));
		return DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
	}

	for (i = 0; i < spnmappings->num_values; i++) {
		char *mapping, *p, *str;
		mapping = talloc_strdup(mem_ctx, 
					(const char *)spnmappings->values[i].data);
		if (!mapping) {
			DEBUG(1, ("LDB_lookup_spn_alias: ldb_search: dn: %s did not have an sPNMapping", service_dn_str));
			return DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
		}
		
		/* C string manipulation sucks */
		
		p = strchr(mapping, '=');
		if (!p) {
			DEBUG(1, ("ldb_search: dn: %s sPNMapping malformed: %s", 
				  service_dn_str, mapping));
			return DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
		}
		p[0] = '\0';
		p++;
		do {
			str = p;
			p = strchr(p, ',');
			if (p) {
				p[0] = '\0';
				p++;
			}
			if (strcasecmp(str, alias_from) == 0) {
				*alias_to = mapping;
				return DRSUAPI_DS_NAME_STATUS_OK;
			}
		} while (p);
	}
	DEBUG(1, ("LDB_lookup_spn_alias: no alias for service %s applicable", alias_from));
	return DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
}

static WERROR DsCrackNameSPNAlias(struct drsuapi_bind_state *b_state, TALLOC_CTX *mem_ctx,
				  struct smb_krb5_context *smb_krb5_context,
				  uint32_t format_flags, uint32_t format_offered, uint32_t format_desired,
				  const char *name, struct drsuapi_DsNameInfo1 *info1)
{
	WERROR wret;
	krb5_error_code ret;
	krb5_principal principal;
	const char *service;
	char *new_service;
	char *new_princ;
	enum drsuapi_DsNameStatus namestatus;
	
	/* parse principal */
	ret = krb5_parse_name_norealm(smb_krb5_context->krb5_context, 
				      name, &principal);
	if (ret) {
		DEBUG(2, ("Could not parse principal: %s: %s",
			  name, smb_get_krb5_error_message(smb_krb5_context->krb5_context, 
							   ret, mem_ctx)));
		return WERR_NOMEM;
	}
	
	/* grab cifs/, http/ etc */
	if (principal->name.name_string.len < 2) {
		DEBUG(5, ("could not find principal in DB, alias not applicable"));
		info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
		return WERR_OK;
	}
	service = principal->name.name_string.val[0];
	
	/* MAP it */
	namestatus = LDB_lookup_spn_alias(smb_krb5_context->krb5_context, 
					  b_state->sam_ctx, mem_ctx, 
					  service, &new_service);
	
	if (namestatus != DRSUAPI_DS_NAME_STATUS_OK) {
		info1->status = namestatus;
		return WERR_OK;
	}
	
	if (ret != 0) {
		info1->status = DRSUAPI_DS_NAME_STATUS_RESOLVE_ERROR;
		return WERR_OK;
	}
	
	/* ooh, very nasty playing around in the Principal... */
	free(principal->name.name_string.val[0]);
	principal->name.name_string.val[0] = strdup(new_service);
	if (!principal->name.name_string.val[0]) {
		krb5_free_principal(smb_krb5_context->krb5_context, principal);
		return WERR_NOMEM;
	}
	
	ret = krb5_unparse_name_norealm(smb_krb5_context->krb5_context, principal, &new_princ);

	krb5_free_principal(smb_krb5_context->krb5_context, principal);
	
	if (ret) {
		return WERR_NOMEM;
	}
	
	/* reform principal */
	wret = DsCrackNameOneName(b_state, mem_ctx, format_flags, format_offered, format_desired,
				  new_princ, info1);
	free(new_princ);
	return wret;
}

static WERROR DsCrackNameUPN(struct drsuapi_bind_state *b_state, TALLOC_CTX *mem_ctx,
			     struct smb_krb5_context *smb_krb5_context,
			     uint32_t format_flags, uint32_t format_offered, uint32_t format_desired,
			     const char *name, struct drsuapi_DsNameInfo1 *info1)
{
	WERROR status;
	const char *domain_filter = NULL;
	const char *result_filter = NULL;
	krb5_error_code ret;
	krb5_principal principal;
	char **realm;
	char *unparsed_name_short;

	/* Prevent recursion */
	if (!name) {
		info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
		return WERR_OK;
	}

	ret = krb5_parse_name(smb_krb5_context->krb5_context, name, &principal);
	if (ret) {
		info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
		return WERR_OK;
	}
	
	domain_filter = NULL;
	realm = krb5_princ_realm(smb_krb5_context->krb5_context, principal);
	domain_filter = talloc_asprintf(mem_ctx, 
					"(&(&(|(&(dnsRoot=%s)(nETBIOSName=*))(nETBIOSName=%s))(objectclass=crossRef))(ncName=*))",
					*realm, *realm);
	ret = krb5_unparse_name_norealm(smb_krb5_context->krb5_context, principal, &unparsed_name_short);
	krb5_free_principal(smb_krb5_context->krb5_context, principal);
		
	if (ret) {
		free(unparsed_name_short);
		return WERR_NOMEM;
	}
	
	/* This may need to be extended for more userPrincipalName variations */
	result_filter = talloc_asprintf(mem_ctx, "(&(objectClass=user)(samAccountName=%s))", 
					unparsed_name_short);
	if (!result_filter || !domain_filter) {
		free(unparsed_name_short);
		return WERR_NOMEM;
	}
	status = DsCrackNameOneFilter(b_state, mem_ctx, 
				      smb_krb5_context, 
				      format_flags, format_offered, format_desired, 
				      NULL, unparsed_name_short, domain_filter, result_filter, 
				      info1);
	free(unparsed_name_short);
	return status;
}

static WERROR DsCrackNameOneName(struct drsuapi_bind_state *b_state, TALLOC_CTX *mem_ctx,
				 uint32_t format_flags, uint32_t format_offered, uint32_t format_desired,
				 const char *name, struct drsuapi_DsNameInfo1 *info1)
{
	krb5_error_code ret;
	const char *domain_filter = NULL;
	const char *result_filter = NULL;
	struct ldb_dn *name_dn = NULL;

	struct smb_krb5_context *smb_krb5_context;
	ret = smb_krb5_init_context(mem_ctx, &smb_krb5_context);
				
	if (ret) {
		return WERR_NOMEM;
	}

	info1->status = DRSUAPI_DS_NAME_STATUS_RESOLVE_ERROR;
	info1->dns_domain_name = NULL;
	info1->result_name = NULL;

	if (!name) {
		return WERR_INVALID_PARAM;
	}

	/* TODO: - fill the correct names in all cases!
	 *       - handle format_flags
	 */

	/* here we need to set the domain_filter and/or the result_filter */
	switch (format_offered) {
	case DRSUAPI_DS_NAME_FORMAT_CANONICAL: {
		char *str;
		
		str = talloc_strdup(mem_ctx, name);
		WERR_TALLOC_CHECK(str);
		
		if (strlen(str) == 0 || str[strlen(str)-1] != '/') {
			info1->status = DRSUAPI_DS_NAME_STATUS_RESOLVE_ERROR;
			return WERR_OK;
		}
		
		str[strlen(str)-1] = '\0';
		
		domain_filter = talloc_asprintf(mem_ctx, 
						"(&(&(&(dnsRoot=%s)(objectclass=crossRef)))(nETBIOSName=*)(ncName=*))", 
						str);
		WERR_TALLOC_CHECK(domain_filter);
		
		break;
	}
	case DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT: {
		char *p;
		char *domain;
		const char *account = NULL;
		
		domain = talloc_strdup(mem_ctx, name);
		WERR_TALLOC_CHECK(domain);
		
		p = strchr(domain, '\\');
		if (!p) {
			/* invalid input format */
			info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
			return WERR_OK;
		}
		p[0] = '\0';
		
		if (p[1]) {
			account = &p[1];
		}
		
		domain_filter = talloc_asprintf(mem_ctx, 
						"(&(&(nETBIOSName=%s)(objectclass=crossRef))(ncName=*))", 
						domain);
		WERR_TALLOC_CHECK(domain_filter);
		if (account) {
			result_filter = talloc_asprintf(mem_ctx, "(sAMAccountName=%s)",
							account);
			WERR_TALLOC_CHECK(result_filter);
		}
		
		talloc_free(domain);
		break;
	}
	case DRSUAPI_DS_NAME_FORMAT_FQDN_1779: {
		name_dn = ldb_dn_explode(mem_ctx, name);
		domain_filter = NULL;
		if (!name_dn) {
			info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
			return WERR_OK;
		}
		break;
	}
	case DRSUAPI_DS_NAME_FORMAT_GUID: {
		struct GUID guid;
		char *ldap_guid;
		NTSTATUS nt_status;
		domain_filter = NULL;

		nt_status = GUID_from_string(name, &guid);
		if (!NT_STATUS_IS_OK(nt_status)) {
			info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
			return WERR_OK;
		}
			
		ldap_guid = ldap_encode_ndr_GUID(mem_ctx, &guid);
		if (!ldap_guid) {
			return WERR_NOMEM;
		}
		result_filter = talloc_asprintf(mem_ctx, "(objectGUID=%s)",
						ldap_guid);
		WERR_TALLOC_CHECK(result_filter);
		break;
	}
	
	case DRSUAPI_DS_NAME_FORMAT_SID_OR_SID_HISTORY: {
		struct dom_sid *sid = dom_sid_parse_talloc(mem_ctx, name);
		char *ldap_sid;
									    
		domain_filter = NULL;
		if (!sid) {
			info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
			return WERR_OK;
		}
		ldap_sid = ldap_encode_ndr_dom_sid(mem_ctx, 
						   sid);
		if (!ldap_sid) {
			return WERR_NOMEM;
		}
		result_filter = talloc_asprintf(mem_ctx, "(objectSid=%s)",
						ldap_sid);
		WERR_TALLOC_CHECK(result_filter);
		break;
	}
	case DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL: {
		krb5_principal principal;
		char *unparsed_name;
		ret = krb5_parse_name(smb_krb5_context->krb5_context, name, &principal);
		if (ret) {
			info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
			return WERR_OK;
		}
		
		domain_filter = NULL;
		
		ret = krb5_unparse_name(smb_krb5_context->krb5_context, principal, &unparsed_name);
		if (ret) {
			krb5_free_principal(smb_krb5_context->krb5_context, principal);
			return WERR_NOMEM;
		}

		krb5_free_principal(smb_krb5_context->krb5_context, principal);
		result_filter = talloc_asprintf(mem_ctx, "(&(objectClass=user)(userPrincipalName=%s))", 
						unparsed_name);
		
		free(unparsed_name);
		WERR_TALLOC_CHECK(result_filter);
		break;
	}
	case DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL: {
		krb5_principal principal;
		char *unparsed_name_short;
		ret = krb5_parse_name_norealm(smb_krb5_context->krb5_context, name, &principal);
		if (ret) {
			info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
			return WERR_OK;
		}
		
		domain_filter = NULL;
		
		ret = krb5_unparse_name_norealm(smb_krb5_context->krb5_context, principal, &unparsed_name_short);
		krb5_free_principal(smb_krb5_context->krb5_context, principal);
		if (ret) {
			return WERR_NOMEM;
		}

		result_filter = talloc_asprintf(mem_ctx, "(&(objectClass=user)(servicePrincipalName=%s))", 
						unparsed_name_short);
		free(unparsed_name_short);
		WERR_TALLOC_CHECK(result_filter);
		
		break;
	}
	default: {
		info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
		return WERR_OK;
	}

	}
	
	return DsCrackNameOneFilter(b_state, mem_ctx, 
				    smb_krb5_context, 
				    format_flags, format_offered, format_desired, 
				    name_dn, name, 
				    domain_filter, result_filter, 
				    info1);
}

static WERROR DsCrackNameOneFilter(struct drsuapi_bind_state *b_state, TALLOC_CTX *mem_ctx,
				   struct smb_krb5_context *smb_krb5_context,
				   uint32_t format_flags, uint32_t format_offered, uint32_t format_desired,
				   const struct ldb_dn *name_dn, const char *name, 
				   const char *domain_filter, const char *result_filter, 
				   struct drsuapi_DsNameInfo1 *info1)
{
	int ldb_ret;
	struct ldb_message **domain_res = NULL;
	const char * const *domain_attrs;
	const char * const *result_attrs;
	struct ldb_message **result_res = NULL;
	const struct ldb_dn *result_basedn;

	/* here we need to set the attrs lists for domain and result lookups */
	switch (format_desired) {
		case DRSUAPI_DS_NAME_FORMAT_FQDN_1779: {
			const char * const _domain_attrs[] = { "ncName", "dnsRoot", NULL};
			const char * const _result_attrs[] = { "dn", NULL};
			
			domain_attrs = _domain_attrs;
			result_attrs = _result_attrs;
			break;
		}
		case DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT: {
			const char * const _domain_attrs[] = { "ncName", "dnsRoot", "nETBIOSName", NULL};
			const char * const _result_attrs[] = { "sAMAccountName", "objectSid", NULL};
			
			domain_attrs = _domain_attrs;
			result_attrs = _result_attrs;
			break;
		}
		case DRSUAPI_DS_NAME_FORMAT_GUID: {
			const char * const _domain_attrs[] = { "ncName", "dnsRoot", NULL};
			const char * const _result_attrs[] = { "objectGUID", NULL};
			
			domain_attrs = _domain_attrs;
			result_attrs = _result_attrs;
			break;
		}
		default:
			return WERR_OK;
	}

	if (domain_filter) {
		/* if we have a domain_filter look it up and set the result_basedn and the dns_domain_name */
		ldb_ret = gendb_search(b_state->sam_ctx, mem_ctx, NULL, &domain_res, domain_attrs,
				       "%s", domain_filter);
	} else {
		ldb_ret = gendb_search(b_state->sam_ctx, mem_ctx, NULL, &domain_res, domain_attrs,
				       "(ncName=%s)", ldb_dn_linearize(mem_ctx, samdb_base_dn(mem_ctx)));
	} 

	switch (ldb_ret) {
	case 1:
		break;
	case 0:
		info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
		return WERR_OK;
	case -1:
		info1->status = DRSUAPI_DS_NAME_STATUS_RESOLVE_ERROR;
		return WERR_OK;
	default:
		info1->status = DRSUAPI_DS_NAME_STATUS_NOT_UNIQUE;
		return WERR_OK;
	}

	info1->dns_domain_name	= samdb_result_string(domain_res[0], "dnsRoot", NULL);
	WERR_TALLOC_CHECK(info1->dns_domain_name);
	info1->status		= DRSUAPI_DS_NAME_STATUS_DOMAIN_ONLY;

	if (result_filter) {
		result_basedn = samdb_result_dn(mem_ctx, domain_res[0], "ncName", NULL);
		
		ldb_ret = gendb_search(b_state->sam_ctx, mem_ctx, result_basedn, &result_res,
				       result_attrs, "%s", result_filter);
	} else if (format_offered == DRSUAPI_DS_NAME_FORMAT_FQDN_1779) {
		ldb_ret = gendb_search_dn(b_state->sam_ctx, mem_ctx, name_dn, &result_res,
					  result_attrs);
	} else {
		name_dn = samdb_result_dn(mem_ctx, domain_res[0], "ncName", NULL);
		ldb_ret = gendb_search_dn(b_state->sam_ctx, mem_ctx, name_dn, &result_res,
					  result_attrs);
	}

	switch (ldb_ret) {
	case 1:
		break;
	case 0:
		switch (format_offered) {
		case DRSUAPI_DS_NAME_FORMAT_SERVICE_PRINCIPAL: 
			return DsCrackNameSPNAlias(b_state, mem_ctx, 
						   smb_krb5_context, 
						   format_flags, format_offered, format_desired,
						   name, info1);
			
		case DRSUAPI_DS_NAME_FORMAT_USER_PRINCIPAL:
			return DsCrackNameUPN(b_state, mem_ctx, smb_krb5_context, 
					      format_flags, format_offered, format_desired,
					      name, info1);
		}
		break;
	case -1:
		info1->status = DRSUAPI_DS_NAME_STATUS_RESOLVE_ERROR;
		return WERR_OK;
	default:
		info1->status = DRSUAPI_DS_NAME_STATUS_NOT_UNIQUE;
		return WERR_OK;
	}

	/* here we can use result_res[0] and domain_res[0] */
	switch (format_desired) {
	case DRSUAPI_DS_NAME_FORMAT_FQDN_1779: {
		info1->result_name	= ldb_dn_linearize(mem_ctx, result_res[0]->dn);
		WERR_TALLOC_CHECK(info1->result_name);

		info1->status		= DRSUAPI_DS_NAME_STATUS_OK;
		return WERR_OK;
	}
	case DRSUAPI_DS_NAME_FORMAT_NT4_ACCOUNT: {
		const struct dom_sid *sid = samdb_result_dom_sid(mem_ctx, result_res[0], "objectSid");
		const char *_acc = "", *_dom = "";
		
		if ((sid->num_auths < 4) || (sid->num_auths > 5)) {
			info1->status = DRSUAPI_DS_NAME_STATUS_NO_MAPPING;
			return WERR_OK;
		}

		if (sid->num_auths == 4) {
			ldb_ret = gendb_search(b_state->sam_ctx, mem_ctx, NULL, &domain_res, domain_attrs,
					       "(ncName=%s)", ldb_dn_linearize(mem_ctx, result_res[0]->dn));
			if (ldb_ret != 1) {
				info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
				return WERR_OK;
			}
			_dom = samdb_result_string(domain_res[0], "nETBIOSName", NULL);
			WERR_TALLOC_CHECK(_dom);
		
		} else if (sid->num_auths == 5) {
			const char *attrs[] = { NULL };
			struct ldb_message **domain_res2;
			struct dom_sid *dom_sid = dom_sid_dup(mem_ctx, sid);
			if (!dom_sid) {
				return WERR_OK;
			}
			dom_sid->num_auths--;
			ldb_ret = gendb_search(b_state->sam_ctx, mem_ctx, NULL, &domain_res2, attrs,
					       "(objectSid=%s)", ldap_encode_ndr_dom_sid(mem_ctx, dom_sid));
			if (ldb_ret != 1) {
				info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
				return WERR_OK;
			}
			ldb_ret = gendb_search(b_state->sam_ctx, mem_ctx, NULL, &domain_res, domain_attrs,
					       "(ncName=%s)", ldb_dn_linearize(mem_ctx, domain_res2[0]->dn));
			if (ldb_ret != 1) {
				info1->status = DRSUAPI_DS_NAME_STATUS_NOT_FOUND;
				return WERR_OK;
			}
			
			_dom = samdb_result_string(domain_res2[0], "nETBIOSName", NULL);
			WERR_TALLOC_CHECK(_dom);

			_acc = samdb_result_string(result_res[0], "sAMAccountName", NULL);
			WERR_TALLOC_CHECK(_acc);
		}

		info1->result_name	= talloc_asprintf(mem_ctx, "%s\\%s", _dom, _acc);
		WERR_TALLOC_CHECK(info1->result_name);
		
		info1->status		= DRSUAPI_DS_NAME_STATUS_OK;
		return WERR_OK;
	}
	case DRSUAPI_DS_NAME_FORMAT_GUID: {
		struct GUID guid;
		
		guid = samdb_result_guid(result_res[0], "objectGUID");
		
		info1->result_name	= GUID_string2(mem_ctx, &guid);
		WERR_TALLOC_CHECK(info1->result_name);
		
		info1->status		= DRSUAPI_DS_NAME_STATUS_OK;
		return WERR_OK;
	}
	default:
		return WERR_OK;
	}
	
	return WERR_INVALID_PARAM;
}

/* 
  drsuapi_DsCrackNames 
*/
WERROR dcesrv_drsuapi_DsCrackNames(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct drsuapi_DsCrackNames *r)
{
	WERROR status;
	struct drsuapi_bind_state *b_state;
	struct dcesrv_handle *h;

	r->out.level = r->in.level;
	ZERO_STRUCT(r->out.ctr);

	DCESRV_PULL_HANDLE_WERR(h, r->in.bind_handle, DRSUAPI_BIND_HANDLE);
	b_state = h->data;

	switch (r->in.level) {
		case 1: {
			struct drsuapi_DsNameCtr1 *ctr1;
			struct drsuapi_DsNameInfo1 *names;
			int count;
			int i;

			ctr1 = talloc(mem_ctx, struct drsuapi_DsNameCtr1);
			WERR_TALLOC_CHECK(ctr1);

			count = r->in.req.req1.count;
			names = talloc_array(mem_ctx, struct drsuapi_DsNameInfo1, count);
			WERR_TALLOC_CHECK(names);

			for (i=0; i < count; i++) {
				status = DsCrackNameOneName(b_state, mem_ctx,
							    r->in.req.req1.format_flags,
							    r->in.req.req1.format_offered,
							    r->in.req.req1.format_desired,
							    r->in.req.req1.names[i].str,
							    &names[i]);
				if (!W_ERROR_IS_OK(status)) {
					return status;
				}
			}

			ctr1->count = count;
			ctr1->array = names;
			r->out.ctr.ctr1 = ctr1;

			return WERR_OK;
		}
	}
	
	return WERR_UNKNOWN_LEVEL;
}
