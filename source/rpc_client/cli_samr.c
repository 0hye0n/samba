/* 
   Unix SMB/Netbios implementation.
   Version 1.9.
   NT Domain Authentication SMB / MSRPC client
   Copyright (C) Andrew Tridgell 1994-1997
   Copyright (C) Luke Kenneth Casson Leighton 1996-1997
   
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



#ifdef SYSLOG
#undef SYSLOG
#endif

#include "includes.h"
#include "nterr.h"

extern int DEBUGLEVEL;



/****************************************************************************
do a SAMR query user groups
****************************************************************************/
BOOL get_samr_query_usergroups(struct cli_state *cli, 
				POLICY_HND *pol_open_domain, uint32 user_rid,
				uint32 *num_groups, DOM_GID *gid)
{
	POLICY_HND pol_open_user;
	if (pol_open_domain == NULL || num_groups == NULL || gid == NULL) return False;

	/* send open domain (on user sid) */
	if (!do_samr_open_user(cli,
				pol_open_domain,
				0x02011b, user_rid,
				&pol_open_user))
	{
		return False;
	}

	/* send user groups query */
	if (!do_samr_query_usergroups(cli,
				&pol_open_user,
				num_groups, gid))
	{
		DEBUG(5,("do_samr_query_usergroups: error in query user groups\n"));
	}

	return do_samr_close(cli, &pol_open_user);
}

/****************************************************************************
do a SAMR query user info
****************************************************************************/
BOOL get_samr_query_userinfo(struct cli_state *cli, 
				POLICY_HND *pol_open_domain,
				uint32 info_level,
				uint32 user_rid, SAM_USER_INFO_21 *usr)
{
	POLICY_HND pol_open_user;
	if (pol_open_domain == NULL || usr == NULL) return False;

	bzero(usr, sizeof(*usr));

	/* send open domain (on user sid) */
	if (!do_samr_open_user(cli,
				pol_open_domain,
				0x02011b, user_rid,
				&pol_open_user))
	{
		return False;
	}

	/* send user info query */
	if (!do_samr_query_userinfo(cli,
				&pol_open_user,
				info_level, (void*)usr))
	{
		DEBUG(5,("do_samr_query_userinfo: error in query user info, level 0x%x\n",
		          info_level));
	}

	return do_samr_close(cli, &pol_open_user);
}

/****************************************************************************
do a SAMR change user password command
****************************************************************************/
BOOL do_samr_chgpasswd_user(struct cli_state *cli,
		char *srv_name, char *user_name,
		char nt_newpass[516], uchar nt_oldhash[16],
		char lm_newpass[516], uchar lm_oldhash[16])
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_CHGPASSWD_USER q_e;
	BOOL valid_pwc = False;

	/* create and send a MSRPC command with api SAMR_CHGPASSWD_USER */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Change User Password. server:%s username:%s\n",
	        srv_name, user_name));

	make_samr_q_chgpasswd_user(&q_e, srv_name, user_name,
	                           nt_newpass, nt_oldhash,
	                           lm_newpass, lm_oldhash);

	/* turn parameters into data stream */
	samr_io_q_chgpasswd_user("", &q_e, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_CHGPASSWD_USER, &data, &rdata))
	{
		SAMR_R_CHGPASSWD_USER r_e;
		BOOL p;

		samr_io_r_chgpasswd_user("", &r_e, &rdata, 0);

		p = rdata.offset != 0;
		if (p && r_e.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_CHGPASSWD_USER: %s\n", get_nt_error_msg(r_e.status)));
			p = False;
		}

		if (p)
		{
			valid_pwc = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_pwc;
}

/****************************************************************************
do a SAMR unknown 0x38 command
****************************************************************************/
BOOL do_samr_unknown_38(struct cli_state *cli, char *srv_name)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_UNKNOWN_38 q_e;
	BOOL valid_un8 = False;

	/* create and send a MSRPC command with api SAMR_ENUM_DOM_USERS */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Unknown 38 server:%s\n", srv_name));

	make_samr_q_unknown_38(&q_e, srv_name);

	/* turn parameters into data stream */
	samr_io_q_unknown_38("", &q_e, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_UNKNOWN_38, &data, &rdata))
	{
		SAMR_R_UNKNOWN_38 r_e;
		BOOL p;

		samr_io_r_unknown_38("", &r_e, &rdata, 0);

		p = rdata.offset != 0;
#if 0
		if (p && r_e.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_UNKNOWN_38: %s\n", get_nt_error_msg(r_e.status)));
			p = False;
		}
#endif
		if (p)
		{
			valid_un8 = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_un8;
}

/****************************************************************************
do a SAMR unknown 0x8 command
****************************************************************************/
BOOL do_samr_query_dom_info(struct cli_state *cli, 
				POLICY_HND *domain_pol, uint16 switch_value)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_QUERY_DOMAIN_INFO q_e;
	BOOL valid_un8 = False;

	/* create and send a MSRPC command with api SAMR_ENUM_DOM_USERS */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Unknown 8 switch:%d\n", switch_value));

	if (domain_pol == NULL) return False;

	/* store the parameters */
	make_samr_q_query_dom_info(&q_e, domain_pol, switch_value);

	/* turn parameters into data stream */
	samr_io_q_query_dom_info("", &q_e, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_QUERY_DOMAIN_INFO, &data, &rdata))
	{
#if 0
		SAMR_R_QUERY_DOMAIN_INFO r_e;
		BOOL p;

		samr_io_r_query_dom_info("", &r_e, &rdata, 0);

		p = rdata.offset != 0;
		if (p && r_e.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_QUERY_DOMAIN_INFO: %s\n", get_nt_error_msg(r_e.status)));
			p = False;
		}

		if (p)
		{
			valid_un8 = True;
		}
#endif
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_un8;
}

