
/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1998,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1998,
 *  Copyright (C) Paul Ashton                       1998.
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
extern struct pipe_id_info pipe_names[];
extern fstring global_myworkgroup;
extern pstring global_myname;

/********************************************************************
 rpc pipe call id 
 ********************************************************************/
static uint32 get_rpc_call_id(void)
{
  static uint32 call_id = 0;
  return ++call_id;
}

/*******************************************************************
 uses SMBreadX to get rest of rpc data
 ********************************************************************/

static BOOL rpc_read(struct cli_state *cli, 
                     prs_struct *rdata, uint32 data_to_read,
                     uint32 rdata_offset)
{
	int size = cli->max_recv_frag;
	int file_offset = rdata_offset;
	int num_read;
	char *data;
	uint32 err;
	uint32 new_data_size = rdata->data->data_used + data_to_read;

	file_offset -= rdata_offset;

	DEBUG(5,("rpc_read: data_to_read: %d data offset: %d file offset: %d\n",
	data_to_read, rdata_offset, file_offset));

	if (new_data_size > rdata->data->data_size)
	{
		mem_grow_data(&rdata->data, True, new_data_size, True);
		DEBUG(5,("rpc_read: grow buffer to %d\n", rdata->data->data_used));
	}

	data = rdata->data->data + rdata_offset;

	do /* read data using SMBreadX */
	{
		if (size > data_to_read)
		size = data_to_read;

		new_data_size = rdata->data->data_used + size;

		if (new_data_size > rdata->data->data_size)
		{
			mem_grow_data(&rdata->data, True, new_data_size, True);
			DEBUG(5,("rpc_read: grow buffer to %d\n", rdata->data->data_used));
		}

		num_read = cli_read(cli, cli->nt_pipe_fnum, data, file_offset, size);

		DEBUG(5,("rpc_read: read offset: %d read: %d to read: %d\n",
		          file_offset, num_read, data_to_read));

		data_to_read -= num_read;
		file_offset  += num_read;
		data         += num_read;

		if (cli_error(cli, NULL, &err, NULL)) return False;

	} while (num_read > 0 && data_to_read > 0);
	/* && err == (0x80000000 | STATUS_BUFFER_OVERFLOW)); */

	mem_realloc_data(rdata->data, file_offset + rdata_offset);
	rdata->data->offset.end = file_offset + rdata_offset;

	DEBUG(5,("rpc_read: offset end: 0x%x.  data left to read:0x%x\n",
	          rdata->data->offset.end, data_to_read));

	return True;
}

/****************************************************************************
 checks the header
 ****************************************************************************/
static BOOL rpc_check_hdr(prs_struct *rdata, RPC_HDR *rhdr, 
                          BOOL *first, BOOL *last, int *len)
{
	DEBUG(5,("rpc_check_hdr: rdata->data->data_used: %d\n", rdata->data->data_used));

	smb_io_rpc_hdr   ("rpc_hdr   ", rhdr   , rdata, 0);

	if (!rdata->offset || rdata->offset != 0x10)
	{
		DEBUG(0,("cli_pipe: error in rpc header\n"));
		return False;
	}

	DEBUG(5,("rpc_check_hdr: (after smb_io_rpc_hdr call) rdata->data->data_used: %d\n",
	          rdata->data->data_used));

	(*first   ) = IS_BITS_SET_ALL(rhdr->flags, RPC_FLG_FIRST);
	(*last    ) = IS_BITS_SET_ALL(rhdr->flags, RPC_FLG_LAST );
	(*len     ) = rhdr->frag_len - rdata->data->data_used;

	return rhdr->pkt_type != RPC_FAULT;
}

static void NTLMSSPcalc_ap( struct cli_state *cli, unsigned char *data, int len)
{
	unsigned char *hash = cli->ntlmssp_hash;
    unsigned char index_i = hash[256];
    unsigned char index_j = hash[257];
    int ind;

    for( ind = 0; ind < len; ind++)
    {
        unsigned char tc;
        unsigned char t;

        index_i++;
        index_j += hash[index_i];

        tc = hash[index_i];
        hash[index_i] = hash[index_j];
        hash[index_j] = tc;

        t = hash[index_i] + hash[index_j];
        data[ind] = data[ind] ^ hash[t];
    }

    hash[256] = index_i;
    hash[257] = index_j;
}

/****************************************************************************
 decrypt data on an rpc pipe
 ****************************************************************************/

