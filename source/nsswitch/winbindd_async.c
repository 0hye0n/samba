/* 
   Unix SMB/CIFS implementation.

   Async helpers for blocking functions

   Copyright (C) Volker Lendecke 2005
   
   The helpers always consist of three functions: 

   * A request setup function that takes the necessary parameters together
     with a continuation function that is to be called upon completion

   * A private continuation function that is internal only. This is to be
     called by the lower-level functions in do_async(). Its only task is to
     properly call the continuation function named above.

   * A worker function that is called inside the appropriate child process.

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
#include "winbindd.h"

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_WINBIND

struct do_async_state {
	TALLOC_CTX *mem_ctx;
	struct winbindd_request request;
	struct winbindd_response response;
	void (*cont)(TALLOC_CTX *mem_ctx,
		     BOOL success,
		     struct winbindd_response *response,
		     void *c, void *private);
	void *c, *private;
};

static void do_async_recv(void *private, BOOL success)
{
	struct do_async_state *state =
		talloc_get_type_abort(private, struct do_async_state);

	state->cont(state->mem_ctx, success, &state->response,
		    state->c, state->private);
}

static void do_async(TALLOC_CTX *mem_ctx, struct winbindd_child *child,
		     const struct winbindd_request *request,
		     void (*cont)(TALLOC_CTX *mem_ctx, BOOL success,
				  struct winbindd_response *response,
				  void *c, void *private),
		     void *c, void *private)
{
	struct do_async_state *state;

	state = TALLOC_ZERO_P(mem_ctx, struct do_async_state);
	if (state == NULL) {
		DEBUG(0, ("talloc failed\n"));
		cont(mem_ctx, False, NULL, c, private);
		return;
	}

	state->mem_ctx = mem_ctx;
	state->request = *request;
	state->request.length = sizeof(state->request);
	state->cont = cont;
	state->c = c;
	state->private = private;

	async_request(mem_ctx, child, &state->request,
		      &state->response, do_async_recv, state);
}

static void do_async_domain(TALLOC_CTX *mem_ctx, struct winbindd_domain *domain,
			    const struct winbindd_request *request,
			    void (*cont)(TALLOC_CTX *mem_ctx, BOOL success,
					 struct winbindd_response *response,
					 void *c, void *private),
			    void *c, void *private)
{
	struct do_async_state *state;

	state = TALLOC_ZERO_P(mem_ctx, struct do_async_state);
	if (state == NULL) {
		DEBUG(0, ("talloc failed\n"));
		cont(mem_ctx, False, NULL, c, private);
		return;
	}

	state->mem_ctx = mem_ctx;
	state->request = *request;
	state->request.length = sizeof(state->request);
	state->cont = cont;
	state->c = c;
	state->private = private;

	async_domain_request(mem_ctx, domain, &state->request,
			     &state->response, do_async_recv, state);
}

static void idmap_set_mapping_recv(TALLOC_CTX *mem_ctx, BOOL success,
				   struct winbindd_response *response,
				   void *c, void *private)
{
	void (*cont)(void *priv, BOOL succ) = c;

	if (!success) {
		DEBUG(5, ("Could not trigger idmap_set_mapping\n"));
		cont(private, False);
		return;
	}

	if (response->result != WINBINDD_OK) {
		DEBUG(5, ("idmap_set_mapping returned an error\n"));
		cont(private, False);
		return;
	}

	cont(private, True);
}

void idmap_set_mapping_async(TALLOC_CTX *mem_ctx, const DOM_SID *sid,
			     unid_t id, int id_type,
			     void (*cont)(void *private, BOOL success),
			     void *private)
{
	struct winbindd_request request;
	ZERO_STRUCT(request);
	request.cmd = WINBINDD_DUAL_IDMAPSET;
	if (id_type == ID_USERID)
		request.data.dual_idmapset.uid = id.uid;
	else
		request.data.dual_idmapset.gid = id.gid;
	request.data.dual_idmapset.type = id_type;
	sid_to_string(request.data.dual_idmapset.sid, sid);

	do_async(mem_ctx, idmap_child(), &request, idmap_set_mapping_recv,
		 cont, private);
}

enum winbindd_result winbindd_dual_idmapset(struct winbindd_domain *domain,
					    struct winbindd_cli_state *state)
{
	DOM_SID sid;
	unid_t id;
	NTSTATUS result;

	DEBUG(3, ("[%5lu]: dual_idmapset\n", (unsigned long)state->pid));

	if (!string_to_sid(&sid, state->request.data.dual_idmapset.sid))
		return WINBINDD_ERROR;

	if (state->request.data.dual_idmapset.type == ID_USERID)
		id.uid = state->request.data.dual_idmapset.uid;
	else
		id.gid = state->request.data.dual_idmapset.gid;

	result = idmap_set_mapping(&sid, id,
				   state->request.data.dual_idmapset.type);
	return NT_STATUS_IS_OK(result) ? WINBINDD_OK : WINBINDD_ERROR;
}


static void uid2name_recv(TALLOC_CTX *mem_ctx, BOOL success,
			  struct winbindd_response *response,
			  void *c, void *private)
{
	void (*cont)(void *priv, BOOL succ, const char *name) = c;

	if (!success) {
		DEBUG(5, ("Could not trigger uid2name\n"));
		cont(private, False, NULL);
		return;
	}

	if (response->result != WINBINDD_OK) {
		DEBUG(5, ("uid2name returned an error\n"));
		cont(private, False, NULL);
		return;
	}

	cont(private, True, response->data.name.name);
}

void winbindd_uid2name_async(TALLOC_CTX *mem_ctx, uid_t uid,
			     void (*cont)(void *private, BOOL success,
					  const char *name),
			     void *private)
{
	struct winbindd_request request;
	ZERO_STRUCT(request);
	request.cmd = WINBINDD_DUAL_UID2NAME;
	request.data.uid = uid;
	do_async(mem_ctx, idmap_child(), &request, uid2name_recv,
		 cont, private);
}

enum winbindd_result winbindd_dual_uid2name(struct winbindd_domain *domain,
					    struct winbindd_cli_state *state)
{
	struct passwd *pw;

	DEBUG(3, ("[%5lu]: uid2name %lu\n", (unsigned long)state->pid, 
		  (unsigned long)state->request.data.uid));

	pw = getpwuid(state->request.data.uid);
	if (pw == NULL) {
		DEBUG(5, ("User %lu not found\n",
			  (unsigned long)state->request.data.uid));
		return WINBINDD_ERROR;
	}

	fstrcpy(state->response.data.name.name, pw->pw_name);
	return WINBINDD_OK;
}


static void gid2name_recv(TALLOC_CTX *mem_ctx, BOOL success,
			  struct winbindd_response *response,
			  void *c, void *private)
{
	void (*cont)(void *priv, BOOL succ, const char *name) = c;

	if (!success) {
		DEBUG(5, ("Could not trigger gid2name\n"));
		cont(private, False, NULL);
		return;
	}

	if (response->result != WINBINDD_OK) {
		DEBUG(5, ("gid2name returned an error\n"));
		cont(private, False, NULL);
		return;
	}

	cont(private, True, response->data.name.name);
}

void winbindd_gid2name_async(TALLOC_CTX *mem_ctx, gid_t gid,
			     void (*cont)(void *private, BOOL success,
					  const char *name),
			     void *private)
{
	struct winbindd_request request;
	ZERO_STRUCT(request);
	request.cmd = WINBINDD_DUAL_GID2NAME;
	request.data.gid = gid;
	do_async(mem_ctx, idmap_child(), &request, gid2name_recv,
		 cont, private);
}

enum winbindd_result winbindd_dual_gid2name(struct winbindd_domain *domain,
					    struct winbindd_cli_state *state)
{
	struct group *gr;

	DEBUG(3, ("[%5lu]: gid2name %lu\n", (unsigned long)state->pid, 
		  (unsigned long)state->request.data.gid));

	gr = getgrgid(state->request.data.gid);
	if (gr == NULL)
		return WINBINDD_ERROR;

	fstrcpy(state->response.data.name.name, gr->gr_name);
	return WINBINDD_OK;
}


static void lookupsid_recv(TALLOC_CTX *mem_ctx, BOOL success,
			   struct winbindd_response *response,
			   void *c, void *private)
{
	void (*cont)(void *priv, BOOL succ, const char *dom_name,
		     const char *name, enum SID_NAME_USE type) = c;

	if (!success) {
		DEBUG(5, ("Could not trigger lookupsid\n"));
		cont(private, False, NULL, NULL, SID_NAME_UNKNOWN);
		return;
	}

	if (response->result != WINBINDD_OK) {
		DEBUG(5, ("lookupsid returned an error\n"));
		cont(private, False, NULL, NULL, SID_NAME_UNKNOWN);
		return;
	}

	cont(private, True, response->data.name.dom_name,
	     response->data.name.name, response->data.name.type);
}

void winbindd_lookupsid_async(TALLOC_CTX *mem_ctx, const DOM_SID *sid,
			      void (*cont)(void *private, BOOL success,
					   const char *dom_name,
					   const char *name,
					   enum SID_NAME_USE type),
			      void *private)
{
	struct winbindd_domain *domain;
	struct winbindd_request request;

	domain = find_lookup_domain_from_sid(sid);
	if (domain == NULL) {
		DEBUG(5, ("Could not find domain for sid %s\n",
			  sid_string_static(sid)));
		cont(private, False, NULL, NULL, SID_NAME_UNKNOWN);
		return;
	}

	ZERO_STRUCT(request);
	request.cmd = WINBINDD_LOOKUPSID;
	fstrcpy(request.data.sid, sid_string_static(sid));

	do_async_domain(mem_ctx, domain, &request, lookupsid_recv,
			cont, private);
}

enum winbindd_result winbindd_dual_lookupsid(struct winbindd_domain *domain,
					     struct winbindd_cli_state *state)
{
	enum SID_NAME_USE type;
	DOM_SID sid;
	fstring name;
	fstring dom_name;

	/* Ensure null termination */
	state->request.data.sid[sizeof(state->request.data.sid)-1]='\0';

	DEBUG(3, ("[%5lu]: lookupsid %s\n", (unsigned long)state->pid, 
		  state->request.data.sid));

	/* Lookup sid from PDC using lsa_lookup_sids() */

	if (!string_to_sid(&sid, state->request.data.sid)) {
		DEBUG(5, ("%s not a SID\n", state->request.data.sid));
		return WINBINDD_ERROR;
	}

	/* Lookup the sid */

	if (!winbindd_lookup_name_by_sid(state->mem_ctx, &sid, dom_name, name,
					 &type)) {
		return WINBINDD_ERROR;
	}

	fstrcpy(state->response.data.name.dom_name, dom_name);
	fstrcpy(state->response.data.name.name, name);
	state->response.data.name.type = type;

	return WINBINDD_OK;
}

