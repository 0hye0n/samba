
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
do a LSA Open Policy
****************************************************************************/
BOOL do_lsa_open_policy(struct cli_state *cli,
			char *server_name, POLICY_HND *hnd)
{
	prs_struct rbuf;
	prs_struct buf; 
	LSA_Q_OPEN_POL q_o;
    BOOL valid_pol = False;

	if (hnd == NULL) return False;

	prs_init(&buf , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rbuf, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api LSA_OPENPOLICY */

	DEBUG(4,("LSA Open Policy\n"));

	/* store the parameters */
	make_q_open_pol(&q_o, server_name, 0, 0, 0x1);

	/* turn parameters into data stream */
	lsa_io_q_open_pol("", &q_o, &buf, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, LSA_OPENPOLICY, &buf, &rbuf))
	{
		LSA_R_OPEN_POL r_o;
		BOOL p;

		lsa_io_r_open_pol("", &r_o, &rbuf, 0);
		p = rbuf.offset != 0;

		if (p && r_o.status != 0)
		{
			/* report error code */
			DEBUG(0,("LSA_OPENPOLICY: %s\n", get_nt_error_msg(r_o.status)));
			p = False;
		}

		if (p)
		{
			/* ok, at last: we're happy. return the policy handle */
			memcpy(hnd, r_o.pol.data, sizeof(hnd->data));
			valid_pol = True;
		}
	}

	prs_mem_free(&rbuf);
	prs_mem_free(&buf );

	return valid_pol;
}

/****************************************************************************
do a LSA Query Info Policy
****************************************************************************/
BOOL do_lsa_query_info_pol(struct cli_state *cli,
			POLICY_HND *hnd, uint16 info_class,
			fstring domain_name, fstring domain_sid)
{
	prs_struct rbuf;
	prs_struct buf; 
	LSA_Q_QUERY_INFO q_q;
    BOOL valid_response = False;

	if (hnd == NULL || domain_name == NULL || domain_sid == NULL) return False;

	prs_init(&buf , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rbuf, 0   , 4, SAFETY_MARGIN, True );

	/* create and send a MSRPC command with api LSA_QUERYINFOPOLICY */

	DEBUG(4,("LSA Query Info Policy\n"));

	/* store the parameters */
	make_q_query(&q_q, hnd, info_class);

	/* turn parameters into data stream */
	lsa_io_q_query("", &q_q, &buf, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, LSA_QUERYINFOPOLICY, &buf, &rbuf))
	{
		LSA_R_QUERY_INFO r_q;
		BOOL p;

		lsa_io_r_query("", &r_q, &rbuf, 0);
		p = rbuf.offset != 0;
		
		if (p && r_q.status != 0)
		{
			/* report error code */
			DEBUG(0,("LSA_QUERYINFOPOLICY: %s\n", get_nt_error_msg(r_q.status)));
			p = False;
		}

		if (p && r_q.info_class != q_q.info_class)
		{
			/* report different info classes */
			DEBUG(0,("LSA_QUERYINFOPOLICY: error info_class (q,r) differ - (%x,%x)\n",
					q_q.info_class, r_q.info_class));
			p = False;
		}

		if (p)
		{
			/* ok, at last: we're happy. */
			switch (r_q.info_class)
			{
				case 3:
				{
					char *dom_name = unistrn2(r_q.dom.id3.uni_domain_name.buffer,
					                          r_q.dom.id3.uni_domain_name.uni_str_len);
					fstrcpy(domain_name, dom_name);
					sid_to_string(domain_sid, &(r_q.dom.id3.dom_sid.sid));

					valid_response = True;
					break;
				}
				case 5:
				{
					char *dom_name = unistrn2(r_q.dom.id5.uni_domain_name.buffer,
					                          r_q.dom.id5.uni_domain_name.uni_str_len);
					fstrcpy(domain_name, dom_name);
					sid_to_string(domain_sid, &(r_q.dom.id5.dom_sid.sid));

					valid_response = True;
					break;
				}
				default:
				{
					DEBUG(3,("LSA_QUERYINFOPOLICY: unknown info class\n"));
					domain_name[0] = 0;
					domain_sid [0] = 0;

					break;
				}
			}
			DEBUG(3,("LSA_QUERYINFOPOLICY (level %x): domain:%s  domain sid:%s\n",
			          r_q.info_class, domain_name, domain_sid));
		}
	}

	prs_mem_free(&rbuf);
	prs_mem_free(&buf );

	return valid_response;
}

/****************************************************************************
do a LSA Close
****************************************************************************/
BOOL do_lsa_close(struct cli_state *cli, POLICY_HND *hnd)
{
	prs_struct rbuf;
	prs_struct buf; 
	LSA_Q_CLOSE q_c;
    BOOL valid_close = False;

	if (hnd == NULL) return False;

	/* create and send a MSRPC command with api LSA_OPENPOLICY */

	prs_init(&buf , 1024, 4, SAFETY_MARGIN, False);
	prs_init(&rbuf, 0   , 4, SAFETY_MARGIN, True );

	DEBUG(4,("LSA Close\n"));

	/* store the parameters */
	make_lsa_q_close(&q_c, hnd);

	/* turn parameters into data stream */
	lsa_io_q_close("", &q_c, &buf, 0);

	/* send the data on \PIPE\ */
	if (rpc_api_pipe_req(cli, LSA_CLOSE, &buf, &rbuf))
	{
		LSA_R_CLOSE r_c;
		BOOL p;

		lsa_io_r_close("", &r_c, &rbuf, 0);
		p = rbuf.offset != 0;

		if (p && r_c.status != 0)
		{
			/* report error code */
			DEBUG(0,("LSA_CLOSE: %s\n", get_nt_error_msg(r_c.status)));
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
				DEBUG(0,("LSA_CLOSE: non-zero handle returned\n"));
			}
		}
	}

	prs_mem_free(&rbuf);
	prs_mem_free(&buf );

	return valid_close;
}