static BOOL rpc_auth_pipe(struct cli_state *cli, prs_struct *rdata,
				int len, int auth_len)
{
	RPC_AUTH_NTLMSSP_CHK chk;
	uint32 crc32;
	int data_len = len - 0x18 - auth_len - 8;
	char *reply_data = mem_data(&rdata->data, 0x18);

	BOOL auth_verify = IS_BITS_SET_ALL(cli->ntlmssp_srv_flgs, NTLMSSP_NEGOTIATE_SIGN);
	BOOL auth_seal   = IS_BITS_SET_ALL(cli->ntlmssp_srv_flgs, NTLMSSP_NEGOTIATE_SEAL);

	DEBUG(5,("rpc_auth_pipe: len: %d auth_len: %d verify %s seal %s\n",
	          len, auth_len, BOOLSTR(auth_verify), BOOLSTR(auth_seal)));

	if (reply_data == NULL) return False;

	if (auth_seal)
	{
		DEBUG(10,("rpc_auth_pipe: seal\n"));
		dump_data(100, reply_data, data_len);
		NTLMSSPcalc_ap(cli, (uchar*)reply_data, data_len);
		dump_data(100, reply_data, data_len);
	}

	if (auth_verify || auth_seal)
	{
		RPC_HDR_AUTH         rhdr_auth; 
		prs_struct auth_req;
		char *data = mem_data(&rdata->data, len - auth_len - 8);
		prs_init(&auth_req , 0x08, 4, 0, True);
		memcpy(auth_req.data->data, data, 8);
		smb_io_rpc_hdr_auth("hdr_auth", &rhdr_auth, &auth_req, 0);
		prs_mem_free(&auth_req);

		if (!rpc_hdr_auth_chk(&rhdr_auth))
		{
			return False;
		}
	}

	if (auth_verify)
	{
		prs_struct auth_verf;
		char *data = mem_data(&rdata->data, len - auth_len);
		if (data == NULL) return False;

		DEBUG(10,("rpc_auth_pipe: verify\n"));
		dump_data(100, data, auth_len);
		NTLMSSPcalc_ap(cli, (uchar*)(data+4), auth_len - 4);
		prs_init(&auth_verf, 0x08, 4, 0, True);
		memcpy(auth_verf.data->data, data, 16);
		smb_io_rpc_auth_ntlmssp_chk("auth_sign", &chk, &auth_verf, 0);
		dump_data(100, data, auth_len);
		prs_mem_free(&auth_verf);
	}

	if (auth_verify)
	{
		crc32 = crc32_calc_buffer(data_len, reply_data);
		if (!rpc_auth_ntlmssp_chk(&chk, crc32 , cli->ntlmssp_seq_num))
		{
			return False;
		}
		cli->ntlmssp_seq_num++;
	}
	return True;
}


/****************************************************************************
 send data on an rpc pipe, which *must* be in one fragment.
 receive response data from an rpc pipe, which may be large...

 read the first fragment: unfortunately have to use SMBtrans for the first
 bit, then SMBreadX for subsequent bits.

 if first fragment received also wasn't the last fragment, continue
 getting fragments until we _do_ receive the last fragment.

 [note: from a data abstraction viewpoint, this function is marginally
        complicated by the return side of cli_api_pipe getting in the way
        (i.e, the SMB header stuff).  the proper way to do this is to split
        cli_api_pipe down into receive / transmit.  oh, and split cli_readx
        down.  in other words, state-based (kernel) techniques...]

 ****************************************************************************/

