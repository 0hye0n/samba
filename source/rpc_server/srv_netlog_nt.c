/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
 *  Copyright (C) Paul Ashton                       1997.
 *  Copyright (C) Jeremy Allison               1998-2001.
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

/* This is the implementation of the netlogon pipe. */

#include "includes.h"

extern int DEBUGLEVEL;

extern BOOL sam_logon_in_ssb;
extern pstring samlogon_user;
extern pstring global_myname;
extern DOM_SID global_sam_sid;

/*************************************************************************
 init_net_r_req_chal:
 *************************************************************************/

static void init_net_r_req_chal(NET_R_REQ_CHAL *r_c,
                                DOM_CHAL *srv_chal, NTSTATUS status)
{
	DEBUG(6,("init_net_r_req_chal: %d\n", __LINE__));
	memcpy(r_c->srv_chal.data, srv_chal->data, sizeof(srv_chal->data));
	r_c->status = status;
}

/*************************************************************************
 error messages cropping up when using nltest.exe...
 *************************************************************************/

#define ERROR_NO_SUCH_DOMAIN   0x54b
#define ERROR_NO_LOGON_SERVERS 0x51f

/*************************************************************************
 net_reply_logon_ctrl:
 *************************************************************************/

/* Some flag values reverse engineered from NLTEST.EXE */

#define LOGON_CTRL_IN_SYNC          0x00
#define LOGON_CTRL_REPL_NEEDED      0x01
#define LOGON_CTRL_REPL_IN_PROGRESS 0x02

NTSTATUS _net_logon_ctrl(pipes_struct *p, NET_Q_LOGON_CTRL *q_u, 
		       NET_R_LOGON_CTRL *r_u)
{
	uint32 flags = 0x0;
	uint32 pdc_connection_status = 0x00; /* Maybe a win32 error code? */
	
	/* Setup the Logon Control response */

	init_net_r_logon_ctrl(r_u, q_u->query_level, flags, 
			      pdc_connection_status);

	return r_u->status;
}

/*************************************************************************
 net_reply_logon_ctrl2:
 *************************************************************************/

NTSTATUS _net_logon_ctrl2(pipes_struct *p, NET_Q_LOGON_CTRL2 *q_u, NET_R_LOGON_CTRL2 *r_u)
{
    /* lkclXXXX - guess what - absolutely no idea what these are! */
    uint32 flags = 0x0;
    uint32 pdc_connection_status = 0x0;
    uint32 logon_attempts = 0x0;
    uint32 tc_status = ERROR_NO_LOGON_SERVERS;
    char *trusted_domain = "test_domain";

	DEBUG(6,("_net_logon_ctrl2: %d\n", __LINE__));

	/* set up the Logon Control2 response */
	init_net_r_logon_ctrl2(r_u, q_u->query_level,
			       flags, pdc_connection_status, logon_attempts,
			       tc_status, trusted_domain);

	DEBUG(6,("_net_logon_ctrl2: %d\n", __LINE__));

	return r_u->status;
}

/*************************************************************************
 net_reply_trust_dom_list:
 *************************************************************************/

NTSTATUS _net_trust_dom_list(pipes_struct *p, NET_Q_TRUST_DOM_LIST *q_u, NET_R_TRUST_DOM_LIST *r_u)
{
	char *trusted_domain = "test_domain";
	uint32 num_trust_domains = 1;

	DEBUG(6,("_net_trust_dom_list: %d\n", __LINE__));

	/* set up the Trusted Domain List response */
	init_r_trust_dom(r_u, num_trust_domains, trusted_domain);

	DEBUG(6,("_net_trust_dom_list: %d\n", __LINE__));

	return r_u->status;
}

/***********************************************************************************
 init_net_r_srv_pwset:
 ***********************************************************************************/

static void init_net_r_srv_pwset(NET_R_SRV_PWSET *r_s,
                             DOM_CRED *srv_cred, NTSTATUS status)  
{
	DEBUG(5,("init_net_r_srv_pwset: %d\n", __LINE__));

	memcpy(&r_s->srv_cred, srv_cred, sizeof(r_s->srv_cred));
	r_s->status = status;

	DEBUG(5,("init_net_r_srv_pwset: %d\n", __LINE__));
}

/******************************************************************
 gets a machine password entry.  checks access rights of the host.
 ******************************************************************/