/****************************************************************************
do a SAMR enumerate users
****************************************************************************/
BOOL do_samr_enum_dom_users(struct cli_state *cli, 
				POLICY_HND *pol, uint16 num_entries, uint16 unk_0,
				uint16 acb_mask, uint16 unk_1, uint32 size,
				struct acct_info **sam,
				int *num_sam_users)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_ENUM_DOM_USERS q_e;
    BOOL valid_pol = False;

	/* create and send a MSRPC command with api SAMR_ENUM_DOM_USERS */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Enum SAM DB max size:%x\n", size));

	if (pol == NULL || num_sam_users == NULL) return False;

	/* store the parameters */
	make_samr_q_enum_dom_users(&q_e, pol,
	                           num_entries, unk_0,
	                           acb_mask, unk_1, size);

	/* turn parameters into data stream */
	samr_io_q_enum_dom_users("", &q_e, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_ENUM_DOM_USERS, &data, &rdata))
	{
		SAMR_R_ENUM_DOM_USERS r_e;
		BOOL p;

		samr_io_r_enum_dom_users("", &r_e, &rdata, 0);

		p = rdata.offset != 0;
		if (p && r_e.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_ENUM_DOM_USERS: %s\n", get_nt_error_msg(r_e.status)));
			p = False;
		}

		if (p)
		{
			int i;
			int name_idx = 0;

			*num_sam_users = r_e.num_entries2;
			if (*num_sam_users > MAX_SAM_ENTRIES)
			{
				*num_sam_users = MAX_SAM_ENTRIES;
				DEBUG(2,("do_samr_enum_dom_users: sam user entries limited to %d\n",
				          *num_sam_users));
			}

			*sam = (struct acct_info*) malloc(sizeof(struct acct_info) * (*num_sam_users));
				    
			if ((*sam) == NULL)
			{
				*num_sam_users = 0;
			}

			for (i = 0; i < *num_sam_users; i++)
			{

				(*sam)[i].smb_userid = r_e.sam[i].rid;
				if (r_e.sam[i].hdr_name.buffer)
				{
					char *acct_name = unistrn2(r_e.uni_acct_name[name_idx].buffer,
					                           r_e.uni_acct_name[name_idx].uni_str_len);
					fstrcpy((*sam)[i].acct_name, acct_name);
					name_idx++;
				}
				else
				{
					bzero((*sam)[i].acct_name, sizeof((*sam)[i].acct_name));
				}
				DEBUG(5,("do_samr_enum_dom_users: idx: %4d rid: %8x acct: %s\n",
				          i, (*sam)[i].smb_userid, (*sam)[i].acct_name));
			}
			valid_pol = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_pol;
}

/****************************************************************************
do a SAMR Connect
****************************************************************************/
BOOL do_samr_connect(struct cli_state *cli, 
				char *srv_name, uint32 unknown_0,
				POLICY_HND *connect_pol)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_CONNECT q_o;
    BOOL valid_pol = False;

	/* create and send a MSRPC command with api SAMR_CONNECT */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Open Policy server:%s undoc value:%x\n",
				srv_name, unknown_0));

	if (srv_name == NULL || connect_pol == NULL) return False;

	/* store the parameters */
	make_samr_q_connect(&q_o, srv_name, unknown_0);

	/* turn parameters into data stream */
	samr_io_q_connect("", &q_o,  &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_CONNECT, &data, &rdata))
	{
		SAMR_R_CONNECT r_o;
		BOOL p;

		samr_io_r_connect("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_CONNECT: %s\n", get_nt_error_msg(r_o.status)));
			p = False;
		}

		if (p)
		{
			memcpy(connect_pol, &r_o.connect_pol, sizeof(r_o.connect_pol));
			valid_pol = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_pol;
}