static BOOL rpc_api_pipe(struct cli_state *cli, uint16 cmd, 
                  prs_struct *param , prs_struct *data,
                  prs_struct *rparam, prs_struct *rdata)
{
	int len;

	uint16 setup[2]; /* only need 2 uint16 setup parameters */
	uint32 err;
	BOOL first = True;
	BOOL last  = True;
	RPC_HDR    rhdr;

	/*
	* Setup the pointers from the incoming.
	*/
	char *pparams = param ? param->data->data : NULL;
	int params_len = param ? param->data->data_used : 0;
	char *pdata = data ? data->data->data : NULL;
	int data_len = data ? data->data->data_used : 0;

	/*
	* Setup the pointers to the outgoing.
	*/
	char **pp_ret_params = rparam ? &rparam->data->data : NULL;
	uint32 *p_ret_params_len = rparam ? &rparam->data->data_used : NULL;

	char **pp_ret_data = rdata ? &rdata->data->data : NULL;
	uint32 *p_ret_data_len = rdata ? &rdata->data->data_used : NULL;

	/* create setup parameters. */
	setup[0] = cmd; 
	setup[1] = cli->nt_pipe_fnum; /* pipe file handle.  got this from an SMBOpenX. */

	DEBUG(5,("rpc_api_pipe: cmd:%x fnum:%x\n", cmd, cli->nt_pipe_fnum));

	/* send the data: receive a response. */
	if (!cli_api_pipe(cli, "\\PIPE\\\0\0\0", 8,
	          setup, 2, 0,                     /* Setup, length, max */
	          pparams, params_len, 0,          /* Params, length, max */
	          pdata, data_len, 1024,           /* data, length, max */                  
	          pp_ret_params, p_ret_params_len, /* return params, len */
	          pp_ret_data, p_ret_data_len))    /* return data, len */
	{
		DEBUG(0, ("cli_pipe: return critical error. Error was %s\n", cli_errstr(cli)));
		return False;
	}

	if (rdata->data->data == NULL) return False;

	/**** parse the header: check it's a response record */

	rdata->data->offset.start = 0;
	rdata->data->offset.end   = rdata->data->data_used;
	rdata->offset = 0;

	/* cli_api_pipe does an ordinary Realloc - we have no margins now. */
	rdata->data->margin = 0;
	if (rparam) rparam->data->margin = 0;

	if (!rpc_check_hdr(rdata, &rhdr, &first, &last, &len))
	{
		return False;
	}

	if (rhdr.pkt_type == RPC_BINDACK)
	{
		if (!last && !first)
		{
			DEBUG(5,("rpc_api_pipe: bug in AS/U, setting fragment first/last ON\n"));
			first = True;
			last = True;
		}
	}

	if (rhdr.pkt_type == RPC_RESPONSE)
	{
		RPC_HDR_RESP rhdr_resp;
		smb_io_rpc_hdr_resp("rpc_hdr_resp", &rhdr_resp, rdata, 0);
	}

	DEBUG(5,("rpc_api_pipe: len left: %d smbtrans read: %d\n",
	          len, rdata->data->data_used));

	/* check if data to be sent back was too large for one SMB. */
	/* err status is only informational: the _real_ check is on the length */
	if (len > 0) /* || err == (0x80000000 | STATUS_BUFFER_OVERFLOW)) */
	{
		if (!rpc_read(cli, rdata, len, rdata->data->data_used))
		{
			return False;
		}
	}

	if (rhdr.auth_len != 0 && !rpc_auth_pipe(cli, rdata, rhdr.frag_len, rhdr.auth_len))
	{
		return False;
	}

	/* only one rpc fragment, and it has been read */
	if (first && last)
	{
		DEBUG(6,("rpc_api_pipe: fragment first and last both set\n"));
		return True;
	}

	while (!last) /* read more fragments until we get the last one */
	{
		RPC_HDR_RESP rhdr_resp;
		int num_read;
		prs_struct hps;

		prs_init(&hps, 0x8, 4, 0, True);

		num_read = cli_read(cli, cli->nt_pipe_fnum, hps.data->data, 0, 0x18);
		DEBUG(5,("rpc_api_pipe: read header (size:%d)\n", num_read));

		if (num_read != 0x18) return False;

		if (!rpc_check_hdr(&hps, &rhdr, &first, &last, &len))
		{
			return False;
		}

		smb_io_rpc_hdr_resp("rpc_hdr_resp", &rhdr_resp, &hps, 0);

		prs_mem_free(&hps);

		if (cli_error(cli, NULL, &err, NULL)) return False;

		if (first)
		{
			DEBUG(0,("rpc_api_pipe: wierd rpc header received\n"));
			return False;
		}

		if (!rpc_read(cli, rdata, len, rdata->data->data_used))
		{
			return False;
		}

		if (rhdr.auth_len != 0 && !rpc_auth_pipe(cli, rdata, rhdr.frag_len, rhdr.auth_len))
		{
			return False;
		}
	}

	return True;
}

/*******************************************************************
 creates a DCE/RPC bind request

 - initialises the parse structure.
 - dynamically allocates the header data structure
 - caller is expected to free the header data structure once used.

 ********************************************************************/
static BOOL create_rpc_bind_req(prs_struct *rhdr,
                                prs_struct *rhdr_rb,
                                prs_struct *rhdr_auth,
                                prs_struct *auth_req,
                                prs_struct *auth_ntlm,
				uint32 rpc_call_id,
                                RPC_IFACE *abstract, RPC_IFACE *transfer,
                                char *my_name, char *domain, uint32 neg_flags)
{
	RPC_HDR_RB           hdr_rb;
	RPC_HDR              hdr;
	RPC_HDR_AUTH         hdr_auth;
	RPC_AUTH_VERIFIER    auth_verifier;
	RPC_AUTH_NTLMSSP_NEG ntlmssp_neg;

	/* create the bind request RPC_HDR_RB */
	make_rpc_hdr_rb(&hdr_rb, 0x1630, 0x1630, 0x0,
	                0x1, 0x0, 0x1, abstract, transfer);

	/* stream the bind request data */
	smb_io_rpc_hdr_rb("", &hdr_rb,  rhdr_rb, 0);
	mem_realloc_data(rhdr_rb->data, rhdr_rb->offset);

	if (auth_req != NULL && rhdr_auth != NULL && auth_ntlm != NULL)
	{
		make_rpc_hdr_auth(&hdr_auth, 0x0a, 0x06, 0x00, 1);
		smb_io_rpc_hdr_auth("hdr_auth", &hdr_auth, rhdr_auth, 0);
		mem_realloc_data(rhdr_auth->data, rhdr_auth->offset);

		make_rpc_auth_verifier(&auth_verifier,
		                       "NTLMSSP", NTLMSSP_NEGOTIATE);

		smb_io_rpc_auth_verifier("auth_verifier", &auth_verifier, auth_req, 0);
		mem_realloc_data(auth_req->data, auth_req->offset);

		make_rpc_auth_ntlmssp_neg(&ntlmssp_neg,
		                       neg_flags, my_name, domain);

		smb_io_rpc_auth_ntlmssp_neg("ntlmssp_neg", &ntlmssp_neg, auth_req, 0);
		mem_realloc_data(auth_req->data, auth_req->offset);
	}

	/* create the request RPC_HDR */
	make_rpc_hdr(&hdr, RPC_BIND, 0x0, rpc_call_id,
	             (auth_req  != NULL ? auth_req ->offset : 0) +
	             (auth_ntlm != NULL ? auth_ntlm->offset : 0) +
	             (rhdr_auth != NULL ? rhdr_auth->offset : 0) +
	             rhdr_rb->offset + 0x10,
	             (auth_req  != NULL ? auth_req ->offset : 0) +
	             (auth_ntlm != NULL ? auth_ntlm->offset : 0));

	smb_io_rpc_hdr("hdr"   , &hdr   , rhdr, 0);
	mem_realloc_data(rhdr->data, rhdr->offset);

	if (rhdr->data == NULL || rhdr_rb->data == NULL) return False;

	/***/
	/*** link rpc header, bind acknowledgment and authentication responses ***/
	/***/

	if (auth_req != NULL)
	{
		prs_link(NULL     , rhdr      , rhdr_rb  );
		prs_link(rhdr     , rhdr_rb   , rhdr_auth);
		prs_link(rhdr_rb  , rhdr_auth , auth_req );
		prs_link(rhdr_auth, auth_req  , auth_ntlm);
		prs_link(auth_req , auth_ntlm , NULL     );
	}
	else
	{
		prs_link(NULL, rhdr   , rhdr_rb);
		prs_link(rhdr, rhdr_rb, NULL   );
	}

	return True;
}