static BOOL get_md4pw(char *md4pw, char *mach_acct)
{
	SAM_ACCOUNT *sampass = NULL;
	uint8 *pass;
	BOOL ret;

#if 0
    /*
     * Currently this code is redundent as we already have a filter
     * by hostname list. What this code really needs to do is to 
     * get a hosts allowed/hosts denied list from the SAM database
     * on a per user basis, and make the access decision there.
     * I will leave this code here for now as a reminder to implement
     * this at a later date. JRA.
     */

	if (!allow_access(lp_domain_hostsdeny(), lp_domain_hostsallow(),
	                  client_name(), client_addr()))
	{
		DEBUG(0,("get_md4pw: Workstation %s denied access to domain\n", mach_acct));
		return False;
	}
#endif /* 0 */

	if(!pdb_init_sam(&sampass))
		return False;

	/* JRA. This is ok as it is only used for generating the challenge. */
	become_root();
	ret=pdb_getsampwnam(sampass, mach_acct);
	unbecome_root();
 
 	if (ret==False) {
 		DEBUG(0,("get_md4pw: Workstation %s: no account in domain\n", mach_acct));
		pdb_free_sam(sampass);
		return False;
	}

	if (!(pdb_get_acct_ctrl(sampass) & ACB_DISABLED) && ((pass=pdb_get_nt_passwd(sampass)) != NULL)) {
		memcpy(md4pw, pass, 16);
		dump_data(5, md4pw, 16);
 		pdb_free_sam(sampass);
		return True;
	}
 	
	DEBUG(0,("get_md4pw: Workstation %s: no account in domain\n", mach_acct));
	pdb_free_sam(sampass);
	return False;

}

/*************************************************************************
 _net_req_chal
 *************************************************************************/

NTSTATUS _net_req_chal(pipes_struct *p, NET_Q_REQ_CHAL *q_u, NET_R_REQ_CHAL *r_u)
{
	NTSTATUS status = NT_STATUS_OK;
	fstring mach_acct;

	if (!get_valid_user_struct(p->vuid))
		return NT_STATUS_NO_SUCH_USER;

	rpcstr_pull(mach_acct,q_u->uni_logon_clnt.buffer,sizeof(fstring),q_u->uni_logon_clnt.uni_str_len*2,0);

	strlower(mach_acct);
	fstrcat(mach_acct, "$");

	if (get_md4pw((char *)p->dc.md4pw, mach_acct)) {
		/* copy the client credentials */
		memcpy(p->dc.clnt_chal.data          , q_u->clnt_chal.data, sizeof(q_u->clnt_chal.data));
		memcpy(p->dc.clnt_cred.challenge.data, q_u->clnt_chal.data, sizeof(q_u->clnt_chal.data));

		/* create a server challenge for the client */
		/* Set these to random values. */
		generate_random_buffer(p->dc.srv_chal.data, 8, False);

		memcpy(p->dc.srv_cred.challenge.data, p->dc.srv_chal.data, 8);

		memset((char *)p->dc.sess_key, '\0', sizeof(p->dc.sess_key));

		/* from client / server challenges and md4 password, generate sess key */
		cred_session_key(&p->dc.clnt_chal, &p->dc.srv_chal,
				 (char *)p->dc.md4pw, p->dc.sess_key);

		/* Save the machine account name. */
		fstrcpy(p->dc.mach_acct, mach_acct);

	} else {
		/* lkclXXXX take a guess at a good error message to return :-) */
		status = NT_STATUS_NOLOGON_WORKSTATION_TRUST_ACCOUNT;
	}

	/* set up the LSA REQUEST CHALLENGE response */
	init_net_r_req_chal(r_u, &p->dc.srv_chal, status);

	return r_u->status;
}

/*************************************************************************
 init_net_r_auth:
 *************************************************************************/

static void init_net_r_auth(NET_R_AUTH *r_a, DOM_CHAL *resp_cred, NTSTATUS status)
{
	memcpy(r_a->srv_chal.data, resp_cred->data, sizeof(resp_cred->data));
	r_a->status = status;
}

/*************************************************************************
 _net_auth
 *************************************************************************/

