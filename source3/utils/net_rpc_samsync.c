/* 
   Unix SMB/CIFS implementation.
   dump the remote SAM using rpc samsync operations

   Copyright (C) Andrew Tridgell 2002
   Copyright (C) Tim Potter 2001,2002

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
#include "../utils/net.h"

static void display_group_mem_info(uint32 rid, SAM_GROUP_MEM_INFO *g)
{
	int i;
	d_printf("Group mem %u: ", rid);
	for (i=0;i<g->num_members;i++) {
		d_printf("%u ", g->rids[i]);
	}
	d_printf("\n");
}

static void display_alias_info(uint32 rid, SAM_ALIAS_INFO *a)
{
	d_printf("Alias '%s' ", unistr2_static(&a->uni_als_name));
	d_printf("desc='%s' rid=%u\n", unistr2_static(&a->uni_als_desc), a->als_rid);
}

static void display_alias_mem(uint32 rid, SAM_ALIAS_MEM_INFO *a)
{
	int i;
	d_printf("Alias rid %u: ", rid);
	for (i=0;i<a->num_members;i++) {
		d_printf("%s ", sid_string_static(&a->sids[i].sid));
	}
	d_printf("\n");
}

static void display_account_info(uint32 rid, SAM_ACCOUNT_INFO *a)
{
	fstring hex_nt_passwd, hex_lm_passwd;
	uchar lm_passwd[16], nt_passwd[16];

	/* Decode hashes from password hash */
	sam_pwd_hash(a->user_rid, a->pass.buf_lm_pwd, lm_passwd, 0);
	sam_pwd_hash(a->user_rid, a->pass.buf_nt_pwd, nt_passwd, 0);
	
	/* Encode as strings */
	smbpasswd_sethexpwd(hex_lm_passwd, lm_passwd, a->acb_info);
	smbpasswd_sethexpwd(hex_nt_passwd, nt_passwd, a->acb_info);
	
	printf("%s:%d:%s:%s:%s:LCT-0\n", unistr2_static(&a->uni_acct_name),
	       a->user_rid, hex_lm_passwd, hex_nt_passwd,
	       smbpasswd_encode_acb_info(a->acb_info));
}

static void display_domain_info(SAM_DOMAIN_INFO *a)
{
	d_printf("Domain name: %s\n", unistr2_static(&a->uni_dom_name));
}

static void display_group_info(uint32 rid, SAM_GROUP_INFO *a)
{
	d_printf("Group '%s' ", unistr2_static(&a->uni_grp_name));
	d_printf("desc='%s', rid=%u\n", unistr2_static(&a->uni_grp_desc), rid);
}

static void display_sam_entry(SAM_DELTA_HDR *hdr_delta, SAM_DELTA_CTR *delta)
{
	switch (hdr_delta->type) {
	case SAM_DELTA_ACCOUNT_INFO:
		display_account_info(hdr_delta->target_rid, &delta->account_info);
		break;
	case SAM_DELTA_GROUP_MEM:
		display_group_mem_info(hdr_delta->target_rid, &delta->grp_mem_info);
		break;
	case SAM_DELTA_ALIAS_INFO:
		display_alias_info(hdr_delta->target_rid, &delta->alias_info);
		break;
	case SAM_DELTA_ALIAS_MEM:
		display_alias_mem(hdr_delta->target_rid, &delta->als_mem_info);
		break;
	case SAM_DELTA_DOMAIN_INFO:
		display_domain_info(&delta->domain_info);
		break;
	case SAM_DELTA_GROUP_INFO:
		display_group_info(hdr_delta->target_rid, &delta->group_info);
		break;
	default:
		d_printf("Unknown delta record type %d\n", hdr_delta->type);
		break;
	}
}