/*******************************************************************
 creates a DCE/RPC bind authentication response

 - initialises the parse structure.
 - dynamically allocates the header data structure
 - caller is expected to free the header data structure once used.

 ********************************************************************/
static BOOL create_rpc_bind_resp(struct pwd_info *pwd,
				char *domain, char *user_name, char *my_name,
				uint32 ntlmssp_cli_flgs,
				uint32 rpc_call_id,
				prs_struct *rhdr,
                                prs_struct *rhdr_autha,
                                prs_struct *auth_resp)
{
	unsigned char lm_owf[24];
	unsigned char nt_owf[24];
	RPC_HDR               hdr;
	RPC_HDR_AUTHA         hdr_autha;
	RPC_AUTH_VERIFIER     auth_verifier;
	RPC_AUTH_NTLMSSP_RESP ntlmssp_resp;

	make_rpc_hdr_autha(&hdr_autha, 0x1630, 0x1630, 0x0a, 0x06, 0x00);
	smb_io_rpc_hdr_autha("hdr_autha", &hdr_autha, rhdr_autha, 0);
	mem_realloc_data(rhdr_autha->data, rhdr_autha->offset);

	make_rpc_auth_verifier(&auth_verifier,
			       "NTLMSSP", NTLMSSP_AUTH);

	smb_io_rpc_auth_verifier("auth_verifier", &auth_verifier, auth_resp, 0);
	mem_realloc_data(auth_resp->data, auth_resp->offset);

	pwd_get_lm_nt_owf(pwd, lm_owf, nt_owf);
			
	make_rpc_auth_ntlmssp_resp(&ntlmssp_resp,
			         lm_owf, nt_owf,
			         domain, user_name, my_name,
			         ntlmssp_cli_flgs);

	smb_io_rpc_auth_ntlmssp_resp("ntlmssp_resp", &ntlmssp_resp, auth_resp, 0);
	mem_realloc_data(auth_resp->data, auth_resp->offset);

	/* create the request RPC_HDR */
	make_rpc_hdr(&hdr, RPC_BINDRESP, 0x0, rpc_call_id,
	             auth_resp->offset + rhdr_autha->offset + 0x10,
	             auth_resp->offset);

	smb_io_rpc_hdr("hdr"   , &hdr   , rhdr, 0);
	mem_realloc_data(rhdr->data, rhdr->offset);

	if (rhdr->data == NULL || rhdr_autha->data == NULL) return False;

	/***/
	/*** link rpc header and authentication responses ***/
	/***/

	prs_link(NULL      , rhdr       , rhdr_autha);
	prs_link(rhdr      , rhdr_autha , auth_resp );
	prs_link(rhdr_autha, auth_resp  , NULL );

	return True;
}


/*******************************************************************
 creates a DCE/RPC bind request

 - initialises the parse structure.
 - dynamically allocates the header data structure
 - caller is expected to free the header data structure once used.

 ********************************************************************/

