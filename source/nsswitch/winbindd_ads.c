/* 
   Unix SMB/CIFS implementation.

   Winbind ADS backend functions

   Copyright (C) Andrew Tridgell 2001
   
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

#include "winbindd.h"

#ifdef HAVE_ADS

/* the realm of our primary LDAP server */
static char *primary_realm;


/*
  a wrapper around ldap_search_s that retries depending on the error code
  this is supposed to catch dropped connections and auto-reconnect
*/
ADS_STATUS ads_do_search_retry(ADS_STRUCT *ads, const char *bind_path, int scope, 
			       const char *exp,
			       const char **attrs, void **res)
{
	ADS_STATUS status;
	int count = 3;
	char *bp;

	if (!ads->ld &&
	    time(NULL) - ads->last_attempt < ADS_RECONNECT_TIME) {
		return ADS_ERROR(LDAP_SERVER_DOWN);
	}

	bp = strdup(bind_path);

	while (count--) {
		status = ads_do_search_all(ads, bp, scope, exp, attrs, res);
		if (ADS_ERR_OK(status)) {
			DEBUG(5,("Search for %s gave %d replies\n",
				 exp, ads_count_replies(ads, *res)));
			free(bp);
			return status;
		}

		if (*res) ads_msgfree(ads, *res);
		*res = NULL;
		DEBUG(3,("Reopening ads connection to %s after error %s\n", 
			 ads->ldap_server, ads_errstr(status)));
		if (ads->ld) {
			ldap_unbind(ads->ld); 
		}
		ads->ld = NULL;
		status = ads_connect(ads);
		if (!ADS_ERR_OK(status)) {
			DEBUG(1,("ads_search_retry: failed to reconnect (%s)\n",
				 ads_errstr(status)));
			ads_destroy(&ads);
			free(bp);
			return status;
		}
	}
	free(bp);

	DEBUG(1,("ads reopen failed after error %s\n", ads_errstr(status)));
	return status;
}


ADS_STATUS ads_search_retry(ADS_STRUCT *ads, void **res, 
			    const char *exp, 
			    const char **attrs)
{
	return ads_do_search_retry(ads, ads->bind_path, LDAP_SCOPE_SUBTREE,
				   exp, attrs, res);
}

ADS_STATUS ads_search_retry_dn(ADS_STRUCT *ads, void **res, 
			       const char *dn, 
			       const char **attrs)
{
	return ads_do_search_retry(ads, dn, LDAP_SCOPE_BASE,
				   "(objectclass=*)", attrs, res);
}

/*
  return our ads connections structure for a domain. We keep the connection
  open to make things faster
*/
static ADS_STRUCT *ads_cached_connection(struct winbindd_domain *domain)
{
	ADS_STRUCT *ads;
	ADS_STATUS status;
	char *ccache;
	struct in_addr server_ip;
	char *sname;

	if (domain->private) {
		return (ADS_STRUCT *)domain->private;
	}

	/* we don't want this to affect the users ccache */
	ccache = lock_path("winbindd_ccache");
	SETENV("KRB5CCNAME", ccache, 1);
	unlink(ccache);

	if (resolve_name(domain->name, &server_ip, 0x1b)) {
		sname = inet_ntoa(server_ip);
	} else if (resolve_name(domain->name, &server_ip, 0x1c)) {
		sname = inet_ntoa(server_ip);
	} else {
		if (strcasecmp(domain->name, lp_workgroup()) != 0) {
			DEBUG(1,("can't find domain controller for %s\n", domain->name));
			return NULL;
		}
		sname = NULL;
	}

	ads = ads_init(primary_realm, domain->name, NULL, NULL, NULL);
	if (!ads) {
		DEBUG(1,("ads_init for domain %s failed\n", domain->name));
		return NULL;
	}

	/* the machine acct password might have change - fetch it every time */
	SAFE_FREE(ads->password);
	ads->password = secrets_fetch_machine_password();

	status = ads_connect(ads);
	if (!ADS_ERR_OK(status)) {
		extern struct winbindd_methods msrpc_methods;
		DEBUG(1,("ads_connect for domain %s failed: %s\n", 
			 domain->name, ads_errstr(status)));
		ads_destroy(&ads);

		/* if we get ECONNREFUSED then it might be a NT4
                   server, fall back to MSRPC */
		if (status.error_type == ADS_ERROR_SYSTEM &&
		    status.rc == ECONNREFUSED) {
			DEBUG(1,("Trying MSRPC methods\n"));
			domain->methods = &msrpc_methods;
		}
		return NULL;
	}

	/* remember our primary realm for trusted domain support */
	if (!primary_realm) {
		primary_realm = strdup(ads->realm);
	}

	fstrcpy(domain->full_name, ads->server_realm);

	domain->private = (void *)ads;
	return ads;
}

