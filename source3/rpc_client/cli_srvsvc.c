
/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
 *  Copyright (C) Paul Ashton                       1997.
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


#ifdef SYSLOG
#undef SYSLOG
#endif

#include "includes.h"

extern int DEBUGLEVEL;

/****************************************************************************
do a server net conn enum
****************************************************************************/
BOOL do_srv_net_srv_conn_enum(struct cli_state *cli, uint16 fnum,
			char *server_name, char *qual_name,
			uint32 switch_value, SRV_CONN_INFO_CTR *ctr,
			uint32 preferred_len,
			ENUM_HND *hnd)
{
	prs_struct data; 
	prs_struct rdata;
	SRV_Q_NET_CONN_ENUM q_o;
    BOOL valid_enum = False;

	if (server_name == NULL || ctr == NULL || preferred_len == 0) return False;

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api SRV_NETCONNENUM */

	DEBUG(4,("SRV Net Server Connection Enum(%s, %s), level %d, enum:%8x\n",
				server_name, qual_name, switch_value, get_enum_hnd(hnd)));
				
	ctr->switch_value = switch_value;
	ctr->ptr_conn_ctr = 1;
	ctr->conn.info0.num_entries_read = 0;
	ctr->conn.info0.ptr_conn_info    = 1;

	/* store the parameters */
	make_srv_q_net_conn_enum(&q_o, server_name, qual_name,
	                         switch_value, ctr,
	                         preferred_len,
	                         hnd);

	/* turn parameters into data stream */
	srv_io_q_net_conn_enum("", &q_o, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, fnum, SRV_NETCONNENUM, &data, &rdata))
	{
		SRV_R_NET_CONN_ENUM r_o;
		BOOL p;

		r_o.ctr = ctr;

		srv_io_r_net_conn_enum("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SRV_R_NET_SRV_GET_INFO: %s\n", get_nt_error_msg(r_o.status)));
			p = 0;
		}

		if (p && r_o.ctr->switch_value != switch_value)
		{
			/* different switch levels.  oops. */
			DEBUG(0,("SRV_R_NET_SRV_CONN_ENUM: info class %d does not match request %d\n",
				r_o.ctr->switch_value, switch_value));
			p = 0;
		}

		if (p)
		{
			/* ok, at last: we're happy. */
			valid_enum = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );
	
	return valid_enum;
}

/****************************************************************************
do a server net sess enum
****************************************************************************/
BOOL do_srv_net_srv_sess_enum(struct cli_state *cli, uint16 fnum,
			char *server_name, char *qual_name, char *user_name,
			uint32 switch_value, SRV_SESS_INFO_CTR *ctr,
			uint32 preferred_len,
			ENUM_HND *hnd)
{
	prs_struct data; 
	prs_struct rdata;
	SRV_Q_NET_SESS_ENUM q_o;
    BOOL valid_enum = False;

	if (server_name == NULL || ctr == NULL || preferred_len == 0) return False;

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api SRV_NETSESSENUM */

	DEBUG(4,("SRV Net Session Enum (%s), level %d, enum:%8x\n",
				server_name, switch_value, get_enum_hnd(hnd)));
				
	ctr->switch_value = switch_value;
	ctr->ptr_sess_ctr = 1;
	ctr->sess.info0.num_entries_read = 0;
	ctr->sess.info0.ptr_sess_info    = 1;

	/* store the parameters */
	make_srv_q_net_sess_enum(&q_o, server_name, qual_name, user_name,
	                         switch_value, ctr,
	                         preferred_len,
	                         hnd);

	/* turn parameters into data stream */
	srv_io_q_net_sess_enum("", &q_o, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, fnum, SRV_NETSESSENUM, &data, &rdata))
	{
		SRV_R_NET_SESS_ENUM r_o;
		BOOL p;

		r_o.ctr = ctr;

		srv_io_r_net_sess_enum("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SRV_R_NET_SRV_SESS_ENUM: %s\n", get_nt_error_msg(r_o.status)));
			p = 0;
		}

		if (p && r_o.ctr->switch_value != switch_value)
		{
			/* different switch levels.  oops. */
			DEBUG(0,("SRV_R_NET_SRV_SESS_ENUM: info class %d does not match request %d\n",
				r_o.ctr->switch_value, switch_value));
			p = 0;
		}

		if (p)
		{
			/* ok, at last: we're happy. */
			valid_enum = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );
	
	return valid_enum;
}

