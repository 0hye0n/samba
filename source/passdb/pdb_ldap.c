/* 
   Unix SMB/CIFS implementation.
   LDAP protocol helper functions for SAMBA
   Copyright (C) Jean Fran�ois Micouleau	1998
   Copyright (C) Gerald Carter			2001
   Copyright (C) Shahms King			2001
   Copyright (C) Andrew Bartlett		2002
   Copyright (C) Stefan (metze) Metzmacher	2002
   Copyright (C) Jim McDonough                  2003
    
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

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_PASSDB

/* TODO:
*  persistent connections: if using NSS LDAP, many connections are made
*      however, using only one within Samba would be nice
*  
*  Clean up SSL stuff, compile on OpenLDAP 1.x, 2.x, and Netscape SDK
*
*  Other LDAP based login attributes: accountExpires, etc.
*  (should be the domain of Samba proper, but the sam_password/SAM_ACCOUNT
*  structures don't have fields for some of these attributes)
*
*  SSL is done, but can't get the certificate based authentication to work
*  against on my test platform (Linux 2.4, OpenLDAP 2.x)
*/

/* NOTE: this will NOT work against an Active Directory server
*  due to the fact that the two password fields cannot be retrieved
*  from a server; recommend using security = domain in this situation
*  and/or winbind
*/

#include <lber.h>
#include <ldap.h>

#include "smb_ldap.h"

#ifndef SAM_ACCOUNT
#define SAM_ACCOUNT struct sam_passwd
#endif

static uint32 ldapsam_get_next_available_nua_rid(struct smb_ldap_privates *ldap_state);

static const char *attr[] = {"uid", "pwdLastSet", "logonTime",
			     "logoffTime", "kickoffTime", "cn",
			     "pwdCanChange", "pwdMustChange",
			     "displayName", "homeDrive",
			     "smbHome", "scriptPath",
			     "profilePath", "description",
			     "userWorkstations", "rid",
			     "primaryGroupID", "lmPassword",
			     "ntPassword", "acctFlags",
			     "domain", "objectClass", 
			     "uidNumber", "gidNumber", 
			     "homeDirectory", NULL };

/*******************************************************************
 run the search by name.
******************************************************************/
static int ldapsam_search_one_user (struct smb_ldap_privates *ldap_state, const char *filter, LDAPMessage ** result)
{
	int scope = LDAP_SCOPE_SUBTREE;
	int rc;

	DEBUG(2, ("ldapsam_search_one_user: searching for:[%s]\n", filter));

	rc = smb_ldap_search(ldap_state, lp_ldap_suffix (), scope, filter, attr, 0, result);

	if (rc != LDAP_SUCCESS)	{
		char *ld_error = NULL;
		ldap_get_option(ldap_state->ldap_struct, LDAP_OPT_ERROR_STRING,
				&ld_error);
		DEBUG(0,("ldapsam_search_one_user: Problem during the LDAP search: %s (%s)\n", 
			ld_error?ld_error:"(unknown)", ldap_err2string (rc)));
		DEBUG(3,("ldapsam_search_one_user: Query was: %s, %s\n", lp_ldap_suffix(), 
			filter));
		SAFE_FREE(ld_error);
	}
	
	return rc;
}

/*******************************************************************
 run the search by name.
******************************************************************/
static int ldapsam_search_one_user_by_name (struct smb_ldap_privates *ldap_state, const char *user,
			     LDAPMessage ** result)
{
	pstring filter;
	char *escape_user = escape_ldap_string_alloc(user);

	if (!escape_user) {
		return LDAP_NO_MEMORY;
	}

	/*
	 * in the filter expression, replace %u with the real name
	 * so in ldap filter, %u MUST exist :-)
	 */
	pstrcpy(filter, lp_ldap_filter());

	/* 
	 * have to use this here because $ is filtered out
	   * in pstring_sub
	 */
	

	all_string_sub(filter, "%u", escape_user, sizeof(pstring));
	SAFE_FREE(escape_user);

	return ldapsam_search_one_user(ldap_state, filter, result);
}

/*******************************************************************
 run the search by uid.
******************************************************************/
static int ldapsam_search_one_user_by_uid(struct smb_ldap_privates *ldap_state, 
					  int uid,
					  LDAPMessage ** result)
{
	struct passwd *user;
	pstring filter;
	char *escape_user;

	/* Get the username from the system and look that up in the LDAP */
	
	if ((user = getpwuid_alloc(uid)) == NULL) {
		DEBUG(3,("ldapsam_search_one_user_by_uid: Failed to locate uid [%d]\n", uid));
		return LDAP_NO_SUCH_OBJECT;
	}
	
	pstrcpy(filter, lp_ldap_filter());
	
	escape_user = escape_ldap_string_alloc(user->pw_name);
	if (!escape_user) {
		passwd_free(&user);
		return LDAP_NO_MEMORY;
	}

	all_string_sub(filter, "%u", escape_user, sizeof(pstring));

	passwd_free(&user);
	SAFE_FREE(escape_user);

	return ldapsam_search_one_user(ldap_state, filter, result);
}

/*******************************************************************
 run the search by rid.
******************************************************************/
static int ldapsam_search_one_user_by_rid (struct smb_ldap_privates *ldap_state, 
					   uint32 rid,
					   LDAPMessage ** result)
{
	pstring filter;
	int rc;

	/* check if the user rid exsists, if not, try searching on the uid */
	
	snprintf(filter, sizeof(filter) - 1, "rid=%i", rid);
	rc = ldapsam_search_one_user(ldap_state, filter, result);
	
	if (rc != LDAP_SUCCESS)
		rc = ldapsam_search_one_user_by_uid(ldap_state,
						    fallback_pdb_user_rid_to_uid(rid), 
						    result);

	return rc;
}

/*******************************************************************
 Delete complete object or objectclass and attrs from
 object found in search_result depending on lp_ldap_delete_dn
******************************************************************/
static NTSTATUS ldapsam_delete_entry(struct smb_ldap_privates *ldap_state,
				     LDAPMessage *result,
				     const char *objectclass,
				     const char **attrs)
{
	int rc;
	LDAPMessage *entry;
	LDAPMod **mods = NULL;
	char *name, *dn;
	BerElement *ptr = NULL;

	rc = ldap_count_entries(ldap_state->ldap_struct, result);

	if (rc != 1) {
		DEBUG(0, ("Entry must exist exactly once!\n"));
		return NT_STATUS_UNSUCCESSFUL;
	}

	entry = ldap_first_entry(ldap_state->ldap_struct, result);
	dn    = ldap_get_dn(ldap_state->ldap_struct, entry);

	if (lp_ldap_delete_dn()) {
		NTSTATUS ret = NT_STATUS_OK;
		rc = smb_ldap_delete(ldap_state, dn);

		if (rc != LDAP_SUCCESS) {
			DEBUG(0, ("Could not delete object %s\n", dn));
			ret = NT_STATUS_UNSUCCESSFUL;
		}
		ldap_memfree(dn);
		return ret;
	}

	/* Ok, delete only the SAM attributes */

	for (name = ldap_first_attribute(ldap_state->ldap_struct, entry, &ptr);
	     name != NULL;
	     name = ldap_next_attribute(ldap_state->ldap_struct, entry, ptr)) {

		const char **attrib;

		/* We are only allowed to delete the attributes that
		   really exist. */

		for (attrib = attrs; *attrib != NULL; attrib++) {
			if (StrCaseCmp(*attrib, name) == 0) {
				DEBUG(10, ("deleting attribute %s\n", name));
				smb_ldap_make_a_mod(&mods, LDAP_MOD_DELETE, name, NULL);
			}
		}

		ldap_memfree(name);
	}

	if (ptr != NULL) {
		ber_free(ptr, 0);
	}
	
	smb_ldap_make_a_mod(&mods, LDAP_MOD_DELETE, "objectClass", objectclass);

	rc = smb_ldap_modify(ldap_state, dn, mods);
	ldap_mods_free(mods, 1);

	if (rc != LDAP_SUCCESS) {
		char *ld_error = NULL;
		ldap_get_option(ldap_state->ldap_struct, LDAP_OPT_ERROR_STRING,
				&ld_error);
		
		DEBUG(0, ("could not delete attributes for %s, error: %s (%s)\n",
			  dn, ldap_err2string(rc), ld_error?ld_error:"unknown"));
		SAFE_FREE(ld_error);
		ldap_memfree(dn);
		return NT_STATUS_UNSUCCESSFUL;
	}

	ldap_memfree(dn);
	return NT_STATUS_OK;
}
					  
/* New Interface is being implemented here */