static void lookupname_recv(TALLOC_CTX *mem_ctx, BOOL success,
			    struct winbindd_response *response,
			    void *c, void *private)
{
	void (*cont)(void *priv, BOOL succ, const DOM_SID *sid,
		     enum SID_NAME_USE type) = c;
	DOM_SID sid;

	if (!success) {
		DEBUG(5, ("Could not trigger lookup_name\n"));
		cont(private, False, NULL, SID_NAME_UNKNOWN);
		return;
	}

	if (response->result != WINBINDD_OK) {
		DEBUG(5, ("lookup_name returned an error\n"));
		cont(private, False, NULL, SID_NAME_UNKNOWN);
		return;
	}

	if (!string_to_sid(&sid, response->data.sid.sid)) {
		DEBUG(0, ("Could not convert string %s to sid\n",
			  response->data.sid.sid));
		cont(private, False, NULL, SID_NAME_UNKNOWN);
		return;
	}

	cont(private, True, &sid, response->data.sid.type);
}

void winbindd_lookupname_async(TALLOC_CTX *mem_ctx, const char *dom_name,
			       const char *name,
			       void (*cont)(void *private, BOOL success,
					    const DOM_SID *sid,
					    enum SID_NAME_USE type),
			       void *private)
{
	struct winbindd_request request;
	struct winbindd_domain *domain;

	domain = find_lookup_domain_from_name(dom_name);

	if (domain == NULL) {
		DEBUG(5, ("Could not find domain for name %s\n", dom_name));
		cont(private, False, NULL, SID_NAME_UNKNOWN);
		return;
	}

	ZERO_STRUCT(request);
	request.cmd = WINBINDD_LOOKUPNAME;
	fstrcpy(request.data.name.dom_name, dom_name);
	fstrcpy(request.data.name.name, name);

	do_async_domain(mem_ctx, domain, &request, lookupname_recv,
			cont, private);
}

