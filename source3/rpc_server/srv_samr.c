#define OLD_NTDOMAIN 1
/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
 *  Copyright (C) Paul Ashton                       1997.
 *  Copyright (C) Hewlett-Packard Company           1999.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "includes.h"

extern int DEBUGLEVEL;

extern fstring global_myworkgroup;
extern pstring global_myname;
extern DOM_SID global_sam_sid;

extern rid_name domain_group_rids[];
extern rid_name domain_alias_rids[];
extern rid_name builtin_alias_rids[];

/*******************************************************************
  This next function should be replaced with something that
  dynamically returns the correct user info..... JRA.
 ********************************************************************/

static BOOL get_sampwd_entries(SAM_USER_INFO_21 *pw_buf, int start_idx,
                                int *total_entries, int *num_entries,
                                int max_num_entries, uint16 acb_mask)
{
	SAM_ACCOUNT *pwd = NULL;

	(*num_entries) = 0;
	(*total_entries) = 0;

	if (pw_buf == NULL) 
		return False;

	if (!pdb_setsampwent(False))
	{
		DEBUG(0, ("get_sampwd_entries: Unable to open passdb.\n"));
		return False;
	}

	while ( ((pwd=pdb_getsampwent()) != NULL) && ((*num_entries) < max_num_entries) ) 
	{
		int user_name_len;

		if (start_idx > 0) {
			/* skip the requested number of entries.
			   not very efficient, but hey...
			 */
			start_idx--;
			continue;
		}

		user_name_len = strlen(pdb_get_username(pwd))+1;
		init_unistr2(&(pw_buf[(*num_entries)].uni_user_name), pdb_get_username(pwd), user_name_len);
		init_uni_hdr(&(pw_buf[(*num_entries)].hdr_user_name), user_name_len);
		pw_buf[(*num_entries)].user_rid = pdb_get_user_rid(pwd);
		memset((char *)pw_buf[(*num_entries)].nt_pwd, '\0', 16);

		/* Now check if the NT compatible password is available. */
		if (pdb_get_nt_passwd(pwd) != NULL) 
		{
			memcpy( pw_buf[(*num_entries)].nt_pwd , pdb_get_nt_passwd(pwd), 16);
		}

		pw_buf[(*num_entries)].acb_info = pdb_get_acct_ctrl(pwd);

		DEBUG(5, ("entry idx: %d user %s, rid 0x%x, acb %x",
		          (*num_entries), pdb_get_username(pwd),
		          pdb_get_user_rid(pwd), pdb_get_acct_ctrl(pwd)));

		if (acb_mask == 0 || (pdb_get_acct_ctrl(pwd) & acb_mask)) 
		{
			DEBUG(5,(" acb_mask %x accepts\n", acb_mask));
			(*num_entries)++;
		} 
		else 
		{
			DEBUG(5,(" acb_mask %x rejects\n", acb_mask));
		}

		(*total_entries)++;
	}

	pdb_endsampwent();

	return (*num_entries) > 0;
}

/*******************************************************************
 This function uses the username map file and tries to map a UNIX
 user name to an DOS name.  (Sort of the reverse of the
 map_username() function.)  Since more than one DOS name can map
 to the UNIX name, to reverse the mapping you have to specify
 which corresponding DOS name you want; that's where the name_idx
 parameter comes in.  Returns the string requested or NULL if it
 fails or can't complete the request for any reason.  This doesn't
 handle group names (starting with '@') or names starting with
 '+' or '&'.  If they are encountered, they are skipped.
********************************************************************/

static char *unmap_unixname(char *unix_user_name, int name_idx)
{
	char *mapfile = lp_username_map();
	char **lines;
	static pstring tok;
	int i;

	if (!*unix_user_name) return NULL;
	if (!*mapfile) return NULL;

	lines = file_lines_load(mapfile, NULL,False);
	if (!lines) {
		DEBUG(0,("unmap_unixname: can't open username map %s\n", mapfile));
		return NULL;
	}

	DEBUG(5,("unmap_unixname: scanning username map %s, index: %d\n", mapfile, name_idx));

	for (i=0; lines[i]; i++) {
		char *unixname = lines[i];
		char *dosname = strchr(unixname,'=');

		if (!dosname)
			continue;

		*dosname++ = 0;

		while (isspace(*unixname))
			unixname++;
		if ('!' == *unixname) {
			unixname++;
			while (*unixname && isspace(*unixname))
				unixname++;
		}
    
		if (!*unixname || strchr("#;",*unixname))
			continue;

		if (strncmp(unixname, unix_user_name, strlen(unix_user_name)))
			continue;

		/* We have matched the UNIX user name */

		while(next_token(&dosname, tok, LIST_SEP, sizeof(tok))) {
			if (!strchr("@&+", *tok)) {
				name_idx--;
				if (name_idx < 0 ) {
					break;
				}
			}
		}

		if (name_idx >= 0) {
			DEBUG(0,("unmap_unixname: index too high - not that many DOS names\n"));
			file_lines_free(lines);
			return NULL;
		} else {
			file_lines_free(lines);
			return tok;
		}
	}

	DEBUG(0,("unmap_unixname: Couldn't find the UNIX user name\n"));
	file_lines_free(lines);
	return NULL;
}

/*******************************************************************
 This function sets up a list of users taken from the list of
 users that UNIX knows about, as well as all the user names that
 Samba maps to a valid UNIX user name.  (This should work with
 /etc/passwd or NIS.)
********************************************************************/

static BOOL get_passwd_entries(SAM_USER_INFO_21 *pw_buf,
				int start_idx,
				int *total_entries, int *num_entries,
				int max_num_entries,
				uint16 acb_mask)
{
	static struct passwd *pwd = NULL;
	static uint32 pw_rid;
	static BOOL orig_done = False;
	static int current_idx = 0;
	static int mapped_idx = 0;
	char *sep;

	DEBUG(5, ("get_passwd_entries: retrieving a list of UNIX users\n"));

	(*num_entries) = 0;
	(*total_entries) = 0;

	/* Skip all this stuff if we're in appliance mode */

	if (lp_hide_local_users()) goto done;

	if (pw_buf == NULL) return False;

	if (current_idx == 0) {
		sys_setpwent();
	}

	/* These two cases are inefficient, but should be called very rarely */
	/* they are the cases where the starting index isn't picking up      */
	/* where we left off last time.  It is efficient when it starts over */
	/* at zero though.                                                   */
	if (start_idx > current_idx) {
		/* We aren't far enough; advance to start_idx */
		while (current_idx < start_idx) {
			char *unmap_name;

			if(!orig_done) {
				if ((pwd = sys_getpwent()) == NULL) break;
				current_idx++;
				orig_done = True;
			}

			while (((unmap_name = unmap_unixname(pwd->pw_name, mapped_idx)) != NULL) && 
			        (current_idx < start_idx)) {
				current_idx++;
				mapped_idx++;
			}

			if (unmap_name == NULL) {
				orig_done = False;
				mapped_idx = 0;
			}
		}
	} else if (start_idx < current_idx) {
		/* We are already too far; start over and advance to start_idx */
		sys_endpwent();
		sys_setpwent();
		current_idx = 0;
		mapped_idx = 0;
		orig_done = False;
		while (current_idx < start_idx) {
			char *unmap_name;

			if(!orig_done) {
				if ((pwd = sys_getpwent()) == NULL) break;
				current_idx++;
				orig_done = True;
			}

			while (((unmap_name = unmap_unixname(pwd->pw_name, mapped_idx)) != NULL) && 
			        (current_idx < start_idx)) {
				current_idx++;
				mapped_idx++;
			}

			if (unmap_name == NULL) {
				orig_done = False;
				mapped_idx = 0;
			}
		}
	}

	sep = lp_winbind_separator();

	/* now current_idx == start_idx */
	while ((*num_entries) < max_num_entries) {
		int user_name_len;
		char *unmap_name;

		/* This does the original UNIX user itself */
		if(!orig_done) {
			if ((pwd = sys_getpwent()) == NULL) break;

			/* Don't enumerate winbind users as they are not local */

			if (strchr(pwd->pw_name, *sep) != NULL) {
				continue;
			}

			user_name_len = strlen(pwd->pw_name);
			pw_rid = pdb_uid_to_user_rid(pwd->pw_uid);
			ZERO_STRUCTP(&pw_buf[(*num_entries)]);
			init_unistr2(&(pw_buf[(*num_entries)].uni_user_name), pwd->pw_name, user_name_len);
			init_uni_hdr(&(pw_buf[(*num_entries)].hdr_user_name), user_name_len);
			pw_buf[(*num_entries)].user_rid = pw_rid;
			memset((char *)pw_buf[(*num_entries)].nt_pwd, '\0', 16);

			pw_buf[(*num_entries)].acb_info = ACB_NORMAL;

			DEBUG(5, ("get_passwd_entries: entry idx %d user %s, rid 0x%x\n", (*num_entries), pwd->pw_name, pw_rid));

			(*num_entries)++;
			(*total_entries)++;
			current_idx++;
			orig_done = True;
		}

		/* This does all the user names that map to the UNIX user */
		while (((unmap_name = unmap_unixname(pwd->pw_name, mapped_idx)) != NULL) && 
		        (*num_entries < max_num_entries)) {
			user_name_len = strlen(unmap_name);
			ZERO_STRUCTP(&pw_buf[(*num_entries)]);
			init_unistr2(&(pw_buf[(*num_entries)].uni_user_name), unmap_name, user_name_len);
			init_uni_hdr(&(pw_buf[(*num_entries)].hdr_user_name), user_name_len);
			pw_buf[(*num_entries)].user_rid = pw_rid;
			memset((char *)pw_buf[(*num_entries)].nt_pwd, '\0', 16);

			pw_buf[(*num_entries)].acb_info = ACB_NORMAL;

			DEBUG(5, ("get_passwd_entries: entry idx %d user %s, rid 0x%x\n", (*num_entries), pwd->pw_name, pw_rid));

			(*num_entries)++;
			(*total_entries)++;
			current_idx++;
			mapped_idx++;
		}

		if (unmap_name == NULL) {
			/* done with 'aliases', go on to next UNIX user */
			orig_done = False;
			mapped_idx = 0;
		}
	}

	if (pwd == NULL) {
		/* totally done, reset everything */
		sys_endpwent();
		current_idx = 0;
		mapped_idx = 0;
	}

done:
	return (*num_entries) > 0;
}