/**********************************************************************
Initialize SAM_ACCOUNT from an LDAP query (unix attributes only)
*********************************************************************/
static BOOL get_unix_attributes (struct smb_ldap_privates *ldap_state, 
				SAM_ACCOUNT * sampass,
				LDAPMessage * entry)
{
	pstring  homedir;
	pstring  temp;
	uid_t uid;
	gid_t gid;
	char **ldap_values;
	char **values;

	if ((ldap_values = ldap_get_values (ldap_state->ldap_struct, entry, "objectClass")) == NULL) {
		DEBUG (1, ("get_unix_attributes: no objectClass! \n"));
		return False;
	}

	for (values=ldap_values;*values;values++) {
		if (strcasecmp(*values, "posixAccount") == 0) {
			break;
		}
	}
	
	if (!*values) { /*end of array, no posixAccount */
		DEBUG(10, ("user does not have posixAcccount attributes\n"));
		ldap_value_free(ldap_values);
		return False;
	}
	ldap_value_free(ldap_values);

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "homeDirectory", homedir)) 
		return False;
	
	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "uidNumber", temp))
		return False;
	
	uid = (uid_t)atol(temp);
	
	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "gidNumber", temp))
		return False;
	
	gid = (gid_t)atol(temp);

	pdb_set_unix_homedir(sampass, homedir, PDB_SET);
	pdb_set_uid(sampass, uid, PDB_SET);
	pdb_set_gid(sampass, gid, PDB_SET);
	
	DEBUG(10, ("user has posixAcccount attributes\n"));
	return True;
}


/**********************************************************************
Initialize SAM_ACCOUNT from an LDAP query
(Based on init_sam_from_buffer in pdb_tdb.c)
*********************************************************************/
static BOOL init_sam_from_ldap (struct smb_ldap_privates *ldap_state, 
				SAM_ACCOUNT * sampass,
				LDAPMessage * entry)
{
	time_t  logon_time,
			logoff_time,
			kickoff_time,
			pass_last_set_time, 
			pass_can_change_time, 
			pass_must_change_time;
	pstring 	username, 
			domain,
			nt_username,
			fullname,
			homedir,
			dir_drive,
			logon_script,
			profile_path,
			acct_desc,
			munged_dial,
			workstations;
	struct passwd	*pw;
	uint32 		user_rid, 
			group_rid;
	uint8 		smblmpwd[LM_HASH_LEN],
			smbntpwd[NT_HASH_LEN];
	uint16 		acct_ctrl = 0, 
			logon_divs;
	uint32 hours_len;
	uint8 		hours[MAX_HOURS_LEN];
	pstring temp;
	uid_t		uid = -1;
	gid_t 		gid = getegid();


	/*
	 * do a little initialization
	 */
	username[0] 	= '\0';
	domain[0] 	= '\0';
	nt_username[0] 	= '\0';
	fullname[0] 	= '\0';
	homedir[0] 	= '\0';
	dir_drive[0] 	= '\0';
	logon_script[0] = '\0';
	profile_path[0] = '\0';
	acct_desc[0] 	= '\0';
	munged_dial[0] 	= '\0';
	workstations[0] = '\0';
	 

	if (sampass == NULL || ldap_state == NULL || entry == NULL) {
		DEBUG(0, ("init_sam_from_ldap: NULL parameters found!\n"));
		return False;
	}

	if (ldap_state->ldap_struct == NULL) {
		DEBUG(0, ("init_sam_from_ldap: ldap_state->ldap_struct is NULL!\n"));
		return False;
	}
	
	smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "uid", username);
	DEBUG(2, ("Entry found for user: %s\n", username));

	pstrcpy(nt_username, username);

	pstrcpy(domain, lp_workgroup());
	
	pdb_set_username(sampass, username, PDB_SET);

	pdb_set_domain(sampass, domain, PDB_DEFAULT);
	pdb_set_nt_username(sampass, nt_username, PDB_SET);

	smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "rid", temp);
	user_rid = (uint32)atol(temp);

	pdb_set_user_sid_from_rid(sampass, user_rid, PDB_SET);

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "primaryGroupID", temp)) {
		group_rid = 0;
	} else {
		group_rid = (uint32)atol(temp);
		pdb_set_group_sid_from_rid(sampass, group_rid, PDB_SET);
	}


	/* 
	 * If so configured, try and get the values from LDAP 
	 */

	if (!lp_ldap_trust_ids() || (!get_unix_attributes(ldap_state, sampass, entry))) {
		
		/* 
		 * Otherwise just ask the system getpw() calls.
		 */
	
		pw = getpwnam_alloc(username);
		if (pw == NULL) {
			if (! ldap_state->permit_non_unix_accounts) {
				DEBUG (2,("init_sam_from_ldap: User [%s] does not exist via system getpwnam!\n", username));
				return False;
			}
		} else {
			uid = pw->pw_uid;
			pdb_set_uid(sampass, uid, PDB_SET);
			gid = pw->pw_gid;
			pdb_set_gid(sampass, gid, PDB_SET);
			
			pdb_set_unix_homedir(sampass, pw->pw_dir, PDB_SET);

			passwd_free(&pw);
		}
	}

	if (group_rid == 0 && pdb_get_init_flags(sampass,PDB_GID) != PDB_DEFAULT) {
		GROUP_MAP map;
		gid = pdb_get_gid(sampass);
		/* call the mapping code here */
		if(pdb_getgrgid(&map, gid, MAPPING_WITHOUT_PRIV)) {
			pdb_set_group_sid(sampass, &map.sid, PDB_SET);
		} 
		else {
			pdb_set_group_sid_from_rid(sampass, pdb_gid_to_group_rid(gid), PDB_SET);
		}
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "pwdLastSet", temp)) {
		/* leave as default */
	} else {
		pass_last_set_time = (time_t) atol(temp);
		pdb_set_pass_last_set_time(sampass, pass_last_set_time, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "logonTime", temp)) {
		/* leave as default */
	} else {
		logon_time = (time_t) atol(temp);
		pdb_set_logon_time(sampass, logon_time, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "logoffTime", temp)) {
		/* leave as default */
	} else {
		logoff_time = (time_t) atol(temp);
		pdb_set_logoff_time(sampass, logoff_time, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "kickoffTime", temp)) {
		/* leave as default */
	} else {
		kickoff_time = (time_t) atol(temp);
		pdb_set_kickoff_time(sampass, kickoff_time, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "pwdCanChange", temp)) {
		/* leave as default */
	} else {
		pass_can_change_time = (time_t) atol(temp);
		pdb_set_pass_can_change_time(sampass, pass_can_change_time, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "pwdMustChange", temp)) {
		/* leave as default */
	} else {
		pass_must_change_time = (time_t) atol(temp);
		pdb_set_pass_must_change_time(sampass, pass_must_change_time, PDB_SET);
	}

	/* recommend that 'gecos' and 'displayName' should refer to the same
	 * attribute OID.  userFullName depreciated, only used by Samba
	 * primary rules of LDAP: don't make a new attribute when one is already defined
	 * that fits your needs; using cn then displayName rather than 'userFullName'
	 */

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry,
				  "displayName", fullname)) {
		if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry,
					  "cn", fullname)) {
			/* leave as default */
		} else {
			pdb_set_fullname(sampass, fullname, PDB_SET);
		}
	} else {
		pdb_set_fullname(sampass, fullname, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "homeDrive", dir_drive)) {
		pdb_set_dir_drive(sampass, talloc_sub_specified(sampass->mem_ctx, 
								  lp_logon_drive(),
								  username, domain, 
								  uid, gid),
				  PDB_DEFAULT);
	} else {
		pdb_set_dir_drive(sampass, dir_drive, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "smbHome", homedir)) {
		pdb_set_homedir(sampass, talloc_sub_specified(sampass->mem_ctx, 
								  lp_logon_home(),
								  username, domain, 
								  uid, gid), 
				  PDB_DEFAULT);
	} else {
		pdb_set_homedir(sampass, homedir, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "scriptPath", logon_script)) {
		pdb_set_logon_script(sampass, talloc_sub_specified(sampass->mem_ctx, 
								     lp_logon_script(),
								     username, domain, 
								     uid, gid), 
				     PDB_DEFAULT);
	} else {
		pdb_set_logon_script(sampass, logon_script, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "profilePath", profile_path)) {
		pdb_set_profile_path(sampass, talloc_sub_specified(sampass->mem_ctx, 
								     lp_logon_path(),
								     username, domain, 
								     uid, gid), 
				     PDB_DEFAULT);
	} else {
		pdb_set_profile_path(sampass, profile_path, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "description", acct_desc)) {
		/* leave as default */
	} else {
		pdb_set_acct_desc(sampass, acct_desc, PDB_SET);
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "userWorkstations", workstations)) {
		/* leave as default */;
	} else {
		pdb_set_workstations(sampass, workstations, PDB_SET);
	}

	/* FIXME: hours stuff should be cleaner */
	
	logon_divs = 168;
	hours_len = 21;
	memset(hours, 0xff, hours_len);

	if (!smb_ldap_get_single_attribute (ldap_state->ldap_struct, entry, "lmPassword", temp)) {
		/* leave as default */
	} else {
		pdb_gethexpwd(temp, smblmpwd);
		memset((char *)temp, '\0', strlen(temp)+1);
		if (!pdb_set_lanman_passwd(sampass, smblmpwd, PDB_SET))
			return False;
		ZERO_STRUCT(smblmpwd);
	}

	if (!smb_ldap_get_single_attribute (ldap_state->ldap_struct, entry, "ntPassword", temp)) {
		/* leave as default */
	} else {
		pdb_gethexpwd(temp, smbntpwd);
		memset((char *)temp, '\0', strlen(temp)+1);
		if (!pdb_set_nt_passwd(sampass, smbntpwd, PDB_SET))
			return False;
		ZERO_STRUCT(smbntpwd);
	}

	if (!smb_ldap_get_single_attribute (ldap_state->ldap_struct, entry, "acctFlags", temp)) {
		acct_ctrl |= ACB_NORMAL;
	} else {
		acct_ctrl = pdb_decode_acct_ctrl(temp);

		if (acct_ctrl == 0)
			acct_ctrl |= ACB_NORMAL;

		pdb_set_acct_ctrl(sampass, acct_ctrl, PDB_SET);
	}

	pdb_set_hours_len(sampass, hours_len, PDB_SET);
	pdb_set_logon_divs(sampass, logon_divs, PDB_SET);

	pdb_set_munged_dial(sampass, munged_dial, PDB_SET);
	
	/* pdb_set_unknown_3(sampass, unknown3, PDB_SET); */
	/* pdb_set_unknown_5(sampass, unknown5, PDB_SET); */
	/* pdb_set_unknown_6(sampass, unknown6, PDB_SET); */

	pdb_set_hours(sampass, hours, PDB_SET);

	return True;
}