enum winbindd_result winbindd_dual_lookupname(struct winbindd_domain *domain,
					      struct winbindd_cli_state *state)
{
	enum SID_NAME_USE type;
	char *name_domain, *name_user;
	DOM_SID sid;
	char *p;

	/* Ensure null termination */
	state->request.data.sid[sizeof(state->request.data.name.dom_name)-1]='\0';

	/* Ensure null termination */
	state->request.data.sid[sizeof(state->request.data.name.name)-1]='\0';

	/* cope with the name being a fully qualified name */
	p = strstr(state->request.data.name.name, lp_winbind_separator());
	if (p) {
		*p = 0;
		name_domain = state->request.data.name.name;
		name_user = p+1;
	} else {
		name_domain = state->request.data.name.dom_name;
		name_user = state->request.data.name.name;
	}

	DEBUG(3, ("[%5lu]: lookupname %s%s%s\n", (unsigned long)state->pid,
		  name_domain, lp_winbind_separator(), name_user));

	/* Lookup name from PDC using lsa_lookup_names() */
	if (!winbindd_lookup_sid_by_name(state->mem_ctx, domain, name_domain,
					 name_user, &sid, &type)) {
		return WINBINDD_ERROR;
	}

	sid_to_string(state->response.data.sid.sid, &sid);
	state->response.data.sid.type = type;