/****************************************************************************
do a server net share enum
****************************************************************************/
BOOL do_srv_net_srv_share_enum(struct cli_state *cli, uint16 fnum,
			char *server_name, 
			uint32 switch_value, SRV_SHARE_INFO_CTR *ctr,
			uint32 preferred_len,
			ENUM_HND *hnd)
{
	prs_struct data; 
	prs_struct rdata;
	SRV_Q_NET_SHARE_ENUM q_o;
    BOOL valid_enum = False;

	if (server_name == NULL || ctr == NULL || preferred_len == 0) return False;

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api SRV_NETSHAREENUM */

	DEBUG(4,("SRV Get Share Info (%s), level %d, enum:%8x\n",
				server_name, switch_value, get_enum_hnd(hnd)));
				
	q_o.share_level = switch_value;

	ctr->switch_value = switch_value;
	ctr->ptr_share_ctr = 1;
	ctr->share.info1.num_entries_read = 0;
	ctr->share.info1.ptr_share_info    = 1;

	/* store the parameters */
	make_srv_q_net_share_enum(&q_o, server_name, 
	                         switch_value, ctr,
	                         preferred_len,
	                         hnd);

	/* turn parameters into data stream */
	srv_io_q_net_share_enum("", &q_o, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, fnum, SRV_NETSHAREENUM, &data, &rdata))
	{
		SRV_R_NET_SHARE_ENUM r_o;
		BOOL p;

		r_o.ctr = ctr;

		srv_io_r_net_share_enum("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SRV_R_NET_SRV_GET_INFO: %s\n", get_nt_error_msg(r_o.status)));
			p = 0;
		}

		if (p && r_o.ctr->switch_value != switch_value)
		{
			/* different switch levels.  oops. */
			DEBUG(0,("SRV_R_NET_SRV_SHARE_ENUM: info class %d does not match request %d\n",
				r_o.ctr->switch_value, switch_value));
			p = 0;
		}

		if (p)
		{
			/* ok, at last: we're happy. */
			valid_enum = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );
	
	return valid_enum;
}

/****************************************************************************
do a server net file enum
****************************************************************************/
BOOL do_srv_net_srv_file_enum(struct cli_state *cli, uint16 fnum,
			char *server_name, char *qual_name, uint32 file_id,
			uint32 switch_value, SRV_FILE_INFO_CTR *ctr,
			uint32 preferred_len,
			ENUM_HND *hnd)
{
	prs_struct data; 
	prs_struct rdata;
	SRV_Q_NET_FILE_ENUM q_o;
    BOOL valid_enum = False;

	if (server_name == NULL || ctr == NULL || preferred_len == 0) return False;

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api SRV_NETFILEENUM */

	DEBUG(4,("SRV Get File Info (%s), level %d, enum:%8x\n",
				server_name, switch_value, get_enum_hnd(hnd)));
				
	q_o.file_level = switch_value;

	ctr->switch_value = switch_value;
	ctr->ptr_file_ctr = 1;
	ctr->file.info3.num_entries_read = 0;
	ctr->file.info3.ptr_file_info    = 1;

	/* store the parameters */
	make_srv_q_net_file_enum(&q_o, server_name, qual_name, file_id,
	                         switch_value, ctr,
	                         preferred_len,
	                         hnd);

	/* turn parameters into data stream */
	srv_io_q_net_file_enum("", &q_o, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, fnum, SRV_NETFILEENUM, &data, &rdata))
	{
		SRV_R_NET_FILE_ENUM r_o;
		BOOL p;

		r_o.ctr = ctr;

		srv_io_r_net_file_enum("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SRV_R_NET_SRV_GET_INFO: %s\n", get_nt_error_msg(r_o.status)));
			p = 0;
		}

		if (p && r_o.ctr->switch_value != switch_value)
		{
			/* different switch levels.  oops. */
			DEBUG(0,("SRV_R_NET_SRV_FILE_ENUM: info class %d does not match request %d\n",
				r_o.ctr->switch_value, switch_value));
			p = 0;
		}

		if (p)
		{
			/* ok, at last: we're happy. */
			valid_enum = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );
	
	return valid_enum;
}