/**********************************************************************
  An LDAP modification is needed in two cases:
  * If we are updating the record AND the attribute is CHANGED.
  * If we are adding   the record AND it is SET or CHANGED (ie not default)
*********************************************************************/
static BOOL need_ldap_mod(BOOL pdb_add, const SAM_ACCOUNT * sampass, enum pdb_elements element) {
	if (pdb_add) {
		return (!IS_SAM_DEFAULT(sampass, element));
	} else {
		return IS_SAM_CHANGED(sampass, element);
	}
}

/**********************************************************************
  Set attribute to newval in LDAP, regardless of what value the
  attribute had in LDAP before.
*********************************************************************/
static void make_ldap_mod(LDAP *ldap_struct, LDAPMessage *existing,
			  LDAPMod ***mods,
			  const char *attribute, const char *newval)
{
	char **values = NULL;

	if (existing != NULL) {
		values = ldap_get_values(ldap_struct, existing, attribute);
	}

	if ((values != NULL) && (values[0] != NULL) &&
	    strcmp(values[0], newval) == 0) {
		
		/* Believe it or not, but LDAP will deny a delete and
		   an add at the same time if the values are the
		   same... */

		ldap_value_free(values);
		return;
	}

	/* Regardless of the real operation (add or modify)
	   we add the new value here. We rely on deleting
	   the old value, should it exist. */

	if ((newval != NULL) && (strlen(newval) > 0)) {
		smb_ldap_make_a_mod(mods, LDAP_MOD_ADD, attribute, newval);
	}

	if (values == NULL) {
		/* There has been no value before, so don't delete it.
		   Here's a possible race: We might end up with
		   duplicate attributes */
		return;
	}

	/* By deleting exactly the value we found in the entry this
	   should be race-free in the sense that the LDAP-Server will
	   deny the complete operation if somebody changed the
	   attribute behind our back. */

	smb_ldap_make_a_mod(mods, LDAP_MOD_DELETE, attribute, values[0]);
	ldap_value_free(values);
}

/**********************************************************************
Initialize SAM_ACCOUNT from an LDAP query
(Based on init_buffer_from_sam in pdb_tdb.c)
*********************************************************************/
static BOOL init_ldap_from_sam (struct smb_ldap_privates *ldap_state, 
				LDAPMessage *existing,
				LDAPMod *** mods, const SAM_ACCOUNT * sampass,
				BOOL (*need_update)(const SAM_ACCOUNT *,
						    enum pdb_elements))
{
	pstring temp;
	uint32 rid;

	if (mods == NULL || sampass == NULL) {
		DEBUG(0, ("init_ldap_from_sam: NULL parameters found!\n"));
		return False;
	}

	*mods = NULL;

	/* 
	 * took out adding "objectclass: sambaAccount"
	 * do this on a per-mod basis
	 */
	if (need_update(sampass, PDB_USERNAME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods, 
			      "uid", pdb_get_username(sampass));

	DEBUG(2, ("Setting entry for user: %s\n", pdb_get_username(sampass)));

	rid = pdb_get_user_rid(sampass);

	if (rid == 0) {
		if (!IS_SAM_DEFAULT(sampass, PDB_UID)) {
			rid = fallback_pdb_uid_to_user_rid(pdb_get_uid(sampass));
		} else if (ldap_state->permit_non_unix_accounts) {
			rid = ldapsam_get_next_available_nua_rid(ldap_state);
			if (rid == 0) {
				DEBUG(0, ("NO user RID specified on account %s, and "
					  "finding next available NUA RID failed, "
					  "cannot store!\n",
					  pdb_get_username(sampass)));
				ldap_mods_free(*mods, 1);
				return False;
			}
		} else {
			DEBUG(0, ("NO user RID specified on account %s, "
				  "cannot store!\n", pdb_get_username(sampass)));
			ldap_mods_free(*mods, 1);
			return False;
		}
	}

	slprintf(temp, sizeof(temp) - 1, "%i", rid);

	if (need_update(sampass, PDB_USERSID))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "rid", temp);


	rid = pdb_get_group_rid(sampass);

	if (rid == 0) {
		if (!IS_SAM_DEFAULT(sampass, PDB_GID)) {
			rid = pdb_gid_to_group_rid(pdb_get_gid(sampass));
		} else if (ldap_state->permit_non_unix_accounts) {
			rid = DOMAIN_GROUP_RID_USERS;
		} else {
			DEBUG(0, ("NO group RID specified on account %s, "
				  "cannot store!\n", pdb_get_username(sampass)));
			ldap_mods_free(*mods, 1);
			return False;
		}
	}

	slprintf(temp, sizeof(temp) - 1, "%i", rid);