	return WINBINDD_OK;
}

static BOOL print_sidlist(TALLOC_CTX *mem_ctx, const DOM_SID *sids,
			  int num_sids, char **result)
{
	int i;
	size_t buflen = 0;
	ssize_t len = 0;

	*result = NULL;
	for (i=0; i<num_sids; i++)
		sprintf_append(mem_ctx, result, &len, &buflen,
			       "%s\n", sid_string_static(&sids[i]));

	return (num_sids == 0) || (*result != NULL);
}

static BOOL parse_sidlist(TALLOC_CTX *mem_ctx, char *sidstr,
			  DOM_SID **sids, int *num_sids)
{
	char *p, *q;

	p = sidstr;
	if (p == NULL)
		return True;

	while (p[0] != '\0') {
		DOM_SID sid;
		q = strchr(p, '\n');
		if (q == NULL) {
			DEBUG(0, ("Got invalid sidstr: %s\n", p));
			return False;
		}
		*q = '\0';
		q += 1;
		if (!string_to_sid(&sid, p)) {
			DEBUG(0, ("Could not parse sid %s\n", p));
			return False;
		}
		add_sid_to_array(mem_ctx, &sid, sids, num_sids);
		p = q;
	}
	return True;
}

static void getsidaliases_recv(TALLOC_CTX *mem_ctx, BOOL success,
			       struct winbindd_response *response,
			       void *c, void *private)
{
	void (*cont)(void *priv, BOOL succ,
		     DOM_SID *aliases, int num_aliases) = c;
	char *aliases_str;
	DOM_SID *sids = NULL;
	int num_sids = 0;

	if (!success) {
		DEBUG(5, ("Could not trigger getsidaliases\n"));
		cont(private, success, NULL, 0);
		return;
	}

	if (response->result != WINBINDD_OK) {
		DEBUG(5, ("getsidaliases returned an error\n"));
		cont(private, False, NULL, 0);
		return;
	}

	aliases_str = response->extra_data;

	if (aliases_str == NULL) {
		DEBUG(10, ("getsidaliases return 0 SIDs\n"));
		cont(private, True, NULL, 0);
		return;
	}

	if (!parse_sidlist(mem_ctx, aliases_str, &sids, &num_sids)) {
		DEBUG(0, ("Could not parse sids\n"));
		cont(private, False, NULL, 0);
		return;
	}

	cont(private, True, sids, num_sids);
}

void winbindd_getsidaliases_async(struct winbindd_domain *domain,
				  TALLOC_CTX *mem_ctx,
				  const DOM_SID *sids, int num_sids,
			 	  void (*cont)(void *private,
				 	       BOOL success,
					       const DOM_SID *aliases,
					       int num_aliases),
				  void *private)
{
	struct winbindd_request request;
	char *sidstr = NULL;
	char *keystr;

	if (num_sids == 0) {
		cont(private, True, NULL, 0);
		return;
	}

	if (!print_sidlist(mem_ctx, sids, num_sids, &sidstr)) {
		cont(private, False, NULL, 0);
		return;
	}

	keystr = cache_store_request_data(mem_ctx, sidstr);
	if (keystr == NULL) {
		cont(private, False, NULL, 0);
		return;
	}

	ZERO_STRUCT(request);
	request.cmd = WINBINDD_DUAL_GETSIDALIASES;
	fstrcpy(request.domain_name, domain->name);
	fstrcpy(request.data.dual_sidaliases.cache_key, keystr);

	do_async_domain(mem_ctx, domain, &request, getsidaliases_recv,
			cont, private);
}

enum winbindd_result winbindd_dual_getsidaliases(struct winbindd_domain *domain,
						 struct winbindd_cli_state *state)
{
	DOM_SID *sids = NULL;
	int num_sids = 0;
	char *key = state->request.data.dual_sidaliases.cache_key;
	char *sidstr;
	int i, num_aliases;
	uint32 *alias_rids;
	NTSTATUS result;

	DEBUG(3, ("[%5lu]: getsidaliases\n", (unsigned long)state->pid));

	/* Ensure null termination */
        state->request.domain_name[sizeof(state->request.domain_name)-1]='\0';
        state->request.data.dual_sidaliases.cache_key
		[sizeof(state->request.data.dual_sidaliases.cache_key)-1]='\0';