/* useful utility */
static void sid_from_rid(struct winbindd_domain *domain, uint32 rid, DOM_SID *sid)
{
	sid_copy(sid, &domain->sid);
	sid_append_rid(sid, rid);
}

/* turn a sAMAccountType into a SID_NAME_USE */
static enum SID_NAME_USE ads_atype_map(uint32 atype)
{
	switch (atype & 0xF0000000) {
	case ATYPE_GROUP:
		return SID_NAME_DOM_GRP;
	case ATYPE_USER:
		return SID_NAME_USER;
	default:
		DEBUG(1,("hmm, need to map account type 0x%x\n", atype));
	}
	return SID_NAME_UNKNOWN;
}

/* 
   in order to support usernames longer than 21 characters we need to 
   use both the sAMAccountName and the userPrincipalName attributes 
   It seems that not all users have the userPrincipalName attribute set
*/
static char *pull_username(ADS_STRUCT *ads, TALLOC_CTX *mem_ctx, void *msg)
{
	char *ret, *p;

	ret = ads_pull_string(ads, mem_ctx, msg, "userPrincipalName");
	if (ret && (p = strchr(ret, '@'))) {
		*p = 0;
		return ret;
	}
	return ads_pull_string(ads, mem_ctx, msg, "sAMAccountName");
}


/* Query display info for a realm. This is the basic user list fn */
static NTSTATUS query_user_list(struct winbindd_domain *domain,
			       TALLOC_CTX *mem_ctx,
			       uint32 *num_entries, 
			       WINBIND_USERINFO **info)
{
	ADS_STRUCT *ads = NULL;
	const char *attrs[] = {"userPrincipalName",
			       "sAMAccountName",
			       "name", "objectSid", "primaryGroupID", 
			       "sAMAccountType", NULL};
	int i, count;
	ADS_STATUS rc;
	void *res = NULL;
	void *msg = NULL;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	*num_entries = 0;

	DEBUG(3,("ads: query_user_list\n"));

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	rc = ads_search_retry(ads, &res, "(objectCategory=user)", attrs);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("query_user_list ads_search: %s\n", ads_errstr(rc)));
		goto done;
	}

	count = ads_count_replies(ads, res);
	if (count == 0) {
		DEBUG(1,("query_user_list: No users found\n"));
		goto done;
	}

	(*info) = talloc_zero(mem_ctx, count * sizeof(**info));
	if (!*info) {
		status = NT_STATUS_NO_MEMORY;
		goto done;
	}

	i = 0;

	for (msg = ads_first_entry(ads, res); msg; msg = ads_next_entry(ads, msg)) {
		char *name, *gecos;
		DOM_SID sid;
		uint32 rid, group;
		uint32 atype;

		if (!ads_pull_uint32(ads, msg, "sAMAccountType", &atype) ||
		    ads_atype_map(atype) != SID_NAME_USER) {
			DEBUG(1,("Not a user account? atype=0x%x\n", atype));
			continue;
		}

		name = pull_username(ads, mem_ctx, msg);
		gecos = ads_pull_string(ads, mem_ctx, msg, "name");
		if (!ads_pull_sid(ads, msg, "objectSid", &sid)) {
			DEBUG(1,("No sid for %s !?\n", name));
			continue;
		}
		if (!ads_pull_uint32(ads, msg, "primaryGroupID", &group)) {
			DEBUG(1,("No primary group for %s !?\n", name));
			continue;
		}

		if (!sid_peek_rid(&sid, &rid)) {
			DEBUG(1,("No rid for %s !?\n", name));
			continue;
		}

		(*info)[i].acct_name = name;
		(*info)[i].full_name = gecos;
		(*info)[i].user_rid = rid;
		(*info)[i].group_rid = group;
		i++;
	}

	(*num_entries) = i;
	status = NT_STATUS_OK;

	DEBUG(3,("ads query_user_list gave %d entries\n", (*num_entries)));

