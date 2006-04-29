/* 
   Unix SMB/CIFS implementation.

   endpoint server for the lsarpc pipe

   Copyright (C) Andrew Tridgell 2004
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
#include "rpc_server/dcerpc_server.h"
#include "rpc_server/common/common.h"
#include "auth/auth.h"
#include "dsdb/samdb/samdb.h"
#include "libcli/ldap/ldap.h"
#include "libcli/security/security.h"
#include "libcli/auth/libcli_auth.h"
#include "passdb/secrets.h"
#include "db_wrap.h"

/*
  this type allows us to distinguish handle types
*/
enum lsa_handle {
	LSA_HANDLE_POLICY,
	LSA_HANDLE_ACCOUNT,
	LSA_HANDLE_SECRET,
	LSA_HANDLE_TRUSTED_DOMAIN
};

/*
  state associated with a lsa_OpenPolicy() operation
*/
struct lsa_policy_state {
	struct dcesrv_handle *handle;
	struct ldb_context *sam_ldb;
	struct sidmap_context *sidmap;
	uint32_t access_mask;
	const struct ldb_dn *domain_dn;
	const struct ldb_dn *builtin_dn;
	const struct ldb_dn *system_dn;
	const char *domain_name;
	struct dom_sid *domain_sid;
	struct dom_sid *builtin_sid;
};


/*
  state associated with a lsa_OpenAccount() operation
*/
struct lsa_account_state {
	struct lsa_policy_state *policy;
	uint32_t access_mask;
	struct dom_sid *account_sid;
	const struct ldb_dn *account_dn;
};


/*
  state associated with a lsa_OpenSecret() operation
*/
struct lsa_secret_state {
	struct lsa_policy_state *policy;
	uint32_t access_mask;
	const struct ldb_dn *secret_dn;
	struct ldb_context *sam_ldb;
	BOOL global;
};

/*
  state associated with a lsa_OpenTrustedDomain() operation
*/
struct lsa_trusted_domain_state {
	struct lsa_policy_state *policy;
	uint32_t access_mask;
	const struct ldb_dn *trusted_domain_dn;
};

/* 
  lsa_Close 
*/
static NTSTATUS lsa_Close(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			  struct lsa_Close *r)
{
	struct dcesrv_handle *h;

	*r->out.handle = *r->in.handle;

	DCESRV_PULL_HANDLE(h, r->in.handle, DCESRV_HANDLE_ANY);

	talloc_free(h);

	ZERO_STRUCTP(r->out.handle);

	return NT_STATUS_OK;
}


/* 
  lsa_Delete 
*/
static NTSTATUS lsa_Delete(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			   struct lsa_Delete *r)
{
	struct dcesrv_handle *h;
	int ret;

	DCESRV_PULL_HANDLE(h, r->in.handle, DCESRV_HANDLE_ANY);
	if (h->wire_handle.handle_type == LSA_HANDLE_SECRET) {
		struct lsa_secret_state *secret_state = h->data;
		ret = samdb_delete(secret_state->sam_ldb, mem_ctx, secret_state->secret_dn);
		talloc_free(h);
		if (ret != 0) {
			return NT_STATUS_INVALID_HANDLE;
		}

		return NT_STATUS_OK;
	} else if (h->wire_handle.handle_type == LSA_HANDLE_TRUSTED_DOMAIN) {
		struct lsa_trusted_domain_state *trusted_domain_state = h->data;
		ret = samdb_delete(trusted_domain_state->policy->sam_ldb, mem_ctx, 
				   trusted_domain_state->trusted_domain_dn);
		talloc_free(h);
		if (ret != 0) {
			return NT_STATUS_INVALID_HANDLE;
		}

		return NT_STATUS_OK;
	} 
	
	return NT_STATUS_INVALID_HANDLE;
}


/* 
  lsa_EnumPrivs 
*/
static NTSTATUS lsa_EnumPrivs(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			      struct lsa_EnumPrivs *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;
	int i;
	const char *privname;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	i = *r->in.resume_handle;
	if (i == 0) i = 1;

	while ((privname = sec_privilege_name(i)) &&
	       r->out.privs->count < r->in.max_count) {
		struct lsa_PrivEntry *e;

		r->out.privs->privs = talloc_realloc(r->out.privs,
						       r->out.privs->privs, 
						       struct lsa_PrivEntry, 
						       r->out.privs->count+1);
		if (r->out.privs->privs == NULL) {
			return NT_STATUS_NO_MEMORY;
		}
		e = &r->out.privs->privs[r->out.privs->count];
		e->luid.low = i;
		e->luid.high = 0;
		e->name.string = privname;
		r->out.privs->count++;
		i++;
	}

	*r->out.resume_handle = i;

	return NT_STATUS_OK;
}