	sidstr = cache_retrieve_request_data(state->mem_ctx, key);
	if (sidstr == NULL)
		sidstr = talloc_strdup(state->mem_ctx, "\n"); /* No SID */

	DEBUG(10, ("Sidlist: %s\n", sidstr));

	if (!parse_sidlist(state->mem_ctx, sidstr, &sids, &num_sids)) {
		DEBUG(0, ("Could not parse SID list: %s\n", sidstr));
		return WINBINDD_ERROR;
	}

	num_aliases = 0;
	alias_rids = NULL;

	result = domain->methods->lookup_useraliases(domain,
						     state->mem_ctx,
						     num_sids, sids,
						     &num_aliases,
						     &alias_rids);

	if (!NT_STATUS_IS_OK(result)) {
		DEBUG(3, ("Could not lookup_useraliases: %s\n",
			  nt_errstr(result)));
		return WINBINDD_ERROR;
	}

	num_sids = 0;
	sids = NULL;

	DEBUG(10, ("Got %d aliases\n", num_aliases));

	for (i=0; i<num_aliases; i++) {
		DOM_SID sid;
		DEBUGADD(10, (" rid %d\n", alias_rids[i]));
		sid_copy(&sid, &domain->sid);
		sid_append_rid(&sid, alias_rids[i]);
		add_sid_to_array(state->mem_ctx, &sid, &sids, &num_sids);
	}

	if (!print_sidlist(NULL, sids, num_sids,
			   (char **)&state->response.extra_data)) {
		DEBUG(0, ("Could not print_sidlist\n"));
		return WINBINDD_ERROR;
	}

	if (state->response.extra_data != NULL) {
		DEBUG(10, ("aliases_list: %s\n",
			   (char *)state->response.extra_data));
		state->response.length += strlen(state->response.extra_data)+1;
	}
	
	return WINBINDD_OK;
}

struct gettoken_state {
	TALLOC_CTX *mem_ctx;
	DOM_SID user_sid;
	struct winbindd_domain *alias_domain;
	struct winbindd_domain *builtin_domain;
	DOM_SID *sids;
	int num_sids;
	void (*cont)(void *private, BOOL success, DOM_SID *sids, int num_sids);
	void *private;
};

static void gettoken_recvdomgroups(TALLOC_CTX *mem_ctx, BOOL success,
				   struct winbindd_response *response,
				   void *c, void *private);
static void gettoken_recvaliases(void *private, BOOL success,
				 const DOM_SID *aliases,
				 int num_aliases);
				 

void winbindd_gettoken_async(TALLOC_CTX *mem_ctx, const DOM_SID *user_sid,
			     void (*cont)(void *private, BOOL success,
					  DOM_SID *sids, int num_sids),
			     void *private)
{
	struct winbindd_domain *domain;
	struct winbindd_request request;
	struct gettoken_state *state;

	state = TALLOC_P(mem_ctx, struct gettoken_state);
	if (state == NULL) {
		DEBUG(0, ("talloc failed\n"));
		cont(private, False, NULL, 0);
		return;
	}

	state->mem_ctx = mem_ctx;
	sid_copy(&state->user_sid, user_sid);
	state->alias_domain = find_our_domain();
	state->builtin_domain = find_builtin_domain();
	state->cont = cont;
	state->private = private;

	domain = find_domain_from_sid_noinit(user_sid);
	if (domain == NULL) {
		DEBUG(5, ("Could not find domain from SID %s\n",
			  sid_string_static(user_sid)));
		cont(private, False, NULL, 0);
		return;
	}

	ZERO_STRUCT(request);
	request.cmd = WINBINDD_GETUSERDOMGROUPS;
	fstrcpy(request.data.sid, sid_string_static(user_sid));

	do_async_domain(mem_ctx, domain, &request, gettoken_recvdomgroups,
			NULL, state);
}

static void gettoken_recvdomgroups(TALLOC_CTX *mem_ctx, BOOL success,
				   struct winbindd_response *response,
				   void *c, void *private)
{
	struct gettoken_state *state =
		talloc_get_type_abort(private, struct gettoken_state);
	char *sids_str;
	
	if (!success) {
		DEBUG(10, ("Could not get domain groups\n"));
		state->cont(state->private, False, NULL, 0);
		return;
	}

	sids_str = response->extra_data;

	if (sids_str == NULL) {
		DEBUG(10, ("Received no domain groups\n"));
		state->cont(state->private, True, NULL, 0);
		return;
	}

	state->sids = NULL;
	state->num_sids = 0;

	add_sid_to_array(mem_ctx, &state->user_sid, &state->sids,
			 &state->num_sids);

	if (!parse_sidlist(mem_ctx, sids_str, &state->sids,
			   &state->num_sids)) {
		DEBUG(0, ("Could not parse sids\n"));
		state->cont(state->private, False, NULL, 0);
		return;
	}

	if (state->alias_domain == NULL) {
		DEBUG(10, ("Don't expand domain local groups\n"));
		state->cont(state->private, True, state->sids,
			    state->num_sids);
		return;
	}

	winbindd_getsidaliases_async(state->alias_domain, mem_ctx,
				     state->sids, state->num_sids,
				     gettoken_recvaliases, state);
}