static BOOL create_rpc_request(prs_struct *rhdr, uint8 op_num, int data_len,
				int auth_len)
{
	uint32 alloc_hint;
	RPC_HDR_REQ hdr_req;
	RPC_HDR     hdr;

	DEBUG(5,("create_rpc_request: opnum: 0x%x data_len: 0x%x\n",
	op_num, data_len));

	/* create the rpc header RPC_HDR */
	make_rpc_hdr(&hdr   , RPC_REQUEST, RPC_FLG_FIRST | RPC_FLG_LAST,
	             get_rpc_call_id(), data_len, auth_len);

	if (auth_len != 0)
	{
		alloc_hint = data_len - 0x18 - auth_len - 16;
	}
	else
	{
		alloc_hint = data_len - 0x18;
	}

	DEBUG(10,("create_rpc_request: data_len: %x auth_len: %x alloc_hint: %x\n",
	           data_len, auth_len, alloc_hint));

	/* create the rpc request RPC_HDR_REQ */
	make_rpc_hdr_req(&hdr_req, alloc_hint, op_num);

	/* stream-time... */
	smb_io_rpc_hdr    ("hdr    ", &hdr    , rhdr, 0);
	smb_io_rpc_hdr_req("hdr_req", &hdr_req, rhdr, 0);

	if (rhdr->data == NULL || rhdr->offset != 0x18) return False;

	rhdr->data->offset.start = 0;
	rhdr->data->offset.end   = rhdr->offset;

	return True;
}


/****************************************************************************
 send a request on an rpc pipe.
 ****************************************************************************/
BOOL rpc_api_pipe_req(struct cli_state *cli, uint8 op_num,
                      prs_struct *data, prs_struct *rdata)
{
	/* fudge this, at the moment: create the header; memcpy the data.  oops. */
	prs_struct dataa;
	prs_struct rparam;
	prs_struct hdr;
	prs_struct hdr_auth;
	prs_struct auth_verf;
	int data_len;
	int auth_len;
	BOOL ret;
	BOOL auth_verify;
	BOOL auth_seal;
	uint32 crc32 = 0;

	auth_verify = IS_BITS_SET_ALL(cli->ntlmssp_srv_flgs, NTLMSSP_NEGOTIATE_SIGN);
	auth_seal   = IS_BITS_SET_ALL(cli->ntlmssp_srv_flgs, NTLMSSP_NEGOTIATE_SEAL);

	/* happen to know that NTLMSSP authentication verifier is 16 bytes */
	auth_len               = (auth_verify ? 16 : 0);
	data_len               = data->offset + auth_len + (auth_verify ? 8 : 0) + 0x18;
	data->data->offset.end = data->offset;

	prs_init(&hdr      , data_len, 4, SAFETY_MARGIN, False);
	prs_init(&hdr_auth , 8       , 4, SAFETY_MARGIN, False);
	prs_init(&auth_verf, auth_len, 4, SAFETY_MARGIN, False);
	prs_init(&rparam   , 0       , 4, 0            , True );

	create_rpc_request(&hdr, op_num, data_len, auth_len);

	if (auth_seal)
	{
		crc32 = crc32_calc_buffer(data->offset, mem_data(&data->data, 0));
		NTLMSSPcalc_ap(cli, (uchar*)mem_data(&data->data, 0), data->offset);
	}

	if (auth_seal || auth_verify)
	{
		RPC_HDR_AUTH         rhdr_auth;

		make_rpc_hdr_auth(&rhdr_auth, 0x0a, 0x06, 0x08, (auth_verify ? 1 : 0));
		smb_io_rpc_hdr_auth("hdr_auth", &rhdr_auth, &hdr_auth, 0);
	}

	if (auth_verify)
	{
		RPC_AUTH_NTLMSSP_CHK chk;

		make_rpc_auth_ntlmssp_chk(&chk, NTLMSSP_SIGN_VERSION, crc32, cli->ntlmssp_seq_num++);
		smb_io_rpc_auth_ntlmssp_chk("auth_sign", &chk, &auth_verf, 0);
		NTLMSSPcalc_ap(cli, (uchar*)mem_data(&auth_verf.data, 4), 12);
	}

	if (auth_seal || auth_verify)
	{
		prs_link(NULL     , &hdr      , data      );
		prs_link(&hdr     , data      , &hdr_auth );
		prs_link(data     , &hdr_auth , &auth_verf);
		prs_link(&hdr_auth, &auth_verf, NULL      );
	}
	else
	{
		prs_link(NULL, &hdr, data);
		prs_link(&hdr, data, NULL);
	}

	mem_realloc_data(hdr.data, data_len);

	DEBUG(100,("data_len: %x data_calc_len: %x\n",
		data_len, mem_buf_len(data->data)));

	/* this is a hack due to limitations in rpc_api_pipe */
	prs_init(&dataa, mem_buf_len(hdr.data), 4, 0x0, False);
	mem_buf_copy(dataa.data->data, hdr.data, 0, mem_buf_len(hdr.data));

	ret = rpc_api_pipe(cli, 0x0026, NULL, &dataa, &rparam, rdata);

	prs_mem_free(&hdr_auth );
	prs_mem_free(&auth_verf);
	prs_mem_free(&rparam   );
	prs_mem_free(&hdr      );
	prs_mem_free(&dataa    );

	return ret;
}