done:
	if (res) ads_msgfree(ads, res);

	return status;
}

/* list all domain groups */
static NTSTATUS enum_dom_groups(struct winbindd_domain *domain,
				TALLOC_CTX *mem_ctx,
				uint32 *num_entries, 
				struct acct_info **info)
{
	ADS_STRUCT *ads = NULL;
	const char *attrs[] = {"userPrincipalName", "sAMAccountName",
			       "name", "objectSid", 
			       "sAMAccountType", NULL};
	int i, count;
	ADS_STATUS rc;
	void *res = NULL;
	void *msg = NULL;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	*num_entries = 0;

	DEBUG(3,("ads: enum_dom_groups\n"));

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	rc = ads_search_retry(ads, &res, "(objectCategory=group)", attrs);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("enum_dom_groups ads_search: %s\n", ads_errstr(rc)));
		goto done;
	}

	count = ads_count_replies(ads, res);
	if (count == 0) {
		DEBUG(1,("enum_dom_groups: No groups found\n"));
		goto done;
	}

	(*info) = talloc_zero(mem_ctx, count * sizeof(**info));
	if (!*info) {
		status = NT_STATUS_NO_MEMORY;
		goto done;
	}

	i = 0;

	for (msg = ads_first_entry(ads, res); msg; msg = ads_next_entry(ads, msg)) {
		char *name, *gecos;
		DOM_SID sid;
		uint32 rid;
		uint32 account_type;

		if (!ads_pull_uint32(ads, msg, "sAMAccountType", 
				     &account_type) ||
		    !(account_type & ATYPE_GROUP)) continue;

		name = pull_username(ads, mem_ctx, msg);
		gecos = ads_pull_string(ads, mem_ctx, msg, "name");
		if (!ads_pull_sid(ads, msg, "objectSid", &sid)) {
			DEBUG(1,("No sid for %s !?\n", name));
			continue;
		}

		if (!sid_peek_rid(&sid, &rid)) {
			DEBUG(1,("No rid for %s !?\n", name));
			continue;
		}

		fstrcpy((*info)[i].acct_name, name);
		fstrcpy((*info)[i].acct_desc, gecos);
		(*info)[i].rid = rid;
		i++;
	}

	(*num_entries) = i;

	status = NT_STATUS_OK;

	DEBUG(3,("ads enum_dom_groups gave %d entries\n", (*num_entries)));

done:
	if (res) ads_msgfree(ads, res);

	return status;
}


/* convert a single name to a sid in a domain */
static NTSTATUS name_to_sid(struct winbindd_domain *domain,
			    const char *name,
			    DOM_SID *sid,
			    enum SID_NAME_USE *type)
{
	ADS_STRUCT *ads = NULL;
	const char *attrs[] = {"objectSid", "sAMAccountType", NULL};
	int count;
	ADS_STATUS rc;
	void *res = NULL;
	char *exp;
	uint32 t;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	DEBUG(3,("ads: name_to_sid\n"));

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	/* accept either the win2000 or the pre-win2000 username */
	asprintf(&exp, "(|(sAMAccountName=%s)(userPrincipalName=%s@%s))", 
		 name, name, ads->realm);
	rc = ads_search_retry(ads, &res, exp, attrs);
	free(exp);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("name_to_sid ads_search: %s\n", ads_errstr(rc)));
		goto done;
	}

	count = ads_count_replies(ads, res);
	if (count != 1) {
		DEBUG(1,("name_to_sid: %s not found\n", name));
		goto done;
	}

	if (!ads_pull_sid(ads, res, "objectSid", sid)) {
		DEBUG(1,("No sid for %s !?\n", name));
		goto done;
	}

	if (!ads_pull_uint32(ads, res, "sAMAccountType", &t)) {
		DEBUG(1,("No sAMAccountType for %s !?\n", name));
		goto done;
	}

	*type = ads_atype_map(t);

	status = NT_STATUS_OK;

	DEBUG(3,("ads name_to_sid mapped %s\n", name));