static void gettoken_recvaliases(void *private, BOOL success,
				 const DOM_SID *aliases,
				 int num_aliases)
{
	struct gettoken_state *state = private;
	int i;

	if (!success) {
		DEBUG(10, ("Could not receive domain local groups\n"));
		state->cont(state->private, False, NULL, 0);
		return;
	}

	for (i=0; i<num_aliases; i++)
		add_sid_to_array(state->mem_ctx, &aliases[i],
				 &state->sids, &state->num_sids);

	if (state->builtin_domain != NULL) {
		struct winbindd_domain *builtin_domain = state->builtin_domain;
		DEBUG(10, ("Expanding our own local groups\n"));
		state->builtin_domain = NULL;
		winbindd_getsidaliases_async(builtin_domain, state->mem_ctx,
					     state->sids, state->num_sids,
					     gettoken_recvaliases, state);
		return;
	}

	state->cont(state->private, True, state->sids, state->num_sids);
}

struct sid2gid_state {
	TALLOC_CTX *mem_ctx;
	DOM_SID sid;
	void (*cont)(void *private, BOOL success, gid_t gid);
	void *private;
};

static void sid2gid_lookup_sid_recv(void *private, BOOL success,
				    const char *dom_name, const char *name,
				    enum SID_NAME_USE type);
static void sid2gid_recv(TALLOC_CTX *mem_ctx, BOOL success,
			 struct winbindd_response *response,
			 void *c, void *private);

void winbindd_sid2gid_async(TALLOC_CTX *mem_ctx, const DOM_SID *sid,
			    void (*cont)(void *private, BOOL success,
					 gid_t gid),
			    void *private)
{
	struct sid2gid_state *state;
	NTSTATUS result;
	gid_t gid;

	if (idmap_proxyonly()) {
		DEBUG(10, ("idmap proxy only\n"));
		cont(private, False, 0);
		return;
	}

	/* Query only the local tdb, everything else might possibly block */

	result = idmap_sid_to_gid(sid, &gid, ID_QUERY_ONLY|ID_CACHE_ONLY);

	if (NT_STATUS_IS_OK(result)) {
		cont(private, True, gid);
		return;
	}

	state = TALLOC_P(mem_ctx, struct sid2gid_state);
	if (state == NULL) {
		DEBUG(0, ("talloc failed\n"));
		cont(private, False, 0);
		return;
	}

	state->mem_ctx = mem_ctx;
	state->sid = *sid;
	state->cont = cont;
	state->private = private;

	winbindd_lookupsid_async(mem_ctx, sid, sid2gid_lookup_sid_recv, state);
}

static void sid2gid_lookup_sid_recv(void *private, BOOL success,
				    const char *dom_name, const char *name,
				    enum SID_NAME_USE type)
{
	struct sid2gid_state *state = private;
	struct winbindd_request request;

	if (!success) {
		DEBUG(5, ("Could not trigger lookup_sid\n"));
		state->cont(state->private, False, 0);
		return;
	}

	if (((type != SID_NAME_DOM_GRP) && (type != SID_NAME_ALIAS) &&
	     (type != SID_NAME_WKN_GRP))) {
		DEBUG(5, ("SID is not a group\n"));
		state->cont(state->private, False, 0);
		return;
	}

	ZERO_STRUCT(request);
	request.cmd = WINBINDD_DUAL_SID2GID;
	fstrcpy(request.data.dual_sid2id.sid, sid_string_static(&state->sid));
	fstrcpy(request.data.dual_sid2id.name, name);

	do_async(state->mem_ctx, idmap_child(), &request,
		 sid2gid_recv, NULL, state);
}

static void sid2gid_recv(TALLOC_CTX *mem_ctx, BOOL success,
			 struct winbindd_response *response,
			 void *c, void *private)
{
	struct sid2gid_state *state = private;

	if (!success) {
		DEBUG(5, ("Could not trigger sid2gid\n"));
		state->cont(state->private, False, 0);
		return;
	}

	state->cont(state->private, True, response->data.gid);
}