/****************************************************************************
do an rpc bind
****************************************************************************/

static BOOL rpc_pipe_set_hnd_state(struct cli_state *cli, char *pipe_name, uint16 device_state)
{
	BOOL state_set = False;
	char param[2];
	uint16 setup[2]; /* only need 2 uint16 setup parameters */
	char *rparam = NULL;
	char *rdata = NULL;
	uint32 rparam_len, rdata_len;

	if (pipe_name == NULL) return False;

	DEBUG(5,("Set Handle state Pipe[%x]: %s - device state:%x\n",
	cli->nt_pipe_fnum, pipe_name, device_state));

	/* create parameters: device state */
	SSVAL(param, 0, device_state);

	/* create setup parameters. */
	setup[0] = 0x0001; 
	setup[1] = cli->nt_pipe_fnum; /* pipe file handle.  got this from an SMBOpenX. */

	/* send the data on \PIPE\ */
	if (cli_api_pipe(cli, "\\PIPE\\\0\0\0", 8,
	            setup, 2, 0,                /* setup, length, max */
	            param, 2, 0,                /* param, length, max */
	            NULL, 0, 1024,              /* data, length, max */
	            &rparam, &rparam_len,        /* return param, length */
	            &rdata, &rdata_len))         /* return data, length */
	{
		DEBUG(5, ("Set Handle state: return OK\n"));
		state_set = True;
	}

	if (rparam) free(rparam);
	if (rdata ) free(rdata );

	return state_set;
}

/****************************************************************************
 check the rpc bind acknowledge response
****************************************************************************/

static BOOL valid_pipe_name(char *pipe_name, RPC_IFACE *abstract, RPC_IFACE *transfer)
{
	int pipe_idx = 0;

	while (pipe_names[pipe_idx].client_pipe != NULL)
	{
		if (strequal(pipe_name, pipe_names[pipe_idx].client_pipe ))
		{
			DEBUG(5,("Bind Abstract Syntax: "));	
			dump_data(5, (char*)&(pipe_names[pipe_idx].abstr_syntax), 
			          sizeof(pipe_names[pipe_idx].abstr_syntax));
			DEBUG(5,("Bind Transfer Syntax: "));
			dump_data(5, (char*)&(pipe_names[pipe_idx].trans_syntax),
			          sizeof(pipe_names[pipe_idx].trans_syntax));

			/* copy the required syntaxes out so we can do the right bind */
			*transfer = pipe_names[pipe_idx].trans_syntax;
			*abstract = pipe_names[pipe_idx].abstr_syntax;

			return True;
		}
		pipe_idx++;
	};

	DEBUG(5,("Bind RPC Pipe[%s] unsupported\n", pipe_name));
	return False;
}

/****************************************************************************
 check the rpc bind acknowledge response
****************************************************************************/

static BOOL check_bind_response(RPC_HDR_BA *hdr_ba, char *pipe_name, RPC_IFACE *transfer)
{
	int i = 0;

	while ((pipe_names[i].client_pipe != NULL) && hdr_ba->addr.len > 0)
	{
		DEBUG(6,("bind_rpc_pipe: searching pipe name: client:%s server:%s\n",
		pipe_names[i].client_pipe , pipe_names[i].server_pipe ));

		if ((strequal(pipe_name, pipe_names[i].client_pipe )))
		{
			if (strequal(hdr_ba->addr.str, pipe_names[i].server_pipe ))
			{
				DEBUG(5,("bind_rpc_pipe: server pipe_name found: %s\n",
				         pipe_names[i].server_pipe ));
				break;
			}
			else
			{
				DEBUG(4,("bind_rpc_pipe: pipe_name %s != expected pipe %s.  oh well!\n",
				         pipe_names[i].server_pipe ,
				         hdr_ba->addr.str));
				break;
			}
		}
		else
		{
			i++;
		}
	}

	if (pipe_names[i].server_pipe == NULL)
	{
		DEBUG(2,("bind_rpc_pipe: pipe name %s unsupported\n", hdr_ba->addr.str));
		return False;
	}

	/* check the transfer syntax */
	if ((hdr_ba->transfer.version != transfer->version) ||
	     (memcmp(&hdr_ba->transfer.uuid, &transfer->uuid, sizeof(transfer->uuid)) !=0))
	{
		DEBUG(0,("bind_rpc_pipe: transfer syntax differs\n"));
		return False;
	}

	/* lkclXXXX only accept one result: check the result(s) */
	if (hdr_ba->res.num_results != 0x1 || hdr_ba->res.result != 0)
	{
		DEBUG(2,("bind_rpc_pipe: bind denied results: %d reason: %x\n",
		          hdr_ba->res.num_results, hdr_ba->res.reason));
	}

	DEBUG(5,("bind_rpc_pipe: accepted!\n"));
	return True;
}

/****************************************************************************
do an rpc bind
****************************************************************************/