done:
	if (res) ads_msgfree(ads, res);

	return status;
}

/* convert a sid to a user or group name */
static NTSTATUS sid_to_name(struct winbindd_domain *domain,
			    TALLOC_CTX *mem_ctx,
			    DOM_SID *sid,
			    char **name,
			    enum SID_NAME_USE *type)
{
	ADS_STRUCT *ads = NULL;
	const char *attrs[] = {"userPrincipalName", 
			       "sAMAccountName",
			       "sAMAccountType", NULL};
	ADS_STATUS rc;
	void *msg = NULL;
	char *exp;
	char *sidstr;
	uint32 atype;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	DEBUG(3,("ads: sid_to_name\n"));

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	sidstr = sid_binstring(sid);
	asprintf(&exp, "(objectSid=%s)", sidstr);
	rc = ads_search_retry(ads, &msg, exp, attrs);
	free(exp);
	free(sidstr);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("sid_to_name ads_search: %s\n", ads_errstr(rc)));
		goto done;
	}

	if (!ads_pull_uint32(ads, msg, "sAMAccountType", &atype)) {
		goto done;
	}

	*name = pull_username(ads, mem_ctx, msg);
	*type = ads_atype_map(atype);

	status = NT_STATUS_OK;

	DEBUG(3,("ads sid_to_name mapped %s\n", *name));

done:
	if (msg) ads_msgfree(ads, msg);

	return status;
}


/* convert a sid to a distnguished name */
static NTSTATUS sid_to_distinguished_name(struct winbindd_domain *domain,
					  TALLOC_CTX *mem_ctx,
					  DOM_SID *sid,
					  char **dn)
{
	ADS_STRUCT *ads = NULL;
	const char *attrs[] = {"distinguishedName", NULL};
	ADS_STATUS rc;
	void *msg = NULL;
	char *exp;
	char *sidstr;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	DEBUG(3,("ads: sid_to_distinguished_name\n"));

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	sidstr = sid_binstring(sid);
	asprintf(&exp, "(objectSid=%s)", sidstr);
	rc = ads_search_retry(ads, &msg, exp, attrs);
	free(exp);
	free(sidstr);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("sid_to_distinguished_name ads_search: %s\n", ads_errstr(rc)));
		goto done;
	}

	*dn = ads_pull_string(ads, mem_ctx, msg, "distinguishedName");

	status = NT_STATUS_OK;

	DEBUG(3,("ads sid_to_distinguished_name mapped %s\n", *dn));

done:
	if (msg) ads_msgfree(ads, msg);

	return status;
}


/* Lookup user information from a rid */
static NTSTATUS query_user(struct winbindd_domain *domain, 
			   TALLOC_CTX *mem_ctx, 
			   uint32 user_rid, 
			   WINBIND_USERINFO *info)
{
	ADS_STRUCT *ads = NULL;
	const char *attrs[] = {"userPrincipalName", 
			       "sAMAccountName",
			       "name", "objectSid", 
			       "primaryGroupID", NULL};
	ADS_STATUS rc;
	int count;
	void *msg = NULL;
	char *exp;
	DOM_SID sid;
	char *sidstr;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	DEBUG(3,("ads: query_user\n"));

	sid_from_rid(domain, user_rid, &sid);

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	sidstr = sid_binstring(&sid);
	asprintf(&exp, "(objectSid=%s)", sidstr);
	rc = ads_search_retry(ads, &msg, exp, attrs);
	free(exp);
	free(sidstr);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("query_user(rid=%d) ads_search: %s\n", user_rid, ads_errstr(rc)));
		goto done;
	}

	count = ads_count_replies(ads, msg);
	if (count != 1) {
		DEBUG(1,("query_user(rid=%d): Not found\n", user_rid));
		goto done;
	}

	info->acct_name = pull_username(ads, mem_ctx, msg);
	info->full_name = ads_pull_string(ads, mem_ctx, msg, "name");
	if (!ads_pull_sid(ads, msg, "objectSid", &sid)) {
		DEBUG(1,("No sid for %d !?\n", user_rid));
		goto done;
	}
	if (!ads_pull_uint32(ads, msg, "primaryGroupID", &info->group_rid)) {
		DEBUG(1,("No primary group for %d !?\n", user_rid));
		goto done;
	}
	
	if (!sid_peek_rid(&sid, &info->user_rid)) {
		DEBUG(1,("No rid for %d !?\n", user_rid));
		goto done;
	}

	status = NT_STATUS_OK;

	DEBUG(3,("ads query_user gave %s\n", info->acct_name));