/*******************************************************************
 samr_reply_unknown_1
 ********************************************************************/
static BOOL samr_reply_close_hnd(SAMR_Q_CLOSE_HND *q_u,
				prs_struct *rdata)
{
	SAMR_R_CLOSE_HND r_u;

	/* set up the SAMR unknown_1 response */
	ZERO_STRUCT(r_u.pol);

	/* close the policy handle */
	if (close_lsa_policy_hnd(&(q_u->pol)))
	{
		r_u.status = 0;
	}
	else
	{
		r_u.status = 0xC0000000 | NT_STATUS_OBJECT_NAME_INVALID;
	}

	DEBUG(5,("samr_reply_close_hnd: %d\n", __LINE__));

	/* store the response in the SMB stream */
	if(!samr_io_r_close_hnd("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_reply_close_hnd: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_close_hnd
 ********************************************************************/
static BOOL api_samr_close_hnd(pipes_struct *p)
{
	SAMR_Q_CLOSE_HND q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr unknown 1 */
	if(!samr_io_q_close_hnd("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_close_hnd(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_open_domain
 ********************************************************************/
static BOOL samr_reply_open_domain(SAMR_Q_OPEN_DOMAIN *q_u,
				prs_struct *rdata)
{
	SAMR_R_OPEN_DOMAIN r_u;
	BOOL pol_open = False;

	r_u.status = 0x0;

	/* find the connection policy handle. */
	if (r_u.status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
	{
		r_u.status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	/* get a (unique) handle.  open a policy on it. */
	if (r_u.status == 0x0 && !(pol_open = open_lsa_policy_hnd(&(r_u.domain_pol))))
	{
		r_u.status = 0xC0000000 | NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	/* associate the domain SID with the (unique) handle. */
	if (r_u.status == 0x0 && !set_lsa_policy_samr_sid(&(r_u.domain_pol), &(q_u->dom_sid.sid)))
	{
		/* oh, whoops.  don't know what error message to return, here */
		r_u.status = 0xC0000000 | NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	if (r_u.status != 0 && pol_open)
	{
		close_lsa_policy_hnd(&(r_u.domain_pol));
	}

	DEBUG(5,("samr_open_domain: %d\n", __LINE__));

	/* store the response in the SMB stream */
	if(!samr_io_r_open_domain("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_open_domain: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_open_domain
 ********************************************************************/
static BOOL api_samr_open_domain(pipes_struct *p)
{
	SAMR_Q_OPEN_DOMAIN q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open */
	if(!samr_io_q_open_domain("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_open_domain(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_unknown_2c
 ********************************************************************/
static BOOL samr_reply_unknown_2c(SAMR_Q_UNKNOWN_2C *q_u,
				prs_struct *rdata)
{
	SAMR_R_UNKNOWN_2C r_u;
	uint32 status = 0x0;

	/* find the policy handle.  open a policy on it. */
	if (status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->user_pol)) == -1))
	{
		status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	/* find the user's rid */
	if ((status == 0x0) && (get_lsa_policy_samr_rid(&(q_u->user_pol)) == 0xffffffff))
	{
		status = NT_STATUS_OBJECT_TYPE_MISMATCH;
	}

	init_samr_r_unknown_2c(&r_u, status);

	DEBUG(5,("samr_unknown_2c: %d\n", __LINE__));

	/* store the response in the SMB stream */
	if(!samr_io_r_unknown_2c("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_unknown_2c: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_unknown_2c
 ********************************************************************/
static BOOL api_samr_unknown_2c(pipes_struct *p)
{
	SAMR_Q_UNKNOWN_2C q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open */
	if(!samr_io_q_unknown_2c("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_unknown_2c(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_unknown_3
 ********************************************************************/
static BOOL samr_reply_unknown_3(SAMR_Q_UNKNOWN_3 *q_u,
				prs_struct *rdata)
{
	SAMR_R_UNKNOWN_3 r_u;
	DOM_SID3 sid[MAX_SAM_SIDS];
	uint32 rid;
	uint32 status;

	status = 0x0;

	/* find the policy handle.  open a policy on it. */
	if (status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->user_pol)) == -1))
	{
		status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	/* find the user's rid */
	if (status == 0x0 && (rid = get_lsa_policy_samr_rid(&(q_u->user_pol))) == 0xffffffff)
	{
		status = NT_STATUS_OBJECT_TYPE_MISMATCH;
	}

	if (status == 0x0)
	{
		DOM_SID user_sid;
		DOM_SID everyone_sid;

		user_sid = global_sam_sid;

		SMB_ASSERT_ARRAY(user_sid.sub_auths, user_sid.num_auths+1);

		/*
		 * Add the user RID.
		 */
		user_sid.sub_auths[user_sid.num_auths++] = rid;
		
			string_to_sid(&everyone_sid, "S-1-1");

			/* maybe need another 1 or 2 (S-1-5-0x20-0x220 and S-1-5-20-0x224) */
			/* these two are DOMAIN_ADMIN and DOMAIN_ACCT_OP group RIDs */
			init_dom_sid3(&(sid[0]), 0x035b, 0x0002, &everyone_sid);
			init_dom_sid3(&(sid[1]), 0x0044, 0x0002, &user_sid);
	}

	init_samr_r_unknown_3(&r_u,
				0x0001, 0x8004,
				0x00000014, 0x0002, 0x0070,
				2, sid, status);

	DEBUG(5,("samr_unknown_3: %d\n", __LINE__));

	/* store the response in the SMB stream */
	if(!samr_io_r_unknown_3("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_unknown_3: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_unknown_3
 ********************************************************************/
static BOOL api_samr_unknown_3(pipes_struct *p)
{
	SAMR_Q_UNKNOWN_3 q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open */
	if(!samr_io_q_unknown_3("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_unknown_3(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_enum_dom_users
 ********************************************************************/
static BOOL samr_reply_enum_dom_users(SAMR_Q_ENUM_DOM_USERS *q_u,
				prs_struct *rdata)
{
	SAMR_R_ENUM_DOM_USERS r_e;
	SAM_USER_INFO_21 pass[MAX_SAM_ENTRIES];
	int num_entries;
	int total_entries;
	
	ZERO_STRUCT(r_e);

	r_e.status = 0x0;
	r_e.total_num_entries = 0;

	/* find the policy handle.  open a policy on it. */
	if (r_e.status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
	{
		r_e.status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	DEBUG(5,("samr_reply_enum_dom_users: %d\n", __LINE__));

	become_root();
	get_sampwd_entries(pass, 0, &total_entries, &num_entries, MAX_SAM_ENTRIES, q_u->acb_mask);
	unbecome_root();

	init_samr_r_enum_dom_users(&r_e, total_entries,
	                           q_u->unknown_0, num_entries,
	                           pass, r_e.status);

	/* store the response in the SMB stream */
	if(!samr_io_r_enum_dom_users("", &r_e, rdata, 0))
		return False;

	DEBUG(5,("samr_enum_dom_users: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_enum_dom_users
 ********************************************************************/
static BOOL api_samr_enum_dom_users(pipes_struct *p)
{
	SAMR_Q_ENUM_DOM_USERS q_e;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open */
	if(!samr_io_q_enum_dom_users("", &q_e, data, 0))
		return False;

	/* construct reply. */
	if(!samr_reply_enum_dom_users(&q_e, rdata))
		return False;

	return True;
}

/*******************************************************************
 samr_reply_enum_dom_groups
 ********************************************************************/
static BOOL samr_reply_enum_dom_groups(SAMR_Q_ENUM_DOM_GROUPS *q_u,
				prs_struct *rdata)
{
	SAMR_R_ENUM_DOM_GROUPS r_e;
	SAM_USER_INFO_21 pass[MAX_SAM_ENTRIES];
	int num_entries;
	BOOL got_grps;
	char *dummy_group = "Domain Admins";
	
	ZERO_STRUCT(r_e);

	r_e.status = 0x0;
	r_e.num_entries = 0;

	/* find the policy handle.  open a policy on it. */
	if (r_e.status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
	{
		r_e.status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	DEBUG(5,("samr_reply_enum_dom_groups: %d\n", __LINE__));

	got_grps = True;
	num_entries = 1;
	ZERO_STRUCTP(&pass[0]);
	init_unistr2(&(pass[0].uni_user_name), dummy_group, strlen(dummy_group)+1);
	pass[0].user_rid = DOMAIN_GROUP_RID_ADMINS;

	if (r_e.status == 0 && got_grps)
	{
		init_samr_r_enum_dom_groups(&r_e, q_u->start_idx, num_entries, pass, r_e.status);
	}

	/* store the response in the SMB stream */
	if(!samr_io_r_enum_dom_groups("", &r_e, rdata, 0))
		return False;

	DEBUG(5,("samr_enum_dom_groups: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_enum_dom_groups
 ********************************************************************/
static BOOL api_samr_enum_dom_groups(pipes_struct *p)
{
	SAMR_Q_ENUM_DOM_GROUPS q_e;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open */
	if(!samr_io_q_enum_dom_groups("", &q_e, data, 0))
		return False;

	/* construct reply. */
	if(!samr_reply_enum_dom_groups(&q_e, rdata))
		return False;

	return True;
}

/*******************************************************************
 samr_reply_enum_dom_aliases
 ********************************************************************/
static BOOL samr_reply_enum_dom_aliases(SAMR_Q_ENUM_DOM_ALIASES *q_u,
				prs_struct *rdata)
{
	SAMR_R_ENUM_DOM_ALIASES r_e;
	SAM_USER_INFO_21 pass[MAX_SAM_ENTRIES];
	int num_entries = 0;
	DOM_SID sid;
	fstring sid_str;
	fstring sam_sid_str;
	struct group *grp;
	
	ZERO_STRUCT(r_e);

	/* find the policy handle.  open a policy on it. */
	if (r_e.status == 0x0 && !get_lsa_policy_samr_sid(&q_u->pol, &sid))
	{
		r_e.status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	sid_to_string(sid_str, &sid);
	sid_to_string(sam_sid_str, &global_sam_sid);

	DEBUG(5,("samr_reply_enum_dom_aliases: sid %s\n", sid_str));

	/* well-known aliases */
	if (strequal(sid_str, "S-1-5-32"))
	{
		char *name;
		while (!lp_hide_local_users() &&
		       num_entries < MAX_SAM_ENTRIES && 
		       ((name = builtin_alias_rids[num_entries].name) != NULL))
		{
			init_unistr2(&(pass[num_entries].uni_user_name), name, strlen(name)+1);
			pass[num_entries].user_rid = builtin_alias_rids[num_entries].rid;
			num_entries++;
		}
	}
	else if (strequal(sid_str, sam_sid_str) && !lp_hide_local_users())
	{
		char *name;
		char *sep;

		sep = lp_winbind_separator();

		/* local aliases */
		/* we return the UNIX groups here.  This seems to be the right */
		/* thing to do, since NT member servers return their local     */
                /* groups in the same situation.                               */
		setgrent();

		while (num_entries < MAX_SAM_ENTRIES && ((grp = getgrent()) != NULL))
		{
			int i;
			uint32 trid;
			name = grp->gr_name;

			/* Don't return winbind groups as they are not local! */

			if (strchr(name, *sep) != NULL) {
				continue;
			}

			trid = pdb_gid_to_group_rid(grp->gr_gid);
			for( i = 0; i < num_entries; i++)
				if ( pass[i].user_rid == trid ) break;
			if ( i < num_entries )
				continue; /* rid was there, dup! */

			init_unistr2(&(pass[num_entries].uni_user_name), name, strlen(name)+1);
			pass[num_entries].user_rid = trid;
			num_entries++;
		}

		endgrent();
	}
		
	init_samr_r_enum_dom_aliases(&r_e, num_entries, pass, r_e.status);

	/* store the response in the SMB stream */
	if(!samr_io_r_enum_dom_aliases("", &r_e, rdata, 0))
		return False;

	DEBUG(5,("samr_enum_dom_aliases: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_enum_dom_aliases
 ********************************************************************/
static BOOL api_samr_enum_dom_aliases(pipes_struct *p)
{
	SAMR_Q_ENUM_DOM_ALIASES q_e;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;
	
	ZERO_STRUCT(q_e);

	/* grab the samr open */
	if(!samr_io_q_enum_dom_aliases("", &q_e, data, 0))
		return False;

	/* construct reply. */
	if(!samr_reply_enum_dom_aliases(&q_e, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_query_dispinfo
 ********************************************************************/
static BOOL samr_reply_query_dispinfo(SAMR_Q_QUERY_DISPINFO *q_u, prs_struct *rdata)
{
	SAMR_R_QUERY_DISPINFO r_e;
	SAM_INFO_CTR ctr;
	SAM_INFO_1 info1;
	SAM_INFO_2 info2;
	SAM_USER_INFO_21 pass[MAX_SAM_ENTRIES];
	int num_entries = 0;
	int total_entries = 0;
	BOOL got_pwds;
	uint16 switch_level = 0x0;

	ZERO_STRUCT(r_e);

	DEBUG(5,("samr_reply_query_dispinfo: %d\n", __LINE__));

	/* find the policy handle.  open a policy on it. */
	if (r_e.status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
	{
		r_e.status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
		DEBUG(5,("samr_reply_query_dispinfo: invalid handle\n"));
	}

	if (r_e.status == 0x0)
	{
	  /* decide how many entries to get depending on the max_entries 
	     and max_size passed by client */
	  uint32 retsize;

	  if(q_u->max_entries > MAX_SAM_ENTRIES)
	    q_u->max_entries = MAX_SAM_ENTRIES;
	  
	  retsize = (q_u->max_entries * (sizeof(SAM_ENTRY1)+sizeof(SAM_STR1)))
	    + 3*sizeof(uint32);

	  if(retsize > q_u->max_size)
	    {
	      /* determine max_entries based on max_size */
	      q_u->max_entries = (q_u->max_size - 3*sizeof(uint32)) /
		(sizeof(SAM_ENTRY1)+sizeof(SAM_STR1));
	      q_u->max_entries = (q_u->max_entries>0?q_u->max_entries:1);
	    }

	  DEBUG(10,("samr_reply_query_dispinfo: Setting q_u->max_entries to %u\n",q_u->max_entries));

		become_root();
		got_pwds = get_passwd_entries(pass, q_u->start_idx, &total_entries, &num_entries, q_u->max_entries, 0);
		unbecome_root();

		/* more left - set resume handle */
		if(total_entries > num_entries)
		  {
		    r_e.status = 0x105;
		  }

		switch (q_u->switch_level)
		{
			case 0x1:
			{
			
				/* query disp info is for users */
				ZERO_STRUCT (info1);
				switch_level = 0x1;
				init_sam_info_1(&info1, ACB_NORMAL,
					q_u->start_idx, num_entries, pass);

				ctr.sam.info1 = &info1;

				break;
			}
			case 0x2:
			{
				/* query disp info is for servers */
				ZERO_STRUCT (info2);
				switch_level = 0x2;
				init_sam_info_2(&info2, ACB_WSTRUST,
					q_u->start_idx, num_entries, pass);

				ctr.sam.info2 = &info2;

				break;
			}
		}
	}

	/* more left - set resume handle */
	if(total_entries > num_entries)
	  {
	    r_e.status = 0x105;
	  }

	if (r_e.status == 0 || r_e.status == 0x105)
	{
	  init_samr_r_query_dispinfo(&r_e, switch_level, &ctr, r_e.status);
	}

	/* store the response in the SMB stream */
	if(!samr_io_r_query_dispinfo("", &r_e, rdata, 0))
		return False;

	DEBUG(5,("samr_query_dispinfo: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_query_dispinfo
 ********************************************************************/
static BOOL api_samr_query_dispinfo(pipes_struct *p)
{
	SAMR_Q_QUERY_DISPINFO q_e;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open */
	if(!samr_io_q_query_dispinfo("", &q_e, data, 0))
		return False;

	/* construct reply. */
	if(!samr_reply_query_dispinfo(&q_e, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_query_aliasinfo
 ********************************************************************/
static BOOL samr_reply_query_aliasinfo(SAMR_Q_QUERY_ALIASINFO *q_u,
				prs_struct *rdata)
{
  SAMR_R_QUERY_ALIASINFO r_e;
  fstring alias_desc = "Local Unix group";
  fstring alias="";
  enum SID_NAME_USE type;
  uint32 alias_rid;

  ZERO_STRUCT(r_e);

  DEBUG(5,("samr_reply_query_aliasinfo: %d\n", __LINE__));

  /* find the policy handle.  open a policy on it. */
  if (r_e.status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
    {
      r_e.status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
    }

  alias_rid = get_lsa_policy_samr_rid(&q_u->pol);
  if(alias_rid == 0xffffffff)
      r_e.status = 0xC0000000 | NT_STATUS_NO_SUCH_ALIAS;

  if(!local_lookup_rid(alias_rid, alias, &type))
    {
      r_e.status = 0xC0000000 | NT_STATUS_NO_SUCH_ALIAS;
    }
  
  init_samr_r_query_aliasinfo(&r_e, q_u->switch_level, alias, alias_desc);
  
  /* store the response in the SMB stream */
  if(!samr_io_r_query_aliasinfo("", &r_e, rdata, 0))
		return False;
  
  DEBUG(5,("samr_query_aliasinfo: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_query_aliasinfo
 ********************************************************************/
static BOOL api_samr_query_aliasinfo(pipes_struct *p)
{
	SAMR_Q_QUERY_ALIASINFO q_e;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open */
	if(!samr_io_q_query_aliasinfo("", &q_e, data, 0))
		return False;

	/* construct reply. */
	if(!samr_reply_query_aliasinfo(&q_e, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_lookup_ids
 ********************************************************************/
static BOOL samr_reply_lookup_ids(SAMR_Q_LOOKUP_IDS *q_u,
				prs_struct *rdata)
{
	uint32 rid[MAX_SAM_ENTRIES];
	uint32 status     = 0;
	int num_rids = q_u->num_sids1;

	SAMR_R_LOOKUP_IDS r_u;

	DEBUG(5,("samr_lookup_ids: %d\n", __LINE__));

	if (num_rids > MAX_SAM_ENTRIES)
	{
		num_rids = MAX_SAM_ENTRIES;
		DEBUG(5,("samr_lookup_ids: truncating entries to %d\n", num_rids));
	}

#if 0
	int i;
	SMB_ASSERT_ARRAY(q_u->uni_user_name, num_rids);

	for (i = 0; i < num_rids && status == 0; i++)
	{
		SAM_ACCOUNT *sam_pass;
		fstring user_name;


		fstrcpy(user_name, unistrn2(q_u->uni_user_name[i].buffer,
		                            q_u->uni_user_name[i].uni_str_len));

		/* find the user account */
		become_root();
		sam_pass = pdb_getsampwnam(user_name);
		unbecome_root();

		if (sam_pass == NULL)
		{
			status = 0xC0000000 | NT_STATUS_NO_SUCH_USER;
			rid[i] = 0;
		}
		else
		{
			rid[i] = pdb_get_user_rid(sam_pass);
		}
	}
#endif

	num_rids = 1;
	rid[0] = BUILTIN_ALIAS_RID_USERS;

	init_samr_r_lookup_ids(&r_u, num_rids, rid, status);

	/* store the response in the SMB stream */
	if(!samr_io_r_lookup_ids("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_lookup_ids: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_lookup_ids
 ********************************************************************/
static BOOL api_samr_lookup_ids(pipes_struct *p)
{
	SAMR_Q_LOOKUP_IDS q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr 0x10 */
	if(!samr_io_q_lookup_ids("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_lookup_ids(&q_u, rdata))
		return False;

	return True;
}

/*******************************************************************
 samr_reply_lookup_names
 ********************************************************************/

static BOOL samr_reply_lookup_names(SAMR_Q_LOOKUP_NAMES *q_u,
				    prs_struct *rdata)
{
  uint32 rid[MAX_SAM_ENTRIES];
  enum SID_NAME_USE type[MAX_SAM_ENTRIES];
  uint32 status = 0;
  int i;
  int num_rids = q_u->num_names1;
  DOM_SID pol_sid;

  SAMR_R_LOOKUP_NAMES r_u;

  DEBUG(5,("samr_lookup_names: %d\n", __LINE__));

  ZERO_ARRAY(rid);
  ZERO_ARRAY(type);

  if (!get_lsa_policy_samr_sid(&q_u->pol, &pol_sid)) {
    status = 0xC0000000 | NT_STATUS_OBJECT_TYPE_MISMATCH;
    init_samr_r_lookup_names(&r_u, 0, rid, type, status);
    if(!samr_io_r_lookup_names("", &r_u, rdata, 0)) {
      DEBUG(0,("samr_reply_lookup_names: failed to marshall SAMR_R_LOOKUP_NAMES.\n"));
      return False;
    }
    return True;
  }

  if (num_rids > MAX_SAM_ENTRIES) {
    num_rids = MAX_SAM_ENTRIES;
    DEBUG(5,("samr_lookup_names: truncating entries to %d\n", num_rids));
  }

  SMB_ASSERT_ARRAY(q_u->uni_name, num_rids);

  for (i = 0; i < num_rids; i++) {
    fstring name;

    status = 0xC0000000 | NT_STATUS_NONE_MAPPED;

    rid [i] = 0xffffffff;
    type[i] = SID_NAME_UNKNOWN;

    fstrcpy(name, dos_unistrn2(q_u->uni_name[i].buffer,
			       q_u->uni_name[i].uni_str_len));

    if(sid_equal(&pol_sid, &global_sam_sid)) 
    {
      DOM_SID sid;
      if(local_lookup_name(global_myname, name, 
			   &sid, &type[i]))
	{
	  sid_split_rid( &sid, &rid[i]);
	  status = 0;
	}
    }
  }

  init_samr_r_lookup_names(&r_u, num_rids, rid, type, status);

  /* store the response in the SMB stream */
  if(!samr_io_r_lookup_names("", &r_u, rdata, 0)) {
    DEBUG(0,("samr_reply_lookup_names: failed to marshall SAMR_R_LOOKUP_NAMES.\n"));
    return False;
  }

  DEBUG(5,("samr_lookup_names: %d\n", __LINE__));

  return True;
}

/*******************************************************************
 api_samr_lookup_names
 ********************************************************************/

static BOOL api_samr_lookup_names(pipes_struct *p)
{
	SAMR_Q_LOOKUP_NAMES q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	memset(&q_u, '\0', sizeof(q_u));

	/* grab the samr lookup names */
	if(!samr_io_q_lookup_names("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_lookup_names: failed to unmarshall SAMR_Q_LOOKUP_NAMES.\n"));
		return False;
	}

	/* construct reply.  always indicate success */
	if(!samr_reply_lookup_names(&q_u, rdata))
		return False;

	return True;
}

/*******************************************************************
 samr_reply_chgpasswd_user
 ********************************************************************/

static BOOL samr_reply_chgpasswd_user(SAMR_Q_CHGPASSWD_USER *q_u,
				prs_struct *rdata)
{
	SAMR_R_CHGPASSWD_USER r_u;
	uint32 status = 0x0;
	fstring user_name;
	fstring wks;

	fstrcpy(user_name, dos_unistrn2(q_u->uni_user_name.buffer, q_u->uni_user_name.uni_str_len));
	fstrcpy(wks      , dos_unistrn2(q_u->uni_dest_host.buffer, q_u->uni_dest_host.uni_str_len));

	DEBUG(5,("samr_chgpasswd_user: user: %s wks: %s\n", user_name, wks));

	if (!pass_oem_change(user_name,
	                     q_u->lm_newpass.pass, q_u->lm_oldhash.hash,
	                     q_u->nt_newpass.pass, q_u->nt_oldhash.hash))
	{
		status = 0xC0000000 | NT_STATUS_WRONG_PASSWORD;
	}

	init_samr_r_chgpasswd_user(&r_u, status);

	/* store the response in the SMB stream */
	if(!samr_io_r_chgpasswd_user("", &r_u, rdata, 0)) {
		DEBUG(0,("samr_reply_chgpasswd_user: Failed to marshall SAMR_R_CHGPASSWD_USER struct.\n" ));
		return False;
	}

	DEBUG(5,("samr_chgpasswd_user: %d\n", __LINE__));
	return True;
}

/*******************************************************************
 api_samr_chgpasswd_user
 ********************************************************************/

static BOOL api_samr_chgpasswd_user(pipes_struct *p)
{
	SAMR_Q_CHGPASSWD_USER q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* unknown 38 command */
	if (!samr_io_q_chgpasswd_user("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_chgpasswd_user: samr_io_q_chgpasswd_user failed to parse RPC packet.\n"));
		return False;
	}

	/* construct reply. */
	if(!samr_reply_chgpasswd_user(&q_u, rdata)) {
		DEBUG(0,("api_samr_chgpasswd_user: samr_reply_chgpasswd_user failed to create reply packet.\n"));
		return False;
	}

	return True;
}


/*******************************************************************
 samr_reply_unknown_38
 ********************************************************************/
static BOOL samr_reply_unknown_38(SAMR_Q_UNKNOWN_38 *q_u, prs_struct *rdata)
{
	SAMR_R_UNKNOWN_38 r_u;

	DEBUG(5,("samr_unknown_38: %d\n", __LINE__));

	init_samr_r_unknown_38(&r_u);

	/* store the response in the SMB stream */
	if(!samr_io_r_unknown_38("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_unknown_38: %d\n", __LINE__));
	return True;
}

/*******************************************************************
 api_samr_unknown_38
 ********************************************************************/
static BOOL api_samr_unknown_38(pipes_struct *p)
{
	SAMR_Q_UNKNOWN_38 q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* unknown 38 command */
	if(!samr_io_q_unknown_38("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_unknown_38(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_lookup_rids
 ********************************************************************/
static BOOL samr_reply_lookup_rids(SAMR_Q_LOOKUP_RIDS *q_u,
				prs_struct *rdata)
{
	fstring group_names[MAX_SAM_ENTRIES];
	uint32  group_attrs[MAX_SAM_ENTRIES];
	uint32 status     = 0;
	int num_gids = q_u->num_gids1;

	SAMR_R_LOOKUP_RIDS r_u;

	DEBUG(5,("samr_reply_lookup_rids: %d\n", __LINE__));

	/* find the policy handle.  open a policy on it. */
	if (status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
	{
		status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	if (status == 0x0)
	{
		int i;
		if (num_gids > MAX_SAM_ENTRIES)
		{
			num_gids = MAX_SAM_ENTRIES;
			DEBUG(5,("samr_reply_lookup_rids: truncating entries to %d\n", num_gids));
		}

		for (i = 0; i < num_gids && status == 0; i++)
		{
			fstrcpy(group_names[i], "dummy group");
			group_attrs[i] = 0x2;
		}
	}

	init_samr_r_lookup_rids(&r_u, num_gids, group_names, group_attrs, status);

	/* store the response in the SMB stream */
	if(!samr_io_r_lookup_rids("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_reply_lookup_rids: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_lookup_rids
 ********************************************************************/
static BOOL api_samr_lookup_rids(pipes_struct *p)
{
	SAMR_Q_LOOKUP_RIDS q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr lookup names */
	if(!samr_io_q_lookup_rids("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_lookup_rids(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 _api_samr_open_user
 ********************************************************************/
static uint32 _api_samr_open_user(POLICY_HND domain_pol, uint32 user_rid, POLICY_HND *user_pol)
{
	SAM_ACCOUNT *sam_pass;
	DOM_SID sid;

	/* find the domain policy handle. */
	if (find_lsa_policy_by_hnd(&domain_pol) == -1)
		return NT_STATUS_INVALID_HANDLE;

	/* get a (unique) handle.  open a policy on it. */
	if (!open_lsa_policy_hnd(user_pol))
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;

	become_root();
	sam_pass = pdb_getsampwrid(user_rid);
	unbecome_root();

	/* check that the RID exists in our domain. */
	if (sam_pass == NULL) {
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_NO_SUCH_USER;
	}
	
	/* Get the domain SID stored in the domain policy */
	if(!get_lsa_policy_samr_sid(&domain_pol, &sid)) {
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_INVALID_HANDLE;
	}

	/* append the user's RID to it */
	if(!sid_append_rid(&sid, user_rid)) {
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_NO_SUCH_USER;
	}

	/* associate the user's SID with the handle. */
	if (!set_lsa_policy_samr_sid(user_pol, &sid)) {
		/* oh, whoops.  don't know what error message to return, here */
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	return NT_STATUS_NO_PROBLEMO;
}

/*******************************************************************
 api_samr_open_user
 ********************************************************************/
static BOOL api_samr_open_user(pipes_struct *p)
{
	SAMR_Q_OPEN_USER q_u;
	SAMR_R_OPEN_USER r_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);

	/* grab the samr unknown 22 */
	if(!samr_io_q_open_user("", &q_u, data, 0))
		return False;

	r_u.status = _api_samr_open_user(q_u.domain_pol, q_u.user_rid, &r_u.user_pol);

	/* store the response in the SMB stream */
	if(!samr_io_r_open_user("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_open_user: %d\n", __LINE__));

	return True;
}

/*************************************************************************
 get_user_info_10
 *************************************************************************/
static BOOL get_user_info_10(SAM_USER_INFO_10 *id10, uint32 user_rid)
{
	SAM_ACCOUNT *sampass;

	if (!pdb_rid_is_user(user_rid))
	{
		DEBUG(4,("RID 0x%x is not a user RID\n", user_rid));
		return False;
	}

	become_root();
	sampass = pdb_getsampwrid(user_rid);
	unbecome_root();

	if (sampass == NULL)
	{
		DEBUG(4,("User 0x%x not found\n", user_rid));
		return False;
	}

	DEBUG(3,("User:[%s]\n", pdb_get_username(sampass)));

	init_sam_user_info10(id10, pdb_get_acct_ctrl(sampass)); 

	return True;
}

/*************************************************************************
 get_user_info_21
 *************************************************************************/
static BOOL get_user_info_21(SAM_USER_INFO_21 *id21, uint32 user_rid)
{
	SAM_ACCOUNT 	*sam_pass;

	if (!pdb_rid_is_user(user_rid))
	{
		DEBUG(4,("RID 0x%x is not a user RID\n", user_rid));
		return False;
	}

	become_root();
	sam_pass = pdb_getsampwrid(user_rid);
	unbecome_root();

	if (sam_pass == NULL)
	{
		DEBUG(4,("User 0x%x not found\n", user_rid));
		return False;
	}

	DEBUG(3,("User:[%s]\n", pdb_get_username(sam_pass)));

	init_sam_user_info21(id21, sam_pass);

	return True;
}

/*******************************************************************
 samr_reply_query_userinfo
 ********************************************************************/
static BOOL samr_reply_query_userinfo(SAMR_Q_QUERY_USERINFO *q_u,
				prs_struct *rdata)
{
	SAMR_R_QUERY_USERINFO r_u;
#if 0
	SAM_USER_INFO_11 id11;
#endif
	SAM_USER_INFO_10 id10;
	SAM_USER_INFO_21 id21;
	void *info = NULL;

	uint32 status = 0x0;
	uint32 rid = 0x0;

	DEBUG(5,("samr_reply_query_userinfo: %d\n", __LINE__));

	/* search for the handle */
	if (status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
	{
		status = NT_STATUS_INVALID_HANDLE;
	}

	/* find the user's rid */
	if (status == 0x0 && (rid = get_lsa_policy_samr_rid(&(q_u->pol))) == 0xffffffff)
	{
		status = NT_STATUS_OBJECT_TYPE_MISMATCH;
	}

	DEBUG(5,("samr_reply_query_userinfo: rid:0x%x\n", rid));

	/* ok!  user info levels (there are lots: see MSDEV help), off we go... */
	if (status == 0x0)
	{
		switch (q_u->switch_value)
		{
			case 0x10:
			{
				info = (void*)&id10;
				status = get_user_info_10(&id10, rid) ? 0 : NT_STATUS_NO_SUCH_USER;
				break;
			}
#if 0
/* whoops - got this wrong.  i think.  or don't understand what's happening. */
			case 0x11:
			{
				NTTIME expire;
				info = (void*)&id11;
				
				expire.low  = 0xffffffff;
				expire.high = 0x7fffffff;

				make_sam_user_info11(&id11, &expire, "BROOKFIELDS$", 0x03ef, 0x201, 0x0080);

				break;
			}
#endif
			case 21:
			{
				info = (void*)&id21;
				status = get_user_info_21(&id21, rid) ? 0 : NT_STATUS_NO_SUCH_USER;
				break;
			}

			default:
			{
				status = NT_STATUS_INVALID_INFO_CLASS;

				break;
			}
		}
	}

	init_samr_r_query_userinfo(&r_u, q_u->switch_value, info, status);

	/* store the response in the SMB stream */
	if(!samr_io_r_query_userinfo("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_reply_query_userinfo: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_query_userinfo
 ********************************************************************/
static BOOL api_samr_query_userinfo(pipes_struct *p)
{
	SAMR_Q_QUERY_USERINFO q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr unknown 24 */
	if(!samr_io_q_query_userinfo("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_query_userinfo(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 samr_reply_query_usergroups
 ********************************************************************/
static BOOL samr_reply_query_usergroups(SAMR_Q_QUERY_USERGROUPS *q_u,
				prs_struct *rdata)
{
	SAMR_R_QUERY_USERGROUPS r_u;
	uint32 status = 0x0;

	SAM_ACCOUNT *sam_pass;
	DOM_GID *gids = NULL;
	int num_groups = 0;
	uint32 rid;

	DEBUG(5,("samr_query_usergroups: %d\n", __LINE__));

	/* find the policy handle.  open a policy on it. */
	if (status == 0x0 && (find_lsa_policy_by_hnd(&(q_u->pol)) == -1))
	{
		status = 0xC0000000 | NT_STATUS_INVALID_HANDLE;
	}

	/* find the user's rid */
	if (status == 0x0 && (rid = get_lsa_policy_samr_rid(&(q_u->pol))) == 0xffffffff)
	{
		status = NT_STATUS_OBJECT_TYPE_MISMATCH;
	}

	if (status == 0x0)
	{
		become_root();
		sam_pass = pdb_getsampwrid(rid);
		unbecome_root();

		if (sam_pass == NULL)
		{
			status = 0xC0000000 | NT_STATUS_NO_SUCH_USER;
		}
	}

	if (status == 0x0)
	{
		pstring groups;
		get_domain_user_groups(groups, pdb_get_username(sam_pass));
                gids = NULL;
		num_groups = make_dom_gids(groups, &gids);
	}

	/* construct the response.  lkclXXXX: gids are not copied! */
	init_samr_r_query_usergroups(&r_u, num_groups, gids, status);

	/* store the response in the SMB stream */
	if(!samr_io_r_query_usergroups("", &r_u, rdata, 0)) {
		if (gids)
			free((char *)gids);
		return False;
	}

	if (gids)
		free((char *)gids);

	DEBUG(5,("samr_query_usergroups: %d\n", __LINE__));
	
	return True;
}

/*******************************************************************
 api_samr_query_usergroups
 ********************************************************************/
static BOOL api_samr_query_usergroups(pipes_struct *p)
{
	SAMR_Q_QUERY_USERGROUPS q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr unknown 32 */
	if(!samr_io_q_query_usergroups("", &q_u, data, 0))
		return False;

	/* construct reply. */
	if(!samr_reply_query_usergroups(&q_u, rdata))
		return False;

	return True;
}


/*******************************************************************
 api_samr_query_dom_info
 ********************************************************************/
static BOOL api_samr_query_dom_info(pipes_struct *p)
{
	SAMR_Q_QUERY_DOMAIN_INFO q_u;
	SAMR_R_QUERY_DOMAIN_INFO r_u;
	SAM_UNK_CTR ctr;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	uint16 switch_value = 0x0;
	uint32 status = 0x0;

	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);
	ZERO_STRUCT(ctr);

	DEBUG(5,("api_samr_query_dom_info: %d\n", __LINE__));

	/* grab the samr unknown 8 command */
	if(!samr_io_q_query_dom_info("", &q_u, data, 0))
		return False;

	/* find the policy handle.  open a policy on it. */
	if (find_lsa_policy_by_hnd(&q_u.domain_pol) == -1) {
		status = NT_STATUS_INVALID_HANDLE;
		DEBUG(5,("api_samr_query_dom_info: invalid handle\n"));
	}

	if (status == 0x0) {
		switch (q_u.switch_value) {
			case 0x01:
				switch_value = 0x1;
				init_unk_info1(&ctr.info.inf1);
				break;
			case 0x02:
				switch_value = 0x2;
				init_unk_info2(&ctr.info.inf2, global_myworkgroup, global_myname);
				break;
			case 0x03:
				switch_value = 0x3;
				init_unk_info3(&ctr.info.inf3);
				break;
			case 0x06:
				switch_value = 0x6;
				init_unk_info6(&ctr.info.inf6);
				break;
			case 0x07:
				switch_value = 0x7;
				init_unk_info7(&ctr.info.inf7);
				break;
			case 0x0c:
				switch_value = 0xc;
				init_unk_info12(&ctr.info.inf12);
				break;
			default:
				status = NT_STATUS_INVALID_INFO_CLASS;
				break;
		}
	}

	init_samr_r_query_dom_info(&r_u, switch_value, &ctr, status);

	/* store the response in the SMB stream */
	if(!samr_io_r_query_dom_info("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("api_samr_query_dom_info: %d\n", __LINE__));

	return True;
}


/*******************************************************************
 _api_samr_create_user
 ********************************************************************/
static BOOL _api_samr_create_user(POLICY_HND dom_pol, UNISTR2 user_account, uint32 acb_info, uint32 access_mask,
				  POLICY_HND *user_pol, uint32 *unknown0, uint32 *user_rid)
{
	SAM_ACCOUNT *sam_pass;
	fstring mach_acct;
	pstring err_str;
	pstring msg_str;
	int local_flags=0;
	DOM_SID sid;
	
	/* find the policy handle.  open a policy on it. */
	if (find_lsa_policy_by_hnd(&dom_pol) == -1)
		return NT_STATUS_INVALID_HANDLE;

	/* find the machine account: tell the caller if it exists.
	   lkclXXXX i have *no* idea if this is a problem or not
	   or even if you are supposed to construct a different
	   reply if the account already exists...
	 */

	fstrcpy(mach_acct, dos_unistrn2(user_account.buffer, user_account.uni_str_len));
	strlower(mach_acct);

	become_root();
	sam_pass = pdb_getsampwnam(mach_acct);
	unbecome_root();
	if (sam_pass != NULL) 
	{
		/* machine account exists: say so */
		return NT_STATUS_USER_EXISTS;
	}

	/* get a (unique) handle.  open a policy on it. */
	if (!open_lsa_policy_hnd(user_pol))
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;

	local_flags=LOCAL_ADD_USER|LOCAL_DISABLE_USER|LOCAL_SET_NO_PASSWORD;
	local_flags|= (acb_info & ACB_WSTRUST) ? LOCAL_TRUST_ACCOUNT:0;

	/*
	 * NB. VERY IMPORTANT ! This call must be done as the current pipe user,
	 * *NOT* surrounded by a become_root()/unbecome_root() call. This ensures
	 * that only people with write access to the smbpasswd file will be able
	 * to create a user. JRA.
	 */

	/* add the user in the /etc/passwd file or the unix authority system */
	if (lp_adduser_script())
		smb_create_user(mach_acct,NULL);

	/* add the user in the smbpasswd file or the Samba authority database */
	if (!local_password_change(mach_acct, local_flags, NULL, err_str, 
	     sizeof(err_str), msg_str, sizeof(msg_str))) 
	{
		DEBUG(0, ("%s\n", err_str));
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_ACCESS_DENIED;
	}

	become_root();
	sam_pass = pdb_getsampwnam(mach_acct);
	unbecome_root();
	if (sam_pass == NULL) {
		/* account doesn't exist: say so */
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_ACCESS_DENIED;
	}

	/* Get the domain SID stored in the domain policy */
	if(!get_lsa_policy_samr_sid(&dom_pol, &sid)) {
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_INVALID_HANDLE;
	}

	/* append the user's RID to it */
	if(!sid_append_rid(&sid, sam_pass->user_rid)) {
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_NO_SUCH_USER;
	}

	/* associate the RID with the (unique) handle. */
	if (!set_lsa_policy_samr_sid(user_pol, &sid)) {
		/* oh, whoops.  don't know what error message to return, here */
		close_lsa_policy_hnd(user_pol);
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	*unknown0 = 0x000703ff;
	*user_rid = pdb_get_user_rid(sam_pass);

	return NT_STATUS_NO_PROBLEMO;
}

/*******************************************************************
 api_samr_create_user
 ********************************************************************/
static BOOL api_samr_create_user(pipes_struct *p)
{
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	SAMR_Q_CREATE_USER q_u;
	SAMR_R_CREATE_USER r_u;

	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);

	/* grab the samr create user */
	if (!samr_io_q_create_user("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_create_user: Unable to unmarshall SAMR_Q_CREATE_USER.\n"));
		return False;
	}

	r_u.status=_api_samr_create_user(q_u.pol, q_u.uni_mach_acct, q_u.acb_info, q_u.access_mask,
					&r_u.pol, &r_u.unknown_0, &r_u.user_rid);

	/* store the response in the SMB stream */
	if(!samr_io_r_create_user("", &r_u, rdata, 0)) {
		DEBUG(0,("api_samr_create_user: Unable to marshall SAMR_R_CREATE_USER.\n"));
		return False;
	}

	return True;
}


/*******************************************************************
 samr_reply_connect_anon
 ********************************************************************/
static BOOL samr_reply_connect_anon(SAMR_Q_CONNECT_ANON *q_u, prs_struct *rdata)
{
	SAMR_R_CONNECT_ANON r_u;
	BOOL pol_open = False;

	/* set up the SAMR connect_anon response */

	r_u.status = 0x0;
	/* get a (unique) handle.  open a policy on it. */
	if (r_u.status == 0x0 && !(pol_open = open_lsa_policy_hnd(&(r_u.connect_pol))))
	{
		r_u.status = 0xC0000000 | NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	/* associate the domain SID with the (unique) handle. */
	if (r_u.status == 0x0 && !set_lsa_policy_samr_pol_status(&(r_u.connect_pol), q_u->unknown_0))
	{
		/* oh, whoops.  don't know what error message to return, here */
		r_u.status = 0xC0000000 | NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	if (r_u.status != 0 && pol_open)
	{
		close_lsa_policy_hnd(&(r_u.connect_pol));
	}

	DEBUG(5,("samr_connect_anon: %d\n", __LINE__));

	/* store the response in the SMB stream */
	if(!samr_io_r_connect_anon("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_connect_anon: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_connect_anon
 ********************************************************************/
static BOOL api_samr_connect_anon(pipes_struct *p)
{
	SAMR_Q_CONNECT_ANON q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open policy */
	if(!samr_io_q_connect_anon("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_connect_anon(&q_u, rdata))
		return False;

	return True;
}

/*******************************************************************
 samr_reply_connect
 ********************************************************************/
static BOOL samr_reply_connect(SAMR_Q_CONNECT *q_u, prs_struct *rdata)
{
	SAMR_R_CONNECT r_u;
	BOOL pol_open = False;

	/* set up the SAMR connect response */

	r_u.status = 0x0;
	/* get a (unique) handle.  open a policy on it. */
	if (r_u.status == 0x0 && !(pol_open = open_lsa_policy_hnd(&(r_u.connect_pol))))
	{
		r_u.status = 0xC0000000 | NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	/* associate the domain SID with the (unique) handle. */
	if (r_u.status == 0x0 && 
	    !set_lsa_policy_samr_pol_status(&(r_u.connect_pol), 
					    q_u->access_mask))
	{
		/* oh, whoops.  don't know what error message to return, here */
		r_u.status = 0xC0000000 | NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	if (r_u.status != 0 && pol_open)
	{
		close_lsa_policy_hnd(&(r_u.connect_pol));
	}

	DEBUG(5,("samr_connect: %d\n", __LINE__));

	/* store the response in the SMB stream */
	if(!samr_io_r_connect("", &r_u, rdata, 0))
		return False;

	DEBUG(5,("samr_connect: %d\n", __LINE__));

	return True;
}

/*******************************************************************
 api_samr_connect
 ********************************************************************/
static BOOL api_samr_connect(pipes_struct *p)
{
	SAMR_Q_CONNECT q_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	/* grab the samr open policy */
	if(!samr_io_q_connect("", &q_u, data, 0))
		return False;

	/* construct reply.  always indicate success */
	if(!samr_reply_connect(&q_u, rdata))
		return False;

	return True;
}

  
/**********************************************************************
 api_samr_lookup_domain
 **********************************************************************/
static BOOL api_samr_lookup_domain(pipes_struct *p)
{
	SAMR_Q_LOOKUP_DOMAIN q_u;
	SAMR_R_LOOKUP_DOMAIN r_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;
  
	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);

	if(!samr_io_q_lookup_domain("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_lookup_domain: Unable to unmarshall SAMR_Q_LOOKUP_DOMAIN.\n"));
		return False;
	}
	
	r_u.status = 0x0;

	if (find_lsa_policy_by_hnd(&q_u.connect_pol) == -1){
		r_u.status = NT_STATUS_INVALID_HANDLE;
		DEBUG(5,("api_samr_lookup_domain: invalid handle\n"));
	}
  
	/* assume the domain name sent is our global_myname and 
	   send global_sam_sid */
	init_samr_r_lookup_domain(&r_u, &global_sam_sid, r_u.status);
  
	if(!samr_io_r_lookup_domain("", &r_u, rdata, 0)){
		DEBUG(0,("api_samr_lookup_domain: Unable to marshall SAMR_R_LOOKUP_DOMAIN.\n"));
		return False;
	}
	
	return True;
}

/**********************************************************************
 api_samr_enum_domains
 **********************************************************************/
static BOOL api_samr_enum_domains(pipes_struct *p)
{
	SAMR_Q_ENUM_DOMAINS q_u;
	SAMR_R_ENUM_DOMAINS r_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;
  
	fstring dom[2];

	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);

	fstrcpy(dom[0],global_myworkgroup);
	fstrcpy(dom[1],"Builtin");

	if(!samr_io_q_enum_domains("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_enum_domains: Unable to unmarshall SAMR_Q_ENUM_DOMAINS.\n"));
		return False;
	}

	r_u.status = NT_STATUS_NO_PROBLEMO;
   
	init_samr_r_enum_domains(&r_u, q_u.start_idx, dom, 2); 

	if(!samr_io_r_enum_domains("", &r_u, rdata, 0)) {
		DEBUG(0,("api_samr_enum_domains: Unable to marshall SAMR_R_ENUM_DOMAINS.\n"));
		free(r_u.sam);
		free(r_u.uni_dom_name);
		return False;
	}

	free(r_u.sam);
	free(r_u.uni_dom_name);
	
	return True;
}


/*******************************************************************
 api_samr_open_alias
 ********************************************************************/
static uint32 _api_samr_open_alias(POLICY_HND domain_pol, uint32 alias_rid, POLICY_HND *alias_pol)
{
	DOM_SID sid;
	
	/* get the domain policy. */
	if (find_lsa_policy_by_hnd(&domain_pol) == -1)
		return NT_STATUS_INVALID_HANDLE;

	/* get a (unique) handle.  open a policy on it. */
	if (!open_lsa_policy_hnd(alias_pol))
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;

	/* Get the domain SID stored in the domain policy */
	if(!get_lsa_policy_samr_sid(&domain_pol, &sid)) {
		close_lsa_policy_hnd(alias_pol);
		return NT_STATUS_INVALID_HANDLE;
	}

	/* append the alias' RID to it */
	if(!sid_append_rid(&sid, alias_rid)) {
		close_lsa_policy_hnd(alias_pol);
		return NT_STATUS_NO_SUCH_USER;
	}

	/* associate a RID with the (unique) handle. */
	if (!set_lsa_policy_samr_sid(alias_pol, &sid)) {
		/* oh, whoops.  don't know what error message to return, here */
		close_lsa_policy_hnd(alias_pol);
		return NT_STATUS_OBJECT_NAME_NOT_FOUND;
	}

	return NT_STATUS_NO_PROBLEMO;
}

/*******************************************************************
 api_samr_open_alias
 ********************************************************************/
static BOOL api_samr_open_alias(pipes_struct *p)
{
	SAMR_Q_OPEN_ALIAS q_u;
	SAMR_R_OPEN_ALIAS r_u;
	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);

	/* grab the samr open policy */
	if(!samr_io_q_open_alias("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_open_alias: Unable to unmarshall SAMR_Q_OPEN_ALIAS.\n"));
		return False;
	}

	r_u.status=_api_samr_open_alias(q_u.dom_pol, q_u.rid_alias, &r_u.pol);

	/* store the response in the SMB stream */
	if(!samr_io_r_open_alias("", &r_u, rdata, 0)) {
		DEBUG(0,("api_samr_open_alias: Unable to marshall SAMR_R_OPEN_ALIAS.\n"));
		return False;
	}
	
	return True;
}

/*******************************************************************
 set_user_info_10
 ********************************************************************/
static BOOL set_user_info_10(const SAM_USER_INFO_10 *id10, uint32 rid)
{
	SAM_ACCOUNT *pwd = pdb_getsampwrid(rid);

	if (id10 == NULL) {
		DEBUG(5, ("set_user_info_10: NULL id10\n"));
		return False;
	}

	if (pwd == NULL)
		return False;

	pdb_set_acct_ctrl(pwd, id10->acb_info);

	if(!pdb_update_sam_account(pwd, True))
		return False;

	return True;
}

/*******************************************************************
 set_user_info_12
 ********************************************************************/
static BOOL set_user_info_12(SAM_USER_INFO_12 *id12, uint32 rid)
{
	SAM_ACCOUNT *pwd = pdb_getsampwrid(rid);

	if (pwd == NULL)
		return False;

	if (id12 == NULL) {
		DEBUG(2, ("set_user_info_12: id12 is NULL\n"));
		return False;
	}

	pdb_set_lanman_passwd (pwd, id12->lm_pwd); 
	pdb_set_nt_passwd     (pwd, id12->nt_pwd); 

	if(!pdb_update_sam_account(pwd, True))
		return False;

	return True;
}

/*******************************************************************
 set_user_info_21
 ********************************************************************/
static BOOL set_user_info_21 (SAM_USER_INFO_21 *id21, uint32 rid)
{
	SAM_ACCOUNT *pwd = pdb_getsampwrid(rid);
	SAM_ACCOUNT new_pwd;

	if (id21 == NULL) {
		DEBUG(5, ("set_user_info_21: NULL id21\n"));
		return False;
	}

	if (pwd == NULL)
		return False;

	/* we make a copy so that we can modify stuff */	
	copy_sam_passwd(&new_pwd, pwd);
	copy_id21_to_sam_passwd(&new_pwd, id21);
	
	/*
	 * The funny part about the previous two calls is 
	 * that pwd still has the password hashes from the 
	 * passdb entry.  These have not been updated from
	 * id21.  I don't know if they need to be set.    --jerry
	 */

	/* write the change out */
	if(!pdb_update_sam_account(&new_pwd, True))
		return False;
	
	return True;
}

/*******************************************************************
 set_user_info_23
 ********************************************************************/
static BOOL set_user_info_23(SAM_USER_INFO_23 *id23, uint32 rid)
{
	SAM_ACCOUNT *pwd = pdb_getsampwrid(rid);
	SAM_ACCOUNT new_pwd;
	BYTE nt_hash[16];
	BYTE lm_hash[16];
	pstring buf;
	uint32 len;

	if (id23 == NULL) {
		DEBUG(5, ("set_user_info_23: NULL id23\n"));
		return False;
	}

	if (pwd == NULL)
		return False;

	copy_sam_passwd(&new_pwd, pwd);
	copy_id23_to_sam_passwd(&new_pwd, id23);

	if (!decode_pw_buffer((char*)id23->pass, buf, 256, &len))
		return False;

	nt_lm_owf_gen(buf, nt_hash, lm_hash);

	pdb_set_lanman_passwd (&new_pwd, lm_hash);
	pdb_set_nt_passwd     (&new_pwd, nt_hash);

	/* update the UNIX password */
	if (lp_unix_password_sync())
		if(!chgpasswd(pdb_get_username(&new_pwd), "", buf, True))
			return False;

	memset(buf, 0, sizeof(buf));

	if(!pdb_update_sam_account(&new_pwd, True))
		return False;
	
	return True;
}

/*******************************************************************
 set_user_info_24
 ********************************************************************/
static BOOL set_user_info_24(const SAM_USER_INFO_24 *id24, uint32 rid)
{
	SAM_ACCOUNT *pwd = pdb_getsampwrid(rid);
	uchar nt_hash[16];
	uchar lm_hash[16];
	uint32 len;
	pstring buf;

	if (pwd == NULL)
		return False;

	memset(buf, 0, sizeof(buf));

	if (!decode_pw_buffer((char*)id24->pass, buf, 256, &len))
		return False;

	DEBUG(0,("set_user_info_24:nt_lm_owf_gen\n"));

	nt_lm_owf_gen(buf, nt_hash, lm_hash);

	pdb_set_lanman_passwd (pwd, lm_hash);
	pdb_set_nt_passwd     (pwd, nt_hash);

	/* update the UNIX password */
	if (lp_unix_password_sync())
		if(!chgpasswd(pdb_get_username(pwd), "", buf, True))
			return False;

	memset(buf, 0, sizeof(buf));
	
	DEBUG(0,("set_user_info_24: pdb_update_sam_account()\n"));

	/* update the SAMBA password */
	if(!pdb_update_sam_account(pwd, True))
		return False;

	return True;
}

/*******************************************************************
 samr_reply_set_userinfo
 ********************************************************************/
static uint32 _samr_set_userinfo(POLICY_HND *pol, uint16 switch_value, 
                                 SAM_USERINFO_CTR *ctr, pipes_struct *p)
{
	uint32 rid = 0x0;
	DOM_SID sid;
	struct current_user user;
	SAM_ACCOUNT *sam_pass;
	unsigned char sess_key[16];

	DEBUG(5, ("_samr_set_userinfo: %d\n", __LINE__));

	if (p->ntlmssp_auth_validated) 
	{
		memcpy(&user, &p->pipe_user, sizeof(user));
	} 
	else 
	{
		extern struct current_user current_user;
		memcpy(&user, &current_user, sizeof(user));
	}

	/* search for the handle */
	if (find_lsa_policy_by_hnd(pol) == -1)
		return NT_STATUS_INVALID_HANDLE;

	/* find the policy handle.  open a policy on it. */
	if (!get_lsa_policy_samr_sid(pol, &sid))
		return NT_STATUS_INVALID_HANDLE;

	sid_split_rid(&sid, &rid);

	DEBUG(5, ("_samr_set_userinfo: rid:0x%x, level:%d\n", rid, switch_value));

	if (ctr == NULL) {
		DEBUG(5, ("_samr_set_userinfo: NULL info level\n"));
		return NT_STATUS_INVALID_INFO_CLASS;
	}


	/* 
	 * We need the NT hash of the user who is changing the user's password.
	 * This NT hash is used to generate a "user session key"
	 * This "user session key" is in turn used to encrypt/decrypt the user's password.
	 */

	become_root();
	sam_pass = pdb_getsampwuid(user.uid);
	unbecome_root();
	if(sam_pass == NULL) {
		DEBUG(0,("_samr_set_userinfo: Unable to get passdb entry for uid %u\n", 
		        (unsigned int)pdb_get_uid(sam_pass) ));
		return NT_STATUS_ACCESS_DENIED;
	}
		
	memset(sess_key, '\0', 16);
	mdfour(sess_key, pdb_get_nt_passwd(sam_pass), 16);

	/* ok!  user info levels (lots: see MSDEV help), off we go... */
	switch (switch_value) {
		case 0x12:
			if (!set_user_info_12(ctr->info.id12, rid))
				return NT_STATUS_ACCESS_DENIED;
			break;

		case 24:
			SamOEMhash(ctr->info.id24->pass, sess_key, 1);
			if (!set_user_info_24(ctr->info.id24, rid))
				return NT_STATUS_ACCESS_DENIED;
			break;

		case 23:
			SamOEMhash(ctr->info.id23->pass, sess_key, 1);
			if (!set_user_info_23(ctr->info.id23, rid))
				return NT_STATUS_ACCESS_DENIED;
			break;

		default:
			return NT_STATUS_INVALID_INFO_CLASS;
	}

	return NT_STATUS_NOPROBLEMO;
}

/*******************************************************************
 api_samr_set_userinfo
 ********************************************************************/
static BOOL api_samr_set_userinfo(pipes_struct *p)
{
	SAMR_Q_SET_USERINFO q_u;
	SAMR_R_SET_USERINFO r_u;

	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	SAM_USERINFO_CTR ctr;

	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);

	q_u.ctr = &ctr;

	if (!samr_io_q_set_userinfo("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_set_userinfo: Unable to unmarshall SAMR_Q_SET_USERINFO.\n"));
		return False;
	}

	r_u.status = _samr_set_userinfo(&q_u.pol, q_u.switch_value, &ctr, p);

	free_samr_q_set_userinfo(&q_u);
	
	if(!samr_io_r_set_userinfo("", &r_u, rdata, 0)) {
		DEBUG(0,("api_samr_set_userinfo: Unable to marshall SAMR_R_SET_USERINFO.\n"));
		return False;
	}

	return True;
}

/*******************************************************************
 samr_reply_set_userinfo2
 ********************************************************************/
static uint32 _samr_set_userinfo2(POLICY_HND *pol, uint16 switch_value, SAM_USERINFO_CTR *ctr)
{
	DOM_SID sid;
	uint32 rid = 0x0;

	DEBUG(5, ("samr_reply_set_userinfo2: %d\n", __LINE__));

	/* search for the handle */
	if (find_lsa_policy_by_hnd(pol) == -1)
		return NT_STATUS_INVALID_HANDLE;

	/* find the policy handle.  open a policy on it. */
	if (!get_lsa_policy_samr_sid(pol, &sid))
		return NT_STATUS_INVALID_HANDLE;

	sid_split_rid(&sid, &rid);

	DEBUG(5, ("samr_reply_set_userinfo2: rid:0x%x\n", rid));

	if (ctr == NULL) {
		DEBUG(5, ("samr_reply_set_userinfo2: NULL info level\n"));
		return NT_STATUS_INVALID_INFO_CLASS;
	}

	ctr->switch_value = switch_value;

	/* ok!  user info levels (lots: see MSDEV help), off we go... */
	switch (switch_value) {
		case 21:
			if (!set_user_info_21(ctr->info.id21, rid))
				return NT_STATUS_ACCESS_DENIED;
			break;
		case 16:
			if (!set_user_info_10(ctr->info.id10, rid))
				return NT_STATUS_ACCESS_DENIED;
			break;
		default:
			return NT_STATUS_INVALID_INFO_CLASS;
	}

	return NT_STATUS_NOPROBLEMO;
}

/*******************************************************************
 api_samr_set_userinfo2
 ********************************************************************/
static BOOL api_samr_set_userinfo2(pipes_struct *p)
{
	SAMR_Q_SET_USERINFO2 q_u;
	SAMR_R_SET_USERINFO2 r_u;
	SAM_USERINFO_CTR ctr;

	prs_struct *data = &p->in_data.data;
	prs_struct *rdata = &p->out_data.rdata;

	ZERO_STRUCT(q_u);
	ZERO_STRUCT(r_u);

	q_u.ctr = &ctr;

	if (!samr_io_q_set_userinfo2("", &q_u, data, 0)) {
		DEBUG(0,("api_samr_set_userinfo2: Unable to unmarshall SAMR_Q_SET_USERINFO2.\n"));
		return False;
	}

	r_u.status = _samr_set_userinfo2(&q_u.pol, q_u.switch_value, &ctr);

	free_samr_q_set_userinfo2(&q_u);

	if(!samr_io_r_set_userinfo2("", &r_u, rdata, 0)) {
		DEBUG(0,("api_samr_set_userinfo2: Unable to marshall SAMR_R_SET_USERINFO2.\n"));
		return False;
	}

	return True;
}


/*******************************************************************
 array of \PIPE\samr operations
 ********************************************************************/
static struct api_struct api_samr_cmds [] =
{
	{ "SAMR_CLOSE_HND"        , SAMR_CLOSE_HND        , api_samr_close_hnd        },
	{ "SAMR_CONNECT"          , SAMR_CONNECT          , api_samr_connect          },
	{ "SAMR_CONNECT_ANON"     , SAMR_CONNECT_ANON     , api_samr_connect_anon     },
	{ "SAMR_ENUM_DOM_USERS"   , SAMR_ENUM_DOM_USERS   , api_samr_enum_dom_users   },
	{ "SAMR_ENUM_DOM_GROUPS"  , SAMR_ENUM_DOM_GROUPS  , api_samr_enum_dom_groups  },
	{ "SAMR_ENUM_DOM_ALIASES" , SAMR_ENUM_DOM_ALIASES , api_samr_enum_dom_aliases },
	{ "SAMR_LOOKUP_IDS"       , SAMR_LOOKUP_IDS       , api_samr_lookup_ids       },
	{ "SAMR_LOOKUP_NAMES"     , SAMR_LOOKUP_NAMES     , api_samr_lookup_names     },
	{ "SAMR_OPEN_USER"        , SAMR_OPEN_USER        , api_samr_open_user        },
	{ "SAMR_QUERY_USERINFO"   , SAMR_QUERY_USERINFO   , api_samr_query_userinfo   },
	{ "SAMR_QUERY_DOMAIN_INFO", SAMR_QUERY_DOMAIN_INFO, api_samr_query_dom_info   },
	{ "SAMR_QUERY_USERGROUPS" , SAMR_QUERY_USERGROUPS , api_samr_query_usergroups },
	{ "SAMR_QUERY_DISPINFO"   , SAMR_QUERY_DISPINFO   , api_samr_query_dispinfo   },
	{ "SAMR_QUERY_ALIASINFO"  , SAMR_QUERY_ALIASINFO  , api_samr_query_aliasinfo  },
	{ "SAMR_CREATE_USER"      , SAMR_CREATE_USER      , api_samr_create_user      },
	{ "SAMR_LOOKUP_RIDS"      , SAMR_LOOKUP_RIDS      , api_samr_lookup_rids      },
	{ "SAMR_UNKNOWN_38"       , SAMR_UNKNOWN_38       , api_samr_unknown_38       },
	{ "SAMR_CHGPASSWD_USER"   , SAMR_CHGPASSWD_USER   , api_samr_chgpasswd_user   },
	{ "SAMR_OPEN_ALIAS"       , SAMR_OPEN_ALIAS       , api_samr_open_alias       },
	{ "SAMR_OPEN_DOMAIN"      , SAMR_OPEN_DOMAIN      , api_samr_open_domain      },
	{ "SAMR_UNKNOWN_3"        , SAMR_UNKNOWN_3        , api_samr_unknown_3        },
	{ "SAMR_UNKNOWN_2C"       , SAMR_UNKNOWN_2C       , api_samr_unknown_2c       },
	{ "SAMR_LOOKUP_DOMAIN"    , SAMR_LOOKUP_DOMAIN    , api_samr_lookup_domain    },
	{ "SAMR_ENUM_DOMAINS"     , SAMR_ENUM_DOMAINS     , api_samr_enum_domains     },
	{ "SAMR_SET_USERINFO"     , SAMR_SET_USERINFO     , api_samr_set_userinfo     },
	{ "SAMR_SET_USERINFO2"    , SAMR_SET_USERINFO2    , api_samr_set_userinfo2    },
	{ NULL                    , 0                     , NULL                      }
};

/*******************************************************************
 receives a samr pipe and responds.
 ********************************************************************/
BOOL api_samr_rpc(pipes_struct *p)
{
	return api_rpcTNP(p, "api_samr_rpc", api_samr_cmds);
}
#undef OLD_NTDOMAIN