static BOOL rpc_pipe_bind(struct cli_state *cli, char *pipe_name, char *my_name)
{
	RPC_IFACE abstract;
	RPC_IFACE transfer;
	prs_struct hdr;
	prs_struct hdr_rb;
	prs_struct hdr_auth;
	prs_struct auth_req;
	prs_struct auth_ntlm;
	prs_struct data;
	prs_struct rdata;
	prs_struct rparam;

	BOOL valid_ack = False;
	BOOL ntlmssp_auth = cli->ntlmssp_cli_flgs != 0;
	uint32 rpc_call_id;

	if (pipe_name == NULL )
	{
		return False;
	}

	DEBUG(5,("Bind RPC Pipe[%x]: %s\n", cli->nt_pipe_fnum, pipe_name));

	if (!valid_pipe_name(pipe_name, &abstract, &transfer)) return False;

	prs_init(&hdr      , 0x10                     , 4, 0x0          , False);
	prs_init(&hdr_rb   , 1024                     , 4, SAFETY_MARGIN, False);
	prs_init(&hdr_auth , (ntlmssp_auth ?    8 : 0), 4, SAFETY_MARGIN, False);
	prs_init(&auth_req , (ntlmssp_auth ? 1024 : 0), 4, SAFETY_MARGIN, False);
	prs_init(&auth_ntlm, (ntlmssp_auth ? 1024 : 0), 4, SAFETY_MARGIN, False);

	prs_init(&rdata    , 0   , 4, SAFETY_MARGIN, True);
	prs_init(&rparam   , 0   , 4, SAFETY_MARGIN, True);

	rpc_call_id = get_rpc_call_id();
	create_rpc_bind_req(&hdr, &hdr_rb,
	                    ntlmssp_auth ? &hdr_auth : NULL,
	                    ntlmssp_auth ? &auth_req : NULL,
	                    ntlmssp_auth ? &auth_ntlm : NULL,
	                    rpc_call_id,
	                    &abstract, &transfer,
	                    global_myname, cli->domain, cli->ntlmssp_cli_flgs);

	/* this is a hack due to limitations in rpc_api_pipe */
	prs_init(&data, mem_buf_len(hdr.data), 4, 0x0, False);
	mem_buf_copy(data.data->data, hdr.data, 0, mem_buf_len(hdr.data));

	/* send data on \PIPE\.  receive a response */
	if (rpc_api_pipe(cli, 0x0026, NULL, &data, &rparam, &rdata))
	{
		RPC_HDR_BA   hdr_ba;
		RPC_HDR_AUTH rhdr_auth;
		RPC_AUTH_VERIFIER rhdr_verf;
		RPC_AUTH_NTLMSSP_CHAL rhdr_chal;

		DEBUG(5, ("rpc_api_pipe: return OK\n"));

		smb_io_rpc_hdr_ba("", &hdr_ba, &rdata, 0);

		if (rdata.offset != 0)
		{
			valid_ack = check_bind_response(&hdr_ba, pipe_name, &transfer);
		}

		if (valid_ack)
		{
			cli->max_xmit_frag = hdr_ba.bba.max_tsize;
			cli->max_recv_frag = hdr_ba.bba.max_rsize;
		}

		if (valid_ack && ntlmssp_auth)
		{
			smb_io_rpc_hdr_auth("", &rhdr_auth, &rdata, 0);
			if (rdata.offset == 0) valid_ack = False;
		}

		if (valid_ack && ntlmssp_auth)
		{
			smb_io_rpc_auth_verifier("", &rhdr_verf, &rdata, 0);
			if (rdata.offset == 0) valid_ack = False;
		}
		if (valid_ack && ntlmssp_auth)
		{
			smb_io_rpc_auth_ntlmssp_chal("", &rhdr_chal, &rdata, 0);
			if (rdata.offset == 0) valid_ack = False;
		}
		if (valid_ack && ntlmssp_auth)
		{
			unsigned char p24[24];
			unsigned char lm_owf[24];
			unsigned char lm_hash[16];

			prs_struct hdra;
			prs_struct hdr_autha;
			prs_struct auth_resp;
			prs_struct dataa;

			cli->ntlmssp_cli_flgs = rhdr_chal.neg_flags;

			prs_init(&hdra     , 0x10, 4, 0x0          , False);
			prs_init(&hdr_autha, 1024, 4, SAFETY_MARGIN, False);
			prs_init(&auth_resp, 1024, 4, SAFETY_MARGIN, False);

			pwd_make_lm_nt_owf(&cli->pwd, rhdr_chal.challenge);

			create_rpc_bind_resp(&cli->pwd, cli->domain,
			                     cli->user_name, global_myname, 
			                     cli->ntlmssp_cli_flgs,
			                     rpc_call_id,
			                     &hdra, &hdr_autha, &auth_resp);
			                    
			pwd_get_lm_nt_owf(&cli->pwd, lm_owf, NULL);
			pwd_get_lm_nt_16(&cli->pwd, lm_hash, NULL);
			NTLMSSPOWFencrypt(lm_hash, lm_owf, p24);
			{
				unsigned char j = 0;
				int ind;
				unsigned char k2[8];

				memcpy(k2, p24, 5);
				k2[5] = 0xe5;
				k2[6] = 0x38;
				k2[7] = 0xb0;

				for (ind = 0; ind < 256; ind++)
				{
					cli->ntlmssp_hash[ind] = (unsigned char)ind;
				}

				for( ind = 0; ind < 256; ind++)
				{
					unsigned char tc;

					j += (cli->ntlmssp_hash[ind] + k2[ind%8]);

					tc = cli->ntlmssp_hash[ind];
					cli->ntlmssp_hash[ind] = cli->ntlmssp_hash[j];
					cli->ntlmssp_hash[j] = tc;
				}

				cli->ntlmssp_hash[256] = 0;
				cli->ntlmssp_hash[257] = 0;
			}
/*			NTLMSSPhash(cli->ntlmssp_hash, p24); */
			memset((char *)lm_hash, '\0', sizeof(lm_hash));

			/* this is a hack due to limitations in rpc_api_pipe */
			prs_init(&dataa, mem_buf_len(hdra.data), 4, 0x0, False);
			mem_buf_copy(dataa.data->data, hdra.data, 0, mem_buf_len(hdra.data));

			if (cli_write(cli, cli->nt_pipe_fnum, 0x0008,
			          dataa.data->data, 0,
			          dataa.data->data_used) < 0)
			{
				valid_ack = False;
			}

			if (valid_ack)
			{
				cli->ntlmssp_srv_flgs = rhdr_chal.neg_flags;
			}

			prs_mem_free(&hdra);
			prs_mem_free(&dataa);
			prs_mem_free(&hdr_autha);
			prs_mem_free(&auth_resp);
		}
	}

	prs_mem_free(&data     );
	prs_mem_free(&hdr      );
	prs_mem_free(&hdr_rb   );
	prs_mem_free(&hdr_auth );
	prs_mem_free(&auth_req );
	prs_mem_free(&auth_ntlm);
	prs_mem_free(&rdata    );
	prs_mem_free(&rparam   );

	return valid_ack;
}