/* 
  lsa_QuerySecObj 
*/
static NTSTATUS lsa_QuerySecurity(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				  struct lsa_QuerySecurity *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_SetSecObj 
*/
static NTSTATUS lsa_SetSecObj(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			      struct lsa_SetSecObj *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_ChangePassword 
*/
static NTSTATUS lsa_ChangePassword(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				   struct lsa_ChangePassword *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

static NTSTATUS lsa_get_policy_state(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				     struct lsa_policy_state **_state)
{
	struct lsa_policy_state *state;

	state = talloc(mem_ctx, struct lsa_policy_state);
	if (!state) {
		return NT_STATUS_NO_MEMORY;
	}

	/* make sure the sam database is accessible */
	state->sam_ldb = samdb_connect(state, dce_call->conn->auth_state.session_info); 
	if (state->sam_ldb == NULL) {
		return NT_STATUS_INVALID_SYSTEM_SERVICE;
	}

	state->sidmap = sidmap_open(state);
	if (state->sidmap == NULL) {
		return NT_STATUS_INVALID_SYSTEM_SERVICE;
	}

	/* work out the domain_dn - useful for so many calls its worth
	   fetching here */
	state->domain_dn = samdb_base_dn(state);
	if (!state->domain_dn) {
		return NT_STATUS_NO_MEMORY;		
	}

	state->domain_name
		= samdb_search_string(state->sam_ldb, state, NULL, "nETBIOSName", 
				      "(&(objectclass=crossRef)(ncName=%s))", ldb_dn_linearize(mem_ctx, state->domain_dn));
	
	if (!state->domain_name) {
		return NT_STATUS_NO_SUCH_DOMAIN;		
	}
	talloc_steal(state, state->domain_name);

	/* work out the builtin_dn - useful for so many calls its worth
	   fetching here */
	state->builtin_dn = samdb_search_dn(state->sam_ldb, state, state->domain_dn, "(objectClass=builtinDomain)");
	if (!state->builtin_dn) {
		return NT_STATUS_NO_SUCH_DOMAIN;		
	}

	/* work out the system_dn - useful for so many calls its worth
	   fetching here */
	state->system_dn = samdb_search_dn(state->sam_ldb, state,
					   state->domain_dn, "(&(objectClass=container)(cn=System))");
	if (!state->system_dn) {
		return NT_STATUS_NO_SUCH_DOMAIN;		
	}

	state->domain_sid = samdb_search_dom_sid(state->sam_ldb, state,
						 state->domain_dn, "objectSid", NULL);
	if (!state->domain_sid) {
		return NT_STATUS_NO_SUCH_DOMAIN;		
	}

	talloc_steal(state, state->domain_sid);

	state->builtin_sid = dom_sid_parse_talloc(state, SID_BUILTIN);
	if (!state->builtin_sid) {
		return NT_STATUS_NO_SUCH_DOMAIN;		
	}

	*_state = state;

	return NT_STATUS_OK;
}

/* 
  lsa_OpenPolicy2
*/
static NTSTATUS lsa_OpenPolicy2(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			       struct lsa_OpenPolicy2 *r)
{
	NTSTATUS status;
	struct lsa_policy_state *state;
	struct dcesrv_handle *handle;

	ZERO_STRUCTP(r->out.handle);

	status = lsa_get_policy_state(dce_call, mem_ctx, &state);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	handle = dcesrv_handle_new(dce_call->context, LSA_HANDLE_POLICY);
	if (!handle) {
		return NT_STATUS_NO_MEMORY;
	}

	handle->data = talloc_steal(handle, state);

	state->access_mask = r->in.access_mask;
	state->handle = handle;
	*r->out.handle = handle->wire_handle;

	/* note that we have completely ignored the attr element of
	   the OpenPolicy. As far as I can tell, this is what w2k3
	   does */

	return NT_STATUS_OK;
}

/* 
  lsa_OpenPolicy
  a wrapper around lsa_OpenPolicy2
*/
static NTSTATUS lsa_OpenPolicy(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				struct lsa_OpenPolicy *r)
{
	struct lsa_OpenPolicy2 r2;

	r2.in.system_name = NULL;
	r2.in.attr = r->in.attr;
	r2.in.access_mask = r->in.access_mask;
	r2.out.handle = r->out.handle;

	return lsa_OpenPolicy2(dce_call, mem_ctx, &r2);
}




/*
  fill in the AccountDomain info
*/
static NTSTATUS lsa_info_AccountDomain(struct lsa_policy_state *state, TALLOC_CTX *mem_ctx,
				       struct lsa_DomainInfo *info)
{
	info->name.string = state->domain_name;
	info->sid         = state->domain_sid;

	return NT_STATUS_OK;
}

/*
  fill in the DNS domain info
*/
static NTSTATUS lsa_info_DNS(struct lsa_policy_state *state, TALLOC_CTX *mem_ctx,
			     struct lsa_DnsDomainInfo *info)
{
	const char * const attrs[] = { "dnsDomain", "objectGUID", "objectSid", NULL };
	int ret;
	struct ldb_message **res;

	ret = gendb_search_dn(state->sam_ldb, mem_ctx, state->domain_dn, &res, attrs);
	if (ret != 1) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	info->name.string = state->domain_name;
	info->sid         = state->domain_sid;
	info->dns_domain.string = samdb_result_string(res[0],           "dnsDomain", NULL);
	info->dns_forest.string = samdb_result_string(res[0],           "dnsDomain", NULL);
	info->domain_guid       = samdb_result_guid(res[0],             "objectGUID");

	return NT_STATUS_OK;
}

/* 
  lsa_QueryInfoPolicy2
*/
static NTSTATUS lsa_QueryInfoPolicy2(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				     struct lsa_QueryInfoPolicy2 *r)
{
	struct lsa_policy_state *state;
	struct dcesrv_handle *h;

	r->out.info = NULL;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	r->out.info = talloc(mem_ctx, union lsa_PolicyInformation);
	if (!r->out.info) {
		return NT_STATUS_NO_MEMORY;
	}

	ZERO_STRUCTP(r->out.info);

	switch (r->in.level) {
	case LSA_POLICY_INFO_DOMAIN:
	case LSA_POLICY_INFO_ACCOUNT_DOMAIN:
		return lsa_info_AccountDomain(state, mem_ctx, &r->out.info->account_domain);

	case LSA_POLICY_INFO_DNS:
		return lsa_info_DNS(state, mem_ctx, &r->out.info->dns);
	}

	return NT_STATUS_INVALID_INFO_CLASS;
}

/* 
  lsa_QueryInfoPolicy 
*/
static NTSTATUS lsa_QueryInfoPolicy(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				    struct lsa_QueryInfoPolicy *r)
{
	struct lsa_QueryInfoPolicy2 r2;
	NTSTATUS status;

	r2.in.handle = r->in.handle;
	r2.in.level = r->in.level;
	
	status = lsa_QueryInfoPolicy2(dce_call, mem_ctx, &r2);

	r->out.info = r2.out.info;

	return status;
}

/* 
  lsa_SetInfoPolicy 
*/
static NTSTATUS lsa_SetInfoPolicy(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				  struct lsa_SetInfoPolicy *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_ClearAuditLog 
*/
static NTSTATUS lsa_ClearAuditLog(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				  struct lsa_ClearAuditLog *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CreateAccount 
*/
static NTSTATUS lsa_CreateAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				  struct lsa_CreateAccount *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_EnumAccounts 
*/
static NTSTATUS lsa_EnumAccounts(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				 struct lsa_EnumAccounts *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;
	int ret, i;
	struct ldb_message **res;
	const char * const attrs[] = { "objectSid", NULL};
	uint32_t count;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	ret = gendb_search(state->sam_ldb, mem_ctx, state->builtin_dn, &res, attrs, 
			   "(|(privilege=*)(objectSid=*))");
	if (ret <= 0) {
		return NT_STATUS_NO_SUCH_USER;
	}

	if (*r->in.resume_handle >= ret) {
		return NT_STATUS_NO_MORE_ENTRIES;
	}

	count = ret - *r->in.resume_handle;
	if (count > r->in.num_entries) {
		count = r->in.num_entries;
	}

	if (count == 0) {
		return NT_STATUS_NO_MORE_ENTRIES;
	}

	r->out.sids->sids = talloc_array(r->out.sids, struct lsa_SidPtr, count);
	if (r->out.sids->sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0;i<count;i++) {
		r->out.sids->sids[i].sid = 
			samdb_result_dom_sid(r->out.sids->sids, 
					     res[i + *r->in.resume_handle],
					     "objectSid");
		NT_STATUS_HAVE_NO_MEMORY(r->out.sids->sids[i].sid);
	}

	r->out.sids->num_sids = count;
	*r->out.resume_handle = count + *r->in.resume_handle;

	return NT_STATUS_OK;
	
}


/*
  lsa_CreateTrustedDomainEx2
*/
static NTSTATUS lsa_CreateTrustedDomainEx2(struct dcesrv_call_state *dce_call,
					   TALLOC_CTX *mem_ctx,
					   struct lsa_CreateTrustedDomainEx2 *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lsa_CreateTrustedDomainEx
*/
static NTSTATUS lsa_CreateTrustedDomainEx(struct dcesrv_call_state *dce_call,
					  TALLOC_CTX *mem_ctx,
					  struct lsa_CreateTrustedDomainEx *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/* 
  lsa_CreateTrustedDomain 
*/
static NTSTATUS lsa_CreateTrustedDomain(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
					struct lsa_CreateTrustedDomain *r)
{
	struct dcesrv_handle *policy_handle;
	struct lsa_policy_state *policy_state;
	struct lsa_trusted_domain_state *trusted_domain_state;
	struct dcesrv_handle *handle;
	struct ldb_message **msgs, *msg;
	const char *attrs[] = {
		NULL
	};
	const char *name;
	int ret;

	DCESRV_PULL_HANDLE(policy_handle, r->in.handle, LSA_HANDLE_POLICY);
	ZERO_STRUCTP(r->out.trustdom_handle);
	
	policy_state = policy_handle->data;

	if (!r->in.info->name.string) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	name = r->in.info->name.string;
	
	trusted_domain_state = talloc(mem_ctx, struct lsa_trusted_domain_state);
	if (!trusted_domain_state) {
		return NT_STATUS_NO_MEMORY;
	}
	trusted_domain_state->policy = policy_state;

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the trusted_domain record */
	ret = gendb_search(trusted_domain_state->policy->sam_ldb,
			   mem_ctx, policy_state->system_dn, &msgs, attrs,
			   "(&(cn=%s)(objectclass=trustedDomain))", 
			   ldb_binary_encode_string(mem_ctx, r->in.info->name.string));
	if (ret > 0) {
		return NT_STATUS_OBJECT_NAME_COLLISION;
	}
	
	if (ret < 0 || ret > 1) {
		DEBUG(0,("Found %d records matching DN %s\n", ret,
			 ldb_dn_linearize(mem_ctx, policy_state->system_dn)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}
	
	msg->dn = ldb_dn_build_child(mem_ctx, "cn",
				     r->in.info->name.string, 
				     policy_state->system_dn);
	if (!msg->dn) {
		return NT_STATUS_NO_MEMORY;
	}
	
	samdb_msg_add_string(trusted_domain_state->policy->sam_ldb, mem_ctx, msg, "cn", name);
	samdb_msg_add_string(trusted_domain_state->policy->sam_ldb, mem_ctx, msg, "flatname", name);

	if (r->in.info->sid) {
		const char *sid_string = dom_sid_string(mem_ctx, r->in.info->sid);
		if (!sid_string) {
			return NT_STATUS_NO_MEMORY;
		}
			
		samdb_msg_add_string(trusted_domain_state->policy->sam_ldb, mem_ctx, msg, "securityIdentifier", sid_string);
	}

	samdb_msg_add_string(trusted_domain_state->policy->sam_ldb, mem_ctx, msg, "objectClass", "trustedDomain");
	
	trusted_domain_state->trusted_domain_dn = talloc_reference(trusted_domain_state, msg->dn);

	/* create the trusted_domain */
	ret = samdb_add(trusted_domain_state->policy->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		DEBUG(0,("Failed to create trusted_domain record %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	handle = dcesrv_handle_new(dce_call->context, LSA_HANDLE_TRUSTED_DOMAIN);
	if (!handle) {
		return NT_STATUS_NO_MEMORY;
	}
	
	handle->data = talloc_steal(handle, trusted_domain_state);
	
	trusted_domain_state->access_mask = r->in.access_mask;
	trusted_domain_state->policy = talloc_reference(trusted_domain_state, policy_state);
	
	*r->out.trustdom_handle = handle->wire_handle;
	
	return NT_STATUS_OK;
}

/* 
  lsa_OpenTrustedDomain
*/
static NTSTATUS lsa_OpenTrustedDomain(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				      struct lsa_OpenTrustedDomain *r)
{
	struct dcesrv_handle *policy_handle;
	
	struct lsa_policy_state *policy_state;
	struct lsa_trusted_domain_state *trusted_domain_state;
	struct dcesrv_handle *handle;
	struct ldb_message **msgs;
	const char *attrs[] = {
		NULL
	};

	const char *sid_string;
	int ret;

	DCESRV_PULL_HANDLE(policy_handle, r->in.handle, LSA_HANDLE_POLICY);
	ZERO_STRUCTP(r->out.trustdom_handle);
	policy_state = policy_handle->data;

	trusted_domain_state = talloc(mem_ctx, struct lsa_trusted_domain_state);
	if (!trusted_domain_state) {
		return NT_STATUS_NO_MEMORY;
	}
	trusted_domain_state->policy = policy_state;

	sid_string = dom_sid_string(mem_ctx, r->in.sid);
	if (!sid_string) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the trusted_domain record */
	ret = gendb_search(trusted_domain_state->policy->sam_ldb,
			   mem_ctx, policy_state->system_dn, &msgs, attrs,
			   "(&(securityIdentifier=%s)(objectclass=trustedDomain))", 
			   sid_string);
	if (ret == 0) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}
	
	if (ret != 1) {
		DEBUG(0,("Found %d records matching DN %s\n", ret,
			 ldb_dn_linearize(mem_ctx, policy_state->system_dn)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	trusted_domain_state->trusted_domain_dn = talloc_reference(trusted_domain_state, msgs[0]->dn);
	
	handle = dcesrv_handle_new(dce_call->context, LSA_HANDLE_TRUSTED_DOMAIN);
	if (!handle) {
		return NT_STATUS_NO_MEMORY;
	}
	
	handle->data = talloc_steal(handle, trusted_domain_state);
	
	trusted_domain_state->access_mask = r->in.access_mask;
	trusted_domain_state->policy = talloc_reference(trusted_domain_state, policy_state);
	
	*r->out.trustdom_handle = handle->wire_handle;
	
	return NT_STATUS_OK;
}


/*
  lsa_OpenTrustedDomainByName
*/
static NTSTATUS lsa_OpenTrustedDomainByName(struct dcesrv_call_state *dce_call,
					    TALLOC_CTX *mem_ctx,
					    struct lsa_OpenTrustedDomainByName *r)
{
	struct dcesrv_handle *policy_handle;
	
	struct lsa_policy_state *policy_state;
	struct lsa_trusted_domain_state *trusted_domain_state;
	struct dcesrv_handle *handle;
	struct ldb_message **msgs;
	const char *attrs[] = {
		NULL
	};

	int ret;

	DCESRV_PULL_HANDLE(policy_handle, r->in.handle, LSA_HANDLE_POLICY);
	ZERO_STRUCTP(r->out.trustdom_handle);
	policy_state = policy_handle->data;

	if (!r->in.name.string) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	
	trusted_domain_state = talloc(mem_ctx, struct lsa_trusted_domain_state);
	if (!trusted_domain_state) {
		return NT_STATUS_NO_MEMORY;
	}
	trusted_domain_state->policy = policy_state;

	/* search for the trusted_domain record */
	ret = gendb_search(trusted_domain_state->policy->sam_ldb,
			   mem_ctx, policy_state->system_dn, &msgs, attrs,
			   "(&(flatname=%s)(objectclass=trustedDomain))", 
			   ldb_binary_encode_string(mem_ctx, r->in.name.string));
	if (ret == 0) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}
	
	if (ret != 1) {
		DEBUG(0,("Found %d records matching DN %s\n", ret,
			 ldb_dn_linearize(mem_ctx, policy_state->system_dn)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	trusted_domain_state->trusted_domain_dn = talloc_reference(trusted_domain_state, msgs[0]->dn);
	
	handle = dcesrv_handle_new(dce_call->context, LSA_HANDLE_TRUSTED_DOMAIN);
	if (!handle) {
		return NT_STATUS_NO_MEMORY;
	}
	
	handle->data = talloc_steal(handle, trusted_domain_state);
	
	trusted_domain_state->access_mask = r->in.access_mask;
	trusted_domain_state->policy = talloc_reference(trusted_domain_state, policy_state);
	
	*r->out.trustdom_handle = handle->wire_handle;
	
	return NT_STATUS_OK;
}


/* 
  lsa_QueryTrustedDomainInfoBySid
*/
static NTSTATUS lsa_QueryTrustedDomainInfoBySid(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
						struct lsa_QueryTrustedDomainInfoBySid *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_SetTrustDomainInfo
*/
static NTSTATUS lsa_SetTrustDomainInfo(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_SetTrustDomainInfo *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_DeleteTrustDomain
*/
static NTSTATUS lsa_DeleteTrustDomain(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				      struct lsa_DeleteTrustDomain *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_QueryTrustedDomainInfo
*/
static NTSTATUS lsa_QueryTrustedDomainInfo(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
					   struct lsa_QueryTrustedDomainInfo *r)
{
	struct dcesrv_handle *h;
	struct lsa_trusted_domain_state *trusted_domain_state;
	struct ldb_message *msg;
	int ret;
	struct ldb_message **res;
	const char *attrs[] = {
		"cn",
		"flatname",
		"posixOffset",
		"securityIdentifier",
		NULL
	};

	DCESRV_PULL_HANDLE(h, r->in.trustdom_handle, LSA_HANDLE_TRUSTED_DOMAIN);

	trusted_domain_state = h->data;

	/* pull all the user attributes */
	ret = gendb_search_dn(trusted_domain_state->policy->sam_ldb, mem_ctx,
			      trusted_domain_state->trusted_domain_dn, &res, attrs);
	if (ret != 1) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}
	msg = res[0];
	
	r->out.info = talloc(mem_ctx, union lsa_TrustedDomainInfo);
	if (!r->out.info) {
		return NT_STATUS_NO_MEMORY;
	}
	switch (r->in.level) {
	case LSA_TRUSTED_DOMAIN_INFO_NAME:
		r->out.info->name.netbios_name.string
			= samdb_result_string(msg, "flatname", NULL);					   
		break;
	case LSA_TRUSTED_DOMAIN_INFO_POSIX_OFFSET:
		r->out.info->posix_offset.posix_offset
			= samdb_result_uint(msg, "posixOffset", 0);					   
		break;
	default:
		/* oops, we don't want to return the info after all */
		talloc_free(r->out.info);
		r->out.info = NULL;
		return NT_STATUS_INVALID_INFO_CLASS;
	}

	return NT_STATUS_OK;
}


/* 
  lsa_SetInformationTrustedDomain
*/
static NTSTATUS lsa_SetInformationTrustedDomain(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_SetInformationTrustedDomain *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/*
  lsa_QueryTrustedDomainInfoByName
*/
static NTSTATUS lsa_QueryTrustedDomainInfoByName(struct dcesrv_call_state *dce_call,
						 TALLOC_CTX *mem_ctx,
						 struct lsa_QueryTrustedDomainInfoByName *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lsa_SetTrustedDomainInfoByName
*/
static NTSTATUS lsa_SetTrustedDomainInfoByName(struct dcesrv_call_state *dce_call,
					       TALLOC_CTX *mem_ctx,
					       struct lsa_SetTrustedDomainInfoByName *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lsa_EnumTrustedDomainsEx
*/
static NTSTATUS lsa_EnumTrustedDomainsEx(struct dcesrv_call_state *dce_call,
					 TALLOC_CTX *mem_ctx,
					 struct lsa_EnumTrustedDomainsEx *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lsa_CloseTrustedDomainEx
*/
static NTSTATUS lsa_CloseTrustedDomainEx(struct dcesrv_call_state *dce_call,
					 TALLOC_CTX *mem_ctx,
					 struct lsa_CloseTrustedDomainEx *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/*
  comparison function for sorting lsa_DomainInformation array
*/
static int compare_DomainInformation(struct lsa_DomainInformation *e1, struct lsa_DomainInformation *e2)
{
	return strcasecmp(e1->name.string, e2->name.string);
}

/* 
  lsa_EnumTrustDom 
*/
static NTSTATUS lsa_EnumTrustDom(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				 struct lsa_EnumTrustDom *r)
{
	struct dcesrv_handle *policy_handle;
	struct lsa_DomainInformation *entries;
	struct lsa_policy_state *policy_state;
	struct ldb_message **domains;
	const char *attrs[] = {
		"flatname", 
		"securityIdentifier",
		NULL
	};


	int count, i;

	*r->out.resume_handle = 0;

	r->out.domains->domains = NULL;
	r->out.domains->count = 0;

	DCESRV_PULL_HANDLE(policy_handle, r->in.handle, LSA_HANDLE_POLICY);

	policy_state = policy_handle->data;

	/* search for all users in this domain. This could possibly be cached and 
	   resumed based on resume_key */
	count = gendb_search(policy_state->sam_ldb, mem_ctx, policy_state->system_dn, &domains, attrs, 
			     "objectclass=trustedDomain");
	if (count == -1) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}
	if (count == 0 || r->in.max_size == 0) {
		return NT_STATUS_OK;
	}

	/* convert to lsa_DomainInformation format */
	entries = talloc_array(mem_ctx, struct lsa_DomainInformation, count);
	if (!entries) {
		return NT_STATUS_NO_MEMORY;
	}
	for (i=0;i<count;i++) {
		entries[i].sid = samdb_result_dom_sid(mem_ctx, domains[i], "securityIdentifier");
		entries[i].name.string = samdb_result_string(domains[i], "flatname", NULL);
	}

	/* sort the results by name */
	qsort(entries, count, sizeof(struct lsa_DomainInformation), 
	      (comparison_fn_t)compare_DomainInformation);

	if (*r->in.resume_handle >= count) {
		*r->out.resume_handle = -1;

		return NT_STATUS_NO_MORE_ENTRIES;
	}

	/* return the rest, limit by max_size. Note that we 
	   use the w2k3 element size value of 60 */
	r->out.domains->count = count - *r->in.resume_handle;
	r->out.domains->count = MIN(r->out.domains->count, 
				 1+(r->in.max_size/LSA_ENUM_TRUST_DOMAIN_MULTIPLIER));

	r->out.domains->domains = entries + *r->in.resume_handle;
	r->out.domains->count = r->out.domains->count;

	if (r->out.domains->count < count - *r->in.resume_handle) {
		*r->out.resume_handle = *r->in.resume_handle + r->out.domains->count;
		return STATUS_MORE_ENTRIES;
	}

	return NT_STATUS_OK;
}


/*
  return the authority name and authority sid, given a sid
*/
static NTSTATUS lsa_authority_name(struct lsa_policy_state *state,
				   TALLOC_CTX *mem_ctx, struct dom_sid *sid,
				   const char **authority_name,
				   struct dom_sid **authority_sid)
{
	if (dom_sid_in_domain(state->domain_sid, sid)) {
		*authority_name = state->domain_name;
		*authority_sid = state->domain_sid;
		return NT_STATUS_OK;
	}

	if (dom_sid_in_domain(state->builtin_sid, sid)) {
		*authority_name = "BUILTIN";
		*authority_sid = state->builtin_sid;
		return NT_STATUS_OK;
	}

	*authority_sid = dom_sid_dup(mem_ctx, sid);
	if (*authority_sid == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	(*authority_sid)->num_auths = 0;
	*authority_name = dom_sid_string(mem_ctx, *authority_sid);
	if (*authority_name == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	return NT_STATUS_OK;
}

/*
  add to the lsa_RefDomainList for LookupSids and LookupNames
*/
static NTSTATUS lsa_authority_list(struct lsa_policy_state *state, TALLOC_CTX *mem_ctx, 
				   struct dom_sid *sid, 
				   struct lsa_RefDomainList *domains,
				   uint32_t *sid_index)
{
	NTSTATUS status;
	const char *authority_name;
	struct dom_sid *authority_sid;
	int i;

	/* work out the authority name */
	status = lsa_authority_name(state, mem_ctx, sid, 
				    &authority_name, &authority_sid);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}
	
	/* see if we've already done this authority name */
	for (i=0;i<domains->count;i++) {
		if (strcmp(authority_name, domains->domains[i].name.string) == 0) {
			*sid_index = i;
			return NT_STATUS_OK;
		}
	}

	domains->domains = talloc_realloc(domains, 
					  domains->domains,
					  struct lsa_TrustInformation,
					  domains->count+1);
	if (domains->domains == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	domains->domains[i].name.string = authority_name;
	domains->domains[i].sid         = authority_sid;
	domains->count++;
	*sid_index = i;
	
	return NT_STATUS_OK;
}

/*
  lookup a name for 1 SID
*/
static NTSTATUS lsa_lookup_sid(struct lsa_policy_state *state, TALLOC_CTX *mem_ctx,
			       struct dom_sid *sid, const char *sid_str,
			       const char **name, uint32_t *atype)
{
	int ret;
	struct ldb_message **res;
	const char * const attrs[] = { "sAMAccountName", "sAMAccountType", "name", NULL};
	NTSTATUS status;

	ret = gendb_search(state->sam_ldb, mem_ctx, NULL, &res, attrs, 
			   "objectSid=%s", ldap_encode_ndr_dom_sid(mem_ctx, sid));
	if (ret == 1) {
		*name = ldb_msg_find_string(res[0], "sAMAccountName", NULL);
		if (!*name) {
			*name = ldb_msg_find_string(res[0], "name", NULL);
			if (!*name) {
				*name = talloc_strdup(mem_ctx, sid_str);
				NT_STATUS_HAVE_NO_MEMORY(*name);
			}
		}

		*atype = samdb_result_uint(res[0], "sAMAccountType", 0);

		return NT_STATUS_OK;
	}

	status = sidmap_allocated_sid_lookup(state->sidmap, mem_ctx, sid, name, atype);

	return status;
}


/*
  lsa_LookupSids3
*/
static NTSTATUS lsa_LookupSids3(struct dcesrv_call_state *dce_call,
				TALLOC_CTX *mem_ctx,
				struct lsa_LookupSids3 *r)
{
	struct lsa_policy_state *state;
	int i;
	NTSTATUS status = NT_STATUS_OK;

	r->out.domains = NULL;

	status = lsa_get_policy_state(dce_call, mem_ctx, &state);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	r->out.domains = talloc_zero(mem_ctx,  struct lsa_RefDomainList);
	if (r->out.domains == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	r->out.names = talloc_zero(mem_ctx,  struct lsa_TransNameArray2);
	if (r->out.names == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	*r->out.count = 0;

	r->out.names->names = talloc_array(r->out.names, struct lsa_TranslatedName2, 
					     r->in.sids->num_sids);
	if (r->out.names->names == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0;i<r->in.sids->num_sids;i++) {
		struct dom_sid *sid = r->in.sids->sids[i].sid;
		char *sid_str = dom_sid_string(mem_ctx, sid);
		const char *name;
		uint32_t atype, rtype, sid_index;
		NTSTATUS status2;

		r->out.names->count++;
		(*r->out.count)++;

		r->out.names->names[i].sid_type    = SID_NAME_UNKNOWN;
		r->out.names->names[i].name.string = sid_str;
		r->out.names->names[i].sid_index   = 0xFFFFFFFF;
		r->out.names->names[i].unknown     = 0;

		if (sid_str == NULL) {
			r->out.names->names[i].name.string = "(SIDERROR)";
			status = STATUS_SOME_UNMAPPED;
			continue;
		}

		/* work out the authority name */
		status2 = lsa_authority_list(state, mem_ctx, sid, r->out.domains, &sid_index);
		if (!NT_STATUS_IS_OK(status2)) {
			return status2;
		}

		status2 = lsa_lookup_sid(state, mem_ctx, sid, sid_str, 
					 &name, &atype);
		if (!NT_STATUS_IS_OK(status2)) {
			status = STATUS_SOME_UNMAPPED;
			continue;
		}

		rtype = samdb_atype_map(atype);
		if (rtype == SID_NAME_UNKNOWN) {
			status = STATUS_SOME_UNMAPPED;
			continue;
		}

		r->out.names->names[i].sid_type    = rtype;
		r->out.names->names[i].name.string = name;
		r->out.names->names[i].sid_index   = sid_index;
		r->out.names->names[i].unknown     = 0;
	}
	
	return status;
}


/*
  lsa_LookupSids2
*/
static NTSTATUS lsa_LookupSids2(struct dcesrv_call_state *dce_call,
				TALLOC_CTX *mem_ctx,
				struct lsa_LookupSids2 *r)
{
	struct lsa_LookupSids3 r3;
	NTSTATUS status;

	r3.in.sids     = r->in.sids;
	r3.in.names    = r->in.names;
	r3.in.level    = r->in.level;
	r3.in.count    = r->in.count;
	r3.in.unknown1 = r->in.unknown1;
	r3.in.unknown2 = r->in.unknown2;
	r3.out.count   = r->out.count;
	r3.out.names   = r->out.names;

	status = lsa_LookupSids3(dce_call, mem_ctx, &r3);
	if (dce_call->fault_code != 0) {
		return status;
	}

	r->out.domains = r3.out.domains;
	r->out.names   = r3.out.names;
	r->out.count   = r3.out.count;

	return status;
}


/* 
  lsa_LookupSids 
*/
static NTSTATUS lsa_LookupSids(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			       struct lsa_LookupSids *r)
{
	struct lsa_LookupSids3 r3;
	NTSTATUS status;
	int i;

	r3.in.sids     = r->in.sids;
	r3.in.names    = NULL;
	r3.in.level    = r->in.level;
	r3.in.count    = r->in.count;
	r3.in.unknown1 = 0;
	r3.in.unknown2 = 0;
	r3.out.count   = r->out.count;
	r3.out.names   = NULL;

	status = lsa_LookupSids3(dce_call, mem_ctx, &r3);
	if (dce_call->fault_code != 0) {
		return status;
	}

	r->out.domains = r3.out.domains;
	if (!r3.out.names) {
		r->out.names = NULL;
		return status;
	}

	r->out.names = talloc(mem_ctx, struct lsa_TransNameArray);
	if (r->out.names == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	r->out.names->count = r3.out.names->count;
	r->out.names->names = talloc_array(r->out.names, struct lsa_TranslatedName, 
					     r->out.names->count);
	if (r->out.names->names == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	for (i=0;i<r->out.names->count;i++) {
		r->out.names->names[i].sid_type    = r3.out.names->names[i].sid_type;
		r->out.names->names[i].name.string = r3.out.names->names[i].name.string;
		r->out.names->names[i].sid_index   = r3.out.names->names[i].sid_index;
	}

	return status;
}


/* 
  lsa_OpenAccount 
*/
static NTSTATUS lsa_OpenAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				struct lsa_OpenAccount *r)
{
	struct dcesrv_handle *h, *ah;
	struct lsa_policy_state *state;
	struct lsa_account_state *astate;

	ZERO_STRUCTP(r->out.acct_handle);

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	astate = talloc(dce_call->conn, struct lsa_account_state);
	if (astate == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	astate->account_sid = dom_sid_dup(astate, r->in.sid);
	if (astate->account_sid == NULL) {
		talloc_free(astate);
		return NT_STATUS_NO_MEMORY;
	}
	
	/* check it really exists */
	astate->account_dn = samdb_search_dn(state->sam_ldb, astate,
					     NULL, "(&(objectSid=%s)(objectClass=group))", 
					     ldap_encode_ndr_dom_sid(mem_ctx, astate->account_sid));
	if (astate->account_dn == NULL) {
		talloc_free(astate);
		return NT_STATUS_NO_SUCH_USER;
	}
	
	astate->policy = talloc_reference(astate, state);
	astate->access_mask = r->in.access_mask;

	ah = dcesrv_handle_new(dce_call->context, LSA_HANDLE_ACCOUNT);
	if (!ah) {
		talloc_free(astate);
		return NT_STATUS_NO_MEMORY;
	}

	ah->data = talloc_steal(ah, astate);

	*r->out.acct_handle = ah->wire_handle;

	return NT_STATUS_OK;
}


/* 
  lsa_EnumPrivsAccount 
*/
static NTSTATUS lsa_EnumPrivsAccount(struct dcesrv_call_state *dce_call, 
				     TALLOC_CTX *mem_ctx,
				     struct lsa_EnumPrivsAccount *r)
{
	struct dcesrv_handle *h;
	struct lsa_account_state *astate;
	int ret, i;
	struct ldb_message **res;
	const char * const attrs[] = { "privilege", NULL};
	struct ldb_message_element *el;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_ACCOUNT);

	astate = h->data;

	r->out.privs = talloc(mem_ctx, struct lsa_PrivilegeSet);
	r->out.privs->count = 0;
	r->out.privs->unknown = 0;
	r->out.privs->set = NULL;

	ret = gendb_search_dn(astate->policy->sam_ldb, mem_ctx,
			      astate->account_dn, &res, attrs);
	if (ret != 1) {
		return NT_STATUS_OK;
	}

	el = ldb_msg_find_element(res[0], "privilege");
	if (el == NULL || el->num_values == 0) {
		return NT_STATUS_OK;
	}

	r->out.privs->set = talloc_array(r->out.privs, 
					   struct lsa_LUIDAttribute, el->num_values);
	if (r->out.privs->set == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0;i<el->num_values;i++) {
		int id = sec_privilege_id((const char *)el->values[i].data);
		if (id == -1) {
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
		r->out.privs->set[i].attribute = 0;
		r->out.privs->set[i].luid.low = id;
		r->out.privs->set[i].luid.high = 0;
	}

	r->out.privs->count = el->num_values;

	return NT_STATUS_OK;
}

/* 
  lsa_EnumAccountRights 
*/
static NTSTATUS lsa_EnumAccountRights(struct dcesrv_call_state *dce_call, 
				      TALLOC_CTX *mem_ctx,
				      struct lsa_EnumAccountRights *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;
	int ret, i;
	struct ldb_message **res;
	const char * const attrs[] = { "privilege", NULL};
	const char *sidstr;
	struct ldb_message_element *el;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	sidstr = ldap_encode_ndr_dom_sid(mem_ctx, r->in.sid);
	if (sidstr == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	ret = gendb_search(state->sam_ldb, mem_ctx, NULL, &res, attrs, 
			   "objectSid=%s", sidstr);
	if (ret != 1) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	el = ldb_msg_find_element(res[0], "privilege");
	if (el == NULL || el->num_values == 0) {
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	r->out.rights->count = el->num_values;
	r->out.rights->names = talloc_array(r->out.rights, 
					    struct lsa_StringLarge, r->out.rights->count);
	if (r->out.rights->names == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0;i<el->num_values;i++) {
		r->out.rights->names[i].string = (const char *)el->values[i].data;
	}

	return NT_STATUS_OK;
}



/* 
  helper for lsa_AddAccountRights and lsa_RemoveAccountRights
*/
static NTSTATUS lsa_AddRemoveAccountRights(struct dcesrv_call_state *dce_call, 
					   TALLOC_CTX *mem_ctx,
					   struct lsa_policy_state *state,
					   int ldb_flag,
					   struct dom_sid *sid,
					   const struct lsa_RightSet *rights)
{
	const char *sidstr;
	struct ldb_message *msg;
	struct ldb_message_element el;
	int i, ret;
	struct lsa_EnumAccountRights r2;

	sidstr = ldap_encode_ndr_dom_sid(mem_ctx, sid);
	if (sidstr == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	msg->dn = samdb_search_dn(state->sam_ldb, mem_ctx, NULL, "objectSid=%s", sidstr);
	if (msg->dn == NULL) {
		return NT_STATUS_NO_SUCH_USER;
	}

	if (ldb_msg_add_empty(msg, "privilege", ldb_flag)) {
		return NT_STATUS_NO_MEMORY;
	}

	if (ldb_flag == LDB_FLAG_MOD_ADD) {
		NTSTATUS status;

		r2.in.handle = &state->handle->wire_handle;
		r2.in.sid = sid;
		r2.out.rights = talloc(mem_ctx, struct lsa_RightSet);

		status = lsa_EnumAccountRights(dce_call, mem_ctx, &r2);
		if (!NT_STATUS_IS_OK(status)) {
			ZERO_STRUCTP(r2.out.rights);
		}
	}

	el.num_values = 0;
	el.values = talloc_array(mem_ctx, struct ldb_val, rights->count);
	if (el.values == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	for (i=0;i<rights->count;i++) {
		if (sec_privilege_id(rights->names[i].string) == -1) {
			return NT_STATUS_NO_SUCH_PRIVILEGE;
		}

		if (ldb_flag == LDB_FLAG_MOD_ADD) {
			int j;
			for (j=0;j<r2.out.rights->count;j++) {
				if (strcasecmp_m(r2.out.rights->names[j].string, 
					       rights->names[i].string) == 0) {
					break;
				}
			}
			if (j != r2.out.rights->count) continue;
		}


		el.values[el.num_values].length = strlen(rights->names[i].string);
		el.values[el.num_values].data = (uint8_t *)talloc_strdup(mem_ctx, rights->names[i].string);
		if (el.values[el.num_values].data == NULL) {
			return NT_STATUS_NO_MEMORY;
		}
		el.num_values++;
	}

	if (el.num_values == 0) {
		return NT_STATUS_OK;
	}

	ret = samdb_modify(state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		if (ldb_flag == LDB_FLAG_MOD_DELETE) {
			return NT_STATUS_OBJECT_NAME_NOT_FOUND;
		}
		return NT_STATUS_UNEXPECTED_IO_ERROR;
	}

	return NT_STATUS_OK;
}

/* 
  lsa_AddPrivilegesToAccount
*/
static NTSTATUS lsa_AddPrivilegesToAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
					   struct lsa_AddPrivilegesToAccount *r)
{
	struct lsa_RightSet rights;
	struct dcesrv_handle *h;
	struct lsa_account_state *astate;
	int i;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_ACCOUNT);

	astate = h->data;

	rights.count = r->in.privs->count;
	rights.names = talloc_array(mem_ctx, struct lsa_StringLarge, rights.count);
	if (rights.names == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	for (i=0;i<rights.count;i++) {
		int id = r->in.privs->set[i].luid.low;
		if (r->in.privs->set[i].luid.high) {
			return NT_STATUS_NO_SUCH_PRIVILEGE;
		}
		rights.names[i].string = sec_privilege_name(id);
		if (rights.names[i].string == NULL) {
			return NT_STATUS_NO_SUCH_PRIVILEGE;
		}
	}

	return lsa_AddRemoveAccountRights(dce_call, mem_ctx, astate->policy, 
					  LDB_FLAG_MOD_ADD, astate->account_sid,
					  &rights);
}


/* 
  lsa_RemovePrivilegesFromAccount
*/
static NTSTATUS lsa_RemovePrivilegesFromAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
						struct lsa_RemovePrivilegesFromAccount *r)
{
	struct lsa_RightSet *rights;
	struct dcesrv_handle *h;
	struct lsa_account_state *astate;
	int i;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_ACCOUNT);

	astate = h->data;

	rights = talloc(mem_ctx, struct lsa_RightSet);

	if (r->in.remove_all == 1 && 
	    r->in.privs == NULL) {
		struct lsa_EnumAccountRights r2;
		NTSTATUS status;

		r2.in.handle = &astate->policy->handle->wire_handle;
		r2.in.sid = astate->account_sid;
		r2.out.rights = rights;

		status = lsa_EnumAccountRights(dce_call, mem_ctx, &r2);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}

		return lsa_AddRemoveAccountRights(dce_call, mem_ctx, astate->policy, 
						  LDB_FLAG_MOD_DELETE, astate->account_sid,
						  r2.out.rights);
	}

	if (r->in.remove_all != 0) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	rights->count = r->in.privs->count;
	rights->names = talloc_array(mem_ctx, struct lsa_StringLarge, rights->count);
	if (rights->names == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	for (i=0;i<rights->count;i++) {
		int id = r->in.privs->set[i].luid.low;
		if (r->in.privs->set[i].luid.high) {
			return NT_STATUS_NO_SUCH_PRIVILEGE;
		}
		rights->names[i].string = sec_privilege_name(id);
		if (rights->names[i].string == NULL) {
			return NT_STATUS_NO_SUCH_PRIVILEGE;
		}
	}

	return lsa_AddRemoveAccountRights(dce_call, mem_ctx, astate->policy, 
					  LDB_FLAG_MOD_DELETE, astate->account_sid,
					  rights);
}


/* 
  lsa_GetQuotasForAccount
*/
static NTSTATUS lsa_GetQuotasForAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_GetQuotasForAccount *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_SetQuotasForAccount
*/
static NTSTATUS lsa_SetQuotasForAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_SetQuotasForAccount *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_GetSystemAccessAccount
*/
static NTSTATUS lsa_GetSystemAccessAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_GetSystemAccessAccount *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_SetSystemAccessAccount
*/
static NTSTATUS lsa_SetSystemAccessAccount(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_SetSystemAccessAccount *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CreateSecret 
*/
static NTSTATUS lsa_CreateSecret(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				 struct lsa_CreateSecret *r)
{
	struct dcesrv_handle *policy_handle;
	struct lsa_policy_state *policy_state;
	struct lsa_secret_state *secret_state;
	struct dcesrv_handle *handle;
	struct ldb_message **msgs, *msg;
	const char *attrs[] = {
		NULL
	};

	const char *name;

	int ret;

	DCESRV_PULL_HANDLE(policy_handle, r->in.handle, LSA_HANDLE_POLICY);
	ZERO_STRUCTP(r->out.sec_handle);
	
	policy_state = policy_handle->data;

	if (!r->in.name.string) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	
	secret_state = talloc(mem_ctx, struct lsa_secret_state);
	if (!secret_state) {
		return NT_STATUS_NO_MEMORY;
	}
	secret_state->policy = policy_state;

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	if (strncmp("G$", r->in.name.string, 2) == 0) {
		const char *name2;
		name = &r->in.name.string[2];
		secret_state->sam_ldb = talloc_reference(secret_state, policy_state->sam_ldb);
		secret_state->global = True;

		if (strlen(name) < 1) {
			return NT_STATUS_INVALID_PARAMETER;
		}

		name2 = talloc_asprintf(mem_ctx, "%s Secret", ldb_binary_encode_string(mem_ctx, name));
		/* search for the secret record */
		ret = gendb_search(secret_state->sam_ldb,
				   mem_ctx, policy_state->system_dn, &msgs, attrs,
				   "(&(cn=%s)(objectclass=secret))", 
				   name2);
		if (ret > 0) {
			return NT_STATUS_OBJECT_NAME_COLLISION;
		}
		
		if (ret < 0 || ret > 1) {
			DEBUG(0,("Found %d records matching DN %s\n", ret,
				 ldb_dn_linearize(mem_ctx, policy_state->system_dn)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}

		msg->dn = ldb_dn_build_child(mem_ctx, "cn", name2, policy_state->system_dn);
		if (!name2 || !msg->dn) {
			return NT_STATUS_NO_MEMORY;
		}
		
		samdb_msg_add_string(secret_state->sam_ldb, mem_ctx, msg, "cn", name2);
	
	} else {
		secret_state->global = False;

		name = r->in.name.string;
		if (strlen(name) < 1) {
			return NT_STATUS_INVALID_PARAMETER;
		}

		secret_state->sam_ldb = talloc_reference(secret_state, secrets_db_connect(mem_ctx));
		/* search for the secret record */
		ret = gendb_search(secret_state->sam_ldb, mem_ctx,
				   ldb_dn_explode(mem_ctx, "cn=LSA Secrets"),
				   &msgs, attrs,
				   "(&(cn=%s)(objectclass=secret))", 
				   ldb_binary_encode_string(mem_ctx, name));
		if (ret > 0) {
			return NT_STATUS_OBJECT_NAME_COLLISION;
		}
		
		if (ret < 0 || ret > 1) {
			DEBUG(0,("Found %d records matching DN %s\n", ret,
				 ldb_dn_linearize(mem_ctx, policy_state->system_dn)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}

		msg->dn = ldb_dn_string_compose(mem_ctx, NULL, "cn=%s,cn=LSA Secrets", name);
		samdb_msg_add_string(secret_state->sam_ldb, mem_ctx, msg, "cn", name);
	} 

	/* pull in all the template attributes.  Note this is always from the global samdb */
	ret = samdb_copy_template(secret_state->policy->sam_ldb, mem_ctx, msg, 
				  "(&(name=TemplateSecret)(objectclass=secretTemplate))");
	if (ret != 0) {
		DEBUG(0,("Failed to load TemplateSecret from samdb\n"));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	samdb_msg_add_string(secret_state->sam_ldb, mem_ctx, msg, "objectClass", "secret");
	
	secret_state->secret_dn = talloc_reference(secret_state, msg->dn);

	/* create the secret */
	ret = samdb_add(secret_state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		DEBUG(0,("Failed to create secret record %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	handle = dcesrv_handle_new(dce_call->context, LSA_HANDLE_SECRET);
	if (!handle) {
		return NT_STATUS_NO_MEMORY;
	}
	
	handle->data = talloc_steal(handle, secret_state);
	
	secret_state->access_mask = r->in.access_mask;
	secret_state->policy = talloc_reference(secret_state, policy_state);
	
	*r->out.sec_handle = handle->wire_handle;
	
	return NT_STATUS_OK;
}


/* 
  lsa_OpenSecret 
*/
static NTSTATUS lsa_OpenSecret(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			       struct lsa_OpenSecret *r)
{
	struct dcesrv_handle *policy_handle;
	
	struct lsa_policy_state *policy_state;
	struct lsa_secret_state *secret_state;
	struct dcesrv_handle *handle;
	struct ldb_message **msgs;
	const char *attrs[] = {
		NULL
	};

	const char *name;

	int ret;

	DCESRV_PULL_HANDLE(policy_handle, r->in.handle, LSA_HANDLE_POLICY);
	ZERO_STRUCTP(r->out.sec_handle);
	policy_state = policy_handle->data;

	if (!r->in.name.string) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	
	secret_state = talloc(mem_ctx, struct lsa_secret_state);
	if (!secret_state) {
		return NT_STATUS_NO_MEMORY;
	}
	secret_state->policy = policy_state;

	if (strncmp("G$", r->in.name.string, 2) == 0) {
		name = &r->in.name.string[2];
		secret_state->sam_ldb = talloc_reference(secret_state, policy_state->sam_ldb);
		secret_state->global = True;

		if (strlen(name) < 1) {
			return NT_STATUS_INVALID_PARAMETER;
		}

		/* search for the secret record */
		ret = gendb_search(secret_state->sam_ldb,
				   mem_ctx, policy_state->system_dn, &msgs, attrs,
				   "(&(cn=%s Secret)(objectclass=secret))", 
				   ldb_binary_encode_string(mem_ctx, name));
		if (ret == 0) {
			return NT_STATUS_OBJECT_NAME_NOT_FOUND;
		}
		
		if (ret != 1) {
			DEBUG(0,("Found %d records matching DN %s\n", ret,
				 ldb_dn_linearize(mem_ctx, policy_state->system_dn)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	
	} else {
		secret_state->sam_ldb = talloc_reference(secret_state, secrets_db_connect(mem_ctx));

		secret_state->global = False;
		name = r->in.name.string;
		if (strlen(name) < 1) {
			return NT_STATUS_INVALID_PARAMETER;
		}

		/* search for the secret record */
		ret = gendb_search(secret_state->sam_ldb, mem_ctx,
				   ldb_dn_explode(mem_ctx, "cn=LSA Secrets"),
				   &msgs, attrs,
				   "(&(cn=%s)(objectclass=secret))", 
				   ldb_binary_encode_string(mem_ctx, name));
		if (ret == 0) {
			return NT_STATUS_OBJECT_NAME_NOT_FOUND;
		}
		
		if (ret != 1) {
			DEBUG(0,("Found %d records matching DN %s\n", ret,
				 ldb_dn_linearize(mem_ctx, policy_state->system_dn)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	} 

	secret_state->secret_dn = talloc_reference(secret_state, msgs[0]->dn);
	
	handle = dcesrv_handle_new(dce_call->context, LSA_HANDLE_SECRET);
	if (!handle) {
		return NT_STATUS_NO_MEMORY;
	}
	
	handle->data = talloc_steal(handle, secret_state);
	
	secret_state->access_mask = r->in.access_mask;
	secret_state->policy = talloc_reference(secret_state, policy_state);
	
	*r->out.sec_handle = handle->wire_handle;
	
	return NT_STATUS_OK;
}


/* 
  lsa_SetSecret 
*/
static NTSTATUS lsa_SetSecret(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
			      struct lsa_SetSecret *r)
{

	struct dcesrv_handle *h;
	struct lsa_secret_state *secret_state;
	struct ldb_message *msg;
	DATA_BLOB session_key;
	DATA_BLOB crypt_secret, secret;
	struct ldb_val val;
	int ret;
	NTSTATUS status = NT_STATUS_OK;

	struct timeval now = timeval_current();
	NTTIME nt_now = timeval_to_nttime(&now);

	DCESRV_PULL_HANDLE(h, r->in.sec_handle, LSA_HANDLE_SECRET);

	secret_state = h->data;

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	msg->dn = talloc_reference(mem_ctx, secret_state->secret_dn);
	if (!msg->dn) {
		return NT_STATUS_NO_MEMORY;
	}
	status = dcesrv_fetch_session_key(dce_call->conn, &session_key);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	if (r->in.old_val) {
		/* Decrypt */
		crypt_secret.data = r->in.old_val->data;
		crypt_secret.length = r->in.old_val->size;
		
		status = sess_decrypt_blob(mem_ctx, &crypt_secret, &session_key, &secret);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
		
		val.data = secret.data;
		val.length = secret.length;
		
		/* set value */
		if (samdb_msg_add_value(secret_state->sam_ldb, 
					mem_ctx, msg, "priorSecret", &val) != 0) {
			return NT_STATUS_NO_MEMORY; 
		}
		
		/* set old value mtime */
		if (samdb_msg_add_uint64(secret_state->sam_ldb, 
					 mem_ctx, msg, "priorSetTime", nt_now) != 0) { 
			return NT_STATUS_NO_MEMORY; 
		}

		if (!r->in.new_val) {
			/* This behaviour varies depending of if this is a local, or a global secret... */
			if (secret_state->global) {
				/* set old value mtime */
				if (samdb_msg_add_uint64(secret_state->sam_ldb, 
							 mem_ctx, msg, "lastSetTime", nt_now) != 0) { 
					return NT_STATUS_NO_MEMORY; 
				}
			} else {
				if (samdb_msg_add_delete(secret_state->sam_ldb, 
							 mem_ctx, msg, "secret")) {
					return NT_STATUS_NO_MEMORY;
				}
				if (samdb_msg_add_delete(secret_state->sam_ldb, 
							 mem_ctx, msg, "lastSetTime")) {
					return NT_STATUS_NO_MEMORY;
				}
			}
		}
	}

	if (r->in.new_val) {
		/* Decrypt */
		crypt_secret.data = r->in.new_val->data;
		crypt_secret.length = r->in.new_val->size;
		
		status = sess_decrypt_blob(mem_ctx, &crypt_secret, &session_key, &secret);
		if (!NT_STATUS_IS_OK(status)) {
			return status;
		}
		
		val.data = secret.data;
		val.length = secret.length;
		
		/* set value */
		if (samdb_msg_add_value(secret_state->sam_ldb, 
					mem_ctx, msg, "secret", &val) != 0) {
			return NT_STATUS_NO_MEMORY; 
		}
		
		/* set new value mtime */
		if (samdb_msg_add_uint64(secret_state->sam_ldb, 
					 mem_ctx, msg, "lastSetTime", nt_now) != 0) { 
			return NT_STATUS_NO_MEMORY; 
		}
		
		/* If the old value is not set, then migrate the
		 * current value to the old value */
		if (!r->in.old_val) {
			const struct ldb_val *new_val;
			NTTIME last_set_time;
			struct ldb_message **res;
			const char *attrs[] = {
				"secret",
				"lastSetTime",
				NULL
			};
			
			/* search for the secret record */
			ret = gendb_search_dn(secret_state->sam_ldb,mem_ctx,
					      secret_state->secret_dn, &res, attrs);
			if (ret == 0) {
				return NT_STATUS_OBJECT_NAME_NOT_FOUND;
			}
			
			if (ret != 1) {
				DEBUG(0,("Found %d records matching dn=%s\n", ret,
					 ldb_dn_linearize(mem_ctx, secret_state->secret_dn)));
				return NT_STATUS_INTERNAL_DB_CORRUPTION;
			}

			new_val = ldb_msg_find_ldb_val(res[0], "secret");
			last_set_time = ldb_msg_find_uint64(res[0], "lastSetTime", 0);
			
			if (new_val) {
				/* set value */
				if (samdb_msg_add_value(secret_state->sam_ldb, 
							mem_ctx, msg, "priorSecret", 
							new_val) != 0) {
					return NT_STATUS_NO_MEMORY; 
				}
			}
			
			/* set new value mtime */
			if (ldb_msg_find_ldb_val(res[0], "lastSetTime")) {
				if (samdb_msg_add_uint64(secret_state->sam_ldb, 
							 mem_ctx, msg, "priorSetTime", last_set_time) != 0) { 
					return NT_STATUS_NO_MEMORY; 
				}
			}
		}
	}

	/* modify the samdb record */
	ret = samdb_replace(secret_state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		/* we really need samdb.c to return NTSTATUS */
		return NT_STATUS_UNSUCCESSFUL;
	}

	return NT_STATUS_OK;
}


/* 
  lsa_QuerySecret 
*/
static NTSTATUS lsa_QuerySecret(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				struct lsa_QuerySecret *r)
{
	struct dcesrv_handle *h;
	struct lsa_secret_state *secret_state;
	struct ldb_message *msg;
	DATA_BLOB session_key;
	DATA_BLOB crypt_secret, secret;
	int ret;
	struct ldb_message **res;
	const char *attrs[] = {
		"secret",
		"priorSecret",
		"lastSetTime",
		"priorSetTime", 
		NULL
	};

	NTSTATUS nt_status;

	DCESRV_PULL_HANDLE(h, r->in.sec_handle, LSA_HANDLE_SECRET);

	secret_state = h->data;

	/* pull all the user attributes */
	ret = gendb_search_dn(secret_state->sam_ldb, mem_ctx,
			      secret_state->secret_dn, &res, attrs);
	if (ret != 1) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}
	msg = res[0];
	
	nt_status = dcesrv_fetch_session_key(dce_call->conn, &session_key);
	if (!NT_STATUS_IS_OK(nt_status)) {
		return nt_status;
	}
	
	if (r->in.old_val) {
		const struct ldb_val *prior_val;
		r->out.old_val = talloc_zero(mem_ctx, struct lsa_DATA_BUF_PTR);
		if (!r->out.old_val) {
			return NT_STATUS_NO_MEMORY;
		}
		/* Decrypt */
		prior_val = ldb_msg_find_ldb_val(res[0], "priorSecret");
		
		if (prior_val && prior_val->length) {
			secret.data = prior_val->data;
			secret.length = prior_val->length;
		
			crypt_secret = sess_encrypt_blob(mem_ctx, &secret, &session_key);
			if (!crypt_secret.length) {
				return NT_STATUS_NO_MEMORY;
			}
			r->out.old_val->buf = talloc(mem_ctx, struct lsa_DATA_BUF);
			if (!r->out.old_val->buf) {
				return NT_STATUS_NO_MEMORY;
			}
			r->out.old_val->buf->size = crypt_secret.length;
			r->out.old_val->buf->length = crypt_secret.length;
			r->out.old_val->buf->data = crypt_secret.data;
		}
	}
	
	if (r->in.old_mtime) {
		r->out.old_mtime = talloc(mem_ctx, NTTIME);
		if (!r->out.old_mtime) {
			return NT_STATUS_NO_MEMORY;
		}
		*r->out.old_mtime = ldb_msg_find_uint64(res[0], "priorSetTime", 0);
	}
	
	if (r->in.new_val) {
		const struct ldb_val *new_val;
		r->out.new_val = talloc_zero(mem_ctx, struct lsa_DATA_BUF_PTR);
		if (!r->out.new_val) {
			return NT_STATUS_NO_MEMORY;
		}

		/* Decrypt */
		new_val = ldb_msg_find_ldb_val(res[0], "secret");
		
		if (new_val && new_val->length) {
			secret.data = new_val->data;
			secret.length = new_val->length;
		
			crypt_secret = sess_encrypt_blob(mem_ctx, &secret, &session_key);
			if (!crypt_secret.length) {
				return NT_STATUS_NO_MEMORY;
			}
			r->out.new_val->buf = talloc(mem_ctx, struct lsa_DATA_BUF);
			if (!r->out.new_val->buf) {
				return NT_STATUS_NO_MEMORY;
			}
			r->out.new_val->buf->length = crypt_secret.length;
			r->out.new_val->buf->size = crypt_secret.length;
			r->out.new_val->buf->data = crypt_secret.data;
		}
	}
	
	if (r->in.new_mtime) {
		r->out.new_mtime = talloc(mem_ctx, NTTIME);
		if (!r->out.new_mtime) {
			return NT_STATUS_NO_MEMORY;
		}
		*r->out.new_mtime = ldb_msg_find_uint64(res[0], "lastSetTime", 0);
	}
	
	return NT_STATUS_OK;
}


/* 
  lsa_LookupPrivValue
*/
static NTSTATUS lsa_LookupPrivValue(struct dcesrv_call_state *dce_call, 
				    TALLOC_CTX *mem_ctx,
				    struct lsa_LookupPrivValue *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;
	int id;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	id = sec_privilege_id(r->in.name->string);
	if (id == -1) {
		return NT_STATUS_NO_SUCH_PRIVILEGE;
	}

	r->out.luid->low = id;
	r->out.luid->high = 0;

	return NT_STATUS_OK;	
}


/* 
  lsa_LookupPrivName 
*/
static NTSTATUS lsa_LookupPrivName(struct dcesrv_call_state *dce_call, 
				   TALLOC_CTX *mem_ctx,
				   struct lsa_LookupPrivName *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;
	const char *privname;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	if (r->in.luid->high != 0) {
		return NT_STATUS_NO_SUCH_PRIVILEGE;
	}

	privname = sec_privilege_name(r->in.luid->low);
	if (privname == NULL) {
		return NT_STATUS_NO_SUCH_PRIVILEGE;
	}

	r->out.name = talloc(mem_ctx, struct lsa_StringLarge);
	if (r->out.name == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	r->out.name->string = privname;

	return NT_STATUS_OK;	
}


/* 
  lsa_LookupPrivDisplayName
*/
static NTSTATUS lsa_LookupPrivDisplayName(struct dcesrv_call_state *dce_call, 
					  TALLOC_CTX *mem_ctx,
					  struct lsa_LookupPrivDisplayName *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;
	int id;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	id = sec_privilege_id(r->in.name->string);
	if (id == -1) {
		return NT_STATUS_NO_SUCH_PRIVILEGE;
	}
	
	r->out.disp_name = talloc(mem_ctx, struct lsa_StringLarge);
	if (r->out.disp_name == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	r->out.disp_name->string = sec_privilege_display_name(id, r->in.language_id);
	if (r->out.disp_name->string == NULL) {
		return NT_STATUS_INTERNAL_ERROR;
	}

	return NT_STATUS_OK;
}


/* 
  lsa_DeleteObject
*/
static NTSTATUS lsa_DeleteObject(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_DeleteObject *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_EnumAccountsWithUserRight
*/
static NTSTATUS lsa_EnumAccountsWithUserRight(struct dcesrv_call_state *dce_call, 
					      TALLOC_CTX *mem_ctx,
					      struct lsa_EnumAccountsWithUserRight *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;
	int ret, i;
	struct ldb_message **res;
	const char * const attrs[] = { "objectSid", NULL};
	const char *privname;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	if (r->in.name == NULL) {
		return NT_STATUS_NO_SUCH_PRIVILEGE;
	} 

	privname = r->in.name->string;
	if (sec_privilege_id(privname) == -1) {
		return NT_STATUS_NO_SUCH_PRIVILEGE;
	}

	ret = gendb_search(state->sam_ldb, mem_ctx, NULL, &res, attrs, 
			   "privilege=%s", privname);
	if (ret <= 0) {
		return NT_STATUS_NO_SUCH_USER;
	}

	r->out.sids->sids = talloc_array(r->out.sids, struct lsa_SidPtr, ret);
	if (r->out.sids->sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	for (i=0;i<ret;i++) {
		r->out.sids->sids[i].sid = samdb_result_dom_sid(r->out.sids->sids,
								res[i], "objectSid");
		NT_STATUS_HAVE_NO_MEMORY(r->out.sids->sids[i].sid);
	}
	r->out.sids->num_sids = ret;

	return NT_STATUS_OK;
}


/* 
  lsa_AddAccountRights
*/
static NTSTATUS lsa_AddAccountRights(struct dcesrv_call_state *dce_call, 
				     TALLOC_CTX *mem_ctx,
				     struct lsa_AddAccountRights *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	return lsa_AddRemoveAccountRights(dce_call, mem_ctx, state, 
					  LDB_FLAG_MOD_ADD,
					  r->in.sid, r->in.rights);
}


/* 
  lsa_RemoveAccountRights
*/
static NTSTATUS lsa_RemoveAccountRights(struct dcesrv_call_state *dce_call, 
					TALLOC_CTX *mem_ctx,
					struct lsa_RemoveAccountRights *r)
{
	struct dcesrv_handle *h;
	struct lsa_policy_state *state;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	return lsa_AddRemoveAccountRights(dce_call, mem_ctx, state, 
					  LDB_FLAG_MOD_DELETE,
					  r->in.sid, r->in.rights);
}


/* 
  lsa_StorePrivateData
*/
static NTSTATUS lsa_StorePrivateData(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_StorePrivateData *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_RetrievePrivateData
*/
static NTSTATUS lsa_RetrievePrivateData(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_RetrievePrivateData *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_GetUserName
*/
static NTSTATUS lsa_GetUserName(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				struct lsa_GetUserName *r)
{
	NTSTATUS status = NT_STATUS_OK;
	const char *account_name;
	const char *authority_name;
	struct lsa_String *_account_name;
	struct lsa_StringPointer *_authority_name = NULL;

	/* this is what w2k3 does */
	r->out.account_name = r->in.account_name;
	r->out.authority_name = r->in.authority_name;

	if (r->in.account_name && r->in.account_name->string) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	if (r->in.authority_name &&
	    r->in.authority_name->string &&
	    r->in.authority_name->string->string) {
		return NT_STATUS_INVALID_PARAMETER;
	}

	account_name = talloc_reference(mem_ctx, dce_call->conn->auth_state.session_info->server_info->account_name);
	authority_name = talloc_reference(mem_ctx, dce_call->conn->auth_state.session_info->server_info->domain_name);

	_account_name = talloc(mem_ctx, struct lsa_String);
	NT_STATUS_HAVE_NO_MEMORY(_account_name);
	_account_name->string = account_name;

	if (r->in.authority_name) {
		_authority_name = talloc(mem_ctx, struct lsa_StringPointer);
		NT_STATUS_HAVE_NO_MEMORY(_authority_name);
		_authority_name->string = talloc(mem_ctx, struct lsa_String);
		NT_STATUS_HAVE_NO_MEMORY(_authority_name->string);
		_authority_name->string->string = authority_name;
	}

	r->out.account_name = _account_name;
	r->out.authority_name = _authority_name;

	return status;
}

/*
  lsa_SetInfoPolicy2
*/
static NTSTATUS lsa_SetInfoPolicy2(struct dcesrv_call_state *dce_call,
				   TALLOC_CTX *mem_ctx,
				   struct lsa_SetInfoPolicy2 *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lsa_QueryDomainInformationPolicy
*/
static NTSTATUS lsa_QueryDomainInformationPolicy(struct dcesrv_call_state *dce_call,
						 TALLOC_CTX *mem_ctx,
						 struct lsa_QueryDomainInformationPolicy *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lsa_SetDomInfoPolicy
*/
static NTSTATUS lsa_SetDomainInformationPolicy(struct dcesrv_call_state *dce_call,
					      TALLOC_CTX *mem_ctx,
					      struct lsa_SetDomainInformationPolicy *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lsa_TestCall
*/
static NTSTATUS lsa_TestCall(struct dcesrv_call_state *dce_call,
			     TALLOC_CTX *mem_ctx,
			     struct lsa_TestCall *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}

/*
  lookup a SID for 1 name
*/
static NTSTATUS lsa_lookup_name(struct lsa_policy_state *state, TALLOC_CTX *mem_ctx,
				const char *name, struct dom_sid **sid, uint32_t *atype)
{
	int ret;
	struct ldb_message **res;
	const char * const attrs[] = { "objectSid", "sAMAccountType", NULL};
	const char *p;

	p = strchr_m(name, '\\');
	if (p != NULL) {
		/* TODO: properly parse the domain prefix here, and use it to 
		   limit the search */
		name = p + 1;
	}

	ret = gendb_search(state->sam_ldb, mem_ctx, NULL, &res, attrs, "sAMAccountName=%s", ldb_binary_encode_string(mem_ctx, name));
	if (ret == 1) {
		*sid = samdb_result_dom_sid(mem_ctx, res[0], "objectSid");
		if (*sid == NULL) {
			return NT_STATUS_INVALID_SID;
		}

		*atype = samdb_result_uint(res[0], "sAMAccountType", 0);

		return NT_STATUS_OK;
	}

	/* need to add a call into sidmap to check for a allocated sid */

	return NT_STATUS_INVALID_SID;
}


/*
  lsa_LookupNames4
*/
static NTSTATUS lsa_LookupNames4(struct dcesrv_call_state *dce_call,
				 TALLOC_CTX *mem_ctx,
				 struct lsa_LookupNames4 *r)
{
	struct lsa_policy_state *state;
	int i;
	NTSTATUS status = NT_STATUS_OK;

	status = lsa_get_policy_state(dce_call, mem_ctx, &state);
	if (!NT_STATUS_IS_OK(status)) {
		return status;
	}

	r->out.domains = NULL;

	r->out.domains = talloc_zero(mem_ctx,  struct lsa_RefDomainList);
	if (r->out.domains == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	r->out.sids = talloc_zero(mem_ctx,  struct lsa_TransSidArray3);
	if (r->out.sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	*r->out.count = 0;

	r->out.sids->sids = talloc_array(r->out.sids, struct lsa_TranslatedSid3, 
					   r->in.num_names);
	if (r->out.sids->sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0;i<r->in.num_names;i++) {
		const char *name = r->in.names[i].string;
		struct dom_sid *sid;
		uint32_t atype, rtype, sid_index;
		NTSTATUS status2;

		r->out.sids->count++;
		(*r->out.count)++;

		r->out.sids->sids[i].sid_type    = SID_NAME_UNKNOWN;
		r->out.sids->sids[i].sid         = NULL;
		r->out.sids->sids[i].sid_index   = 0xFFFFFFFF;
		r->out.sids->sids[i].unknown     = 0;

		status2 = lsa_lookup_name(state, mem_ctx, name, &sid, &atype);
		if (!NT_STATUS_IS_OK(status2) || sid->num_auths == 0) {
			status = STATUS_SOME_UNMAPPED;
			continue;
		}

		rtype = samdb_atype_map(atype);
		if (rtype == SID_NAME_UNKNOWN) {
			status = STATUS_SOME_UNMAPPED;
			continue;
		}

		status2 = lsa_authority_list(state, mem_ctx, sid, r->out.domains, &sid_index);
		if (!NT_STATUS_IS_OK(status2)) {
			return status2;
		}

		r->out.sids->sids[i].sid_type    = rtype;
		r->out.sids->sids[i].sid         = sid;
		r->out.sids->sids[i].sid_index   = sid_index;
		r->out.sids->sids[i].unknown     = 0;
	}
	
	return status;
}

/* 
  lsa_LookupNames3
*/
static NTSTATUS lsa_LookupNames3(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
				 struct lsa_LookupNames3 *r)
{
	struct lsa_LookupNames4 r2;
	NTSTATUS status;
	struct dcesrv_handle *h;
	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);
	
	r2.in.num_names = r->in.num_names;
	r2.in.names = r->in.names;
	r2.in.sids = r->in.sids;
	r2.in.count = r->in.count;
	r2.in.unknown1 = r->in.unknown1;
	r2.in.unknown2 = r->in.unknown2;
	r2.out.domains = r->out.domains;
	r2.out.sids = r->out.sids;
	r2.out.count = r->out.count;
	
	status = lsa_LookupNames4(dce_call, mem_ctx, &r2);
	if (dce_call->fault_code != 0) {
		return status;
	}
	
	r->out.domains = r2.out.domains;
	r->out.sids = r2.out.sids;
	r->out.count = r2.out.count;
	return status;
}

/*
  lsa_LookupNames2
*/
static NTSTATUS lsa_LookupNames2(struct dcesrv_call_state *dce_call,
				 TALLOC_CTX *mem_ctx,
				 struct lsa_LookupNames2 *r)
{
	struct lsa_policy_state *state;
	struct dcesrv_handle *h;
	int i;
	NTSTATUS status = NT_STATUS_OK;

	r->out.domains = NULL;

	DCESRV_PULL_HANDLE(h, r->in.handle, LSA_HANDLE_POLICY);

	state = h->data;

	r->out.domains = talloc_zero(mem_ctx,  struct lsa_RefDomainList);
	if (r->out.domains == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	r->out.sids = talloc_zero(mem_ctx,  struct lsa_TransSidArray2);
	if (r->out.sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	*r->out.count = 0;

	r->out.sids->sids = talloc_array(r->out.sids, struct lsa_TranslatedSid2, 
					   r->in.num_names);
	if (r->out.sids->sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	for (i=0;i<r->in.num_names;i++) {
		const char *name = r->in.names[i].string;
		struct dom_sid *sid;
		uint32_t atype, rtype, sid_index;
		NTSTATUS status2;

		r->out.sids->count++;
		(*r->out.count)++;

		r->out.sids->sids[i].sid_type    = SID_NAME_UNKNOWN;
		r->out.sids->sids[i].rid         = 0xFFFFFFFF;
		r->out.sids->sids[i].sid_index   = 0xFFFFFFFF;
		r->out.sids->sids[i].unknown     = 0;

		status2 = lsa_lookup_name(state, mem_ctx, name, &sid, &atype);
		if (!NT_STATUS_IS_OK(status2) || sid->num_auths == 0) {
			status = STATUS_SOME_UNMAPPED;
			continue;
		}

		rtype = samdb_atype_map(atype);
		if (rtype == SID_NAME_UNKNOWN) {
			status = STATUS_SOME_UNMAPPED;
			continue;
		}

		status2 = lsa_authority_list(state, mem_ctx, sid, r->out.domains, &sid_index);
		if (!NT_STATUS_IS_OK(status2)) {
			return status2;
		}

		r->out.sids->sids[i].sid_type    = rtype;
		r->out.sids->sids[i].rid         = sid->sub_auths[sid->num_auths-1];
		r->out.sids->sids[i].sid_index   = sid_index;
		r->out.sids->sids[i].unknown     = 0;
	}
	
	return status;
}

/* 
  lsa_LookupNames 
*/
static NTSTATUS lsa_LookupNames(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LookupNames *r)
{
	struct lsa_LookupNames2 r2;
	NTSTATUS status;
	int i;

	r2.in.handle    = r->in.handle;
	r2.in.num_names = r->in.num_names;
	r2.in.names     = r->in.names;
	r2.in.sids      = NULL;
	r2.in.level     = r->in.level;
	r2.in.count     = r->in.count;
	r2.in.unknown1  = 0;
	r2.in.unknown2  = 0;
	r2.out.count    = r->out.count;

	status = lsa_LookupNames2(dce_call, mem_ctx, &r2);
	if (dce_call->fault_code != 0) {
		return status;
	}

	r->out.domains = r2.out.domains;
	r->out.sids = talloc(mem_ctx, struct lsa_TransSidArray);
	if (r->out.sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	r->out.sids->count = r2.out.sids->count;
	r->out.sids->sids = talloc_array(r->out.sids, struct lsa_TranslatedSid, 
					   r->out.sids->count);
	if (r->out.sids->sids == NULL) {
		return NT_STATUS_NO_MEMORY;
	}
	for (i=0;i<r->out.sids->count;i++) {
		r->out.sids->sids[i].sid_type    = r2.out.sids->sids[i].sid_type;
		r->out.sids->sids[i].rid         = r2.out.sids->sids[i].rid;
		r->out.sids->sids[i].sid_index   = r2.out.sids->sids[i].sid_index;
	}

	return status;
}

/* 
  lsa_CREDRWRITE 
*/
static NTSTATUS lsa_CREDRWRITE(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRWRITE *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRREAD 
*/
static NTSTATUS lsa_CREDRREAD(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRREAD *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRENUMERATE 
*/
static NTSTATUS lsa_CREDRENUMERATE(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRENUMERATE *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRWRITEDOMAINCREDENTIALS 
*/
static NTSTATUS lsa_CREDRWRITEDOMAINCREDENTIALS(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRWRITEDOMAINCREDENTIALS *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRREADDOMAINCREDENTIALS 
*/
static NTSTATUS lsa_CREDRREADDOMAINCREDENTIALS(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRREADDOMAINCREDENTIALS *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRDELETE 
*/
static NTSTATUS lsa_CREDRDELETE(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRDELETE *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRGETTARGETINFO 
*/
static NTSTATUS lsa_CREDRGETTARGETINFO(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRGETTARGETINFO *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRPROFILELOADED 
*/
static NTSTATUS lsa_CREDRPROFILELOADED(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRPROFILELOADED *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRGETSESSIONTYPES 
*/
static NTSTATUS lsa_CREDRGETSESSIONTYPES(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRGETSESSIONTYPES *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARREGISTERAUDITEVENT 
*/
static NTSTATUS lsa_LSARREGISTERAUDITEVENT(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARREGISTERAUDITEVENT *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARGENAUDITEVENT 
*/
static NTSTATUS lsa_LSARGENAUDITEVENT(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARGENAUDITEVENT *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARUNREGISTERAUDITEVENT 
*/
static NTSTATUS lsa_LSARUNREGISTERAUDITEVENT(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARUNREGISTERAUDITEVENT *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARQUERYFORESTTRUSTINFORMATION 
*/
static NTSTATUS lsa_LSARQUERYFORESTTRUSTINFORMATION(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARQUERYFORESTTRUSTINFORMATION *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARSETFORESTTRUSTINFORMATION 
*/
static NTSTATUS lsa_LSARSETFORESTTRUSTINFORMATION(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARSETFORESTTRUSTINFORMATION *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_CREDRRENAME 
*/
static NTSTATUS lsa_CREDRRENAME(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_CREDRRENAME *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}



/* 
  lsa_LSAROPENPOLICYSCE 
*/
static NTSTATUS lsa_LSAROPENPOLICYSCE(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSAROPENPOLICYSCE *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARADTREGISTERSECURITYEVENTSOURCE 
*/
static NTSTATUS lsa_LSARADTREGISTERSECURITYEVENTSOURCE(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARADTREGISTERSECURITYEVENTSOURCE *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARADTUNREGISTERSECURITYEVENTSOURCE 
*/
static NTSTATUS lsa_LSARADTUNREGISTERSECURITYEVENTSOURCE(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARADTUNREGISTERSECURITYEVENTSOURCE *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* 
  lsa_LSARADTREPORTSECURITYEVENT 
*/
static NTSTATUS lsa_LSARADTREPORTSECURITYEVENT(struct dcesrv_call_state *dce_call, TALLOC_CTX *mem_ctx,
		       struct lsa_LSARADTREPORTSECURITYEVENT *r)
{
	DCESRV_FAULT(DCERPC_FAULT_OP_RNG_ERROR);
}


/* include the generated boilerplate */
#include "librpc/gen_ndr/ndr_lsa_s.c"