/****************************************************************************
do a server get info 
****************************************************************************/
BOOL do_srv_net_srv_get_info(struct cli_state *cli, uint16 fnum,
			char *server_name, uint32 switch_value, SRV_INFO_CTR *ctr)
{
	prs_struct data; 
	prs_struct rdata;
	SRV_Q_NET_SRV_GET_INFO q_o;
    BOOL valid_info = False;

	if (server_name == NULL || switch_value == 0 || ctr == NULL) return False;

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api SRV_NET_SRV_GET_INFO */

	DEBUG(4,("SRV Get Server Info (%s), level %d\n", server_name, switch_value));

	/* store the parameters */
	make_srv_q_net_srv_get_info(&q_o, server_name, switch_value);

	/* turn parameters into data stream */
	srv_io_q_net_srv_get_info("", &q_o, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, fnum, SRV_NET_SRV_GET_INFO, &data, &rdata))
	{
		SRV_R_NET_SRV_GET_INFO r_o;
		BOOL p;

		r_o.ctr = ctr;

		srv_io_r_net_srv_get_info("", &r_o, &rdata, 0);
		p = rdata.offset != 0;
		p = rdata.offset != 0;
		
		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("SRV_R_NET_SRV_GET_INFO: %s\n", get_nt_error_msg(r_o.status)));
			p = 0;
		}

		if (p && r_o.ctr->switch_value != q_o.switch_value)
		{
			/* different switch levels.  oops. */
			DEBUG(0,("SRV_R_NET_SRV_GET_INFO: info class %d does not match request %d\n",
				r_o.ctr->switch_value, q_o.switch_value));
			p = 0;
		}

		if (p)
		{
			/* ok, at last: we're happy. */
			valid_info = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );
	
	return valid_info;
}

/****************************************************************************
get server time
****************************************************************************/
BOOL do_srv_net_remote_tod(struct cli_state *cli, uint16 fnum,
			   char *server_name, TIME_OF_DAY_INFO *tod)
{
	prs_struct data; 
	prs_struct rdata;
	SRV_Q_NET_REMOTE_TOD q_t;
	BOOL valid_info = False;

	if (server_name == NULL || tod == NULL) return False;

	prs_init(&data , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rdata, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api SRV_NET_REMOTE_TOD */

	DEBUG(4,("SRV Remote TOD (%s)\n", server_name));

	/* store the parameters */
	make_srv_q_net_remote_tod(&q_t, server_name);

	/* turn parameters into data stream */
	srv_io_q_net_remote_tod("", &q_t, &data, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, fnum, SRV_NET_REMOTE_TOD, &data, &rdata))
	{
		SRV_R_NET_REMOTE_TOD r_t;
		BOOL p;

		r_t.tod = tod;

		srv_io_r_net_remote_tod("", &r_t, &rdata, 0);
		p = rdata.offset != 0;
		p = rdata.offset != 0;
		
		if (p && r_t.status != 0)
		{
			/* report error code */
			DEBUG(0,("SRV_R_NET_REMOTE_TOD: %s\n", get_nt_error_msg(r_t.status)));
			p = False;
		}

		if (p)
		{
			valid_info = True;
		}
	}

	prs_mem_free(&data   );
	prs_mem_free(&rdata  );
	
	return valid_info;
}
