/* 
   Unix SMB/CIFS implementation.
   ads (active directory) utility library
   Copyright (C) Andrew Tridgell 2001
   Copyright (C) Remus Koos 2001
   Copyright (C) Jim McDonough 2002
   
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

#ifdef HAVE_ADS

/**
 * @file ldap.c
 * @brief basic ldap client-side routines for ads server communications
 *
 * The routines contained here should do the necessary ldap calls for
 * ads setups.
 * 
 * Important note: attribute names passed into ads_ routines must
 * already be in UTF-8 format.  We do not convert them because in almost
 * all cases, they are just ascii (which is represented with the same
 * codepoints in UTF-8).  This may have to change at some point
 **/


/*
  try a connection to a given ldap server, returning True and setting the servers IP
  in the ads struct if successful
 */
static BOOL ads_try_connect(ADS_STRUCT *ads, const char *server, unsigned port)
{
	char *srv;

	if (!server || !*server) {
		return False;
	}

	DEBUG(5,("ads_try_connect: trying ldap server '%s' port %u\n", server, port));

	/* this copes with inet_ntoa brokenness */
	srv = strdup(server);

	ads->ld = ldap_open(srv, port);
	if (!ads->ld) {
		free(srv);
		return False;
	}
	ads->ldap_port = port;
	ads->ldap_ip = *interpret_addr2(srv);
	free(srv);
	return True;
}

/* used by the IP comparison function */
struct ldap_ip {
	struct in_addr ip;
	unsigned port;
};

/* compare 2 ldap IPs by nearness to our interfaces - used in qsort */
static int ldap_ip_compare(struct ldap_ip *ip1, struct ldap_ip *ip2)
{
	return ip_compare(&ip1->ip, &ip2->ip);
}

/* try connecting to a ldap server via DNS */
static BOOL ads_try_dns(ADS_STRUCT *ads)
{
	char *realm, *ptr;
	char *list = NULL;
	pstring tok;
	struct ldap_ip *ip_list;
	int count, i=0;

	realm = ads->server.realm;
	if (!realm || !*realm) {
		realm = lp_realm();
	}
	if (!realm || !*realm) {
		realm = ads->server.workgroup;
	}
	if (!realm || !*realm) {
		realm = lp_workgroup();
	}
	if (!realm) {
		return False;
	}
	realm = smb_xstrdup(realm);

	DEBUG(6,("ads_try_dns: looking for realm '%s'\n", realm));
	if (ldap_domain2hostlist(realm, &list) != LDAP_SUCCESS) {
		SAFE_FREE(realm);
		return False;
	}

	DEBUG(6,("ads_try_dns: ldap realm '%s' host list '%s'\n", realm, list));
	SAFE_FREE(realm);

	count = count_chars(list, ' ') + 1;
	ip_list = malloc(count * sizeof(struct ldap_ip));
	if (!ip_list) {
		return False;
	}

	ptr = list;
	while (next_token(&ptr, tok, " ", sizeof(tok))) {
		unsigned port = LDAP_PORT;
		char *p = strchr(tok, ':');
		if (p) {
			*p = 0;
			port = atoi(p+1);
		}
		ip_list[i].ip = *interpret_addr2(tok);
		ip_list[i].port = port;
		if (!is_zero_ip(ip_list[i].ip)) {
			i++;
		}
	}
	free(list);

	count = i;

	/* we sort the list of addresses by closeness to our interfaces. This
	   tries to prevent us using a DC on the other side of the country */
	if (count > 1) {
		qsort(ip_list, count, sizeof(struct ldap_ip), 
		      QSORT_CAST ldap_ip_compare);	
	}

	for (i=0;i<count;i++) {
		if (ads_try_connect(ads, inet_ntoa(ip_list[i].ip), ip_list[i].port)) {
			free(ip_list);
			return True;
		}
	}

	SAFE_FREE(ip_list);
	return False;
}

/* try connecting to a ldap server via netbios */
static BOOL ads_try_netbios(ADS_STRUCT *ads)
{
	struct in_addr *ip_list;
	int count;
	int i;
	char *workgroup = ads->server.workgroup;

	if (!workgroup) {
		workgroup = lp_workgroup();
	}

	DEBUG(6,("ads_try_netbios: looking for workgroup '%s'\n", workgroup));

	/* try the PDC first */
	if (get_dc_list(True, workgroup, &ip_list, &count)) { 
		for (i=0;i<count;i++) {
			DEBUG(6,("ads_try_netbios: trying server '%s'\n", 
				 inet_ntoa(ip_list[i])));
			if (ads_try_connect(ads, inet_ntoa(ip_list[i]), LDAP_PORT)) {
				free(ip_list);
				return True;
			}
		}
		free(ip_list);
	}

	/* now any DC, including backups */
	if (get_dc_list(False, workgroup, &ip_list, &count)) { 
		for (i=0;i<count;i++) {
			DEBUG(6,("ads_try_netbios: trying server '%s'\n", 
				 inet_ntoa(ip_list[i])));
			if (ads_try_connect(ads, inet_ntoa(ip_list[i]), LDAP_PORT)) {
				free(ip_list);
				return True;
			}
		}
		free(ip_list);
	}

	return False;
}

/**
 * Connect to the LDAP server
 * @param ads Pointer to an existing ADS_STRUCT
 * @return status of connection
 **/
ADS_STATUS ads_connect(ADS_STRUCT *ads)
{
	int version = LDAP_VERSION3;
	int code;
	ADS_STATUS status;

	ads->last_attempt = time(NULL);
	ads->ld = NULL;

	/* try with a user specified server */
	if (ads->server.ldap_server && 
	    ads_try_connect(ads, ads->server.ldap_server, LDAP_PORT)) {
		goto got_connection;
	}

	/* try with a smb.conf ads server setting if we are connecting
           to the primary workgroup or realm */
	if (!ads->server.foreign &&
	    ads_try_connect(ads, lp_ads_server(), LDAP_PORT)) {
		goto got_connection;
	}

	/* try via DNS */
	if (ads_try_dns(ads)) {
		goto got_connection;
		}

	/* try via netbios lookups */
	if (!lp_disable_netbios() && ads_try_netbios(ads)) {
		goto got_connection;
	}

	return ADS_ERROR_SYSTEM(errno?errno:ENOENT);

got_connection:
	DEBUG(3,("Connected to LDAP server %s\n", inet_ntoa(ads->ldap_ip)));

	status = ads_server_info(ads);
	if (!ADS_ERR_OK(status)) {
		DEBUG(1,("Failed to get ldap server info\n"));
		return status;
	}

	ldap_set_option(ads->ld, LDAP_OPT_PROTOCOL_VERSION, &version);

	if (!ads->auth.user_name) {
		/* by default use the machine account */
		extern pstring global_myname;
		fstring myname;
		fstrcpy(myname, global_myname);
		strlower(myname);
		asprintf(&ads->auth.user_name, "HOST/%s", myname);
	}

	if (!ads->auth.realm) {
		ads->auth.realm = strdup(ads->config.realm);
	}

	if (!ads->auth.kdc_server) {
		ads->auth.kdc_server = strdup(inet_ntoa(ads->ldap_ip));
	}

#if KRB5_DNS_HACK
	/* this is a really nasty hack to avoid ADS DNS problems. It needs a patch
	   to MIT kerberos to work (tridge) */
	{
		char *env;
		asprintf(&env, "KRB5_KDC_ADDRESS_%s", ads->config.realm);
		setenv(env, ads->auth.kdc_server, 1);
		free(env);
	}
#endif

	if (ads->auth.password) {
		if ((code = ads_kinit_password(ads)))
			return ADS_ERROR_KRB5(code);
	}

	if (ads->auth.no_bind) {
		return ADS_SUCCESS;
	}

	return ads_sasl_bind(ads);
}