NTSTATUS _net_auth(pipes_struct *p, NET_Q_AUTH *q_u, NET_R_AUTH *r_u)
{
	NTSTATUS status = NT_STATUS_OK;
	DOM_CHAL srv_cred;
	UTIME srv_time;

	if (!get_valid_user_struct(p->vuid))
		return NT_STATUS_NO_SUCH_USER;

	srv_time.time = 0;

	/* check that the client credentials are valid */
	if (cred_assert(&q_u->clnt_chal, p->dc.sess_key, &p->dc.clnt_cred.challenge, srv_time)) {

		/* create server challenge for inclusion in the reply */
		cred_create(p->dc.sess_key, &p->dc.srv_cred.challenge, srv_time, &srv_cred);

		/* copy the received client credentials for use next time */
		memcpy(p->dc.clnt_cred.challenge.data, q_u->clnt_chal.data, sizeof(q_u->clnt_chal.data));
		memcpy(p->dc.srv_cred .challenge.data, q_u->clnt_chal.data, sizeof(q_u->clnt_chal.data));
	} else {
		status = NT_STATUS_ACCESS_DENIED;
	}

	/* set up the LSA AUTH 2 response */
	init_net_r_auth(r_u, &srv_cred, status);

	return r_u->status;
}

/*************************************************************************
 init_net_r_auth_2:
 *************************************************************************/

static void init_net_r_auth_2(NET_R_AUTH_2 *r_a,
                              DOM_CHAL *resp_cred, NEG_FLAGS *flgs, NTSTATUS status)
{
	memcpy(r_a->srv_chal.data, resp_cred->data, sizeof(resp_cred->data));
	memcpy(&r_a->srv_flgs, flgs, sizeof(r_a->srv_flgs));
	r_a->status = status;
}

/*************************************************************************
 _net_auth_2
 *************************************************************************/

NTSTATUS _net_auth_2(pipes_struct *p, NET_Q_AUTH_2 *q_u, NET_R_AUTH_2 *r_u)
{
	NTSTATUS status = NT_STATUS_OK;
	DOM_CHAL srv_cred;
	UTIME srv_time;
	NEG_FLAGS srv_flgs;

	if (!get_valid_user_struct(p->vuid))
		return NT_STATUS_NO_SUCH_USER;

	srv_time.time = 0;

	/* check that the client credentials are valid */
	if (cred_assert(&q_u->clnt_chal, p->dc.sess_key, &p->dc.clnt_cred.challenge, srv_time)) {

		/* create server challenge for inclusion in the reply */
		cred_create(p->dc.sess_key, &p->dc.srv_cred.challenge, srv_time, &srv_cred);

		/* copy the received client credentials for use next time */
		memcpy(p->dc.clnt_cred.challenge.data, q_u->clnt_chal.data, sizeof(q_u->clnt_chal.data));
		memcpy(p->dc.srv_cred .challenge.data, q_u->clnt_chal.data, sizeof(q_u->clnt_chal.data));
	} else {
		status = NT_STATUS_ACCESS_DENIED;
	}

	srv_flgs.neg_flags = 0x000001ff;

	/* set up the LSA AUTH 2 response */
	init_net_r_auth_2(r_u, &srv_cred, &srv_flgs, status);

	return r_u->status;
}

/*************************************************************************
 _net_srv_pwset
 *************************************************************************/