	if (need_update(sampass, PDB_GROUPSID))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "primaryGroupID", temp);

	/* displayName, cn, and gecos should all be the same
	 *  most easily accomplished by giving them the same OID
	 *  gecos isn't set here b/c it should be handled by the 
	 *  add-user script
	 *  We change displayName only and fall back to cn if
	 *  it does not exist.
	 */

	if (need_update(sampass, PDB_FULLNAME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "displayName", pdb_get_fullname(sampass));

	if (need_update(sampass, PDB_ACCTDESC))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "description", pdb_get_acct_desc(sampass));

	if (need_update(sampass, PDB_WORKSTATIONS))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "userWorkstations", pdb_get_workstations(sampass));

	if (need_update(sampass, PDB_SMBHOME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "smbHome", pdb_get_homedir(sampass));
			
	if (need_update(sampass, PDB_DRIVE))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "homeDrive", pdb_get_dir_drive(sampass));

	if (need_update(sampass, PDB_LOGONSCRIPT))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "scriptPath", pdb_get_logon_script(sampass));

	if (need_update(sampass, PDB_PROFILE))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "profilePath", pdb_get_profile_path(sampass));

	slprintf(temp, sizeof(temp) - 1, "%li", pdb_get_logon_time(sampass));

	if (need_update(sampass, PDB_LOGONTIME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "logonTime", temp);

	slprintf(temp, sizeof(temp) - 1, "%li", pdb_get_logoff_time(sampass));

	if (need_update(sampass, PDB_LOGOFFTIME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "logoffTime", temp);

	slprintf (temp, sizeof (temp) - 1, "%li",
		  pdb_get_kickoff_time(sampass));

	if (need_update(sampass, PDB_KICKOFFTIME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "kickoffTime", temp);

	slprintf (temp, sizeof (temp) - 1, "%li",
		  pdb_get_pass_can_change_time(sampass));

	if (need_update(sampass, PDB_CANCHANGETIME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "pwdCanChange", temp);

	slprintf (temp, sizeof (temp) - 1, "%li",
		  pdb_get_pass_must_change_time(sampass));

	if (need_update(sampass, PDB_MUSTCHANGETIME))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "pwdMustChange", temp);

	if ((pdb_get_acct_ctrl(sampass)&(ACB_WSTRUST|ACB_SVRTRUST|ACB_DOMTRUST))||
	    (lp_ldap_passwd_sync()!=LDAP_PASSWD_SYNC_ONLY)) {

		pdb_sethexpwd (temp, pdb_get_lanman_passwd(sampass),
			       pdb_get_acct_ctrl(sampass));

		if (need_update(sampass, PDB_LMPASSWD))
			make_ldap_mod(ldap_state->ldap_struct, existing, mods,
				      "lmPassword", temp);

		pdb_sethexpwd (temp, pdb_get_nt_passwd(sampass),
			       pdb_get_acct_ctrl(sampass));

		if (need_update(sampass, PDB_NTPASSWD))
			make_ldap_mod(ldap_state->ldap_struct, existing, mods,
				      "ntPassword", temp);

		slprintf (temp, sizeof (temp) - 1, "%li",
			  pdb_get_pass_last_set_time(sampass));

		if (need_update(sampass, PDB_PASSLASTSET))
			make_ldap_mod(ldap_state->ldap_struct, existing, mods,
				      "pwdLastSet", temp);
	}

	/* FIXME: Hours stuff goes in LDAP  */

	if (need_update(sampass, PDB_ACCTCTRL))
		make_ldap_mod(ldap_state->ldap_struct, existing, mods,
			      "acctFlags",
			      pdb_encode_acct_ctrl (pdb_get_acct_ctrl(sampass),
						    NEW_PW_FORMAT_SPACE_PADDED_LEN));

	return True;
}


/**********************************************************************
Connect to LDAP server and find the next available RID.
*********************************************************************/
static uint32 check_nua_rid_is_avail(struct smb_ldap_privates *ldap_state, uint32 top_rid) 
{
	LDAPMessage *result;
	uint32 final_rid = (top_rid & (~USER_RID_TYPE)) + RID_MULTIPLIER;
	if (top_rid == 0) {
		return 0;
	}
	
	if (final_rid < ldap_state->low_nua_rid || final_rid > ldap_state->high_nua_rid) {
		return 0;
	}

	if (ldapsam_search_one_user_by_rid(ldap_state, final_rid, &result) != LDAP_SUCCESS) {
		DEBUG(0, ("Cannot allocate NUA RID %d (0x%x), as the confirmation search failed!\n", final_rid, final_rid));
		return 0;
	}

	if (ldap_count_entries(ldap_state->ldap_struct, result) != 0) {
		DEBUG(0, ("Cannot allocate NUA RID %d (0x%x), as the RID is already in use!!\n", final_rid, final_rid));
		ldap_msgfree(result);
		return 0;
	}

	DEBUG(5, ("NUA RID %d (0x%x), declared valid\n", final_rid, final_rid));
	ldap_msgfree(result);
	return final_rid;
}

/**********************************************************************
Extract the RID from an LDAP entry
*********************************************************************/
static uint32 entry_to_user_rid(struct smb_ldap_privates *ldap_state, LDAPMessage *entry) {
	uint32 rid;
	SAM_ACCOUNT *user = NULL;
	if (!NT_STATUS_IS_OK(pdb_init_sam(&user))) {
		return 0;
	}

	if (init_sam_from_ldap(ldap_state, user, entry)) {
		rid = pdb_get_user_rid(user);
	} else {
		rid =0;
	}
     	pdb_free_sam(&user);
	if (rid >= ldap_state->low_nua_rid && rid <= ldap_state->high_nua_rid) {
		return rid;
	}
	return 0;
}


/**********************************************************************
Connect to LDAP server and find the next available RID.
*********************************************************************/
static uint32 search_top_nua_rid(struct smb_ldap_privates *ldap_state)
{
	int rc;
	pstring filter;
	LDAPMessage *result;
	LDAPMessage *entry;
	char *final_filter = NULL;
	uint32 top_rid = 0;
	uint32 count;
	uint32 rid;

	pstrcpy(filter, lp_ldap_filter());
	all_string_sub(filter, "%u", "*", sizeof(pstring));

#if 0
	asprintf(&final_filter, "(&(%s)(&(rid>=%d)(rid<=%d)))", filter, ldap_state->low_nua_rid, ldap_state->high_nua_rid);
#else 
	final_filter = strdup(filter);
#endif	
	DEBUG(2, ("ldapsam_get_next_available_nua_rid: searching for:[%s]\n", final_filter));

	rc = smb_ldap_search(ldap_state, lp_ldap_suffix(),
			   LDAP_SCOPE_SUBTREE, final_filter, attr, 0,
			   &result);

	if (rc != LDAP_SUCCESS) {
		DEBUG(3, ("LDAP search failed! cannot find base for NUA RIDs: %s\n", ldap_err2string(rc)));
		DEBUGADD(3, ("Query was: %s, %s\n", lp_ldap_suffix(), final_filter));

		free(final_filter);
		result = NULL;
		return 0;
	}
	
	count = ldap_count_entries(ldap_state->ldap_struct, result);
	DEBUG(2, ("search_top_nua_rid: %d entries in the base!\n", count));
	
	if (count == 0) {
		DEBUG(3, ("LDAP search returned no records, assuming no non-unix-accounts present!: %s\n", ldap_err2string(rc)));
		DEBUGADD(3, ("Query was: %s, %s\n", lp_ldap_suffix(), final_filter));
		free(final_filter);
		ldap_msgfree(result);
		result = NULL;
		return ldap_state->low_nua_rid;
	}
	
	free(final_filter);
	entry = ldap_first_entry(ldap_state->ldap_struct,result);

	top_rid = entry_to_user_rid(ldap_state, entry);

	while ((entry = ldap_next_entry(ldap_state->ldap_struct, entry))) {

		rid = entry_to_user_rid(ldap_state, entry);
		if (rid > top_rid) {
			top_rid = rid;
		}
	}

	ldap_msgfree(result);

	if (top_rid < ldap_state->low_nua_rid) 
		top_rid = ldap_state->low_nua_rid;

	return top_rid;
}

/**********************************************************************
Connect to LDAP server and find the next available RID.
*********************************************************************/
static uint32 ldapsam_get_next_available_nua_rid(struct smb_ldap_privates *ldap_state) {
	uint32 next_nua_rid;
	uint32 top_nua_rid;

	top_nua_rid = search_top_nua_rid(ldap_state);

	next_nua_rid = check_nua_rid_is_avail(ldap_state, 
					      top_nua_rid);
	
	return next_nua_rid;
}

/**********************************************************************
Connect to LDAP server for password enumeration
*********************************************************************/
static NTSTATUS ldapsam_setsampwent(struct pdb_methods *my_methods, BOOL update)
{
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	int rc;
	pstring filter;

	pstrcpy(filter, lp_ldap_filter());
	all_string_sub(filter, "%u", "*", sizeof(pstring));

	rc = smb_ldap_search(ldap_state, lp_ldap_suffix(),
			   LDAP_SCOPE_SUBTREE, filter, attr, 0,
			   &ldap_state->result);

	if (rc != LDAP_SUCCESS) {
		DEBUG(0, ("LDAP search failed: %s\n", ldap_err2string(rc)));
		DEBUG(3, ("Query was: %s, %s\n", lp_ldap_suffix(), filter));
		ldap_msgfree(ldap_state->result);
		ldap_state->result = NULL;
		return NT_STATUS_UNSUCCESSFUL;
	}

	DEBUG(2, ("ldapsam_setsampwent: %d entries in the base!\n",
		ldap_count_entries(ldap_state->ldap_struct,
		ldap_state->result)));

	ldap_state->entry = ldap_first_entry(ldap_state->ldap_struct,
				 ldap_state->result);
	ldap_state->index = 0;

	return NT_STATUS_OK;
}

/**********************************************************************
End enumeration of the LDAP password list 
*********************************************************************/
static void ldapsam_endsampwent(struct pdb_methods *my_methods)
{
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	if (ldap_state->result) {
		ldap_msgfree(ldap_state->result);
		ldap_state->result = NULL;
	}
}

/**********************************************************************
Get the next entry in the LDAP password database 
*********************************************************************/
static NTSTATUS ldapsam_getsampwent(struct pdb_methods *my_methods, SAM_ACCOUNT *user)
{
	NTSTATUS ret = NT_STATUS_UNSUCCESSFUL;
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	BOOL bret = False;

	while (!bret) {
		if (!ldap_state->entry)
			return ret;
		
		ldap_state->index++;
		bret = init_sam_from_ldap(ldap_state, user, ldap_state->entry);
		
		ldap_state->entry = ldap_next_entry(ldap_state->ldap_struct,
					    ldap_state->entry);	
	}

	return NT_STATUS_OK;
}

/**********************************************************************
Get SAM_ACCOUNT entry from LDAP by username 
*********************************************************************/
static NTSTATUS ldapsam_getsampwnam(struct pdb_methods *my_methods, SAM_ACCOUNT *user, const char *sname)
{
	NTSTATUS ret = NT_STATUS_UNSUCCESSFUL;
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	LDAPMessage *result;
	LDAPMessage *entry;
	int count;
	
	if (ldapsam_search_one_user_by_name(ldap_state, sname, &result) != LDAP_SUCCESS) {
		return NT_STATUS_NO_SUCH_USER;
	}
	
	count = ldap_count_entries(ldap_state->ldap_struct, result);
	
	if (count < 1) {
		DEBUG(4,
		      ("We don't find this user [%s] count=%d\n", sname,
		       count));
		return NT_STATUS_NO_SUCH_USER;
	} else if (count > 1) {
		DEBUG(1,
		      ("Duplicate entries for this user [%s] Failing. count=%d\n", sname,
		       count));
		return NT_STATUS_NO_SUCH_USER;
	}

	entry = ldap_first_entry(ldap_state->ldap_struct, result);
	if (entry) {
		if (!init_sam_from_ldap(ldap_state, user, entry)) {
			DEBUG(1,("ldapsam_getsampwnam: init_sam_from_ldap failed for user '%s'!\n", sname));
			ldap_msgfree(result);
			return NT_STATUS_NO_SUCH_USER;
		}
		ldap_msgfree(result);
		ret = NT_STATUS_OK;
	} else {
		ldap_msgfree(result);
	}
	return ret;
}

/**********************************************************************
Get SAM_ACCOUNT entry from LDAP by rid 
*********************************************************************/
static NTSTATUS ldapsam_getsampwrid(struct pdb_methods *my_methods, SAM_ACCOUNT *user, uint32 rid)
{
	NTSTATUS ret = NT_STATUS_UNSUCCESSFUL;
	struct smb_ldap_privates *ldap_state = 
		(struct smb_ldap_privates *)my_methods->private_data;
	LDAPMessage *result;
	LDAPMessage *entry;
	int count;

	if (ldapsam_search_one_user_by_rid(ldap_state, rid, &result) != LDAP_SUCCESS) {
		return NT_STATUS_NO_SUCH_USER;
	}

	count = ldap_count_entries(ldap_state->ldap_struct, result);
		
	if (count < 1) {
		DEBUG(4,
		      ("We don't find this rid [%i] count=%d\n", rid,
		       count));
		return NT_STATUS_NO_SUCH_USER;
	} else if (count > 1) {
		DEBUG(1,
		      ("More than one user with rid [%i]. Failing. count=%d\n", rid,
		       count));
		return NT_STATUS_NO_SUCH_USER;
	}

	entry = ldap_first_entry(ldap_state->ldap_struct, result);
	if (entry) {
		if (!init_sam_from_ldap(ldap_state, user, entry)) {
			DEBUG(1,("ldapsam_getsampwrid: init_sam_from_ldap failed!\n"));
			ldap_msgfree(result);
			return NT_STATUS_NO_SUCH_USER;
		}
		ldap_msgfree(result);
		ret = NT_STATUS_OK;
	} else {
		ldap_msgfree(result);
	}
	return ret;
}

static NTSTATUS ldapsam_getsampwsid(struct pdb_methods *my_methods, SAM_ACCOUNT * user, const DOM_SID *sid)
{
	uint32 rid;
	if (!sid_peek_check_rid(get_global_sam_sid(), sid, &rid))
		return NT_STATUS_NO_SUCH_USER;
	return ldapsam_getsampwrid(my_methods, user, rid);
}	

/********************************************************************
Do the actual modification - also change a plaittext passord if 
it it set.
**********************************************************************/

static NTSTATUS ldapsam_modify_entry(struct pdb_methods *my_methods, 
				     SAM_ACCOUNT *newpwd, char *dn,
				     LDAPMod **mods, int ldap_op, BOOL pdb_add)
{
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	int rc;
	
	if (!my_methods || !newpwd || !dn) {
		return NT_STATUS_INVALID_PARAMETER;
	}
	
	if (!mods) {
		DEBUG(5,("mods is empty: nothing to modify\n"));
		/* may be password change below however */
	} else {
		switch(ldap_op)
		{
			case LDAP_MOD_ADD: 
				smb_ldap_make_a_mod(&mods, LDAP_MOD_ADD, "objectclass", "account");
				rc = smb_ldap_add(ldap_state, dn, mods);
				break;
			case LDAP_MOD_REPLACE: 
				rc = smb_ldap_modify(ldap_state, dn ,mods);
				break;
			default: 	
				DEBUG(0,("Wrong LDAP operation type: %d!\n", ldap_op));
				return NT_STATUS_UNSUCCESSFUL;
		}
		
		if (rc!=LDAP_SUCCESS) {
			char *ld_error = NULL;
			ldap_get_option(ldap_state->ldap_struct, LDAP_OPT_ERROR_STRING,
					&ld_error);
			DEBUG(1,
			      ("failed to %s user dn= %s with: %s\n\t%s\n",
			       ldap_op == LDAP_MOD_ADD ? "add" : "modify",
			       dn, ldap_err2string(rc),
			       ld_error?ld_error:"unknown"));
			SAFE_FREE(ld_error);
			return NT_STATUS_UNSUCCESSFUL;
		}  
	}
	
#ifdef LDAP_EXOP_X_MODIFY_PASSWD
	if (!(pdb_get_acct_ctrl(newpwd)&(ACB_WSTRUST|ACB_SVRTRUST|ACB_DOMTRUST))&&
		(lp_ldap_passwd_sync()!=LDAP_PASSWD_SYNC_OFF)&&
		need_ldap_mod(pdb_add, newpwd, PDB_PLAINTEXT_PW)&&
		(pdb_get_plaintext_passwd(newpwd)!=NULL)) {
		BerElement *ber;
		struct berval *bv;
		char *retoid;
		struct berval *retdata;

		if ((ber = ber_alloc_t(LBER_USE_DER))==NULL) {
			DEBUG(0,("ber_alloc_t returns NULL\n"));
			return NT_STATUS_UNSUCCESSFUL;
		}
		ber_printf (ber, "{");
		ber_printf (ber, "ts", LDAP_TAG_EXOP_X_MODIFY_PASSWD_ID,dn);
	        ber_printf (ber, "ts", LDAP_TAG_EXOP_X_MODIFY_PASSWD_NEW, pdb_get_plaintext_passwd(newpwd));
	        ber_printf (ber, "N}");

	        if ((rc = ber_flatten (ber, &bv))<0) {
			DEBUG(0,("ber_flatten returns a value <0\n"));
			return NT_STATUS_UNSUCCESSFUL;
		}
		
		ber_free(ber,1);

		if ((rc = smb_ldap_extended_operation(ldap_state, LDAP_EXOP_X_MODIFY_PASSWD,
						    bv, NULL, NULL, &retoid, &retdata))!=LDAP_SUCCESS) {
			DEBUG(0,("LDAP Password could not be changed for user %s: %s\n",
				pdb_get_username(newpwd),ldap_err2string(rc)));
		} else {
			DEBUG(3,("LDAP Password changed for user %s\n",pdb_get_username(newpwd)));
    
			ber_bvfree(retdata);
			ber_memfree(retoid);
		}
		ber_bvfree(bv);
	}
#else
	DEBUG(10,("LDAP PASSWORD SYNC is not supported!\n"));
#endif /* LDAP_EXOP_X_MODIFY_PASSWD */
	return NT_STATUS_OK;
}

/**********************************************************************
Delete entry from LDAP for username 
*********************************************************************/
static NTSTATUS ldapsam_delete_sam_account(struct pdb_methods *my_methods, SAM_ACCOUNT * sam_acct)
{
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	const char *sname;
	int rc;
	LDAPMessage *result;
	NTSTATUS ret;
	const char *sam_user_attrs[] =
	{ "lmPassword", "ntPassword", "pwdLastSet", "logonTime", "logoffTime",
	  "kickoffTime", "pwdCanChange", "pwdMustChange", "acctFlags",
	  "displayName", "smbHome", "homeDrive", "scriptPath", "profilePath",
	  "userWorkstations", "primaryGroupID", "domain", "rid", NULL };

	if (!sam_acct) {
		DEBUG(0, ("sam_acct was NULL!\n"));
		return NT_STATUS_INVALID_PARAMETER;
	}

	sname = pdb_get_username(sam_acct);

	DEBUG (3, ("Deleting user %s from LDAP.\n", sname));

	rc = ldapsam_search_one_user_by_name(ldap_state, sname, &result);
	if (rc != LDAP_SUCCESS) {
		return NT_STATUS_NO_SUCH_USER;
	}

	ret = ldapsam_delete_entry(ldap_state, result, "sambaAccount",
				   sam_user_attrs);
	ldap_msgfree(result);
	return ret;
}

/**********************************************************************
  Helper function to determine for update_sam_account whether
  we need LDAP modification.
*********************************************************************/
static BOOL element_is_changed(const SAM_ACCOUNT *sampass,
			       enum pdb_elements element)
{
	return IS_SAM_CHANGED(sampass, element);
}

/**********************************************************************
Update SAM_ACCOUNT 
*********************************************************************/
static NTSTATUS ldapsam_update_sam_account(struct pdb_methods *my_methods, SAM_ACCOUNT * newpwd)
{
	NTSTATUS ret = NT_STATUS_UNSUCCESSFUL;
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	int rc;
	char *dn;
	LDAPMessage *result;
	LDAPMessage *entry;
	LDAPMod **mods;

	rc = ldapsam_search_one_user_by_name(ldap_state, pdb_get_username(newpwd), &result);
	if (rc != LDAP_SUCCESS) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	if (ldap_count_entries(ldap_state->ldap_struct, result) == 0) {
		DEBUG(0, ("No user to modify!\n"));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}

	entry = ldap_first_entry(ldap_state->ldap_struct, result);
	dn = ldap_get_dn(ldap_state->ldap_struct, entry);

	if (!init_ldap_from_sam(ldap_state, entry, &mods, newpwd,
				element_is_changed)) {
		DEBUG(0, ("ldapsam_update_sam_account: init_ldap_from_sam failed!\n"));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}
	
        ldap_msgfree(result);
	
	if (mods == NULL) {
		DEBUG(4,("mods is empty: nothing to update for user: %s\n",
			 pdb_get_username(newpwd)));
		ldap_mods_free(mods, 1);
		return NT_STATUS_OK;
	}
	
	ret = ldapsam_modify_entry(my_methods,newpwd,dn,mods,LDAP_MOD_REPLACE, False);
	ldap_mods_free(mods,1);

	if (!NT_STATUS_IS_OK(ret)) {
		char *ld_error = NULL;
		ldap_get_option(ldap_state->ldap_struct, LDAP_OPT_ERROR_STRING,
				&ld_error);
		DEBUG(0,("failed to modify user with uid = %s, error: %s (%s)\n",
			 pdb_get_username(newpwd), ld_error?ld_error:"(unknwon)", ldap_err2string(rc)));
		SAFE_FREE(ld_error);
		return ret;
	}

	DEBUG(2, ("successfully modified uid = %s in the LDAP database\n",
		  pdb_get_username(newpwd)));
	return NT_STATUS_OK;
}

/**********************************************************************
  Helper function to determine for update_sam_account whether
  we need LDAP modification.
*********************************************************************/
static BOOL element_is_set_or_changed(const SAM_ACCOUNT *sampass,
				      enum pdb_elements element)
{
	return (IS_SAM_SET(sampass, element) ||
		IS_SAM_CHANGED(sampass, element));
}

/**********************************************************************
Add SAM_ACCOUNT to LDAP 
*********************************************************************/
static NTSTATUS ldapsam_add_sam_account(struct pdb_methods *my_methods, SAM_ACCOUNT * newpwd)
{
	NTSTATUS ret = NT_STATUS_UNSUCCESSFUL;
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	int rc;
	pstring filter;
	LDAPMessage *result = NULL;
	LDAPMessage *entry  = NULL;
	pstring dn;
	LDAPMod **mods = NULL;
	int 		ldap_op;
	uint32		num_result;
	
	const char *username = pdb_get_username(newpwd);
	if (!username || !*username) {
		DEBUG(0, ("Cannot add user without a username!\n"));
		return NT_STATUS_INVALID_PARAMETER;
	}

	rc = ldapsam_search_one_user_by_name (ldap_state, username, &result);
	if (rc != LDAP_SUCCESS) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	if (ldap_count_entries(ldap_state->ldap_struct, result) != 0) {
		DEBUG(0,("User '%s' already in the base, with samba properties\n", 
			 username));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}
	ldap_msgfree(result);

	slprintf (filter, sizeof (filter) - 1, "uid=%s", username);
	rc = ldapsam_search_one_user(ldap_state, filter, &result);
	if (rc != LDAP_SUCCESS) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	num_result = ldap_count_entries(ldap_state->ldap_struct, result);
	
	if (num_result > 1) {
		DEBUG (0, ("More than one user with that uid exists: bailing out!\n"));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}
	
	/* Check if we need to update an existing entry */
	if (num_result == 1) {
		char *tmp;
		
		DEBUG(3,("User exists without samba properties: adding them\n"));
		ldap_op = LDAP_MOD_REPLACE;
		entry = ldap_first_entry (ldap_state->ldap_struct, result);
		tmp = ldap_get_dn (ldap_state->ldap_struct, entry);
		slprintf (dn, sizeof (dn) - 1, "%s", tmp);
		ldap_memfree (tmp);
	} else {
		/* Check if we need to add an entry */
		DEBUG(3,("Adding new user\n"));
		ldap_op = LDAP_MOD_ADD;
		if (username[strlen(username)-1] == '$') {
                        slprintf (dn, sizeof (dn) - 1, "uid=%s,%s", username, lp_ldap_machine_suffix ());
                } else {
                        slprintf (dn, sizeof (dn) - 1, "uid=%s,%s", username, lp_ldap_user_suffix ());
                }
	}

	if (!init_ldap_from_sam(ldap_state, entry, &mods, newpwd,
				element_is_set_or_changed)) {
		DEBUG(0, ("ldapsam_add_sam_account: init_ldap_from_sam failed!\n"));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;		
	}
	
	ldap_msgfree(result);

	if (mods == NULL) {
		DEBUG(0,("mods is empty: nothing to add for user: %s\n",pdb_get_username(newpwd)));
		return NT_STATUS_UNSUCCESSFUL;
	}
	
	smb_ldap_make_a_mod(&mods, LDAP_MOD_ADD, "objectclass", "sambaAccount");

	ret = ldapsam_modify_entry(my_methods,newpwd,dn,mods,ldap_op, True);
	if (NT_STATUS_IS_ERR(ret)) {
		DEBUG(0,("failed to modify/add user with uid = %s (dn = %s)\n",
			 pdb_get_username(newpwd),dn));
		ldap_mods_free(mods,1);
		return ret;
	}

	DEBUG(2,("added: uid = %s in the LDAP database\n", pdb_get_username(newpwd)));
	ldap_mods_free(mods, 1);
	return NT_STATUS_OK;
}

static void free_private_data(void **vp) 
{
	struct smb_ldap_privates **ldap_state = (struct smb_ldap_privates **)vp;

	smb_ldap_close(*ldap_state);

	if ((*ldap_state)->bind_secret) {
		memset((*ldap_state)->bind_secret, '\0', strlen((*ldap_state)->bind_secret));
	}

	smb_ldap_close(*ldap_state);
		
	SAFE_FREE((*ldap_state)->bind_dn);
	SAFE_FREE((*ldap_state)->bind_secret);

	*ldap_state = NULL;

	/* No need to free any further, as it is talloc()ed */
}

static const char *group_attr[] = {"cn", "ntSid", "ntGroupType",
				   "gidNumber",
				   "displayName", "description",
				   NULL };
				   
static int ldapsam_search_one_group (struct smb_ldap_privates *ldap_state,
				     const char *filter,
				     LDAPMessage ** result)
{
	int scope = LDAP_SCOPE_SUBTREE;
	int rc;

	DEBUG(2, ("ldapsam_search_one_group: searching for:[%s]\n", filter));

	rc = smb_ldap_search(ldap_state, lp_ldap_suffix (), scope,
			    filter, group_attr, 0, result);

	if (rc != LDAP_SUCCESS) {
		char *ld_error = NULL;
		ldap_get_option(ldap_state->ldap_struct, LDAP_OPT_ERROR_STRING,
				&ld_error);
		DEBUG(0, ("ldapsam_search_one_group: "
			  "Problem during the LDAP search: LDAP error: %s (%s)",
			  ld_error?ld_error:"(unknown)", ldap_err2string(rc)));
		DEBUG(3, ("ldapsam_search_one_group: Query was: %s, %s\n",
			  lp_ldap_suffix(), filter));
		SAFE_FREE(ld_error);
	}

	return rc;
}

static BOOL init_group_from_ldap(struct smb_ldap_privates *ldap_state,
				 GROUP_MAP *map, LDAPMessage *entry)
{
	pstring temp;

	if (ldap_state == NULL || map == NULL || entry == NULL ||
	    ldap_state->ldap_struct == NULL) {
		DEBUG(0, ("init_group_from_ldap: NULL parameters found!\n"));
		return False;
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "gidNumber",
				  temp)) {
		DEBUG(0, ("Mandatory attribute gidNumber not found\n"));
		return False;
	}
	DEBUG(2, ("Entry found for group: %s\n", temp));

	map->gid = (gid_t)atol(temp);

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "ntSid",
				  temp)) {
		DEBUG(0, ("Mandatory attribute ntSid not found\n"));
		return False;
	}
	string_to_sid(&map->sid, temp);

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "ntGroupType",
				  temp)) {
		DEBUG(0, ("Mandatory attribute ntGroupType not found\n"));
		return False;
	}
	map->sid_name_use = (uint32)atol(temp);

	if ((map->sid_name_use < SID_NAME_USER) ||
	    (map->sid_name_use > SID_NAME_UNKNOWN)) {
		DEBUG(0, ("Unknown Group type: %d\n", map->sid_name_use));
		return False;
	}

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "displayName",
				  temp)) {
		DEBUG(3, ("Attribute displayName not found\n"));
		temp[0] = '\0';
		if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "cn",
					  temp)) {
			DEBUG(0, ("Attributes cn not found either "
				  "for gidNumber(%i)\n",map->gid));
			return False;
		}
	}
	fstrcpy(map->nt_name, temp);

	if (!smb_ldap_get_single_attribute(ldap_state->ldap_struct, entry, "description",
				  temp)) {
		DEBUG(3, ("Attribute description not found\n"));
		temp[0] = '\0';
	}
	fstrcpy(map->comment, temp);

	map->systemaccount = 0;
	init_privilege(&map->priv_set);

	return True;
}