static void dump_database(struct cli_state *cli, unsigned db_type, DOM_CRED *ret_creds)
{
	unsigned last_rid = -1;
        NTSTATUS result;
	int i;
        TALLOC_CTX *mem_ctx;
        SAM_DELTA_HDR *hdr_deltas;
        SAM_DELTA_CTR *deltas;
        uint32 num_deltas;

	if (!(mem_ctx = talloc_init())) {
		return;
	}

	d_printf("Dumping database %u\n", db_type);

	do {
		result = cli_netlogon_sam_sync(cli, mem_ctx, ret_creds, db_type, last_rid+1,
					       &num_deltas, &hdr_deltas, &deltas);
		clnt_deal_with_creds(cli->sess_key, &(cli->clnt_cred), ret_creds);
		last_rid = 0;
                for (i = 0; i < num_deltas; i++) {
			display_sam_entry(&hdr_deltas[i], &deltas[i]);
			last_rid = hdr_deltas[i].target_rid;
                }
	} while (last_rid && NT_STATUS_EQUAL(result, STATUS_MORE_ENTRIES));

	talloc_destroy(mem_ctx);
}

/* dump sam database via samsync rpc calls */
int rpc_samdump(int argc, const char **argv)
{
        NTSTATUS result;
	struct cli_state *cli = NULL;
	uchar trust_password[16];
	DOM_CRED ret_creds;
	uint32 neg_flags = 0x000001ff;


	ZERO_STRUCT(ret_creds);

	/* Connect to remote machine */
	if (!(cli = net_make_ipc_connection(NET_FLAGS_ANONYMOUS | NET_FLAGS_PDC))) {
		return 1;
	}

	if (!cli_nt_session_open(cli, PIPE_NETLOGON)) {
		DEBUG(0,("Error connecting to NETLOGON pipe\n"));
		goto fail;
	}

	if (!secrets_fetch_trust_account_password(lp_workgroup(), trust_password, NULL)) {
		d_printf("Could not retrieve domain trust secret");
		goto fail;
	}
	
	result = cli_nt_setup_creds(cli, SEC_CHAN_BDC,  trust_password, &neg_flags, 2);
	if (!NT_STATUS_IS_OK(result)) {
		d_printf("Failed to setup BDC creds\n");
		goto fail;
	}

	dump_database(cli, SAM_DATABASE_DOMAIN, &ret_creds);
	dump_database(cli, SAM_DATABASE_BUILTIN, &ret_creds);
	dump_database(cli, SAM_DATABASE_PRIVS, &ret_creds);

	cli_nt_session_close(cli);
        
        return 0;

fail:
	if (cli) {
		cli_nt_session_close(cli);
	}
	return -1;
}

/* Convert a SAM_ACCOUNT_DELTA to a SAM_ACCOUNT. */

static NTSTATUS
sam_account_from_delta(SAM_ACCOUNT *account, SAM_ACCOUNT_INFO *delta)
{
	DOM_SID sid;
	fstring s;

	/* Username, fullname, home dir, dir drive, logon script, acct
	   desc, workstations, profile. */

	unistr2_to_ascii(s, &delta->uni_acct_name, sizeof(s) - 1);
	pdb_set_nt_username(account, s);

	/* Unix username is the same - for sainity */
	pdb_set_username(account, s);

	unistr2_to_ascii(s, &delta->uni_full_name, sizeof(s) - 1);
	pdb_set_fullname(account, s);

	unistr2_to_ascii(s, &delta->uni_home_dir, sizeof(s) - 1);
	pdb_set_homedir(account, s, True);

	unistr2_to_ascii(s, &delta->uni_dir_drive, sizeof(s) - 1);
	pdb_set_dir_drive(account, s, True);

	unistr2_to_ascii(s, &delta->uni_logon_script, sizeof(s) - 1);
	pdb_set_logon_script(account, s, True);

	unistr2_to_ascii(s, &delta->uni_acct_desc, sizeof(s) - 1);
	pdb_set_acct_desc(account, s);

	unistr2_to_ascii(s, &delta->uni_workstations, sizeof(s) - 1);
	pdb_set_workstations(account, s);

	unistr2_to_ascii(s, &delta->uni_profile, sizeof(s) - 1);
	pdb_set_profile_path(account, s, True);

	/* User and group sid */

	sid_copy(&sid, get_global_sam_sid());
	sid_append_rid(&sid, delta->user_rid);
	pdb_set_user_sid(account, &sid);

	sid_copy(&sid, get_global_sam_sid());
	sid_append_rid(&sid, delta->group_rid);
	pdb_set_group_sid(account, &sid);

	/* Logon and password information */

	pdb_set_logon_time(account, nt_time_to_unix(&delta->logon_time), True);
	pdb_set_logoff_time(account, nt_time_to_unix(&delta->logoff_time), 
			    True);

	pdb_set_logon_divs(account, delta->logon_divs);

	/* TODO: logon hours */
	/* TODO: bad password count */
	/* TODO: logon count */

	pdb_set_pass_last_set_time(
		account, nt_time_to_unix(&delta->pwd_last_set_time));

	/* TODO: account expiry time */

	pdb_set_acct_ctrl(account, delta->acb_info);
	return NT_STATUS_OK;
}