/****************************************************************************
do a SAMR Open User
****************************************************************************/
BOOL do_samr_open_user(struct cli_state *cli, 
				POLICY_HND *pol, uint32 unk_0, uint32 rid, 
				POLICY_HND *user_pol)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_OPEN_USER q_o;
    BOOL valid_pol = False;

	/* create and send a MSRPC command with api SAMR_OPEN_USER */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Open User.  unk_0: %08x RID:%x\n",
	          unk_0, rid));

	if (pol == NULL || user_pol == NULL) return False;

	/* store the parameters */
	make_samr_q_open_user(&q_o, pol, unk_0, rid);

	/* turn parameters into data stream */
	samr_io_q_open_user("", &q_o,  &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_OPEN_USER, &data, &rdata))
	{
		SAMR_R_OPEN_USER r_o;
		BOOL p;

		samr_io_r_open_user("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_OPEN_USER: %s\n", get_nt_error_msg(r_o.status)));
			p = False;
		}

		if (p)
		{
			memcpy(user_pol, &r_o.user_pol, sizeof(r_o.user_pol));
			valid_pol = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_pol;
}

/****************************************************************************
do a SAMR Open Domain
****************************************************************************/
BOOL do_samr_open_domain(struct cli_state *cli, 
				POLICY_HND *connect_pol, uint32 rid, DOM_SID *sid,
				POLICY_HND *domain_pol)
{
	pstring sid_str;
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_OPEN_DOMAIN q_o;
	BOOL valid_pol = False;

	/* create and send a MSRPC command with api SAMR_OPEN_DOMAIN */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	sid_to_string(sid_str, sid);
	DEBUG(4,("SAMR Open Domain.  SID:%s RID:%x\n", sid_str, rid));

	if (connect_pol == NULL || sid == NULL || domain_pol == NULL) return False;

	/* store the parameters */
	make_samr_q_open_domain(&q_o, connect_pol, rid, sid);

	/* turn parameters into data stream */
	samr_io_q_open_domain("", &q_o,  &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_OPEN_DOMAIN, &data, &rdata))
	{
		SAMR_R_OPEN_DOMAIN r_o;
		BOOL p;

		samr_io_r_open_domain("", &r_o, &rdata, 0);
		p = rdata.offset != 0;

		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_OPEN_DOMAIN: %s\n", get_nt_error_msg(r_o.status)));
			p = False;
		}

		if (p)
		{
			memcpy(domain_pol, &r_o.domain_pol, sizeof(r_o.domain_pol));
			valid_pol = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_pol;
}

/****************************************************************************
do a SAMR Query Unknown 12
****************************************************************************/
BOOL do_samr_query_unknown_12(struct cli_state *cli, 
				POLICY_HND *pol, uint32 rid, uint32 num_gids, uint32 *gids,
				uint32 *num_aliases,
				fstring als_names    [MAX_LOOKUP_SIDS],
				uint32  num_als_users[MAX_LOOKUP_SIDS])
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_UNKNOWN_12 q_o;
    BOOL valid_query = False;

	/* create and send a MSRPC command with api SAMR_UNKNOWN_12 */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Query Unknown 12.\n"));

	if (pol == NULL || rid == 0 || num_gids == 0 || gids == NULL ||
	    num_aliases == NULL || als_names == NULL || num_als_users == NULL ) return False;

	/* store the parameters */
	make_samr_q_unknown_12(&q_o, pol, rid, num_gids, gids);

	/* turn parameters into data stream */
	samr_io_q_unknown_12("", &q_o,  &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_UNKNOWN_12, &data, &rdata))
	{
		SAMR_R_UNKNOWN_12 r_o;
		BOOL p;

		samr_io_r_unknown_12("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_UNKNOWN_12: %s\n", get_nt_error_msg(r_o.status)));
			p = False;
		}

		if (p)
		{
			if (r_o.ptr_aliases != 0 && r_o.ptr_als_usrs != 0 &&
			    r_o.num_als_usrs1 == r_o.num_aliases1)
			{
				int i;

				valid_query = True;
				*num_aliases = r_o.num_aliases1;

				for (i = 0; i < r_o.num_aliases1; i++)
				{
					fstrcpy(als_names[i], unistrn2(r_o.uni_als_name[i].buffer, r_o.uni_als_name[i].uni_str_len));
				}
				for (i = 0; i < r_o.num_als_usrs1; i++)
				{
					num_als_users[i] = r_o.num_als_usrs[i];
				}
			}
			else if (r_o.ptr_aliases == 0 && r_o.ptr_als_usrs == 0)
			{
				valid_query = True;
				*num_aliases = 0;
			}
			else
			{
				p = False;
			}
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_query;
}