NTSTATUS _net_srv_pwset(pipes_struct *p, NET_Q_SRV_PWSET *q_u, NET_R_SRV_PWSET *r_u)
{
	NTSTATUS status = NT_STATUS_WRONG_PASSWORD;
	DOM_CRED srv_cred;
	pstring mach_acct;
	SAM_ACCOUNT *sampass=NULL;
	BOOL ret = False;
	unsigned char pwd[16];
	int i;

	if (!get_valid_user_struct(p->vuid))
		return NT_STATUS_NO_SUCH_USER;

	/* checks and updates credentials.  creates reply credentials */
	if (!deal_with_creds(p->dc.sess_key, &p->dc.clnt_cred, &q_u->clnt_id.cred, &srv_cred))
		return NT_STATUS_INVALID_HANDLE;

	memcpy(&p->dc.srv_cred, &p->dc.clnt_cred, sizeof(p->dc.clnt_cred));

	DEBUG(5,("_net_srv_pwset: %d\n", __LINE__));

	rpcstr_pull(mach_acct,q_u->clnt_id.login.uni_acct_name.buffer,
			sizeof(mach_acct),q_u->clnt_id.login.uni_acct_name.uni_str_len*2,0);

	DEBUG(3,("Server Password Set Wksta:[%s]\n", mach_acct));
	
	pdb_init_sam(&sampass);

	become_root();
	ret=pdb_getsampwnam(sampass, mach_acct);
	unbecome_root();

	/* Ensure the account exists and is a machine account. */

	if (ret==False || !(pdb_get_acct_ctrl(sampass) & ACB_WSTRUST)) {
		pdb_free_sam(sampass);
		return NT_STATUS_NO_SUCH_USER;
	}

	/*
	 * Check the machine account name we're changing is the same
	 * as the one we've authenticated from. This prevents arbitrary
	 * machines changing other machine account passwords.
	 */

	if (!strequal(mach_acct, p->dc.mach_acct)) {
		pdb_free_sam(sampass);
		return NT_STATUS_ACCESS_DENIED;
	}

	
	DEBUG(100,("Server password set : new given value was :\n"));
	for(i = 0; i < 16; i++)
		DEBUG(100,("%02X ", q_u->pwd[i]));
	DEBUG(100,("\n"));

	cred_hash3( pwd, q_u->pwd, p->dc.sess_key, 0);

	/* lies!  nt and lm passwords are _not_ the same: don't care */
	pdb_set_lanman_passwd (sampass, pwd);
	pdb_set_nt_passwd     (sampass, pwd);
	pdb_set_acct_ctrl     (sampass, ACB_WSTRUST);
 
	become_root();
	ret = pdb_update_sam_account (sampass,False);
	unbecome_root();
 
	if (ret)
		status = NT_STATUS_OK;

	/* set up the LSA Server Password Set response */
	init_net_r_srv_pwset(r_u, &srv_cred, status);

	pdb_free_sam(sampass);
	return r_u->status;
}


/*************************************************************************
 _net_sam_logoff:
 *************************************************************************/

NTSTATUS _net_sam_logoff(pipes_struct *p, NET_Q_SAM_LOGOFF *q_u, NET_R_SAM_LOGOFF *r_u)
{
	DOM_CRED srv_cred;

	if (!get_valid_user_struct(p->vuid))
		return NT_STATUS_NO_SUCH_USER;

	/* checks and updates credentials.  creates reply credentials */
	if (!deal_with_creds(p->dc.sess_key, &p->dc.clnt_cred, 
	                &q_u->sam_id.client.cred, &srv_cred))
		return NT_STATUS_INVALID_HANDLE;

	memcpy(&p->dc.srv_cred, &p->dc.clnt_cred, sizeof(p->dc.clnt_cred));

	/* XXXX maybe we want to say 'no', reject the client's credentials */
	r_u->buffer_creds = 1; /* yes, we have valid server credentials */
	memcpy(&r_u->srv_creds, &srv_cred, sizeof(r_u->srv_creds));

	r_u->status = NT_STATUS_OK;

	return r_u->status;
}

/*************************************************************************
 _net_logon_any:  Use the new authentications subsystem to log in.
 *************************************************************************/