static NTSTATUS
fetch_account_info(uint32 rid, SAM_ACCOUNT_INFO *delta)
{
	NTSTATUS nt_ret;
	fstring account;
	pstring add_script;
	SAM_ACCOUNT *sam_account=NULL;

	fstrcpy(account, unistr2_static(&delta->uni_acct_name));
	d_printf("Creating account: %s\n", account);

	if (!NT_STATUS_IS_OK(nt_ret = pdb_init_sam(&sam_account)))
		return nt_ret;

	if (!pdb_getsampwnam(sam_account, account)) {
		struct passwd *pw;

		/* Create appropriate user */
		if (delta->acb_info & ACB_NORMAL) {
			pstrcpy(add_script, lp_adduser_script());
		} else if ( (delta->acb_info & ACB_WSTRUST) ||
			    (delta->acb_info & ACB_SVRTRUST) ) {
			pstrcpy(add_script, lp_addmachine_script());
		} else {
			DEBUG(1, ("Unknown user type: %s\n",
				  smbpasswd_encode_acb_info(delta->acb_info)));
			pdb_free_sam(&sam_account);
			return NT_STATUS_NO_SUCH_USER;
		}
		if (*add_script) {
			int add_ret;
			all_string_sub(add_script, "%u", account,
				       sizeof(account));
			add_ret = smbrun(add_script,NULL);
			DEBUG(1,("fetch_account: Running the command `%s' "
				 "gave %d\n", add_script, add_ret));
		}
		pw = getpwnam_alloc(account);
		if (pw) {
			nt_ret = pdb_init_sam_pw(&sam_account, pw);

			if (!NT_STATUS_IS_OK(nt_ret)) {
				passwd_free(&pw);
				pdb_free_sam(&sam_account);
				return nt_ret;
			}
			passwd_free(&pw);
		} else {
			DEBUG(3, ("Could not create account %s\n", account));
			pdb_free_sam(&sam_account);
			return NT_STATUS_NO_SUCH_USER;
		}
	}

	sam_account_from_delta(sam_account, delta);
	pdb_add_sam_account(sam_account);
	pdb_free_sam(&sam_account);
	return NT_STATUS_OK;
}

static NTSTATUS
fetch_group_info(uint32 rid, SAM_GROUP_INFO *delta)
{
	fstring name;
	fstring comment;
	struct group *grp;
	DOM_SID group_sid;
	fstring sid_string;
	GROUP_MAP map;
	int flag = TDB_INSERT;
	gid_t gid;

	unistr2_to_ascii(name, &delta->uni_grp_name, sizeof(name)-1);
	unistr2_to_ascii(comment, &delta->uni_grp_desc, sizeof(comment)-1);

	if ((grp = getgrnam(name)) == NULL)
		smb_create_group(name, &gid);

	if ((grp = getgrgid(gid)) == NULL)
		return NT_STATUS_ACCESS_DENIED;

	/* add the group to the mapping table */
	sid_copy(&group_sid, get_global_sam_sid());
	sid_append_rid(&group_sid, rid);
	sid_to_string(sid_string, &group_sid);

	/* Add the group mapping */
	if (get_group_map_from_sid(group_sid, &map, False)) {
		/* Don't TDB_INSERT, mapping exists */
		flag = 0;
	}

	map.gid = grp->gr_gid;
	map.sid = group_sid;
	map.sid_name_use = SID_NAME_DOM_GRP;
	fstrcpy(map.nt_name, name);
	fstrcpy(map.comment, comment);

	map.priv_set.count = 0;
	map.priv_set.set = NULL;

	add_mapping_entry(&map, flag);

	return NT_STATUS_OK;
}