static BOOL init_ldap_from_group(LDAP *ldap_struct,
				 LDAPMessage *existing,
				 LDAPMod ***mods,
				 const GROUP_MAP *map)
{
	pstring tmp;

	if (mods == NULL || map == NULL) {
		DEBUG(0, ("init_ldap_from_group: NULL parameters found!\n"));
		return False;
	}

	*mods = NULL;

	sid_to_string(tmp, &map->sid);
	make_ldap_mod(ldap_struct, existing, mods, "ntSid", tmp);
	snprintf(tmp, sizeof(tmp)-1, "%i", map->sid_name_use);
	make_ldap_mod(ldap_struct, existing, mods, "ntGroupType", tmp);

	make_ldap_mod(ldap_struct, existing, mods, "displayName", map->nt_name);
	make_ldap_mod(ldap_struct, existing, mods, "description", map->comment);

	return True;
}

static NTSTATUS ldapsam_getgroup(struct pdb_methods *methods,
				 const char *filter,
				 GROUP_MAP *map)
{
	struct smb_ldap_privates *ldap_state =
		(struct smb_ldap_privates *)methods->private_data;
	LDAPMessage *result;
	LDAPMessage *entry;
	int count;

	if (ldapsam_search_one_group(ldap_state, filter, &result)
	    != LDAP_SUCCESS) {
		return NT_STATUS_NO_SUCH_GROUP;
	}

	count = ldap_count_entries(ldap_state->ldap_struct, result);

	if (count < 1) {
		DEBUG(4, ("Did not find group for filter %s\n", filter));
		return NT_STATUS_NO_SUCH_GROUP;
	}

	if (count > 1) {
		DEBUG(1, ("Duplicate entries for filter %s: count=%d\n",
			  filter, count));
		return NT_STATUS_NO_SUCH_GROUP;
	}

	entry = ldap_first_entry(ldap_state->ldap_struct, result);

	if (!entry) {
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}

	if (!init_group_from_ldap(ldap_state, map, entry)) {
		DEBUG(1, ("init_group_from_ldap failed for group filter %s\n",
			  filter));
		ldap_msgfree(result);
		return NT_STATUS_NO_SUCH_GROUP;
	}

	ldap_msgfree(result);
	return NT_STATUS_OK;
}