static NTSTATUS _net_logon_any(NET_ID_INFO_CTR *ctr, char *user, char *domain, char *sess_key)
{
	NTSTATUS nt_status = NT_STATUS_LOGON_FAILURE;

	unsigned char local_lm_response[24];
	unsigned char local_nt_response[24];

	auth_usersupplied_info user_info;
	auth_serversupplied_info server_info;
	AUTH_STR ourdomain, theirdomain, smb_username, wksta_name;

	DEBUG(5, ("_net_logon_any: entered with user %s and domain %s\n", user, domain));
		
	ZERO_STRUCT(user_info);
	ZERO_STRUCT(server_info);
	ZERO_STRUCT(ourdomain);
	ZERO_STRUCT(theirdomain);
	ZERO_STRUCT(smb_username);
	ZERO_STRUCT(wksta_name);
	
	ourdomain.str = lp_workgroup();
	ourdomain.len = strlen(ourdomain.str);

	theirdomain.str = domain;
	theirdomain.len = strlen(theirdomain.str);

	user_info.requested_domain = theirdomain;
	user_info.domain = ourdomain;
	
	smb_username.str = user;
	smb_username.len = strlen(smb_username.str);

	user_info.requested_username = smb_username;  /* For the time-being */
	user_info.smb_username = smb_username;

#if 0
	user_info.wksta_name.str = cleint_name();
	user_info.wksta_name.len = strlen(client_name());

	user_info.wksta_name = wksta_name;
#endif

	DEBUG(10,("_net_logon_any: Attempting validation level %d.\n", ctr->switch_value));
	switch (ctr->switch_value) {
	case NET_LOGON_TYPE:
		/* Standard challange/response authenticaion */

		user_info.lm_resp.buffer = (uint8 *)ctr->auth.id2.lm_chal_resp.buffer;
		user_info.lm_resp.len = ctr->auth.id2.lm_chal_resp.str_str_len;
		user_info.nt_resp.buffer = (uint8 *)ctr->auth.id2.nt_chal_resp.buffer;
		user_info.nt_resp.len = ctr->auth.id2.nt_chal_resp.str_str_len;
		memcpy(user_info.chal, ctr->auth.id2.lm_chal, 8);
		break;
	case INTERACTIVE_LOGON_TYPE:
		/* 'Interactive' autheticaion, supplies the password in its MD4 form, encrypted
		   with the session key.  We will convert this to challange/responce for the 
		   auth subsystem to chew on */
	{
		char nt_pwd[16];
		char lm_pwd[16];
		unsigned char key[16];
		
		memset(key, 0, 16);
		memcpy(key, sess_key, 8);
		
		memcpy(lm_pwd, ctr->auth.id1.lm_owf.data, 16);
		memcpy(nt_pwd, ctr->auth.id1.nt_owf.data, 16);

#ifdef DEBUG_PASSWORD
		DEBUG(100,("key:"));
		dump_data(100, (char *)key, 16);
		
		DEBUG(100,("lm owf password:"));
		dump_data(100, lm_pwd, 16);
		
		DEBUG(100,("nt owf password:"));
		dump_data(100, nt_pwd, 16);
#endif
		
		SamOEMhash((uchar *)lm_pwd, key, 16);
		SamOEMhash((uchar *)nt_pwd, key, 16);
		
#ifdef DEBUG_PASSWORD
		DEBUG(100,("decrypt of lm owf password:"));
		dump_data(100, lm_pwd, 16);
		
		DEBUG(100,("decrypt of nt owf password:"));
		dump_data(100, nt_pwd, 16);
#endif

		generate_random_buffer(user_info.chal, 8, False);
		SMBOWFencrypt((const unsigned char *)lm_pwd, user_info.chal, local_lm_response);
		SMBOWFencrypt((const unsigned char *)nt_pwd, user_info.chal, local_nt_response);
		user_info.lm_resp.buffer = (uint8 *)local_lm_response;
		user_info.lm_resp.len = 24;
		user_info.nt_resp.buffer = (uint8 *)local_nt_response;
		user_info.nt_resp.len = 24;
		break;
	}
	default:
		DEBUG(2,("SAM Logon: unsupported switch value\n"));
		return NT_STATUS_INVALID_INFO_CLASS;
	} /* end switch */
	
	nt_status = check_password(&user_info, &server_info);

	DEBUG(5, ("_net_logon_any: exited with status %s\n", 
		  get_nt_error_msg(nt_status)));

	return nt_status;
}



/*************************************************************************
 _net_sam_logon
 *************************************************************************/