/*
  Duplicate a struct berval into talloc'ed memory
 */
static struct berval *dup_berval(TALLOC_CTX *ctx, const struct berval *in_val)
{
	struct berval *value;

	if (!in_val) return NULL;

	value = talloc_zero(ctx, sizeof(struct berval));
	if (in_val->bv_len == 0) return value;

	value->bv_len = in_val->bv_len;
	value->bv_val = talloc_memdup(ctx, in_val->bv_val, in_val->bv_len);
	return value;
}

/*
  Make a values list out of an array of (struct berval *)
 */
static struct berval **ads_dup_values(TALLOC_CTX *ctx, 
				      const struct berval **in_vals)
{
	struct berval **values;
	int i;
       
	if (!in_vals) return NULL;
	for (i=0; in_vals[i]; i++); /* count values */
	values = (struct berval **) talloc_zero(ctx, 
						(i+1)*sizeof(struct berval *));
	if (!values) return NULL;

	for (i=0; in_vals[i]; i++) {
		values[i] = dup_berval(ctx, in_vals[i]);
	}
	return values;
}

/*
  UTF8-encode a values list out of an array of (char *)
 */
static char **ads_push_strvals(TALLOC_CTX *ctx, const char **in_vals)
{
	char **values;
	int i;
       
	if (!in_vals) return NULL;
	for (i=0; in_vals[i]; i++); /* count values */
	values = (char ** ) talloc_zero(ctx, (i+1)*sizeof(char *));
	if (!values) return NULL;

	for (i=0; in_vals[i]; i++) {
		push_utf8_talloc(ctx, &values[i], in_vals[i]);
	}
	return values;
}

/*
  Pull a (char *) array out of a UTF8-encoded values list
 */
static char **ads_pull_strvals(TALLOC_CTX *ctx, const char **in_vals)
{
	char **values;
	int i;
       
	if (!in_vals) return NULL;
	for (i=0; in_vals[i]; i++); /* count values */
	values = (char **) talloc_zero(ctx, (i+1)*sizeof(char *));
	if (!values) return NULL;

	for (i=0; in_vals[i]; i++) {
		pull_utf8_talloc(ctx, &values[i], in_vals[i]);
	}
	return values;
}

/**
 * Do a search with paged results.  cookie must be null on the first
 *  call, and then returned on each subsequent call.  It will be null
 *  again when the entire search is complete 
 * @param ads connection to ads server 
 * @param bind_path Base dn for the search
 * @param scope Scope of search (LDAP_BASE | LDAP_ONE | LDAP_SUBTREE)
 * @param exp Search expression - specified in local charset
 * @param attrs Attributes to retrieve - specified in utf8 or ascii
 * @param res ** which will contain results - free res* with ads_msgfree()
 * @param count Number of entries retrieved on this page
 * @param cookie The paged results cookie to be returned on subsequent calls
 * @return status of search
 **/
