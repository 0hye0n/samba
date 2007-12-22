/* 
   Unix SMB/CIFS implementation.
   Winbind Utility functions

   Copyright (C) Gerald (Jerry) Carter   2007
   
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

#if defined(WITH_WINBIND)

#include "nsswitch/libwbclient/wbclient.h"

/* Call winbindd to convert a name to a sid */

bool winbind_lookup_name(const char *dom_name, const char *name, DOM_SID *sid, 
                         enum lsa_SidType *name_type)
{
	struct wbcDomainSid dom_sid;
	wbcErr result;
	enum wbcSidType type;	

	result = wbcLookupName(dom_name, name, &dom_sid, &type);
	if (result != WBC_ERR_SUCCESS)
		return False;

	memcpy(sid, &dom_sid, sizeof(DOM_SID));	
	*name_type = (enum lsa_SidType)type;	

	return True;	
}

/* Call winbindd to convert sid to name */

bool winbind_lookup_sid(TALLOC_CTX *mem_ctx, const DOM_SID *sid, 
			const char **domain, const char **name,
                        enum lsa_SidType *name_type)
{
	struct wbcDomainSid dom_sid;
	wbcErr result;
	enum wbcSidType type;
	char *domain_name = NULL;
	char *account_name = NULL;

	memcpy(&dom_sid, sid, sizeof(dom_sid));	

	result = wbcLookupSid(&dom_sid, &domain_name, &account_name, &type);
	if (result != WBC_ERR_SUCCESS)
		return False;

	/* Copy out result */

	if (domain) {		
		*domain = talloc_strdup(mem_ctx, domain_name);
	}
	if (name) {
		*name = talloc_strdup(mem_ctx, account_name);
	}
	*name_type = (enum lsa_SidType)type;

	DEBUG(10, ("winbind_lookup_sid: SUCCESS: SID %s -> %s %s\n", 
		   sid_string_dbg(sid), domain_name, account_name));

	SAFE_FREE(domain_name);
	SAFE_FREE(account_name);
	
	if ((domain && !*domain) || (name && !*name)) {		
		DEBUG(0,("winbind_lookup_sid: talloc() failed!\n"));
		return False;
	}	


	return True;
}

/* Ping winbindd to see it is alive */

bool winbind_ping(void)
{
	wbcErr result = wbcPing();

	return (result == WBC_ERR_SUCCESS);
}

/* Call winbindd to convert SID to uid */

bool winbind_sid_to_uid(uid_t *puid, const DOM_SID *sid)
{
	struct wbcDomainSid dom_sid;
	wbcErr result;

	memcpy(&dom_sid, sid, sizeof(dom_sid));	

	result = wbcSidToUid(&dom_sid, puid);	
	if (result != WBC_ERR_SUCCESS)
		return False;

	return (result == WBC_ERR_SUCCESS);	
}

/* Call winbindd to convert uid to sid */

bool winbind_uid_to_sid(DOM_SID *sid, uid_t uid)
{
	struct wbcDomainSid dom_sid;
	wbcErr result;

	result = wbcUidToSid(uid, &dom_sid);
	if (result == WBC_ERR_SUCCESS) {
		memcpy(sid, &dom_sid, sizeof(DOM_SID));
	} else {
		sid_copy(sid, &global_sid_NULL);
	}

	return (result == WBC_ERR_SUCCESS);
}

/* Call winbindd to convert SID to gid */

bool winbind_sid_to_gid(gid_t *pgid, const DOM_SID *sid)
{
	struct wbcDomainSid dom_sid;
	wbcErr result;

	memcpy(&dom_sid, sid, sizeof(dom_sid));	

	result = wbcSidToGid(&dom_sid, pgid);	
	if (result != WBC_ERR_SUCCESS)
		return False;

	return (result == WBC_ERR_SUCCESS);	
}

/* Call winbindd to convert gid to sid */

bool winbind_gid_to_sid(DOM_SID *sid, gid_t gid)
{
	struct wbcDomainSid dom_sid;
	wbcErr result;

	result = wbcGidToSid(gid, &dom_sid);
	if (result == WBC_ERR_SUCCESS) {
		memcpy(sid, &dom_sid, sizeof(DOM_SID));
	} else {
		sid_copy(sid, &global_sid_NULL);
	}

	return (result == WBC_ERR_SUCCESS);
}