done:
	if (msg) ads_msgfree(ads, msg);

	return status;
}


/* Lookup groups a user is a member of. */
static NTSTATUS lookup_usergroups(struct winbindd_domain *domain,
				  TALLOC_CTX *mem_ctx,
				  uint32 user_rid, 
				  uint32 *num_groups, uint32 **user_gids)
{
	ADS_STRUCT *ads = NULL;
	const char *attrs[] = {"distinguishedName", NULL};
	const char *attrs2[] = {"tokenGroups", "primaryGroupID", NULL};
	ADS_STATUS rc;
	int count;
	void *msg = NULL;
	char *exp;
	char *user_dn;
	DOM_SID *sids;
	int i;
	uint32 primary_group;
	DOM_SID sid;
	char *sidstr;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	*num_groups = 0;

	DEBUG(3,("ads: lookup_usergroups\n"));

	(*num_groups) = 0;

	sid_from_rid(domain, user_rid, &sid);

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	sidstr = sid_binstring(&sid);
	asprintf(&exp, "(objectSid=%s)", sidstr);
	rc = ads_search_retry(ads, &msg, exp, attrs);
	free(exp);
	free(sidstr);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("lookup_usergroups(rid=%d) ads_search: %s\n", user_rid, ads_errstr(rc)));
		goto done;
	}

	user_dn = ads_pull_string(ads, mem_ctx, msg, "distinguishedName");

	if (msg) ads_msgfree(ads, msg);

	rc = ads_search_retry_dn(ads, &msg, user_dn, attrs2);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("lookup_usergroups(rid=%d) ads_search tokenGroups: %s\n", user_rid, ads_errstr(rc)));
		goto done;
	}

	if (!ads_pull_uint32(ads, msg, "primaryGroupID", &primary_group)) {
		DEBUG(1,("%s: No primary group for rid=%d !?\n", domain->name, user_rid));
		goto done;
	}

	count = ads_pull_sids(ads, mem_ctx, msg, "tokenGroups", &sids) + 1;
	(*user_gids) = (uint32 *)talloc_zero(mem_ctx, sizeof(uint32) * count);
	(*user_gids)[(*num_groups)++] = primary_group;

	for (i=1;i<count;i++) {
		uint32 rid;
		if (!sid_peek_rid(&sids[i-1], &rid)) continue;
		(*user_gids)[*num_groups] = rid;
		(*num_groups)++;
	}

	status = NT_STATUS_OK;
	DEBUG(3,("ads lookup_usergroups for rid=%d\n", user_rid));
done:
	if (msg) ads_msgfree(ads, msg);

	return status;
}