/* Child part of winbindd_sid2gid. We already know for sure it's a group, as
 * well as the user's name */

enum winbindd_result winbindd_dual_sid2gid(struct winbindd_domain *domain,
					   struct winbindd_cli_state *state)
{
	DOM_SID sid;
	NTSTATUS result;

	DEBUG(3, ("[%5lu]: sid to gid %s\n", (unsigned long)state->pid,
		  state->request.data.dual_sid2id.sid));

	if (!string_to_sid(&sid, state->request.data.dual_sid2id.sid)) {
		DEBUG(1, ("Could not get convert sid %s from string\n",
			  state->request.data.dual_sid2id.sid));
		return WINBINDD_ERROR;
	}

	/* Find gid for this sid and return it, possibly ask the slow remote
	 * idmap */

	result = idmap_sid_to_gid(&sid, &(state->response.data.gid),
				  ID_QUERY_ONLY);

	if (NT_STATUS_IS_OK(result))
		return WINBINDD_OK;

	/* This gets a little tricky.  If we assume that usernames are syncd
	   between /etc/passwd and the windows domain (such as a member of a
	   Samba domain), the we need to get the gid from the OS and not
	   allocate one ourselves */
	   
	if (lp_winbind_trusted_domains_only() && 
	    (sid_compare_domain(&sid, &find_our_domain()->sid) == 0)) {

		const char *group_name = state->request.data.dual_sid2id.name;
		struct group *grp = NULL;
		unid_t id;
			
		/* ok...here's we know that we are dealing with our own domain
		   (the one to which we are joined).  And we know that there
		   must be a UNIX account for this user.  So we lookup the sid
		   and the call getgrnam().*/

		grp = getgrnam(group_name);
		if (grp == NULL) {
			DEBUG(0,("winbindd_sid_to_gid: 'winbind trusted "
				 "domains only' is set but this group [%s] "
				 "doesn't exist!\n", group_name));
			return WINBINDD_ERROR;
		}
			
		state->response.data.gid = grp->gr_gid;

		id.gid = grp->gr_gid;
		idmap_set_mapping( &sid, id, ID_GROUPID );

		return WINBINDD_OK;
	}

	if (state->request.flags & WBFLAG_QUERY_ONLY)
		return WINBINDD_ERROR;

	result = idmap_sid_to_gid(&sid, &(state->response.data.gid), 0);

	if (NT_STATUS_IS_OK(result))
		return WINBINDD_OK;

	DEBUG(4, ("Could not get gid for sid %s\n", state->request.data.sid));
	return WINBINDD_ERROR;
}

struct sid2uid_state {
	TALLOC_CTX *mem_ctx;
	DOM_SID sid;
	void (*cont)(void *private, BOOL success, uid_t uid);
	void *private;
};

static void sid2uid_lookup_sid_recv(void *private, BOOL success,
				    const char *dom_name, const char *name,
				    enum SID_NAME_USE type);
static void sid2uid_recv(TALLOC_CTX *mem_ctx, BOOL success,
			 struct winbindd_response *response,
			 void *c, void *private);

void winbindd_sid2uid_async(TALLOC_CTX *mem_ctx, const DOM_SID *sid,
			    void (*cont)(void *private, BOOL success,
					 uid_t uid),
			    void *private)
{
	struct sid2uid_state *state;
	NTSTATUS result;
	uid_t uid;

	if (idmap_proxyonly()) {
		DEBUG(10, ("idmap proxy only\n"));
		cont(private, False, 0);
		return;
	}

	/* Query only the local tdb, everything else might possibly block */

	result = idmap_sid_to_uid(sid, &uid, ID_QUERY_ONLY|ID_CACHE_ONLY);

	if (NT_STATUS_IS_OK(result)) {
		cont(private, True, uid);
		return;
	}

	state = TALLOC_P(mem_ctx, struct sid2uid_state);
	if (state == NULL) {
		DEBUG(0, ("talloc failed\n"));
		cont(private, False, 0);
		return;
	}

	state->mem_ctx = mem_ctx;
	state->sid = *sid;
	state->cont = cont;
	state->private = private;

	winbindd_lookupsid_async(mem_ctx, sid, sid2uid_lookup_sid_recv, state);
}

static void sid2uid_lookup_sid_recv(void *private, BOOL success,
				    const char *dom_name, const char *name,
				    enum SID_NAME_USE type)
{
	struct sid2uid_state *state = private;
	struct winbindd_request request;

	if (!success) {
		DEBUG(5, ("Could not trigger lookup_sid\n"));
		state->cont(state->private, False, 0);
		return;
	}