/****************************************************************************
 set ntlmssp negotiation flags
 ****************************************************************************/

void cli_nt_set_ntlmssp_flgs(struct cli_state *cli, uint32 ntlmssp_flgs)
{
	cli->ntlmssp_cli_flgs = ntlmssp_flgs;
}


/****************************************************************************
 open a session
 ****************************************************************************/

BOOL cli_nt_session_open(struct cli_state *cli, char *pipe_name)
{
	int fnum;

	/******************* open the pipe *****************/
	if (IS_BITS_SET_ALL(cli->capabilities, CAP_NT_SMBS))
	{
		if ((fnum = cli_nt_create(cli, &(pipe_name[5]))) == -1)
		{
			DEBUG(0,("cli_nt_session_open: cli_nt_create failed on pipe %s to machine %s.  Error was %s\n",
				 &(pipe_name[5]), cli->desthost, cli_errstr(cli)));
			return False;
		}

		cli->nt_pipe_fnum = (uint16)fnum;
	}
	else
	{
		if ((fnum = cli_open(cli, pipe_name, O_CREAT|O_RDWR, DENY_NONE)) == -1)
		{
			DEBUG(0,("cli_nt_session_open: cli_open failed on pipe %s to machine %s.  Error was %s\n",
				 pipe_name, cli->desthost, cli_errstr(cli)));
			return False;
		}

		cli->nt_pipe_fnum = (uint16)fnum;

		/**************** Set Named Pipe State ***************/
		if (!rpc_pipe_set_hnd_state(cli, pipe_name, 0x4300))
		{
			DEBUG(0,("cli_nt_session_open: pipe hnd state failed.  Error was %s\n",
				  cli_errstr(cli)));
			cli_close(cli, cli->nt_pipe_fnum);
			return False;
		}

	}

	/******************* bind request on pipe *****************/

	if (!rpc_pipe_bind(cli, pipe_name, global_myname))
	{
		DEBUG(0,("cli_nt_session_open: rpc bind failed. Error was %s\n",
		          cli_errstr(cli)));
		cli_close(cli, cli->nt_pipe_fnum);
		return False;
	}

	/* 
	 * Setup the remote server name prefixed by \ and the machine account name.
	 */

	fstrcpy(cli->srv_name_slash, "\\\\");
	fstrcat(cli->srv_name_slash, cli->desthost);
	strupper(cli->srv_name_slash);

	fstrcpy(cli->clnt_name_slash, "\\\\");
	fstrcat(cli->clnt_name_slash, global_myname);
	strupper(cli->clnt_name_slash);

	fstrcpy(cli->mach_acct, global_myname);
	fstrcat(cli->mach_acct, "$");
	strupper(cli->mach_acct);

	return True;
}

/****************************************************************************
close the session
****************************************************************************/

void cli_nt_session_close(struct cli_state *cli)
{
	cli_close(cli, cli->nt_pipe_fnum);
}