static void
fetch_sam_entry(SAM_DELTA_HDR *hdr_delta, SAM_DELTA_CTR *delta)
{
	switch(hdr_delta->type) {
	case SAM_DELTA_ACCOUNT_INFO:
		fetch_account_info(hdr_delta->target_rid,
				   &delta->account_info);
		break;
	case SAM_DELTA_GROUP_INFO:
		fetch_group_info(hdr_delta->target_rid,
				 &delta->group_info);
		break;
	default:
		d_printf("Unknown delta record type %d\n", hdr_delta->type);
		break;
	}
}

static void
fetch_database(struct cli_state *cli, unsigned db_type, DOM_CRED *ret_creds)
{
	unsigned last_rid = -1;
        NTSTATUS result;
	int i;
        TALLOC_CTX *mem_ctx;
        SAM_DELTA_HDR *hdr_deltas;
        SAM_DELTA_CTR *deltas;
        uint32 num_deltas;

	if (!(mem_ctx = talloc_init())) {
		return;
	}

	d_printf("Fetching database %u\n", db_type);

	do {
		result = cli_netlogon_sam_sync(cli, mem_ctx, ret_creds,
					       db_type, last_rid+1,
					       &num_deltas,
					       &hdr_deltas, &deltas);
		clnt_deal_with_creds(cli->sess_key, &(cli->clnt_cred),
				     ret_creds);
		last_rid = 0;
                for (i = 0; i < num_deltas; i++) {
			fetch_sam_entry(&hdr_deltas[i], &deltas[i]);
			last_rid = hdr_deltas[i].target_rid;
                }
	} while (last_rid && NT_STATUS_EQUAL(result, STATUS_MORE_ENTRIES));

	talloc_destroy(mem_ctx);
}

/* dump sam database via samsync rpc calls */
int rpc_vampire(int argc, const char **argv)
{
        NTSTATUS result;
	struct cli_state *cli = NULL;
	uchar trust_password[16];
	DOM_CRED ret_creds;
	uint32 neg_flags = 0x000001ff;

	ZERO_STRUCT(ret_creds);

	/* Connect to remote machine */
	if (!(cli = net_make_ipc_connection(NET_FLAGS_ANONYMOUS |
					    NET_FLAGS_PDC))) {
		return 1;
	}

	if (!cli_nt_session_open(cli, PIPE_NETLOGON)) {
		DEBUG(0,("Error connecting to NETLOGON pipe\n"));
		goto fail;
	}

	if (!secrets_fetch_trust_account_password(lp_workgroup(),
						  trust_password, NULL)) {
		d_printf("Could not retrieve domain trust secret");
		goto fail;
	}
	
	result = cli_nt_setup_creds(cli, SEC_CHAN_BDC,  trust_password,
				    &neg_flags, 2);
	if (!NT_STATUS_IS_OK(result)) {
		d_printf("Failed to setup BDC creds\n");
		goto fail;
	}

	fetch_database(cli, SAM_DATABASE_DOMAIN, &ret_creds);
	fetch_database(cli, SAM_DATABASE_BUILTIN, &ret_creds);

	/* Currently we crash on PRIVS somewhere in unmarshalling */
	/* Dump_database(cli, SAM_DATABASE_PRIVS, &ret_creds); */

	cli_nt_session_close(cli);
        
        return 0;

fail:
	if (cli) {
		cli_nt_session_close(cli);
	}
	return -1;
}