	if ((type != SID_NAME_USER) && (type != SID_NAME_COMPUTER)) {
		DEBUG(5, ("SID is not a user\n"));
		state->cont(state->private, False, 0);
		return;
	}

	ZERO_STRUCT(request);
	request.cmd = WINBINDD_DUAL_SID2UID;
	fstrcpy(request.data.dual_sid2id.sid, sid_string_static(&state->sid));
	fstrcpy(request.data.dual_sid2id.name, name);

	do_async(state->mem_ctx, idmap_child(), &request,
		 sid2uid_recv, NULL, state);
}

static void sid2uid_recv(TALLOC_CTX *mem_ctx, BOOL success,
			 struct winbindd_response *response,
			 void *c, void *private)
{
	struct sid2uid_state *state = private;

	if (!success) {
		DEBUG(5, ("Could not trigger sid2uid\n"));
		state->cont(state->private, False, 0);
		return;
	}

	state->cont(state->private, True, response->data.uid);
}

/* Child part of winbindd_sid2uid. We already know for sure it's a user, as
 * well as the user's name */

enum winbindd_result winbindd_dual_sid2uid(struct winbindd_domain *domain,
					   struct winbindd_cli_state *state)
{
	DOM_SID sid;
	NTSTATUS result;

	DEBUG(3, ("[%5lu]: sid to uid %s\n", (unsigned long)state->pid,
		  state->request.data.dual_sid2id.sid));

	if (!string_to_sid(&sid, state->request.data.dual_sid2id.sid)) {
		DEBUG(1, ("Could not get convert sid %s from string\n",
			  state->request.data.dual_sid2id.sid));
		return WINBINDD_ERROR;
	}

	/* Find uid for this sid and return it, possibly ask the slow remote
	 * idmap */

	result = idmap_sid_to_uid(&sid, &(state->response.data.uid),
				  ID_QUERY_ONLY);

	if (NT_STATUS_IS_OK(result))
		return WINBINDD_OK;

	/* This gets a little tricky.  If we assume that usernames are syncd
	   between /etc/passwd and the windows domain (such as a member of a
	   Samba domain), the we need to get the uid from the OS and not
	   allocate one ourselves */
	   
	if (lp_winbind_trusted_domains_only() && 
	    (sid_compare_domain(&sid, &find_our_domain()->sid) == 0)) {

		const char *user_name = state->request.data.dual_sid2id.name;
		struct passwd *pw = NULL;
		unid_t id;
			
		/* ok...here's we know that we are dealing with our own domain
		   (the one to which we are joined).  And we know that there
		   must be a UNIX account for this user.  So we lookup the sid
		   and the call getpwnam().*/

		pw = getpwnam(user_name);
		if (pw == NULL) {
			DEBUG(0,("winbindd_sid_to_uid: 'winbind trusted "
				 "domains only' is set but this user [%s] "
				 "doesn't exist!\n", user_name));
			return WINBINDD_ERROR;
		}
			
		state->response.data.uid = pw->pw_uid;

		id.uid = pw->pw_uid;
		idmap_set_mapping( &sid, id, ID_USERID );

		return WINBINDD_OK;
	}

	if (state->request.flags & WBFLAG_QUERY_ONLY)
		return WINBINDD_ERROR;

	result = idmap_sid_to_uid(&sid, &(state->response.data.uid), 0);

	if (NT_STATUS_IS_OK(result))
		return WINBINDD_OK;

	DEBUG(4, ("Could not get uid for sid %s\n", state->request.data.sid));
	return WINBINDD_ERROR;
}

static void query_user_recv(TALLOC_CTX *mem_ctx, BOOL success,
			    struct winbindd_response *response,
			    void *c, void *private)
{
	void (*cont)(void *priv, BOOL succ, const char *acct_name,
		     const char *full_name, uint32 group_rid) = c;

	if (!success) {
		DEBUG(5, ("Could not trigger query_user\n"));
		cont(private, False, NULL, NULL, -1);
		return;
	}

	cont(private, True, response->data.user_info.acct_name,
	     response->data.user_info.full_name,
	     response->data.user_info.group_rid);
}

void query_user_async(TALLOC_CTX *mem_ctx, struct winbindd_domain *domain,
		      const DOM_SID *sid,
		      void (*cont)(void *private, BOOL success,
				   const char *acct_name,
				   const char *full_name,
				   uint32 group_rid),
		      void *private)
{
	struct winbindd_request request;
	ZERO_STRUCT(request);
	request.cmd = WINBINDD_DUAL_USERINFO;
	sid_to_string(request.data.sid, sid);
	do_async_domain(mem_ctx, domain, &request, query_user_recv,
			cont, private);
}