ADS_STATUS ads_do_paged_search(ADS_STRUCT *ads, const char *bind_path,
			       int scope, const char *exp,
			       const char **attrs, void **res, 
			       int *count, void **cookie)
{
	int rc, i, version;
	char *utf8_exp, *utf8_path, **search_attrs;
	LDAPControl PagedResults, NoReferrals, *controls[3], **rcontrols; 
	BerElement *cookie_be = NULL;
	struct berval *cookie_bv= NULL;
	TALLOC_CTX *ctx;

	*res = NULL;

	if (!(ctx = talloc_init()))
		return ADS_ERROR(LDAP_NO_MEMORY);

	/* 0 means the conversion worked but the result was empty 
	   so we only fail if it's negative.  In any case, it always 
	   at least nulls out the dest */
	if ((push_utf8_talloc(ctx, &utf8_exp, exp) < 0) ||
	    (push_utf8_talloc(ctx, &utf8_path, bind_path) < 0)) {
		rc = LDAP_NO_MEMORY;
		goto done;
	}

	if (!attrs || !(*attrs))
		search_attrs = NULL;
	else {
		/* This would be the utf8-encoded version...*/
		/* if (!(search_attrs = ads_push_strvals(ctx, attrs))) */
		if (!(str_list_copy(&search_attrs, attrs)))
		{
			rc = LDAP_NO_MEMORY;
			goto done;
		}
	}
		
		
	/* Paged results only available on ldap v3 or later */
	ldap_get_option(ads->ld, LDAP_OPT_PROTOCOL_VERSION, &version);
	if (version < LDAP_VERSION3) {
		rc =  LDAP_NOT_SUPPORTED;
		goto done;
	}

	cookie_be = ber_alloc_t(LBER_USE_DER);
	if (cookie && *cookie) {
		ber_printf(cookie_be, "{iO}", (ber_int_t) 1000, *cookie);
		ber_bvfree(*cookie); /* don't need it from last time */
		*cookie = NULL;
	} else {
		ber_printf(cookie_be, "{io}", (ber_int_t) 1000, "", 0);
	}
	ber_flatten(cookie_be, &cookie_bv);
	PagedResults.ldctl_oid = ADS_PAGE_CTL_OID;
	PagedResults.ldctl_iscritical = (char) 1;
	PagedResults.ldctl_value.bv_len = cookie_bv->bv_len;
	PagedResults.ldctl_value.bv_val = cookie_bv->bv_val;

	NoReferrals.ldctl_oid = ADS_NO_REFERRALS_OID;
	NoReferrals.ldctl_iscritical = (char) 0;
	NoReferrals.ldctl_value.bv_len = 0;
	NoReferrals.ldctl_value.bv_val = "";


	controls[0] = &NoReferrals;
	controls[1] = &PagedResults;
	controls[2] = NULL;

	*res = NULL;

	/* we need to disable referrals as the openldap libs don't
	   handle them and paged results at the same time.  Using them
	   together results in the result record containing the server 
	   page control being removed from the result list (tridge/jmcd) 
	
	   leaving this in despite the control that says don't generate
	   referrals, in case the server doesn't support it (jmcd)
	*/
	ldap_set_option(ads->ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

	rc = ldap_search_ext_s(ads->ld, utf8_path, scope, utf8_exp, 
			       search_attrs, 0, controls,
			       NULL, NULL, LDAP_NO_LIMIT, (LDAPMessage **)res);

	ber_free(cookie_be, 1);
	ber_bvfree(cookie_bv);

	if (rc) {
		DEBUG(3,("ldap_search_ext_s(%s) -> %s\n", exp, ldap_err2string(rc)));
		goto done;
	}

	rc = ldap_parse_result(ads->ld, *res, NULL, NULL, NULL,
					NULL, &rcontrols,  0);

	if (!rcontrols) {
		goto done;
	}

	for (i=0; rcontrols[i]; i++) {
		if (strcmp(ADS_PAGE_CTL_OID, rcontrols[i]->ldctl_oid) == 0) {
			cookie_be = ber_init(&rcontrols[i]->ldctl_value);
			ber_scanf(cookie_be,"{iO}", (ber_int_t *) count,
				  &cookie_bv);
			/* the berval is the cookie, but must be freed when
			   it is all done */
			if (cookie_bv->bv_len) /* still more to do */
				*cookie=ber_bvdup(cookie_bv);
			else
				*cookie=NULL;
			ber_bvfree(cookie_bv);
			ber_free(cookie_be, 1);
			break;
		}
	}
	ldap_controls_free(rcontrols);

done:
	talloc_destroy(ctx);
	/* if/when we decide to utf8-encode attrs, take out this next line */
	str_list_free(&search_attrs);

	return ADS_ERROR(rc);
}


/**
 * Get all results for a search.  This uses ads_do_paged_search() to return 
 * all entries in a large search.
 * @param ads connection to ads server 
 * @param bind_path Base dn for the search
 * @param scope Scope of search (LDAP_BASE | LDAP_ONE | LDAP_SUBTREE)
 * @param exp Search expression
 * @param attrs Attributes to retrieve
 * @param res ** which will contain results - free res* with ads_msgfree()
 * @return status of search
 **/
ADS_STATUS ads_do_search_all(ADS_STRUCT *ads, const char *bind_path,
			     int scope, const char *exp,
			     const char **attrs, void **res)
{
	void *cookie = NULL;
	int count = 0;
	ADS_STATUS status;

	status = ads_do_paged_search(ads, bind_path, scope, exp, attrs, res,
				     &count, &cookie);

	if (!ADS_ERR_OK(status)) return status;

	while (cookie) {
		void *res2 = NULL;
		ADS_STATUS status2;
		LDAPMessage *msg, *next;

		status2 = ads_do_paged_search(ads, bind_path, scope, exp, 
					      attrs, &res2, &count, &cookie);

		if (!ADS_ERR_OK(status2)) break;

		/* this relies on the way that ldap_add_result_entry() works internally. I hope
		   that this works on all ldap libs, but I have only tested with openldap */
		for (msg = ads_first_entry(ads, res2); msg; msg = next) {
			next = ads_next_entry(ads, msg);
			ldap_add_result_entry((LDAPMessage **)res, msg);
		}
		/* note that we do not free res2, as the memory is now
                   part of the main returned list */
	}

	return status;
}

/**
 * Run a function on all results for a search.  Uses ads_do_paged_search() and
 *  runs the function as each page is returned, using ads_process_results()
 * @param ads connection to ads server
 * @param bind_path Base dn for the search
 * @param scope Scope of search (LDAP_BASE | LDAP_ONE | LDAP_SUBTREE)
 * @param exp Search expression - specified in local charset
 * @param attrs Attributes to retrieve - specified in UTF-8 or ascii
 * @param fn Function which takes attr name, values list, and data_area
 * @param data_area Pointer which is passed to function on each call
 * @return status of search
 **/
ADS_STATUS ads_do_search_all_fn(ADS_STRUCT *ads, const char *bind_path,
				int scope, const char *exp, const char **attrs,
				BOOL(*fn)(char *, void **, void *), 
				void *data_area)
{
	void *cookie = NULL;
	int count = 0;
	ADS_STATUS status;
	void *res;

	status = ads_do_paged_search(ads, bind_path, scope, exp, attrs, &res,
				     &count, &cookie);

	if (!ADS_ERR_OK(status)) return status;

	ads_process_results(ads, res, fn, data_area);
	ads_msgfree(ads, res);

	while (cookie) {
		status = ads_do_paged_search(ads, bind_path, scope, exp, attrs,
					     &res, &count, &cookie);

		if (!ADS_ERR_OK(status)) break;
		
		ads_process_results(ads, res, fn, data_area);
		ads_msgfree(ads, res);
	}

	return status;
}

/**
 * Do a search with a timeout.
 * @param ads connection to ads server
 * @param bind_path Base dn for the search
 * @param scope Scope of search (LDAP_BASE | LDAP_ONE | LDAP_SUBTREE)
 * @param exp Search expression
 * @param attrs Attributes to retrieve
 * @param res ** which will contain results - free res* with ads_msgfree()
 * @return status of search
 **/
ADS_STATUS ads_do_search(ADS_STRUCT *ads, const char *bind_path, int scope, 
			 const char *exp,
			 const char **attrs, void **res)
{
	struct timeval timeout;
	int rc;
	char *utf8_exp, *utf8_path, **search_attrs = NULL;
	TALLOC_CTX *ctx;

	if (!(ctx = talloc_init()))
		return ADS_ERROR(LDAP_NO_MEMORY);

	/* 0 means the conversion worked but the result was empty 
	   so we only fail if it's negative.  In any case, it always 
	   at least nulls out the dest */
	if ((push_utf8_talloc(ctx, &utf8_exp, exp) < 0) ||
	    (push_utf8_talloc(ctx, &utf8_path, bind_path) < 0)) {
		rc = LDAP_NO_MEMORY;
		goto done;
	}

	if (!attrs || !(*attrs))
		search_attrs = NULL;
	else {
		/* This would be the utf8-encoded version...*/
		/* if (!(search_attrs = ads_push_strvals(ctx, attrs)))  */
		if (!(str_list_copy(&search_attrs, attrs)))
		{
			rc = LDAP_NO_MEMORY;
			goto done;
		}
	}

	timeout.tv_sec = ADS_SEARCH_TIMEOUT;
	timeout.tv_usec = 0;
	*res = NULL;

	/* see the note in ads_do_paged_search - we *must* disable referrals */
	ldap_set_option(ads->ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

	rc = ldap_search_ext_s(ads->ld, utf8_path, scope, utf8_exp,
			       search_attrs, 0, NULL, NULL, 
			       &timeout, LDAP_NO_LIMIT, (LDAPMessage **)res);

	if (rc == LDAP_SIZELIMIT_EXCEEDED) {
		DEBUG(3,("Warning! sizelimit exceeded in ldap. Truncating.\n"));
		rc = 0;
	}

 done:
	talloc_destroy(ctx);
	/* if/when we decide to utf8-encode attrs, take out this next line */
	str_list_free(&search_attrs);
	return ADS_ERROR(rc);
}
/**
 * Do a general ADS search
 * @param ads connection to ads server
 * @param res ** which will contain results - free res* with ads_msgfree()
 * @param exp Search expression
 * @param attrs Attributes to retrieve
 * @return status of search
 **/
ADS_STATUS ads_search(ADS_STRUCT *ads, void **res, 
		      const char *exp, 
		      const char **attrs)
{
	return ads_do_search(ads, ads->config.bind_path, LDAP_SCOPE_SUBTREE, 
			     exp, attrs, res);
}

/**
 * Do a search on a specific DistinguishedName
 * @param ads connection to ads server
 * @param res ** which will contain results - free res* with ads_msgfree()
 * @param dn DistinguishName to search
 * @param attrs Attributes to retrieve
 * @return status of search
 **/
ADS_STATUS ads_search_dn(ADS_STRUCT *ads, void **res, 
			 const char *dn, 
			 const char **attrs)
{
	return ads_do_search(ads, dn, LDAP_SCOPE_BASE, "(objectclass=*)", attrs, res);
}

/**
 * Free up memory from a ads_search
 * @param ads connection to ads server
 * @param msg Search results to free
 **/
void ads_msgfree(ADS_STRUCT *ads, void *msg)
{
	if (!msg) return;
	ldap_msgfree(msg);
}

/**
 * Free up memory from various ads requests
 * @param ads connection to ads server
 * @param mem Area to free
 **/
void ads_memfree(ADS_STRUCT *ads, void *mem)
{
	SAFE_FREE(mem);
}

/**
 * Get a dn from search results
 * @param ads connection to ads server
 * @param res Search results
 * @return dn string
 **/
char *ads_get_dn(ADS_STRUCT *ads, void *res)
{
	char *utf8_dn, *unix_dn;

	utf8_dn = ldap_get_dn(ads->ld, res);
	pull_utf8_allocate((void **) &unix_dn, utf8_dn);
	ldap_memfree(utf8_dn);
	return unix_dn;
}

/**
 * Find a machine account given a hostname
 * @param ads connection to ads server
 * @param res ** which will contain results - free res* with ads_msgfree()
 * @param host Hostname to search for
 * @return status of search
 **/
ADS_STATUS ads_find_machine_acct(ADS_STRUCT *ads, void **res, const char *host)
{
	ADS_STATUS status;
	char *exp;
	const char *attrs[] = {"*", "nTSecurityDescriptor", NULL};

	/* the easiest way to find a machine account anywhere in the tree
	   is to look for hostname$ */
	asprintf(&exp, "(samAccountName=%s$)", host);
	status = ads_search(ads, res, exp, attrs);
	free(exp);
	return status;
}

/**
 * Initialize a list of mods to be used in a modify request
 * @param ctx An initialized TALLOC_CTX
 * @return allocated ADS_MODLIST
 **/
ADS_MODLIST ads_init_mods(TALLOC_CTX *ctx)
{
#define ADS_MODLIST_ALLOC_SIZE 10
	LDAPMod **mods;
	
	if ((mods = (LDAPMod **) talloc_zero(ctx, sizeof(LDAPMod *) * 
					     (ADS_MODLIST_ALLOC_SIZE + 1))))
		/* -1 is safety to make sure we don't go over the end.
		   need to reset it to NULL before doing ldap modify */
		mods[ADS_MODLIST_ALLOC_SIZE] = (LDAPMod *) -1;
	
	return mods;
}


/*
  add an attribute to the list, with values list already constructed
*/
static ADS_STATUS ads_modlist_add(TALLOC_CTX *ctx, ADS_MODLIST *mods, 
				  int mod_op, const char *name, 
				  const void **invals)
{
	int curmod;
	LDAPMod **modlist = (LDAPMod **) *mods;
	void **values;

	if (!invals) {
		values = NULL;
		mod_op = LDAP_MOD_DELETE;
	} else {
		if (mod_op & LDAP_MOD_BVALUES)
			values = (void **) ads_dup_values(ctx, 
					   (const struct berval **)invals);
		else
			values = (void **) ads_push_strvals(ctx, 
						   (const char **) invals);
	}

	/* find the first empty slot */
	for (curmod=0; modlist[curmod] && modlist[curmod] != (LDAPMod *) -1;
	     curmod++);
	if (modlist[curmod] == (LDAPMod *) -1) {
		if (!(modlist = talloc_realloc(ctx, modlist, 
			(curmod+ADS_MODLIST_ALLOC_SIZE+1)*sizeof(LDAPMod *))))
			return ADS_ERROR(LDAP_NO_MEMORY);
		memset(&modlist[curmod], 0, 
		       ADS_MODLIST_ALLOC_SIZE*sizeof(LDAPMod *));
		modlist[curmod+ADS_MODLIST_ALLOC_SIZE] = (LDAPMod *) -1;
		*mods = modlist;
	}
		
	if (!(modlist[curmod] = talloc_zero(ctx, sizeof(LDAPMod))))
		return ADS_ERROR(LDAP_NO_MEMORY);
	modlist[curmod]->mod_type = talloc_strdup(ctx, name);
	if (mod_op & LDAP_MOD_BVALUES)
		modlist[curmod]->mod_bvalues = (struct berval **) values;
	else
		modlist[curmod]->mod_values = (char **) values;
	modlist[curmod]->mod_op = mod_op;
	return ADS_ERROR(LDAP_SUCCESS);
}

/**
 * Add a single string value to a mod list
 * @param ctx An initialized TALLOC_CTX
 * @param mods An initialized ADS_MODLIST
 * @param name The attribute name to add
 * @param val The value to add - NULL means DELETE
 * @return ADS STATUS indicating success of add
 **/
ADS_STATUS ads_mod_str(TALLOC_CTX *ctx, ADS_MODLIST *mods, 
		       const char *name, const char *val)
{
	const char *values[2];

	values[0] = val;
	values[1] = NULL;

	if (!val)
		return ads_modlist_add(ctx, mods, LDAP_MOD_DELETE, name, NULL);
	return ads_modlist_add(ctx, mods, LDAP_MOD_REPLACE, name, 
			       (const void **) values);
}

/**
 * Add an array of string values to a mod list
 * @param ctx An initialized TALLOC_CTX
 * @param mods An initialized ADS_MODLIST
 * @param name The attribute name to add
 * @param vals The array of string values to add - NULL means DELETE
 * @return ADS STATUS indicating success of add
 **/
ADS_STATUS ads_mod_strlist(TALLOC_CTX *ctx, ADS_MODLIST *mods,
			   const char *name, const char **vals)
{
	if (!vals)
		return ads_modlist_add(ctx, mods, LDAP_MOD_DELETE, name, NULL);
	return ads_modlist_add(ctx, mods, LDAP_MOD_REPLACE, 
			       name, (const void **) vals);
}

/**
 * Add a single ber-encoded value to a mod list
 * @param ctx An initialized TALLOC_CTX
 * @param mods An initialized ADS_MODLIST
 * @param name The attribute name to add
 * @param val The value to add - NULL means DELETE
 * @return ADS STATUS indicating success of add
 **/
static ADS_STATUS ads_mod_ber(TALLOC_CTX *ctx, ADS_MODLIST *mods, 
			      const char *name, const struct berval *val)
{
	const struct berval *values[2];

	values[0] = val;
	values[1] = NULL;
	if (!val)
		return ads_modlist_add(ctx, mods, LDAP_MOD_DELETE, name, NULL);
	return ads_modlist_add(ctx, mods, LDAP_MOD_REPLACE|LDAP_MOD_BVALUES,
			       name, (const void **) values);
}

/**
 * Perform an ldap modify
 * @param ads connection to ads server
 * @param mod_dn DistinguishedName to modify
 * @param mods list of modifications to perform
 * @return status of modify
 **/
ADS_STATUS ads_gen_mod(ADS_STRUCT *ads, const char *mod_dn, ADS_MODLIST mods)
{
	int ret,i;
	char *utf8_dn = NULL;
	/* 
	   this control is needed to modify that contains a currently 
	   non-existent attribute (but allowable for the object) to run
	*/
	LDAPControl PermitModify = {
		"1.2.840.113556.1.4.1413",
		{0, NULL},
		(char) 1};
	LDAPControl *controls[2];

	controls[0] = &PermitModify;
	controls[1] = NULL;

	push_utf8_allocate((void **) &utf8_dn, mod_dn);

	/* find the end of the list, marked by NULL or -1 */
	for(i=0;(mods[i]!=0)&&(mods[i]!=(LDAPMod *) -1);i++);
	/* make sure the end of the list is NULL */
	mods[i] = NULL;
	ret = ldap_modify_ext_s(ads->ld, utf8_dn ? utf8_dn : mod_dn,
				(LDAPMod **) mods, controls, NULL);
	SAFE_FREE(utf8_dn);
	return ADS_ERROR(ret);
}

/**
 * Perform an ldap add
 * @param ads connection to ads server
 * @param new_dn DistinguishedName to add
 * @param mods list of attributes and values for DN
 * @return status of add
 **/
ADS_STATUS ads_gen_add(ADS_STRUCT *ads, const char *new_dn, ADS_MODLIST mods)
{
	int ret, i;
	char *utf8_dn = NULL;

	push_utf8_allocate((void **) &utf8_dn, new_dn);
	
	/* find the end of the list, marked by NULL or -1 */
	for(i=0;(mods[i]!=0)&&(mods[i]!=(LDAPMod *) -1);i++);
	/* make sure the end of the list is NULL */
	mods[i] = NULL;

	ret = ldap_add_s(ads->ld, utf8_dn ? utf8_dn : new_dn, mods);
	SAFE_FREE(utf8_dn);
	return ADS_ERROR(ret);
}

/**
 * Delete a DistinguishedName
 * @param ads connection to ads server
 * @param new_dn DistinguishedName to delete
 * @return status of delete
 **/
ADS_STATUS ads_del_dn(ADS_STRUCT *ads, char *del_dn)
{
	int ret;
	char *utf8_dn = NULL;
	push_utf8_allocate((void **) &utf8_dn, del_dn);
	ret = ldap_delete(ads->ld, utf8_dn ? utf8_dn : del_dn);
	return ADS_ERROR(ret);
}

/**
 * Build an org unit string
 *  if org unit is Computers or blank then assume a container, otherwise
 *  assume a \ separated list of organisational units
 * @param org_unit Organizational unit
 * @return org unit string - caller must free
 **/
char *ads_ou_string(const char *org_unit)
{	
	if (!org_unit || !*org_unit || strcasecmp(org_unit, "Computers") == 0) {
		return strdup("cn=Computers");
	}

	return ads_build_path(org_unit, "\\/", "ou=", 1);
}



/*
  add a machine account to the ADS server
*/
static ADS_STATUS ads_add_machine_acct(ADS_STRUCT *ads, const char *hostname, 
				       const char *org_unit)
{
	ADS_STATUS ret;
	char *host_spn, *host_upn, *new_dn, *samAccountName, *controlstr;
	char *ou_str;
	TALLOC_CTX *ctx;
	ADS_MODLIST mods;
	const char *objectClass[] = {"top", "person", "organizationalPerson",
				     "user", "computer", NULL};

	if (!(ctx = talloc_init_named("machine_account")))
		return ADS_ERROR(LDAP_NO_MEMORY);

	ret = ADS_ERROR(LDAP_NO_MEMORY);

	if (!(host_spn = talloc_asprintf(ctx, "HOST/%s", hostname)))
		goto done;
	if (!(host_upn = talloc_asprintf(ctx, "%s@%s", host_spn, ads->config.realm)))
		goto done;
	ou_str = ads_ou_string(org_unit);
	new_dn = talloc_asprintf(ctx, "cn=%s,%s,%s", hostname, ou_str, 
				 ads->config.bind_path);
	free(ou_str);
	if (!new_dn)
		goto done;

	if (!(samAccountName = talloc_asprintf(ctx, "%s$", hostname)))
		goto done;
	if (!(controlstr = talloc_asprintf(ctx, "%u", 
		   UF_DONT_EXPIRE_PASSWD | UF_WORKSTATION_TRUST_ACCOUNT | 
		   UF_TRUSTED_FOR_DELEGATION | UF_USE_DES_KEY_ONLY)))
		goto done;

	if (!(mods = ads_init_mods(ctx)))
		goto done;
	
	ads_mod_str(ctx, &mods, "cn", hostname);
	ads_mod_str(ctx, &mods, "sAMAccountName", samAccountName);
	ads_mod_strlist(ctx, &mods, "objectClass", objectClass);
	ads_mod_str(ctx, &mods, "userPrincipalName", host_upn);
	ads_mod_str(ctx, &mods, "servicePrincipalName", host_spn);
	ads_mod_str(ctx, &mods, "dNSHostName", hostname);
	ads_mod_str(ctx, &mods, "userAccountControl", controlstr);
	ads_mod_str(ctx, &mods, "operatingSystem", "Samba");
	ads_mod_str(ctx, &mods, "operatingSystemVersion", VERSION);

	ads_gen_add(ads, new_dn, mods);
	ret = ads_set_machine_sd(ads, hostname, new_dn);

done:
	talloc_destroy(ctx);
	return ret;
}

/*
  dump a binary result from ldap
*/
static void dump_binary(const char *field, struct berval **values)
{
	int i, j;
	for (i=0; values[i]; i++) {
		printf("%s: ", field);
		for (j=0; j<values[i]->bv_len; j++) {
			printf("%02X", (unsigned char)values[i]->bv_val[j]);
		}
		printf("\n");
	}
}

/*
  dump a sid result from ldap
*/
static void dump_sid(const char *field, struct berval **values)
{
	int i;
	for (i=0; values[i]; i++) {
		DOM_SID sid;
		sid_parse(values[i]->bv_val, values[i]->bv_len, &sid);
		printf("%s: %s\n", field, sid_string_static(&sid));
	}
}

/*
  dump ntSecurityDescriptor
*/
static void dump_sd(const char *filed, struct berval **values)
{
	prs_struct ps;
	
	SEC_DESC   *psd = 0;
	TALLOC_CTX *ctx = 0;

	if (!(ctx = talloc_init_named("sec_io_desc")))
		return;

	/* prepare data */
	prs_init(&ps, values[0]->bv_len, ctx, UNMARSHALL);
	prs_append_data(&ps, values[0]->bv_val, values[0]->bv_len);
	ps.data_offset = 0;

	/* parse secdesc */
	if (!sec_io_desc("sd", &psd, &ps, 1)) {
		prs_mem_free(&ps);
		talloc_destroy(ctx);
		return;
	}
	if (psd) ads_disp_sd(psd);

	prs_mem_free(&ps);
	talloc_destroy(ctx);
}

/*
  dump a string result from ldap
*/
static void dump_string(const char *field, char **values)
{
	int i;
	for (i=0; values[i]; i++) {
		printf("%s: %s\n", field, values[i]);
	}
}

/*
  dump a field from LDAP on stdout
  used for debugging
*/

static BOOL ads_dump_field(char *field, void **values, void *data_area)
{
	struct {
		char *name;
		BOOL string;
		void (*handler)(const char *, struct berval **);
	} handlers[] = {
		{"objectGUID", False, dump_binary},
		{"nTSecurityDescriptor", False, dump_sd},
		{"dnsRecord", False, dump_binary},
		{"objectSid", False, dump_sid},
		{NULL, True, NULL}
	};
	int i;

	if (!field) { /* must be end of an entry */
		printf("\n");
		return False;
	}

	for (i=0; handlers[i].name; i++) {
		if (StrCaseCmp(handlers[i].name, field) == 0) {
			if (!values) /* first time, indicate string or not */
				return handlers[i].string;
			handlers[i].handler(field, (struct berval **) values);
			break;
		}
	}
	if (!handlers[i].name) {
		if (!values) /* first time, indicate string conversion */
			return True;
		dump_string(field, (char **)values);
	}
	return False;
}

/**
 * Dump a result from LDAP on stdout
 *  used for debugging
 * @param ads connection to ads server
 * @param res Results to dump
 **/

void ads_dump(ADS_STRUCT *ads, void *res)
{
	ads_process_results(ads, res, ads_dump_field, NULL);
}

/**
 * Walk through results, calling a function for each entry found.
 *  The function receives a field name, a berval * array of values,
 *  and a data area passed through from the start.  The function is
 *  called once with null for field and values at the end of each
 *  entry.
 * @param ads connection to ads server
 * @param res Results to process
 * @param fn Function for processing each result
 * @param data_area user-defined area to pass to function
 **/
void ads_process_results(ADS_STRUCT *ads, void *res,
			 BOOL(*fn)(char *, void **, void *),
			 void *data_area)
{
	void *msg;
	TALLOC_CTX *ctx;

	if (!(ctx = talloc_init()))
		return;

	for (msg = ads_first_entry(ads, res); msg; 
	     msg = ads_next_entry(ads, msg)) {
		char *utf8_field;
		BerElement *b;
	
		for (utf8_field=ldap_first_attribute(ads->ld,
						     (LDAPMessage *)msg,&b); 
		     utf8_field;
		     utf8_field=ldap_next_attribute(ads->ld,
						    (LDAPMessage *)msg,b)) {
			struct berval **ber_vals;
			char **str_vals, **utf8_vals;
			char *field;
			BOOL string; 

			pull_utf8_talloc(ctx, &field, utf8_field);
			string = fn(field, NULL, data_area);

			if (string) {
				utf8_vals = ldap_get_values(ads->ld,
					       	 (LDAPMessage *)msg, field);
				str_vals = ads_pull_strvals(ctx, 
						  (const char **) utf8_vals);
				fn(field, (void **) str_vals, data_area);
				ldap_value_free(utf8_vals);
			} else {
				ber_vals = ldap_get_values_len(ads->ld, 
						 (LDAPMessage *)msg, field);
				fn(field, (void **) ber_vals, data_area);

				ldap_value_free_len(ber_vals);
			}
			ldap_memfree(utf8_field);
		}
		ber_free(b, 0);
		talloc_destroy_pool(ctx);
		fn(NULL, NULL, data_area); /* completed an entry */

	}
	talloc_destroy(ctx);
}

/**
 * count how many replies are in a LDAPMessage
 * @param ads connection to ads server
 * @param res Results to count
 * @return number of replies
 **/
int ads_count_replies(ADS_STRUCT *ads, void *res)
{
	return ldap_count_entries(ads->ld, (LDAPMessage *)res);
}

/**
 * Join a machine to a realm
 *  Creates the machine account and sets the machine password
 * @param ads connection to ads server
 * @param hostname name of host to add
 * @param org_unit Organizational unit to place machine in
 * @return status of join
 **/
ADS_STATUS ads_join_realm(ADS_STRUCT *ads, const char *hostname, const char *org_unit)
{
	ADS_STATUS status;
	LDAPMessage *res;
	char *host;

	/* hostname must be lowercase */
	host = strdup(hostname);
	strlower(host);

	status = ads_find_machine_acct(ads, (void **)&res, host);
	if (ADS_ERR_OK(status) && ads_count_replies(ads, res) == 1) {
		DEBUG(0, ("Host account for %s already exists - deleting old account\n", host));
		status = ads_leave_realm(ads, host);
		if (!ADS_ERR_OK(status)) {
			DEBUG(0, ("Failed to delete host '%s' from the '%s' realm.\n", 
				  host, ads->config.realm));
			return status;
		}
	}

	status = ads_add_machine_acct(ads, host, org_unit);
	if (!ADS_ERR_OK(status)) {
		DEBUG(0, ("ads_add_machine_acct: %s\n", ads_errstr(status)));
		return status;
	}

	status = ads_find_machine_acct(ads, (void **)&res, host);
	if (!ADS_ERR_OK(status)) {
		DEBUG(0, ("Host account test failed\n"));
		return status;
	}

	free(host);

	return status;
}

/**
 * Delete a machine from the realm
 * @param ads connection to ads server
 * @param hostname Machine to remove
 * @return status of delete
 **/
ADS_STATUS ads_leave_realm(ADS_STRUCT *ads, const char *hostname)
{
	ADS_STATUS status;
	void *res;
	char *hostnameDN, *host; 
	int rc;

	/* hostname must be lowercase */
	host = strdup(hostname);
	strlower(host);

	status = ads_find_machine_acct(ads, &res, host);
	if (!ADS_ERR_OK(status)) {
	    DEBUG(0, ("Host account for %s does not exist.\n", host));
	    return status;
	}

	hostnameDN = ads_get_dn(ads, (LDAPMessage *)res);
	rc = ldap_delete_s(ads->ld, hostnameDN);
	ads_memfree(ads, hostnameDN);
	if (rc != LDAP_SUCCESS) {
		return ADS_ERROR(rc);
	}

	status = ads_find_machine_acct(ads, &res, host);
	if (ADS_ERR_OK(status) && ads_count_replies(ads, res) == 1) {
		DEBUG(0, ("Failed to remove host account.\n"));
		return status;
	}

	free(host);

	return status;
}

/**
 * add machine account to existing security descriptor 
 * @param ads connection to ads server
 * @param hostname machine to add
 * @param dn DN of security descriptor
 * @return status
 **/
ADS_STATUS ads_set_machine_sd(ADS_STRUCT *ads, const char *hostname, char *dn)
{
	const char     *attrs[] = {"ntSecurityDescriptor", "objectSid", 0};
	char           *exp     = 0;
	size_t          sd_size = 0;
	struct berval **bvals   = 0;
	struct berval   bval = {0, NULL};
	prs_struct      ps;
	prs_struct      ps_wire;

	LDAPMessage *res  = 0;
	LDAPMessage *msg  = 0;
	ADS_MODLIST  mods = 0;

	NTSTATUS    status;
	ADS_STATUS  ret;
	DOM_SID     sid;
	SEC_DESC   *psd = 0;
	TALLOC_CTX *ctx = 0;	

	if (!ads) return ADS_ERROR(LDAP_SERVER_DOWN);

	ret = ADS_ERROR(LDAP_SUCCESS);

	asprintf(&exp, "(samAccountName=%s$)", hostname);
	ret = ads_search(ads, (void *) &res, exp, attrs);

	if (!ADS_ERR_OK(ret)) return ret;

	msg   = ads_first_entry(ads, res);
	bvals = ldap_get_values_len(ads->ld, msg, attrs[0]);
	ads_pull_sid(ads, msg, attrs[1], &sid);	
	ads_msgfree(ads, res);
#if 0
	file_save("/tmp/sec_desc.old", bvals[0]->bv_val, bvals[0]->bv_len);
#endif
	if (!(ctx = talloc_init_named("sec_io_desc")))
		return ADS_ERROR(LDAP_NO_MEMORY);

	prs_init(&ps, bvals[0]->bv_len, ctx, UNMARSHALL);
	prs_append_data(&ps, bvals[0]->bv_val, bvals[0]->bv_len);
	ps.data_offset = 0;
	ldap_value_free_len(bvals);

	if (!sec_io_desc("sd", &psd, &ps, 1))
		goto ads_set_sd_error;

	status = sec_desc_add_sid(ctx, &psd, &sid, SEC_RIGHTS_FULL_CTRL, &sd_size);

	if (!NT_STATUS_IS_OK(status))
		goto ads_set_sd_error;

	prs_init(&ps_wire, sd_size, ctx, MARSHALL);
	if (!sec_io_desc("sd_wire", &psd, &ps_wire, 1))
		goto ads_set_sd_error;

#if 0
	file_save("/tmp/sec_desc.new", ps_wire.data_p, sd_size);
#endif
	if (!(mods = ads_init_mods(ctx))) return ADS_ERROR(LDAP_NO_MEMORY);

	bval.bv_len = sd_size;
	bval.bv_val = prs_data_p(&ps_wire);
	ads_mod_ber(ctx, &mods, attrs[0], &bval);
	ret = ads_gen_mod(ads, dn, mods);

	prs_mem_free(&ps);
	prs_mem_free(&ps_wire);
	talloc_destroy(ctx);
	return ret;

ads_set_sd_error:
	prs_mem_free(&ps);
	prs_mem_free(&ps_wire);
	talloc_destroy(ctx);
	return ADS_ERROR(LDAP_NO_MEMORY);
}

/**
 * Set the machine account password
 * @param ads connection to ads server
 * @param hostname machine whose password is being set
 * @param password new password
 * @return status of password change
 **/
ADS_STATUS ads_set_machine_password(ADS_STRUCT *ads,
				    const char *hostname, 
				    const char *password)
{
	ADS_STATUS status;
	char *host = strdup(hostname);
	char *principal; 

	strlower(host);

	/*
	  we need to use the '$' form of the name here, as otherwise the
	  server might end up setting the password for a user instead
	 */
	asprintf(&principal, "%s$@%s", host, ads->auth.realm);
	
	status = krb5_set_password(ads->auth.kdc_server, principal, password);
	
	free(host);
	free(principal);

	return status;
}

/**
 * pull the first entry from a ADS result
 * @param ads connection to ads server
 * @param res Results of search
 * @return first entry from result
 **/
void *ads_first_entry(ADS_STRUCT *ads, void *res)
{
	return (void *)ldap_first_entry(ads->ld, (LDAPMessage *)res);
}

/**
 * pull the next entry from a ADS result
 * @param ads connection to ads server
 * @param res Results of search
 * @return next entry from result
 **/
void *ads_next_entry(ADS_STRUCT *ads, void *res)
{
	return (void *)ldap_next_entry(ads->ld, (LDAPMessage *)res);
}

/**
 * pull a single string from a ADS result
 * @param ads connection to ads server
 * @param mem_ctx TALLOC_CTX to use for allocating result string
 * @param msg Results of search
 * @param field Attribute to retrieve
 * @return Result string in talloc context
 **/
char *ads_pull_string(ADS_STRUCT *ads, 
		      TALLOC_CTX *mem_ctx, void *msg, const char *field)
{
	char **values;
	char *ret = NULL;
	char *ux_string;
	int rc;

	values = ldap_get_values(ads->ld, msg, field);
	if (!values) return NULL;
	
	if (values[0]) {
		rc = pull_utf8_talloc(mem_ctx, &ux_string, 
				      values[0]);
		if (rc != -1)
			ret = ux_string;
		
	}
	ldap_value_free(values);
	return ret;
}

/**
 * pull an array of strings from a ADS result
 * @param ads connection to ads server
 * @param mem_ctx TALLOC_CTX to use for allocating result string
 * @param msg Results of search
 * @param field Attribute to retrieve
 * @return Result strings in talloc context
 **/
char **ads_pull_strings(ADS_STRUCT *ads, 
		       TALLOC_CTX *mem_ctx, void *msg, const char *field)
{
	char **values;
	char **ret = NULL;
	int i, n;

	values = ldap_get_values(ads->ld, msg, field);
	if (!values) return NULL;

	for (i=0;values[i];i++) /* noop */ ;
	n = i;

	ret = talloc(mem_ctx, sizeof(char *) * (n+1));

	for (i=0;i<n;i++) {
		if (pull_utf8_talloc(mem_ctx, &ret[i], values[i]) == -1) {
			return NULL;
		}
	}
	ret[i] = NULL;

	ldap_value_free(values);
	return ret;
}


/**
 * pull a single uint32 from a ADS result
 * @param ads connection to ads server
 * @param msg Results of search
 * @param field Attribute to retrieve
 * @param v Pointer to int to store result
 * @return boolean inidicating success
*/
BOOL ads_pull_uint32(ADS_STRUCT *ads, 
		     void *msg, const char *field, uint32 *v)
{
	char **values;

	values = ldap_get_values(ads->ld, msg, field);
	if (!values) return False;
	if (!values[0]) {
		ldap_value_free(values);
		return False;
	}

	*v = atoi(values[0]);
	ldap_value_free(values);
	return True;
}

/**
 * pull a single DOM_SID from a ADS result
 * @param ads connection to ads server
 * @param msg Results of search
 * @param field Attribute to retrieve
 * @param sid Pointer to sid to store result
 * @return boolean inidicating success
*/
BOOL ads_pull_sid(ADS_STRUCT *ads, 
		  void *msg, const char *field, DOM_SID *sid)
{
	struct berval **values;
	BOOL ret = False;

	values = ldap_get_values_len(ads->ld, msg, field);

	if (!values) return False;

	if (values[0]) {
		ret = sid_parse(values[0]->bv_val, values[0]->bv_len, sid);
	}
	
	ldap_value_free_len(values);
	return ret;
}

/**
 * pull an array of DOM_SIDs from a ADS result
 * @param ads connection to ads server
 * @param mem_ctx TALLOC_CTX for allocating sid array
 * @param msg Results of search
 * @param field Attribute to retrieve
 * @param sids pointer to sid array to allocate
 * @return the count of SIDs pulled
 **/
int ads_pull_sids(ADS_STRUCT *ads, TALLOC_CTX *mem_ctx,
		  void *msg, const char *field, DOM_SID **sids)
{
	struct berval **values;
	BOOL ret;
	int count, i;

	values = ldap_get_values_len(ads->ld, msg, field);

	if (!values) return 0;

	for (i=0; values[i]; i++) /* nop */ ;

	(*sids) = talloc(mem_ctx, sizeof(DOM_SID) * i);

	count = 0;
	for (i=0; values[i]; i++) {
		ret = sid_parse(values[i]->bv_val, values[i]->bv_len, &(*sids)[count]);
		if (ret) count++;
	}
	
	ldap_value_free_len(values);
	return count;
}


/**
 * find the update serial number - this is the core of the ldap cache
 * @param ads connection to ads server
 * @param ads connection to ADS server
 * @param usn Pointer to retrieved update serial number
 * @return status of search
 **/
ADS_STATUS ads_USN(ADS_STRUCT *ads, uint32 *usn)
{
	const char *attrs[] = {"highestCommittedUSN", NULL};
	ADS_STATUS status;
	void *res;

	status = ads_do_search(ads, "", LDAP_SCOPE_BASE, "(objectclass=*)", attrs, &res);
	if (!ADS_ERR_OK(status)) return status;

	if (ads_count_replies(ads, res) != 1) {
		return ADS_ERROR(LDAP_NO_RESULTS_RETURNED);
	}

	ads_pull_uint32(ads, res, "highestCommittedUSN", usn);
	ads_msgfree(ads, res);
	return ADS_SUCCESS;
}


/**
 * Find the servers name and realm - this can be done before authentication 
 *  The ldapServiceName field on w2k  looks like this:
 *    vnet3.home.samba.org:win2000-vnet3$@VNET3.HOME.SAMBA.ORG
 * @param ads connection to ads server
 * @return status of search
 **/
ADS_STATUS ads_server_info(ADS_STRUCT *ads)
{
	const char *attrs[] = {"ldapServiceName", NULL};
	ADS_STATUS status;
	void *res;
	char **values;
	char *p;

	status = ads_do_search(ads, "", LDAP_SCOPE_BASE, "(objectclass=*)", attrs, &res);
	if (!ADS_ERR_OK(status)) return status;

	values = ldap_get_values(ads->ld, res, "ldapServiceName");
	if (!values || !values[0]) return ADS_ERROR(LDAP_NO_RESULTS_RETURNED);

	p = strchr(values[0], ':');
	if (!p) {
		ldap_value_free(values);
		ldap_msgfree(res);
		return ADS_ERROR(LDAP_DECODING_ERROR);
	}

	SAFE_FREE(ads->config.ldap_server_name);

	ads->config.ldap_server_name = strdup(p+1);
	p = strchr(ads->config.ldap_server_name, '$');
	if (!p || p[1] != '@') {
		ldap_value_free(values);
		ldap_msgfree(res);
		SAFE_FREE(ads->config.ldap_server_name);
		return ADS_ERROR(LDAP_DECODING_ERROR);
	}

	*p = 0;

	SAFE_FREE(ads->config.realm);
	SAFE_FREE(ads->config.bind_path);

	ads->config.realm = strdup(p+2);
	ads->config.bind_path = ads_build_dn(ads->config.realm);

	DEBUG(3,("got ldap server name %s@%s\n", 
		 ads->config.ldap_server_name, ads->config.realm));

	return ADS_SUCCESS;
}


/**
 * find the list of trusted domains
 * @param ads connection to ads server
 * @param mem_ctx TALLOC_CTX for allocating results
 * @param num_trusts pointer to number of trusts
 * @param names pointer to trusted domain name list
 * @param sids pointer to list of sids of trusted domains
 * @return the count of SIDs pulled
 **/
ADS_STATUS ads_trusted_domains(ADS_STRUCT *ads, TALLOC_CTX *mem_ctx, 
			       int *num_trusts, 
			       char ***names, 
			       char ***alt_names,
			       DOM_SID **sids)
{
	const char *attrs[] = {"name", "flatname", "securityIdentifier", 
			       "trustDirection", NULL};
	ADS_STATUS status;
	void *res, *msg;
	int count, i;

	*num_trusts = 0;

	status = ads_search(ads, &res, "(objectcategory=trustedDomain)", attrs);
	if (!ADS_ERR_OK(status)) return status;

	count = ads_count_replies(ads, res);
	if (count == 0) {
		ads_msgfree(ads, res);
		return ADS_ERROR(LDAP_NO_RESULTS_RETURNED);
	}

	(*names) = talloc(mem_ctx, sizeof(char *) * count);
	(*alt_names) = talloc(mem_ctx, sizeof(char *) * count);
	(*sids) = talloc(mem_ctx, sizeof(DOM_SID) * count);
	if (! *names || ! *sids) return ADS_ERROR(LDAP_NO_MEMORY);

	for (i=0, msg = ads_first_entry(ads, res); msg; msg = ads_next_entry(ads, msg)) {
		uint32 direction;

		/* direction is a 2 bit bitfield, 1 means they trust us 
		   but we don't trust them, so we should not list them
		   as users from that domain can't login */
		if (ads_pull_uint32(ads, msg, "trustDirection", &direction) &&
		    direction == 1) {
			continue;
		}
		
		(*names)[i] = ads_pull_string(ads, mem_ctx, msg, "name");
		(*alt_names)[i] = ads_pull_string(ads, mem_ctx, msg, "flatname");

		if ((*alt_names)[i] && (*alt_names)[i][0]) {
			/* we prefer the flatname as the primary name
			   for consistency with RPC */
			char *name = (*alt_names)[i];
			(*alt_names)[i] = (*names)[i];
			(*names)[i] = name;
		}
		if (ads_pull_sid(ads, msg, "securityIdentifier", &(*sids)[i])) {
			i++;
		}
	}

	ads_msgfree(ads, res);

	*num_trusts = i;

	return ADS_SUCCESS;
}

/**
 * find the domain sid for our domain
 * @param ads connection to ads server
 * @param sid Pointer to domain sid
 * @return status of search
 **/
ADS_STATUS ads_domain_sid(ADS_STRUCT *ads, DOM_SID *sid)
{
	const char *attrs[] = {"objectSid", NULL};
	void *res;
	ADS_STATUS rc;

	rc = ads_do_search(ads, ads->config.bind_path, LDAP_SCOPE_BASE, "(objectclass=*)", 
			   attrs, &res);
	if (!ADS_ERR_OK(rc)) return rc;
	if (!ads_pull_sid(ads, res, "objectSid", sid)) {
		return ADS_ERROR_SYSTEM(ENOENT);
	}
	ads_msgfree(ads, res);
	
	return ADS_SUCCESS;
}

/* this is rather complex - we need to find the allternate (netbios) name
   for the domain, but there isn't a simple query to do this. Instead
   we look for the principle names on the DCs account and find one that has 
   the right form, then extract the netbios name of the domain from that
*/
ADS_STATUS ads_workgroup_name(ADS_STRUCT *ads, TALLOC_CTX *mem_ctx, char **workgroup)
{
	char *exp;
	ADS_STATUS rc;
	char **principles;
	char *prefix;
	int prefix_length;
	int i;
	void *res;
	const char *attrs[] = {"servicePrincipalName", NULL};

	(*workgroup) = NULL;

	asprintf(&exp, "(&(objectclass=computer)(dnshostname=%s.%s))", 
		 ads->config.ldap_server_name, ads->config.realm);
	rc = ads_search(ads, &res, exp, attrs);
	free(exp);

	if (!ADS_ERR_OK(rc)) {
		return rc;
	}

	principles = ads_pull_strings(ads, mem_ctx, res, "servicePrincipalName");

	ads_msgfree(ads, res);

	if (!principles) {
		return ADS_ERROR(LDAP_NO_RESULTS_RETURNED);
	}

	asprintf(&prefix, "HOST/%s.%s/", 
		 ads->config.ldap_server_name, 
		 ads->config.realm);

	prefix_length = strlen(prefix);

	for (i=0;principles[i]; i++) {
		if (strncasecmp(principles[i], prefix, prefix_length) == 0 &&
		    strcasecmp(ads->config.realm, principles[i]+prefix_length) != 0 &&
		    !strchr(principles[i]+prefix_length, '.')) {
			/* found an alternate (short) name for the domain. */
			DEBUG(3,("Found alternate name '%s' for realm '%s'\n",
				 principles[i]+prefix_length, 
				 ads->config.realm));
			(*workgroup) = talloc_strdup(mem_ctx, principles[i]+prefix_length);
			break;
		}
	}
	free(prefix);

	if (!*workgroup) {
		return ADS_ERROR(LDAP_NO_RESULTS_RETURNED);
	}
	
	return ADS_SUCCESS;
}

#endif