/****************************************************************************
do a SAMR Query User Groups
****************************************************************************/
BOOL do_samr_query_usergroups(struct cli_state *cli, 
				POLICY_HND *pol, uint32 *num_groups, DOM_GID *gid)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_QUERY_USERGROUPS q_o;
    BOOL valid_query = False;

	/* create and send a MSRPC command with api SAMR_QUERY_USERGROUPS */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Query User Groups.\n"));

	if (pol == NULL || gid == NULL || num_groups == 0) return False;

	/* store the parameters */
	make_samr_q_query_usergroups(&q_o, pol);

	/* turn parameters into data stream */
	samr_io_q_query_usergroups("", &q_o,  &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_QUERY_USERGROUPS, &data, &rdata))
	{
		SAMR_R_QUERY_USERGROUPS r_o;
		BOOL p;

		/* get user info */
		r_o.gid = gid;

		samr_io_r_query_usergroups("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_QUERY_USERGROUPS: %s\n", get_nt_error_msg(r_o.status)));
			p = False;
		}

		if (p && r_o.ptr_0 != 0)
		{
			valid_query = True;
			*num_groups = r_o.num_entries;
		}

	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_query;
}

/****************************************************************************
do a SAMR Query User Info
****************************************************************************/
BOOL do_samr_query_userinfo(struct cli_state *cli, 
				POLICY_HND *pol, uint16 switch_value, void* usr)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_QUERY_USERINFO q_o;
    BOOL valid_query = False;

	/* create and send a MSRPC command with api SAMR_QUERY_USERINFO */

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("SAMR Query User Info.  level: %d\n", switch_value));

	if (pol == NULL || usr == NULL || switch_value == 0) return False;

	/* store the parameters */
	make_samr_q_query_userinfo(&q_o, pol, switch_value);

	/* turn parameters into data stream */
	samr_io_q_query_userinfo("", &q_o,  &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_QUERY_USERINFO, &data, &rdata))
	{
		SAMR_R_QUERY_USERINFO r_o;
		BOOL p;

		/* get user info */
		r_o.info.id = usr;

		samr_io_r_query_userinfo("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_R_QUERY_USERINFO: %s\n", get_nt_error_msg(r_o.status)));
			p = False;
		}

		if (p && r_o.switch_value != switch_value)
		{
			DEBUG(0,("SAMR_R_QUERY_USERINFO: received incorrect level %d\n",
			          r_o.switch_value));
		}

		if (p && r_o.ptr != 0)
		{
			valid_query = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_query;
}

/****************************************************************************
do a SAMR Close
****************************************************************************/
BOOL do_samr_close(struct cli_state *cli, POLICY_HND *hnd)
{
	prs_struct data;
	prs_struct rdata;

	SAMR_Q_CLOSE_HND q_c;
    BOOL valid_close = False;

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	if (hnd == NULL) return False;

	/* create and send a MSRPC command with api SAMR_CLOSE_HND */

	DEBUG(4,("SAMR Close\n"));

	/* store the parameters */
	make_samr_q_close_hnd(&q_c, hnd);

	/* turn parameters into data stream */
	samr_io_q_close_hnd("", &q_c,  &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, SAMR_CLOSE_HND, &data, &rdata))
	{
		SAMR_R_CLOSE_HND r_c;
		BOOL p;

		samr_io_r_close_hnd("", &r_c, &rdata, 0);
		p = rdata.offset != 0;

		if (p && r_c.status != 0)
		{
			/* report error code */
			DEBUG(0,("SAMR_CLOSE_HND: %s\n", get_nt_error_msg(r_c.status)));
			p = False;
		}

		if (p)
		{
			/* check that the returned policy handle is all zeros */
			int i;
			valid_close = True;

			for (i = 0; i < sizeof(r_c.pol.data); i++)
			{
				if (r_c.pol.data[i] != 0)
				{
					valid_close = False;
					break;
				}
			}	
			if (!valid_close)
			{
				DEBUG(0,("SAMR_CLOSE_HND: non-zero handle returned\n"));
			}
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );

	return valid_close;
}