static NTSTATUS lookup_groupmem(struct winbindd_domain *domain,
				TALLOC_CTX *mem_ctx,
				uint32 group_rid, uint32 *num_names, 
				uint32 **rid_mem, char ***names, 
				uint32 **name_types)
{
	DOM_SID group_sid;
	const char *attrs[] = {"userPrincipalName", "sAMAccountName",
			       "objectSid", "sAMAccountType", NULL};
	ADS_STATUS rc;
	int count;
	void *res=NULL, *msg=NULL;
	ADS_STRUCT *ads = NULL;
	char *exp, *dn = NULL;
	NTSTATUS status = NT_STATUS_UNSUCCESSFUL;

	*num_names = 0;

	ads = ads_cached_connection(domain);
	if (!ads) goto done;

	sid_from_rid(domain, group_rid, &group_sid);
	status = sid_to_distinguished_name(domain, mem_ctx, &group_sid, &dn);
	if (!NT_STATUS_IS_OK(status)) {
		DEBUG(3,("Failed to find distinguishedName for %s\n", sid_string_static(&group_sid)));
		return status;
	}

	/* search for all users who have that group sid as primary group or as member */
	asprintf(&exp, "(&(objectCategory=user)(|(primaryGroupID=%d)(memberOf=%s)))",
		 group_rid, dn);
	rc = ads_search_retry(ads, &res, exp, attrs);
	free(exp);
	if (!ADS_ERR_OK(rc)) {
		DEBUG(1,("query_user_list ads_search: %s\n", ads_errstr(rc)));
		goto done;
	}

	count = ads_count_replies(ads, res);
	if (count == 0) {
		status = NT_STATUS_OK;
		goto done;
	}

	(*rid_mem) = talloc_zero(mem_ctx, sizeof(uint32) * count);
	(*name_types) = talloc_zero(mem_ctx, sizeof(uint32) * count);
	(*names) = talloc_zero(mem_ctx, sizeof(char *) * count);

	for (msg = ads_first_entry(ads, res); msg; msg = ads_next_entry(ads, msg)) {
		uint32 atype, rid;
		DOM_SID sid;

		(*names)[*num_names] = pull_username(ads, mem_ctx, msg);
		if (!ads_pull_uint32(ads, msg, "sAMAccountType", &atype)) {
			continue;
		}
		(*name_types)[*num_names] = ads_atype_map(atype);
		if (!ads_pull_sid(ads, msg, "objectSid", &sid)) {
			DEBUG(1,("No sid for %s !?\n", (*names)[*num_names]));
			continue;
		}
		if (!sid_peek_rid(&sid, &rid)) {
			DEBUG(1,("No rid for %s !?\n", (*names)[*num_names]));
			continue;
		}
		(*rid_mem)[*num_names] = rid;
		(*num_names)++;
	}	

	status = NT_STATUS_OK;
	DEBUG(3,("ads lookup_groupmem for rid=%d\n", group_rid));
done:
	if (res) ads_msgfree(ads, res);

	return status;
}

/* find the sequence number for a domain */
static NTSTATUS sequence_number(struct winbindd_domain *domain, uint32 *seq)
{
	ADS_STRUCT *ads = NULL;
	ADS_STATUS rc;

	*seq = DOM_SEQUENCE_NONE;

	ads = ads_cached_connection(domain);
	if (!ads) return NT_STATUS_UNSUCCESSFUL;

	rc = ads_USN(ads, seq);
	if (!ADS_ERR_OK(rc)) {
		/* its a dead connection */
		ads_destroy(&ads);
		domain->private = NULL;
	}
	return ads_ntstatus(rc);
}

/* get a list of trusted domains */
static NTSTATUS trusted_domains(struct winbindd_domain *domain,
				TALLOC_CTX *mem_ctx,
				uint32 *num_domains,
				char ***names,
				DOM_SID **dom_sids)
{
	ADS_STRUCT *ads;
	ADS_STATUS rc;

	*num_domains = 0;
	*names = NULL;

	ads = ads_cached_connection(domain);
	if (!ads) return NT_STATUS_UNSUCCESSFUL;

	rc = ads_trusted_domains(ads, mem_ctx, num_domains, names, dom_sids);

	return ads_ntstatus(rc);
}

/* find the domain sid for a domain */
static NTSTATUS domain_sid(struct winbindd_domain *domain, DOM_SID *sid)
{
	ADS_STRUCT *ads;
	ADS_STATUS rc;

	ads = ads_cached_connection(domain);
	if (!ads) return NT_STATUS_UNSUCCESSFUL;

	rc = ads_domain_sid(ads, sid);

	if (!ADS_ERR_OK(rc)) {
		/* its a dead connection */
		ads_destroy(&ads);
		domain->private = NULL;
	}

	return ads_ntstatus(rc);
}

/* the ADS backend methods are exposed via this structure */
struct winbindd_methods ads_methods = {
	True,
	query_user_list,
	enum_dom_groups,
	name_to_sid,
	sid_to_name,
	query_user,
	lookup_usergroups,
	lookup_groupmem,
	sequence_number,
	trusted_domains,
	domain_sid
};

#endif