static NTSTATUS ldapsam_getgrsid(struct pdb_methods *methods, GROUP_MAP *map,
				 DOM_SID sid, BOOL with_priv)
{
	pstring filter;

	snprintf(filter, sizeof(filter)-1,
		 "(&(objectClass=sambaGroupMapping)(ntSid=%s))",
		 sid_string_static(&sid));

	return ldapsam_getgroup(methods, filter, map);
}

static NTSTATUS ldapsam_getgrgid(struct pdb_methods *methods, GROUP_MAP *map,
				 gid_t gid, BOOL with_priv)
{
	pstring filter;

	snprintf(filter, sizeof(filter)-1,
		 "(&(objectClass=sambaGroupMapping)(gidNumber=%d))",
		 gid);

	return ldapsam_getgroup(methods, filter, map);
}

static NTSTATUS ldapsam_getgrnam(struct pdb_methods *methods, GROUP_MAP *map,
				 char *name, BOOL with_priv)
{
	pstring filter;

	/* TODO: Escaping of name? */

	snprintf(filter, sizeof(filter)-1,
		 "(&(objectClass=sambaGroupMapping)(|(displayName=%s)(cn=%s)))",
		 name, name);

	return ldapsam_getgroup(methods, filter, map);
}

static int ldapsam_search_one_group_by_gid(struct smb_ldap_privates *ldap_state,
					   gid_t gid,
					   LDAPMessage **result)
{
	pstring filter;

	snprintf(filter, sizeof(filter)-1,
		 "(&(objectClass=posixGroup)(gidNumber=%i))", gid);

	return ldapsam_search_one_group(ldap_state, filter, result);
}

