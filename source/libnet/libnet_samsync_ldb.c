/* 
   Unix SMB/CIFS implementation.
   
   Extract the user/system database from a remote SamSync server

   Copyright (C) Andrew Bartlett <abartlet@samba.org> 2004-2005
   Copyright (C) Andrew Tridgell 2004
   Copyright (C) Volker Lendecke 2004
   
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
#include "libnet/libnet.h"
#include "librpc/gen_ndr/ndr_netlogon.h"
#include "librpc/gen_ndr/ndr_samr.h"
#include "dlinklist.h"
#include "libcli/ldap/ldap.h"
#include "lib/ldb/include/ldb.h"
#include "dsdb/samdb/samdb.h"
#include "auth/auth.h"

struct samsync_ldb_secret {
	struct samsync_ldb_secret *prev, *next;
	DATA_BLOB secret;
	char *name;
	NTTIME mtime;
};

struct samsync_ldb_trusted_domain {
	struct samsync_ldb_trusted_domain *prev, *next;
        struct dom_sid *sid;
	char *name;
};

struct samsync_ldb_state {
	struct dom_sid *dom_sid[3];
	struct ldb_context *sam_ldb;
	struct ldb_dn *base_dn[3];
	struct samsync_ldb_secret *secrets;
	struct samsync_ldb_trusted_domain *trusted_domains;
};

static NTSTATUS samsync_ldb_add_foreignSecurityPrincipal(TALLOC_CTX *mem_ctx,
							 struct samsync_ldb_state *state,
							 struct dom_sid *sid,
							 struct ldb_dn **fsp_dn)
{
	const char *sidstr = dom_sid_string(mem_ctx, sid);
	/* We assume that ForeignSecurityPrincipals are under the BASEDN of the main domain */
	struct ldb_dn *basedn = samdb_search_dn(state->sam_ldb, mem_ctx,
						state->base_dn[SAM_DATABASE_DOMAIN],
						"(&(objectClass=container)(cn=ForeignSecurityPrincipals))");
	struct ldb_message *msg;
	int ret;

	if (!sidstr) {
		return NT_STATUS_NO_MEMORY;
	}

	if (basedn == NULL) {
		DEBUG(0, ("Failed to find DN for "
			  "ForeignSecurityPrincipal container\n"));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}
	
	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* add core elements to the ldb_message for the alias */
	msg->dn = ldb_dn_build_child(mem_ctx, "CN", sidstr, basedn);
	if (msg->dn == NULL)
		return NT_STATUS_NO_MEMORY;
	
	samdb_msg_add_string(state->sam_ldb, mem_ctx, msg,
			     "objectClass",
			     "foreignSecurityPrincipal");

	*fsp_dn = msg->dn;

	/* create the alias */
	ret = samdb_add(state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		DEBUG(0,("Failed to create foreignSecurityPrincipal "
			 "record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn),
			 ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}
	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_handle_domain(TALLOC_CTX *mem_ctx,
					  struct samsync_ldb_state *state,
					  struct creds_CredentialState *creds,
					  enum netr_SamDatabaseID database,
					  struct netr_DELTA_ENUM *delta) 
{
	struct netr_DELTA_DOMAIN *domain = delta->delta_union.domain;
	const char *domain_name = domain->domain_name.string;
	struct ldb_message *msg;
	int ret;
	
	if (database == SAM_DATABASE_DOMAIN) {
		const char *domain_attrs[] =  {"nETBIOSName", "nCName", NULL};
		struct ldb_message **msgs_domain;
		int ret_domain;

		ret_domain = gendb_search(state->sam_ldb, mem_ctx, NULL, &msgs_domain, domain_attrs,
					  "(&(&(nETBIOSName=%s)(objectclass=crossRef))(ncName=*))", 
					  domain_name);
		if (ret_domain == -1) {
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
		
		if (ret_domain != 1) {
			return NT_STATUS_NO_SUCH_DOMAIN;		
		}

		state->base_dn[database] = samdb_result_dn(state, msgs_domain[0], "nCName", NULL);

		state->dom_sid[database] = samdb_search_dom_sid(state->sam_ldb, state,
								state->base_dn[database], 
								"objectSid", NULL);
	} else if (database == SAM_DATABASE_BUILTIN) {
		/* work out the builtin_dn - useful for so many calls its worth
		   fetching here */
		const char *dnstring = samdb_search_string(state->sam_ldb, mem_ctx, NULL,
							   "distinguishedName", "objectClass=builtinDomain");
		state->base_dn[database] = ldb_dn_explode(state, dnstring);
		state->dom_sid[database] = dom_sid_parse_talloc(state, SID_BUILTIN);
	} else {
		/* PRIVs DB */
		return NT_STATUS_INVALID_PARAMETER;
	}

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	msg->dn = talloc_reference(mem_ctx, state->base_dn[database]);
	if (!msg->dn) {
		return NT_STATUS_NO_MEMORY;
	}

	samdb_msg_add_string(state->sam_ldb, mem_ctx, 
			     msg, "oEMInformation", domain->comment.string);

	samdb_msg_add_int64(state->sam_ldb, mem_ctx, 
			     msg, "forceLogoff", domain->force_logoff_time);

	samdb_msg_add_uint(state->sam_ldb, mem_ctx, 
			  msg, "minPwdLen", domain->min_password_length);

	samdb_msg_add_int64(state->sam_ldb, mem_ctx, 
			  msg, "maxPwdAge", domain->max_password_age);

	samdb_msg_add_int64(state->sam_ldb, mem_ctx, 
			  msg, "minPwdAge", domain->min_password_age);

	samdb_msg_add_uint(state->sam_ldb, mem_ctx, 
			  msg, "pwdHistoryLength", domain->password_history_length);

	samdb_msg_add_uint64(state->sam_ldb, mem_ctx, 
			     msg, "modifiedCount", 
			     domain->sequence_num);

	samdb_msg_add_uint64(state->sam_ldb, mem_ctx, 
			     msg, "creationTime", domain->domain_create_time);

	/* TODO: Account lockout, password properties */
	
	ret = samdb_replace(state->sam_ldb, mem_ctx, msg);

	if (ret) {
		return NT_STATUS_INTERNAL_ERROR;
	}
	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_handle_user(TALLOC_CTX *mem_ctx,
					struct samsync_ldb_state *state,
					struct creds_CredentialState *creds,
					enum netr_SamDatabaseID database,
					struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct netr_DELTA_USER *user = delta->delta_union.user;
	const char *container, *obj_class;
	char *cn_name;
	int cn_name_len;

	struct ldb_message *msg;
	struct ldb_message **msgs;
	int ret;
	uint32_t acb;
	BOOL add = False;
	const char *attrs[] = { NULL };

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the user, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database],
			   &msgs, attrs, "(&(objectClass=user)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		add = True;
	} else if (ret > 1) {
		DEBUG(0, ("More than one user with SID: %s\n", 
			  dom_sid_string(mem_ctx, 
					 dom_sid_add_rid(mem_ctx, 
							 state->dom_sid[database], 
							 rid))));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else {
		msg->dn = talloc_steal(msg, msgs[0]->dn);
	}


	cn_name   = talloc_strdup(mem_ctx, user->account_name.string);
	NT_STATUS_HAVE_NO_MEMORY(cn_name);
	cn_name_len = strlen(cn_name);

#define ADD_OR_DEL(type, attrib, field) do {\
	if (user->field) { \
		samdb_msg_add_ ## type(state->sam_ldb, mem_ctx, msg, \
				     attrib, user->field); \
	} else if (!add) { \
		samdb_msg_add_delete(state->sam_ldb, mem_ctx, msg,  \
				     attrib); \
	} \
        } while (0);

        ADD_OR_DEL(string, "samAccountName", account_name.string);
        ADD_OR_DEL(string, "displayName", full_name.string);

	if (samdb_msg_add_dom_sid(state->sam_ldb, mem_ctx, msg, 
				  "objectSid", dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))) {
		return NT_STATUS_NO_MEMORY; 
	}

        ADD_OR_DEL(uint, "primaryGroupID", primary_gid);
        ADD_OR_DEL(string, "homeDirectory", home_directory.string);
        ADD_OR_DEL(string, "homeDrive", home_drive.string);
        ADD_OR_DEL(string, "scriptPath", logon_script.string);
	ADD_OR_DEL(string, "description", description.string);
	ADD_OR_DEL(string, "userWorkstations", workstations.string);

	ADD_OR_DEL(uint64, "lastLogon", last_logon);
	ADD_OR_DEL(uint64, "lastLogoff", last_logoff);

	if (samdb_msg_add_logon_hours(state->sam_ldb, mem_ctx, msg, "logonHours", &user->logon_hours) != 0) { 
		return NT_STATUS_NO_MEMORY; 
	}

	ADD_OR_DEL(uint, "badPwdCount", bad_password_count);
	ADD_OR_DEL(uint, "logonCount", logon_count);

	ADD_OR_DEL(uint64, "pwdLastSet", last_password_change);
	ADD_OR_DEL(uint64, "accountExpires", acct_expiry);
	
	if (samdb_msg_add_acct_flags(state->sam_ldb, mem_ctx, msg, 
				     "userAccountControl", user->acct_flags) != 0) { 
		return NT_STATUS_NO_MEMORY; 
	} 
	
	/* Passwords.  Ensure there is no plaintext stored against
	 * this entry, as we only have hashes */
	samdb_msg_add_delete(state->sam_ldb, mem_ctx, msg,  
				"unicodePwd"); 
	if (user->lm_password_present) {
		samdb_msg_add_hash(state->sam_ldb, mem_ctx, msg,  
				   "lmPwdHash", &user->lmpassword);
	} else {
		samdb_msg_add_delete(state->sam_ldb, mem_ctx, msg,  
				     "lmPwdHash"); 
	}
	if (user->nt_password_present) {
		samdb_msg_add_hash(state->sam_ldb, mem_ctx, msg,  
				   "ntPwdHash", &user->ntpassword);
	} else {
		samdb_msg_add_delete(state->sam_ldb, mem_ctx, msg,  
				     "ntPwdHash"); 
	}
	    
	ADD_OR_DEL(string, "comment", comment.string);
	ADD_OR_DEL(string, "userParameters", parameters.string);
	ADD_OR_DEL(uint, "countryCode", country_code);
	ADD_OR_DEL(uint, "codePage", code_page);

        ADD_OR_DEL(string, "profilePath", profile_path.string);

#undef ADD_OR_DEL

	acb = user->acct_flags;
	if (acb & (ACB_WSTRUST)) {
		cn_name[cn_name_len - 1] = '\0';
		container = "Computers";
		obj_class = "computer";
		
	} else if (acb & ACB_SVRTRUST) {
		if (cn_name[cn_name_len - 1] != '$') {
			return NT_STATUS_FOOBAR;		
		}
		cn_name[cn_name_len - 1] = '\0';
		container = "Domain Controllers";
		obj_class = "computer";
	} else {
		container = "Users";
		obj_class = "user";
	}
	if (add) {
		samdb_msg_add_string(state->sam_ldb, mem_ctx, msg, 
				     "objectClass", obj_class);
		msg->dn = ldb_dn_string_compose(mem_ctx, state->base_dn[database],
						"CN=%s, CN=%s", cn_name, container);
		if (!msg->dn) {
			return NT_STATUS_NO_MEMORY;		
		}

		ret = samdb_add(state->sam_ldb, mem_ctx, msg);
		if (ret != 0) {
			DEBUG(0,("Failed to create user record %s\n",
				 ldb_dn_linearize(mem_ctx, msg->dn)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	} else {
		ret = samdb_replace(state->sam_ldb, mem_ctx, msg);
		if (ret != 0) {
			DEBUG(0,("Failed to modify user record %s\n",
				 ldb_dn_linearize(mem_ctx, msg->dn)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_delete_user(TALLOC_CTX *mem_ctx,
					struct samsync_ldb_state *state,
					struct creds_CredentialState *creds,
					enum netr_SamDatabaseID database,
					struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct ldb_message **msgs;
	int ret;
	const char *attrs[] = { NULL };

	/* search for the user, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database],
			   &msgs, attrs, "(&(objectClass=user)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		return NT_STATUS_NO_SUCH_USER;
	} else if (ret > 1) {
		DEBUG(0, ("More than one user with SID: %s\n", 
			  dom_sid_string(mem_ctx, 
					 dom_sid_add_rid(mem_ctx, 
							 state->dom_sid[database], 
							 rid))));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	ret = samdb_delete(state->sam_ldb, mem_ctx, msgs[0]->dn);
	if (ret != 0) {
		DEBUG(0,("Failed to delete user record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msgs[0]->dn),
			 ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_handle_group(TALLOC_CTX *mem_ctx,
					 struct samsync_ldb_state *state,
					 struct creds_CredentialState *creds,
					 enum netr_SamDatabaseID database,
					 struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct netr_DELTA_GROUP *group = delta->delta_union.group;
	const char *container, *obj_class;
	const char *cn_name;

	struct ldb_message *msg;
	struct ldb_message **msgs;
	int ret;
	BOOL add = False;
	const char *attrs[] = { NULL };

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the group, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database], &msgs, attrs,
			   "(&(objectClass=group)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		add = True;
	} else if (ret > 1) {
		DEBUG(0, ("More than one group/alias with SID: %s\n", 
			  dom_sid_string(mem_ctx, 
					 dom_sid_add_rid(mem_ctx, 
							 state->dom_sid[database], 
							 rid))));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else {
		msg->dn = talloc_steal(msg, msgs[0]->dn);
	}

	cn_name   = group->group_name.string;

#define ADD_OR_DEL(type, attrib, field) do {\
	if (group->field) { \
		samdb_msg_add_ ## type(state->sam_ldb, mem_ctx, msg, \
				     attrib, group->field); \
	} else if (!add) { \
		samdb_msg_add_delete(state->sam_ldb, mem_ctx, msg,  \
				     attrib); \
	} \
        } while (0);

        ADD_OR_DEL(string, "samAccountName", group_name.string);

	if (samdb_msg_add_dom_sid(state->sam_ldb, mem_ctx, msg, 
				  "objectSid", dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))) {
		return NT_STATUS_NO_MEMORY; 
	}

	ADD_OR_DEL(string, "description", description.string);

#undef ADD_OR_DEL

	container = "Users";
	obj_class = "group";

	if (add) {
		samdb_msg_add_string(state->sam_ldb, mem_ctx, msg, 
				     "objectClass", obj_class);
		msg->dn = ldb_dn_string_compose(mem_ctx, state->base_dn[database],
						"CN=%s, CN=%s", cn_name, container);
		if (!msg->dn) {
			return NT_STATUS_NO_MEMORY;		
		}

		ret = samdb_add(state->sam_ldb, mem_ctx, msg);
		if (ret != 0) {
			DEBUG(0,("Failed to create group record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn),
			 ldb_errstring(state->sam_ldb)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	} else {
		ret = samdb_replace(state->sam_ldb, mem_ctx, msg);
		if (ret != 0) {
			DEBUG(0,("Failed to modify group record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn),
			 ldb_errstring(state->sam_ldb)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_delete_group(TALLOC_CTX *mem_ctx,
					struct samsync_ldb_state *state,
					struct creds_CredentialState *creds,
					enum netr_SamDatabaseID database,
					struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct ldb_message **msgs;
	int ret;
	const char *attrs[] = { NULL };

	/* search for the group, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database], &msgs, attrs,
			   "(&(objectClass=group)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		return NT_STATUS_NO_SUCH_GROUP;
	} else if (ret > 1) {
		DEBUG(0, ("More than one group/alias with SID: %s\n", 
			  dom_sid_string(mem_ctx, 
					 dom_sid_add_rid(mem_ctx, 
							 state->dom_sid[database], 
							 rid))));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}
	
	ret = samdb_delete(state->sam_ldb, mem_ctx, msgs[0]->dn);
	if (ret != 0) {
		DEBUG(0,("Failed to delete group record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msgs[0]->dn),
			 ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_handle_group_member(TALLOC_CTX *mem_ctx,
						struct samsync_ldb_state *state,
						struct creds_CredentialState *creds,
						enum netr_SamDatabaseID database,
						struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct netr_DELTA_GROUP_MEMBER *group_member = delta->delta_union.group_member;
	struct ldb_message *msg;
	struct ldb_message **msgs;
	int ret;
	const char *attrs[] = { NULL };
	int i;

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the group, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database], &msgs, attrs,
			   "(&(objectClass=group)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		return NT_STATUS_NO_SUCH_GROUP;
	} else if (ret > 1) {
		DEBUG(0, ("More than one group/alias with SID: %s\n", 
			  dom_sid_string(mem_ctx, 
					 dom_sid_add_rid(mem_ctx, 
							 state->dom_sid[database], 
							 rid))));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else {
		msg->dn = talloc_steal(msg, msgs[0]->dn);
	}
	
	talloc_free(msgs);

	for (i=0; i<group_member->num_rids; i++) {
		/* search for the group, by rid */
		ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database], &msgs, attrs,
				   "(&(objectClass=user)(objectSid=%s))", 
				   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], group_member->rids[i]))); 

		if (ret == -1) {
			DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		} else if (ret == 0) {
			return NT_STATUS_NO_SUCH_USER;
		} else if (ret > 1) {
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		} else {
			samdb_msg_add_string(state->sam_ldb, mem_ctx, msg, "member", ldb_dn_linearize(mem_ctx, msgs[0]->dn));
		}
	
		talloc_free(msgs);
	}

	ret = samdb_replace(state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		DEBUG(0,("Failed to modify group record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn),
			 ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_handle_alias(TALLOC_CTX *mem_ctx,
					 struct samsync_ldb_state *state,
					 struct creds_CredentialState *creds,
					 enum netr_SamDatabaseID database,
					 struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct netr_DELTA_ALIAS *alias = delta->delta_union.alias;
	const char *container, *obj_class;
	const char *cn_name;

	struct ldb_message *msg;
	struct ldb_message **msgs;
	int ret;
	BOOL add = False;
	const char *attrs[] = { NULL };

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the alias, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database], &msgs, attrs,
			   "(&(objectClass=group)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		add = True;
	} else if (ret > 1) {
		DEBUG(0, ("More than one group/alias with SID: %s\n", 
			  dom_sid_string(mem_ctx, 
					 dom_sid_add_rid(mem_ctx, 
							 state->dom_sid[database], 
							 rid))));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else {
		msg->dn = talloc_steal(mem_ctx, msgs[0]->dn);
	}

	cn_name   = alias->alias_name.string;

#define ADD_OR_DEL(type, attrib, field) do {\
	if (alias->field) { \
		samdb_msg_add_ ## type(state->sam_ldb, mem_ctx, msg, \
				     attrib, alias->field); \
	} else if (!add) { \
		samdb_msg_add_delete(state->sam_ldb, mem_ctx, msg,  \
				     attrib); \
	} \
        } while (0);

        ADD_OR_DEL(string, "samAccountName", alias_name.string);

	if (samdb_msg_add_dom_sid(state->sam_ldb, mem_ctx, msg, 
				  "objectSid", dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))) {
		return NT_STATUS_NO_MEMORY; 
	}

	ADD_OR_DEL(string, "description", description.string);

#undef ADD_OR_DEL

	samdb_msg_add_uint(state->sam_ldb, mem_ctx, msg, "groupType", 0x80000004);

	container = "Users";
	obj_class = "group";

	if (add) {
		samdb_msg_add_string(state->sam_ldb, mem_ctx, msg, 
				     "objectClass", obj_class);
		msg->dn = ldb_dn_string_compose(mem_ctx, state->base_dn[database],
						"CN=%s, CN=%s", cn_name, container);
		if (!msg->dn) {
			return NT_STATUS_NO_MEMORY;		
		}

		ret = samdb_add(state->sam_ldb, mem_ctx, msg);
		if (ret != 0) {
			DEBUG(0,("Failed to create alias record %s: %s\n",
				 ldb_dn_linearize(mem_ctx, msg->dn),
				 ldb_errstring(state->sam_ldb)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	} else {
		ret = samdb_replace(state->sam_ldb, mem_ctx, msg);
		if (ret != 0) {
			DEBUG(0,("Failed to modify alias record %s: %s\n",
				 ldb_dn_linearize(mem_ctx, msg->dn),
				 ldb_errstring(state->sam_ldb)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		}
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_delete_alias(TALLOC_CTX *mem_ctx,
					struct samsync_ldb_state *state,
					struct creds_CredentialState *creds,
					enum netr_SamDatabaseID database,
					struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct ldb_message **msgs;
	int ret;
	const char *attrs[] = { NULL };

	/* search for the alias, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database], &msgs, attrs,
			   "(&(objectClass=group)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		return NT_STATUS_NO_SUCH_ALIAS;
	} else if (ret > 1) {
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	ret = samdb_delete(state->sam_ldb, mem_ctx, msgs[0]->dn);
	if (ret != 0) {
		DEBUG(0,("Failed to delete alias record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msgs[0]->dn),
			 ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_handle_alias_member(TALLOC_CTX *mem_ctx,
						struct samsync_ldb_state *state,
						struct creds_CredentialState *creds,
						enum netr_SamDatabaseID database,
						struct netr_DELTA_ENUM *delta) 
{
	uint32_t rid = delta->delta_id_union.rid;
	struct netr_DELTA_ALIAS_MEMBER *alias_member = delta->delta_union.alias_member;
	struct ldb_message *msg;
	struct ldb_message **msgs;
	int ret;
	const char *attrs[] = { NULL };
	int i;

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the alias, by rid */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[database], &msgs, attrs,
			   "(&(objectClass=group)(objectSid=%s))", 
			   ldap_encode_ndr_dom_sid(mem_ctx, dom_sid_add_rid(mem_ctx, state->dom_sid[database], rid))); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		return NT_STATUS_NO_SUCH_GROUP;
	} else if (ret > 1) {
		DEBUG(0, ("More than one group/alias with SID: %s\n", 
			  dom_sid_string(mem_ctx, 
					 dom_sid_add_rid(mem_ctx, 
							 state->dom_sid[database], 
							 rid))));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else {
		msg->dn = talloc_steal(msg, msgs[0]->dn);
	}
	
	talloc_free(msgs);

	for (i=0; i<alias_member->sids.num_sids; i++) {
		struct ldb_dn *alias_member_dn;
		/* search for members, in the top basedn (normal users are builtin aliases) */
		ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[SAM_DATABASE_DOMAIN], &msgs, attrs,
				   "(objectSid=%s)", 
				   ldap_encode_ndr_dom_sid(mem_ctx, alias_member->sids.sids[i].sid)); 

		if (ret == -1) {
			DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		} else if (ret == 0) {
			NTSTATUS nt_status;
			nt_status = samsync_ldb_add_foreignSecurityPrincipal(mem_ctx, state,
									     alias_member->sids.sids[i].sid, 
									     &alias_member_dn);
			if (!NT_STATUS_IS_OK(nt_status)) {
				return nt_status;
			}
 		} else if (ret > 1) {
			return NT_STATUS_INTERNAL_DB_CORRUPTION;
		} else {
			alias_member_dn = msgs[0]->dn;
		}
		samdb_msg_add_string(state->sam_ldb, mem_ctx, msg, "member", ldb_dn_linearize(mem_ctx, alias_member_dn));
	
		talloc_free(msgs);
	}

	ret = samdb_replace(state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		DEBUG(0,("Failed to modify group record %s: %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn),
			 ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_handle_account(TALLOC_CTX *mem_ctx,
					struct samsync_ldb_state *state,
					struct creds_CredentialState *creds,
					enum netr_SamDatabaseID database,
					struct netr_DELTA_ENUM *delta) 
{
	struct dom_sid *sid = delta->delta_id_union.sid;
	struct netr_DELTA_ACCOUNT *account = delta->delta_union.account;

	struct ldb_message *msg;
	struct ldb_message **msgs;
	struct ldb_dn *privilege_dn;
	int ret;
	const char *attrs[] = { NULL };
	int i;

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the account, by sid, in the top basedn */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[SAM_DATABASE_DOMAIN], &msgs, attrs,
			   "(objectSid=%s)", ldap_encode_ndr_dom_sid(mem_ctx, sid)); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		NTSTATUS nt_status;
		nt_status = samsync_ldb_add_foreignSecurityPrincipal(mem_ctx, state,
								     sid,
								     &privilege_dn);
		privilege_dn = talloc_steal(msg, privilege_dn);
		if (!NT_STATUS_IS_OK(nt_status)) {
			return nt_status;
		}
	} else if (ret > 1) {
		DEBUG(0, ("More than one account with SID: %s\n", 
			  dom_sid_string(mem_ctx, sid)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else {
		privilege_dn = talloc_steal(msg, msgs[0]->dn);
	}

	msg->dn = privilege_dn;

	for (i=0; i< account->privilege_entries; i++) {
		samdb_msg_add_string(state->sam_ldb, mem_ctx, msg, "privilege",
				     account->privilege_name[i].string);
	}

	ret = samdb_replace(state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		DEBUG(0,("Failed to modify privilege record %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	return NT_STATUS_OK;
}

static NTSTATUS samsync_ldb_delete_account(TALLOC_CTX *mem_ctx,
					struct samsync_ldb_state *state,
					struct creds_CredentialState *creds,
					enum netr_SamDatabaseID database,
					struct netr_DELTA_ENUM *delta) 
{
	struct dom_sid *sid = delta->delta_id_union.sid;

	struct ldb_message *msg;
	struct ldb_message **msgs;
	int ret;
	const char *attrs[] = { NULL };

	msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) {
		return NT_STATUS_NO_MEMORY;
	}

	/* search for the account, by sid, in the top basedn */
	ret = gendb_search(state->sam_ldb, mem_ctx, state->base_dn[SAM_DATABASE_DOMAIN], &msgs, attrs,
			   "(objectSid=%s)", 
			   ldap_encode_ndr_dom_sid(mem_ctx, sid)); 

	if (ret == -1) {
		DEBUG(0, ("gendb_search failed: %s\n", ldb_errstring(state->sam_ldb)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else if (ret == 0) {
		return NT_STATUS_NO_SUCH_USER;
	} else if (ret > 1) {
		DEBUG(0, ("More than one account with SID: %s\n", 
			  dom_sid_string(mem_ctx, sid)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	} else {
		msg->dn = talloc_steal(msg, msgs[0]->dn);
	}

	samdb_msg_add_delete(state->sam_ldb, mem_ctx, msg,  
			     "privilage"); 

	ret = samdb_replace(state->sam_ldb, mem_ctx, msg);
	if (ret != 0) {
		DEBUG(0,("Failed to modify privilege record %s\n",
			 ldb_dn_linearize(mem_ctx, msg->dn)));
		return NT_STATUS_INTERNAL_DB_CORRUPTION;
	}

	return NT_STATUS_OK;
}

static NTSTATUS libnet_samsync_ldb_fn(TALLOC_CTX *mem_ctx, 		
				  void *private, 			
				  struct creds_CredentialState *creds,
				  enum netr_SamDatabaseID database,
				  struct netr_DELTA_ENUM *delta,
				  char **error_string)
{
	NTSTATUS nt_status = NT_STATUS_OK;
	struct samsync_ldb_state *state = private;

	*error_string = NULL;
	switch (delta->delta_type) {
	case NETR_DELTA_DOMAIN:
	{
		nt_status = samsync_ldb_handle_domain(mem_ctx, 
						      state,
						      creds,
						      database,
						      delta);
		break;
	}
	case NETR_DELTA_USER:
	{
		nt_status = samsync_ldb_handle_user(mem_ctx, 
						    state,
						    creds,
						    database,
						    delta);
		break;
	}
	case NETR_DELTA_DELETE_USER:
	{
		nt_status = samsync_ldb_delete_user(mem_ctx, 
						    state,
						    creds,
						    database,
						    delta);
		break;
	}
	case NETR_DELTA_GROUP:
	{
		nt_status = samsync_ldb_handle_group(mem_ctx, 
						     state,
						     creds,
						     database,
						     delta);
		break;
	}
	case NETR_DELTA_DELETE_GROUP:
	{
		nt_status = samsync_ldb_delete_group(mem_ctx, 
						    state,
						    creds,
						    database,
						    delta);
		break;
	}
	case NETR_DELTA_GROUP_MEMBER:
	{
		nt_status = samsync_ldb_handle_group_member(mem_ctx, 
							    state,
							    creds,
							    database,
							    delta);
		break;
	}
	case NETR_DELTA_ALIAS:
	{
		nt_status = samsync_ldb_handle_alias(mem_ctx, 
						     state,
						     creds,
						     database,
						     delta);
		break;
	}
	case NETR_DELTA_DELETE_ALIAS:
	{
		nt_status = samsync_ldb_delete_alias(mem_ctx, 
						    state,
						    creds,
						    database,
						    delta);
		break;
	}
	case NETR_DELTA_ALIAS_MEMBER:
	{
		nt_status = samsync_ldb_handle_alias_member(mem_ctx, 
							    state,
							    creds,
							    database,
							    delta);
		break;
	}
	case NETR_DELTA_ACCOUNT:
	{
		nt_status = samsync_ldb_handle_account(mem_ctx, 
						     state,
						     creds,
						     database,
						     delta);
		break;
	}
	case NETR_DELTA_DELETE_ACCOUNT:
	{
		nt_status = samsync_ldb_delete_account(mem_ctx, 
						    state,
						    creds,
						    database,
						    delta);
		break;
	}
	default:
		/* Can't dump them all right now */
		break;
	}
	return nt_status;
}

static NTSTATUS libnet_samsync_ldb_netlogon(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, struct libnet_samsync_ldb *r)
{
	NTSTATUS nt_status;
	struct libnet_SamSync r2;
	struct samsync_ldb_state *state = talloc(mem_ctx, struct samsync_ldb_state);

	if (!state) {
		return NT_STATUS_NO_MEMORY;
	}

	state->secrets = NULL;
	state->trusted_domains = NULL;

	state->sam_ldb = samdb_connect(state, system_session(state));

	r2.error_string = NULL;
	r2.delta_fn = libnet_samsync_ldb_fn;
	r2.fn_ctx = state;
	r2.machine_account = NULL; /* TODO:  Create a machine account, fill this in, and the delete it */
	nt_status = libnet_SamSync_netlogon(ctx, state, &r2);
	r->error_string = r2.error_string;

	if (!NT_STATUS_IS_OK(nt_status)) {
		talloc_free(state);
		return nt_status;
	}
	talloc_free(state);
	return nt_status;
}



static NTSTATUS libnet_samsync_ldb_generic(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, struct libnet_samsync_ldb *r)
{
	NTSTATUS nt_status;
	struct libnet_samsync_ldb r2;
	r2.level = LIBNET_SAMSYNC_LDB_NETLOGON;
	r2.error_string = NULL;
	nt_status = libnet_samsync_ldb(ctx, mem_ctx, &r2);
	r->error_string = r2.error_string;
	
	return nt_status;
}

NTSTATUS libnet_samsync_ldb(struct libnet_context *ctx, TALLOC_CTX *mem_ctx, struct libnet_samsync_ldb *r)
{
	switch (r->level) {
	case LIBNET_SAMSYNC_LDB_GENERIC:
		return libnet_samsync_ldb_generic(ctx, mem_ctx, r);
	case LIBNET_SAMSYNC_LDB_NETLOGON:
		return libnet_samsync_ldb_netlogon(ctx, mem_ctx, r);
	}

	return NT_STATUS_INVALID_LEVEL;
}