NTSTATUS _net_sam_logon(pipes_struct *p, NET_Q_SAM_LOGON *q_u, NET_R_SAM_LOGON *r_u)
{
	NTSTATUS status = NT_STATUS_OK;
	NET_USER_INFO_3 *usr_info = NULL;
	DOM_CRED srv_cred;
	SAM_ACCOUNT *sampass = NULL;
	UNISTR2 *uni_samlogon_user = NULL;
	UNISTR2 *uni_samlogon_domain = NULL;
	fstring nt_username;
	fstring nt_domain;
	BOOL ret;

	usr_info = (NET_USER_INFO_3 *)talloc(p->mem_ctx, sizeof(NET_USER_INFO_3));
	if (!usr_info)
		return NT_STATUS_NO_MEMORY;

	ZERO_STRUCTP(usr_info);
 
	if (!get_valid_user_struct(p->vuid))
		return NT_STATUS_NO_SUCH_USER;
    
	/* checks and updates credentials.  creates reply credentials */
	if (!deal_with_creds(p->dc.sess_key, &p->dc.clnt_cred, &q_u->sam_id.client.cred, &srv_cred))
		return NT_STATUS_INVALID_HANDLE;
	else
		memcpy(&p->dc.srv_cred, &p->dc.clnt_cred, sizeof(p->dc.clnt_cred));
    
	r_u->buffer_creds = 1; /* yes, we have valid server credentials */
	memcpy(&r_u->srv_creds, &srv_cred, sizeof(r_u->srv_creds));

	/* store the user information, if there is any. */
	r_u->user = usr_info;
	r_u->switch_value = 0; /* indicates no info */
	r_u->auth_resp = 1; /* authoritative response */
	r_u->switch_value = 3; /* indicates type of validation user info */

	/* find the username */
    
	switch (q_u->sam_id.logon_level) {
	case INTERACTIVE_LOGON_TYPE:
		uni_samlogon_user = &q_u->sam_id.ctr->auth.id1.uni_user_name;
 		uni_samlogon_domain = &q_u->sam_id.ctr->auth.id1.uni_domain_name;
            
		DEBUG(3,("SAM Logon (Interactive). Domain:[%s].  ", lp_workgroup()));
		break;
	case NET_LOGON_TYPE:
		uni_samlogon_user = &q_u->sam_id.ctr->auth.id2.uni_user_name;
		uni_samlogon_domain = &q_u->sam_id.ctr->auth.id2.uni_domain_name;
            
		DEBUG(3,("SAM Logon (Network). Domain:[%s].  ", lp_workgroup()));
		break;
	default:
		DEBUG(2,("SAM Logon: unsupported switch value\n"));
		return NT_STATUS_INVALID_INFO_CLASS;
	} /* end switch */

	/* check username exists */

	rpcstr_pull(nt_username,uni_samlogon_user->buffer,sizeof(nt_username),uni_samlogon_user->uni_str_len*2,0);
	rpcstr_pull(nt_domain,uni_samlogon_domain->buffer,sizeof(nt_domain),uni_samlogon_domain->uni_str_len*2,0);

	DEBUG(3,("User:[%s] Requested Domain:[%s]\n", nt_username, nt_domain));
        
	/*
	 * Convert to a UNIX username.
	 */

	map_username(nt_username);

	DEBUG(10,("Attempting validation level %d for mapped username %s.\n", q_u->sam_id.ctr->switch_value, nt_username));

	status = _net_logon_any(q_u->sam_id.ctr, nt_username, nt_domain, (char *)p->dc.sess_key);

	/* Check account and password */
    
	if (NT_STATUS_V(status))
		return status;

	pdb_init_sam(&sampass);

	/* get the account information */
	become_root();
	ret = pdb_getsampwnam(sampass, nt_username);
	unbecome_root();

	if (ret == False){
		pdb_free_sam(sampass);
		return NT_STATUS_NO_SUCH_USER;
	}

	/* lkclXXXX this is the point at which, if the login was
		successful, that the SAM Local Security Authority should
		record that the user is logged in to the domain.
	*/
    
	{
		DOM_GID *gids = NULL;
		int num_gids = 0;
		pstring my_name;
		pstring my_workgroup;
		pstring domain_groups;
	
		/* set up pointer indicating user/password failed to be found */
		usr_info->ptr_user_info = 0;
        
		/* XXXX hack to get standard_sub_basic() to use sam logon username */
		/* possibly a better way would be to do a become_user() call */
		sam_logon_in_ssb = True;
		pstrcpy(samlogon_user, nt_username);

		pstrcpy(my_workgroup, lp_workgroup());
		pstrcpy(my_name, global_myname);
		strupper(my_name);

		/*
		 * This is the point at which we get the group
		 * database - we should be getting the gid_t list
		 * from /etc/group and then turning the uids into
		 * rids and then into machine sids for this user.
		 * JRA.
		 */
        
		get_domain_user_groups(domain_groups, nt_username);
        
		/*
		 * make_dom_gids allocates the gids array. JRA.
		 */
		gids = NULL;
		num_gids = make_dom_gids(p->mem_ctx, domain_groups, &gids);
        
		sam_logon_in_ssb = False;
        
		init_net_user_info3(p->mem_ctx, usr_info, sampass,
                            0, /* logon_count */
                            0, /* bad_pw_count */
                            num_gids,    /* uint32 num_groups */
                            gids    , /* DOM_GID *gids */
                            0x20    , /* uint32 user_flgs (?) */
                            NULL, /* char sess_key[16] */
                            my_name     , /* char *logon_srv */
                            my_workgroup, /* char *logon_dom */
                            &global_sam_sid,     /* DOM_SID *dom_sid */
                            NULL); /* char *other_sids */
	}
	pdb_free_sam(sampass);
	return status;
}