static NTSTATUS ldapsam_add_group_mapping_entry(struct pdb_methods *methods,
						GROUP_MAP *map)
{
	struct smb_ldap_privates *ldap_state =
		(struct smb_ldap_privates *)methods->private_data;
	LDAPMessage *result = NULL;
	LDAPMod **mods = NULL;

	char *tmp;
	pstring dn;
	LDAPMessage *entry;

	GROUP_MAP dummy;

	int rc;

	if (NT_STATUS_IS_OK(ldapsam_getgrgid(methods, &dummy,
					     map->gid, False))) {
		DEBUG(0, ("Group %i already exists in LDAP\n", map->gid));
		return NT_STATUS_UNSUCCESSFUL;
	}

	rc = ldapsam_search_one_group_by_gid(ldap_state, map->gid, &result);
	if (rc != LDAP_SUCCESS) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	if (ldap_count_entries(ldap_state->ldap_struct, result) != 1) {
		DEBUG(2, ("Group %i must exist exactly once in LDAP\n",
			  map->gid));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}

	entry = ldap_first_entry(ldap_state->ldap_struct, result);
	tmp = ldap_get_dn(ldap_state->ldap_struct, entry);
	pstrcpy(dn, tmp);
	ldap_memfree(tmp);

	if (!init_ldap_from_group(ldap_state->ldap_struct,
				  result, &mods, map)) {
		DEBUG(0, ("init_ldap_from_group failed!\n"));
		ldap_mods_free(mods, 1);
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}

	ldap_msgfree(result);

	if (mods == NULL) {
		DEBUG(0, ("mods is empty\n"));
		return NT_STATUS_UNSUCCESSFUL;
	}

	smb_ldap_make_a_mod(&mods, LDAP_MOD_ADD, "objectClass",
		   "sambaGroupMapping");

	rc = smb_ldap_modify(ldap_state, dn, mods);
	ldap_mods_free(mods, 1);

	if (rc != LDAP_SUCCESS) {
		char *ld_error = NULL;
		ldap_get_option(ldap_state->ldap_struct, LDAP_OPT_ERROR_STRING,
				&ld_error);
		DEBUG(0, ("failed to add group %i error: %s (%s)\n", map->gid, 
			  ld_error ? ld_error : "(unknown)", ldap_err2string(rc)));
		SAFE_FREE(ld_error);
		return NT_STATUS_UNSUCCESSFUL;
	}

	DEBUG(2, ("successfully modified group %i in LDAP\n", map->gid));
	return NT_STATUS_OK;
}

static NTSTATUS ldapsam_update_group_mapping_entry(struct pdb_methods *methods,
						   GROUP_MAP *map)
{
	struct smb_ldap_privates *ldap_state =
		(struct smb_ldap_privates *)methods->private_data;
	int rc;
	char *dn;
	LDAPMessage *result;
	LDAPMessage *entry;
	LDAPMod **mods;

	rc = ldapsam_search_one_group_by_gid(ldap_state, map->gid, &result);

	if (rc != LDAP_SUCCESS) {
		return NT_STATUS_UNSUCCESSFUL;
	}

	if (ldap_count_entries(ldap_state->ldap_struct, result) == 0) {
		DEBUG(0, ("No group to modify!\n"));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}

	entry = ldap_first_entry(ldap_state->ldap_struct, result);
	dn = ldap_get_dn(ldap_state->ldap_struct, entry);

	if (!init_ldap_from_group(ldap_state->ldap_struct,
				  result, &mods, map)) {
		DEBUG(0, ("init_ldap_from_group failed\n"));
		ldap_msgfree(result);
		return NT_STATUS_UNSUCCESSFUL;
	}

	ldap_msgfree(result);

	if (mods == NULL) {
		DEBUG(4, ("mods is empty: nothing to do\n"));
		return NT_STATUS_UNSUCCESSFUL;
	}

	rc = smb_ldap_modify(ldap_state, dn, mods);

	ldap_mods_free(mods, 1);

	if (rc != LDAP_SUCCESS) {
		char *ld_error = NULL;
		ldap_get_option(ldap_state->ldap_struct, LDAP_OPT_ERROR_STRING,
				&ld_error);
		DEBUG(0, ("failed to modify group %i error: %s (%s)\n", map->gid, 
			  ld_error ? ld_error : "(unknown)", ldap_err2string(rc)));
		SAFE_FREE(ld_error);
	}

	DEBUG(2, ("successfully modified group %i in LDAP\n", map->gid));
	return NT_STATUS_OK;
}