/* Check for a trusted domain */

wbcErr wb_is_trusted_domain(const char *domain)
{
	wbcErr result;
	struct wbcDomainInfo *info = NULL;
	
	result = wbcDomainInfo(domain, &info);

	if (WBC_ERROR_IS_OK(result)) {
		wbcFreeMemory(info);
	}

	return result;	
}

/* Lookup a set of rids in a given domain */

bool winbind_lookup_rids(TALLOC_CTX *mem_ctx,
			 const DOM_SID *domain_sid,
			 int num_rids, uint32 *rids,
			 const char **domain_name,
			 const char ***names, enum lsa_SidType **types)
{
	const char *dom_name = NULL;
	const char **namelist = NULL;
	enum wbcSidType *name_types = NULL;
	struct wbcDomainSid dom_sid;
	wbcErr ret;
	int i;	
	
	memcpy(&dom_sid, domain_sid, sizeof(struct wbcDomainSid));
	
	ret = wbcLookupRids(&dom_sid, num_rids, rids,
			    &dom_name, &namelist, &name_types);
	if (ret != WBC_ERR_SUCCESS)
		return False;
	
	*domain_name = talloc_strdup(mem_ctx, dom_name);
	*names       = TALLOC_ARRAY(mem_ctx, const char*, num_rids);
	*types       = TALLOC_ARRAY(mem_ctx, enum lsa_SidType, num_rids);

	for(i=0; i<num_rids; i++) {
		(*names)[i] = talloc_strdup(names, namelist[i]);
		(*types)[i] = (enum lsa_SidType)name_types[i];

		free(CONST_DISCARD(char*, namelist[i]));		
	}
	free(namelist);
	free(name_types);
	
	return True;	
}

/* Ask Winbind to allocate a new uid for us */

bool winbind_allocate_uid(uid_t *uid)
{
	wbcErr ret;
	
	ret = wbcAllocateUid(uid);
	
	return (ret == WBC_ERR_SUCCESS);
}

/* Ask Winbind to allocate a new gid for us */

bool winbind_allocate_gid(gid_t *gid)
{
	wbcErr ret;
	
	ret = wbcAllocateGid(gid);
	
	return (ret == WBC_ERR_SUCCESS);
}

#else      /* WITH_WINBIND */

bool winbind_lookup_name(const char *dom_name, const char *name, DOM_SID *sid, 
                         enum lsa_SidType *name_type)
{
	return False;
}

/* Call winbindd to convert sid to name */

bool winbind_lookup_sid(TALLOC_CTX *mem_ctx, const DOM_SID *sid, 
			const char **domain, const char **name,
                        enum lsa_SidType *name_type)
{
	return False;
}

/* Ping winbindd to see it is alive */

bool winbind_ping(void)
{
	return False;
}

/* Call winbindd to convert SID to uid */

bool winbind_sid_to_uid(uid_t *puid, const DOM_SID *sid)
{
	return False;
}

/* Call winbindd to convert uid to sid */

bool winbind_uid_to_sid(DOM_SID *sid, uid_t uid)
{
	return False;
}

/* Call winbindd to convert SID to gid */

bool winbind_sid_to_gid(gid_t *pgid, const DOM_SID *sid)
{
	return False;	
}

/* Call winbindd to convert gid to sid */

bool winbind_gid_to_sid(DOM_SID *sid, gid_t gid)
{
	return False;
}

/* Check for a trusted domain */

wbcErr wb_is_trusted_domain(const char *domain)
{
	return WBC_ERR_UNKNOWN_FAILURE;
}

/* Lookup a set of rids in a given domain */

bool winbind_lookup_rids(TALLOC_CTX *mem_ctx,
			 const DOM_SID *domain_sid,
			 int num_rids, uint32 *rids,
			 const char **domain_name,
			 const char ***names, enum lsa_SidType **types)
{
	return False;
}

/* Ask Winbind to allocate a new uid for us */

bool winbind_allocate_uid(uid_t *uid)
{
	return False;
}

/* Ask Winbind to allocate a new gid for us */

bool winbind_allocate_gid(gid_t *gid)
{
	return False;
}

#endif     /* WITH_WINBIND */