static NTSTATUS ldapsam_delete_group_mapping_entry(struct pdb_methods *methods,
						   DOM_SID sid)
{
	struct smb_ldap_privates *ldap_state =
		(struct smb_ldap_privates *)methods->private_data;
	pstring sidstring, filter;
	LDAPMessage *result;
	int rc;
	NTSTATUS ret;

	const char *sam_group_attrs[] = { "ntSid", "ntGroupType",
					  "description", "displayName",
					  NULL };
	sid_to_string(sidstring, &sid);
	snprintf(filter, sizeof(filter)-1,
		 "(&(objectClass=sambaGroupMapping)(ntSid=%s))", sidstring);

	rc = ldapsam_search_one_group(ldap_state, filter, &result);

	if (rc != LDAP_SUCCESS) {
		return NT_STATUS_NO_SUCH_GROUP;
	}

	ret = ldapsam_delete_entry(ldap_state, result, "sambaGroupMapping",
				   sam_group_attrs);
	ldap_msgfree(result);
	return ret;
}

static NTSTATUS ldapsam_setsamgrent(struct pdb_methods *my_methods,
				    BOOL update)
{
	struct smb_ldap_privates *ldap_state =
		(struct smb_ldap_privates *)my_methods->private_data;
	const char *filter = "(objectClass=sambaGroupMapping)";
	int rc;

	rc = smb_ldap_search(ldap_state, lp_ldap_suffix(),
			    LDAP_SCOPE_SUBTREE, filter,
			    group_attr, 0, &ldap_state->result);

	if (rc != LDAP_SUCCESS) {
		DEBUG(0, ("LDAP search failed: %s\n", ldap_err2string(rc)));
		DEBUG(3, ("Query was: %s, %s\n", lp_ldap_suffix(), filter));
		ldap_msgfree(ldap_state->result);
		ldap_state->result = NULL;
		return NT_STATUS_UNSUCCESSFUL;
	}

	DEBUG(2, ("ldapsam_setsampwent: %d entries in the base!\n",
		  ldap_count_entries(ldap_state->ldap_struct,
				     ldap_state->result)));

	ldap_state->entry = ldap_first_entry(ldap_state->ldap_struct,
				 ldap_state->result);
	ldap_state->index = 0;

	return NT_STATUS_OK;
}

static void ldapsam_endsamgrent(struct pdb_methods *my_methods)
{
	ldapsam_endsampwent(my_methods);
}

static NTSTATUS ldapsam_getsamgrent(struct pdb_methods *my_methods,
				    GROUP_MAP *map)
{
	NTSTATUS ret = NT_STATUS_UNSUCCESSFUL;
	struct smb_ldap_privates *ldap_state = (struct smb_ldap_privates *)my_methods->private_data;
	BOOL bret = False;

	while (!bret) {
		if (!ldap_state->entry)
			return ret;
		
		ldap_state->index++;
		bret = init_group_from_ldap(ldap_state, map, ldap_state->entry);
		
		ldap_state->entry = ldap_next_entry(ldap_state->ldap_struct,
					    ldap_state->entry);	
	}

	return NT_STATUS_OK;
}

static NTSTATUS ldapsam_enum_group_mapping(struct pdb_methods *methods,
					   enum SID_NAME_USE sid_name_use,
					   GROUP_MAP **rmap, int *num_entries,
					   BOOL unix_only, BOOL with_priv)
{
	GROUP_MAP map;
	GROUP_MAP *mapt;
	int entries = 0;
	NTSTATUS nt_status;

	*num_entries = 0;
	*rmap = NULL;

	if (!NT_STATUS_IS_OK(ldapsam_setsamgrent(methods, False))) {
		DEBUG(0, ("Unable to open passdb\n"));
		return NT_STATUS_ACCESS_DENIED;
	}

	while (NT_STATUS_IS_OK(nt_status = ldapsam_getsamgrent(methods, &map))) {
		if (sid_name_use != SID_NAME_UNKNOWN &&
		    sid_name_use != map.sid_name_use) {
			DEBUG(11,("enum_group_mapping: group %s is not of the requested type\n", map.nt_name));
			continue;
		}
		if (unix_only==ENUM_ONLY_MAPPED && map.gid==-1) {
			DEBUG(11,("enum_group_mapping: group %s is non mapped\n", map.nt_name));
			continue;
		}

		mapt=(GROUP_MAP *)Realloc((*rmap), (entries+1)*sizeof(GROUP_MAP));
		if (!mapt) {
			DEBUG(0,("enum_group_mapping: Unable to enlarge group map!\n"));
			SAFE_FREE(*rmap);
			return NT_STATUS_UNSUCCESSFUL;
		}
		else
			(*rmap) = mapt;

		mapt[entries] = map;

		entries += 1;

	}
	ldapsam_endsamgrent(methods);

	*num_entries = entries;

	return NT_STATUS_OK;
}

NTSTATUS pdb_init_ldapsam(PDB_CONTEXT *pdb_context, PDB_METHODS **pdb_method, const char *location)
{
	NTSTATUS nt_status;
	struct smb_ldap_privates *ldap_state;
	uint32 low_nua_uid, high_nua_uid;

	if (!NT_STATUS_IS_OK(nt_status = make_pdb_methods(pdb_context->mem_ctx, pdb_method))) {
		return nt_status;
	}

	(*pdb_method)->name = "ldapsam";

	(*pdb_method)->setsampwent = ldapsam_setsampwent;
	(*pdb_method)->endsampwent = ldapsam_endsampwent;
	(*pdb_method)->getsampwent = ldapsam_getsampwent;
	(*pdb_method)->getsampwnam = ldapsam_getsampwnam;
	(*pdb_method)->getsampwsid = ldapsam_getsampwsid;
	(*pdb_method)->add_sam_account = ldapsam_add_sam_account;
	(*pdb_method)->update_sam_account = ldapsam_update_sam_account;
	(*pdb_method)->delete_sam_account = ldapsam_delete_sam_account;

	(*pdb_method)->getgrsid = ldapsam_getgrsid;
	(*pdb_method)->getgrgid = ldapsam_getgrgid;
	(*pdb_method)->getgrnam = ldapsam_getgrnam;
	(*pdb_method)->add_group_mapping_entry = ldapsam_add_group_mapping_entry;
	(*pdb_method)->update_group_mapping_entry = ldapsam_update_group_mapping_entry;
	(*pdb_method)->delete_group_mapping_entry = ldapsam_delete_group_mapping_entry;
	(*pdb_method)->enum_group_mapping = ldapsam_enum_group_mapping;

	/* TODO: Setup private data and free */

	ldap_state = talloc_zero(pdb_context->mem_ctx, sizeof(struct smb_ldap_privates));

	if (!ldap_state) {
		DEBUG(0, ("talloc() failed for ldapsam private_data!\n"));
		return NT_STATUS_NO_MEMORY;
	}

	if (location) {
		ldap_state->uri = talloc_strdup(pdb_context->mem_ctx, location);
#ifdef WITH_LDAP_SAMCONFIG
	} else {
		int ldap_port = lp_ldap_port();
			
		/* remap default port if not using SSL (ie clear or TLS) */
		if ( (lp_ldap_ssl() != LDAP_SSL_ON) && (ldap_port == 636) ) {
			ldap_port = 389;
		}

		ldap_state->uri = talloc_asprintf(pdb_context->mem_ctx, "%s://%s:%d", lp_ldap_ssl() == LDAP_SSL_ON ? "ldaps" : "ldap", lp_ldap_server(), ldap_port);
		if (!ldap_state->uri) {
			return NT_STATUS_NO_MEMORY;
		}
#else
	} else {
		ldap_state->uri = "ldap://localhost";
#endif
	}

	(*pdb_method)->private_data = ldap_state;

	(*pdb_method)->free_private_data = free_private_data;

	if (lp_idmap_uid(&low_nua_uid, &high_nua_uid)) {
		DEBUG(0, ("idmap uid range defined, non unix accounts enabled\n"));

		ldap_state->permit_non_unix_accounts = True;
		
		ldap_state->low_nua_rid=fallback_pdb_uid_to_user_rid(low_nua_uid);

		ldap_state->high_nua_rid=fallback_pdb_uid_to_user_rid(high_nua_uid);
	}

	return NT_STATUS_OK;
}

NTSTATUS pdb_ldap_init(void)
{
	return smb_register_passdb("ldapsam", pdb_init_ldapsam, PASSDB_INTERFACE_VERSION);
}
