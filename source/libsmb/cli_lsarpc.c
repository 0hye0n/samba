/* 
   Unix SMB/Netbios implementation.
   Version 2.2
   RPC pipe client
   Copyright (C) Tim Potter                        2000-2001,
   Copyright (C) Andrew Tridgell              1992-1997,2000,
   Copyright (C) Luke Kenneth Casson Leighton 1996-1997,2000,
   Copyright (C) Paul Ashton                       1997,2000,
   Copyright (C) Elrond                                 2000.
   
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

/* Opens a SMB connection to the lsa pipe */

struct cli_state *cli_lsa_initialise(struct cli_state *cli, char *system_name,
				     struct ntuser_creds *creds)
{
        return cli_pipe_initialise(cli, system_name, PIPE_LSASS, creds);
}

/* Open a LSA policy handle */

uint32 cli_lsa_open_policy(struct cli_state *cli, TALLOC_CTX *mem_ctx,
			   BOOL sec_qos, uint32 des_access, POLICY_HND *pol)
{
	prs_struct qbuf, rbuf;
	LSA_Q_OPEN_POL q;
	LSA_R_OPEN_POL r;
	LSA_SEC_QOS qos;
	uint32 result;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Initialise input parameters */

	if (sec_qos) {
		init_lsa_sec_qos(&qos, 2, 1, 0, des_access);
		init_q_open_pol(&q, '\\', 0, des_access, &qos);
	} else {
		init_q_open_pol(&q, '\\', 0, des_access, NULL);
	}

	/* Marshall data and send request */

	if (!lsa_io_q_open_pol("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, LSA_OPENPOLICY, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!lsa_io_r_open_pol("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Return output parameters */

	if ((result = r.status) == NT_STATUS_OK) {
		*pol = r.pol;
	}

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Open a LSA policy handle */

uint32 cli_lsa_open_policy2(struct cli_state *cli, TALLOC_CTX *mem_ctx,
                            BOOL sec_qos, uint32 des_access, POLICY_HND *pol)
{
	prs_struct qbuf, rbuf;
	LSA_Q_OPEN_POL2 q;
	LSA_R_OPEN_POL2 r;
	LSA_SEC_QOS qos;
	uint32 result;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Initialise input parameters */

	if (sec_qos) {
		init_lsa_sec_qos(&qos, 2, 1, 0, des_access);
		init_q_open_pol2(&q, cli->clnt_name_slash, 0, des_access, 
                                 &qos);
	} else {
		init_q_open_pol2(&q, cli->clnt_name_slash, 0, des_access, 
                                 NULL);
	}

	/* Marshall data and send request */

	if (!lsa_io_q_open_pol2("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, LSA_OPENPOLICY2, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!lsa_io_r_open_pol2("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Return output parameters */

	if ((result = r.status) == NT_STATUS_OK) {
		*pol = r.pol;
	}

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Close a LSA policy handle */

uint32 cli_lsa_close(struct cli_state *cli, TALLOC_CTX *mem_ctx, 
		     POLICY_HND *pol)
{
	prs_struct qbuf, rbuf;
	LSA_Q_CLOSE q;
	LSA_R_CLOSE r;
	uint32 result;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Marshall data and send request */

	init_lsa_q_close(&q, pol);

	if (!lsa_io_q_close("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, LSA_CLOSE, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!lsa_io_r_close("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Return output parameters */

	if ((result = r.status) == NT_STATUS_OK) {
		*pol = r.pol;
	}

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Lookup a list of sids */

uint32 cli_lsa_lookup_sids(struct cli_state *cli, TALLOC_CTX *mem_ctx,
			   POLICY_HND *pol, int num_sids, DOM_SID *sids, 
			   char ***names, uint32 **types, int *num_names)
{
	prs_struct qbuf, rbuf;
	LSA_Q_LOOKUP_SIDS q;
	LSA_R_LOOKUP_SIDS r;
	DOM_R_REF ref;
	LSA_TRANS_NAME_ENUM t_names;
	uint32 result;
	int i;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Marshall data and send request */

	init_q_lookup_sids(mem_ctx, &q, pol, num_sids, sids, 1);

	if (!lsa_io_q_lookup_sids("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, LSA_LOOKUPSIDS, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	ZERO_STRUCT(ref);
	ZERO_STRUCT(t_names);

	r.dom_ref = &ref;
	r.names = &t_names;

	if (!lsa_io_r_lookup_sids("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	result = r.status;

	if (result != NT_STATUS_OK && result != 0x00000107 &&
	    result != (0xC0000000 | NT_STATUS_NONE_MAPPED)) {
		
		/* An actual error occured */

		goto done;
	}

	result = NT_STATUS_OK;

	/* Return output parameters */

	(*num_names) = r.names->num_entries;
	
	if (!((*names) = (char **)talloc(mem_ctx, sizeof(char *) * 
					 r.names->num_entries))) {
		DEBUG(0, ("cli_lsa_lookup_sids(): out of memory\n"));
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	if (!((*types) = (uint32 *)talloc(mem_ctx, sizeof(uint32) * 
				      r.names->num_entries))) {
		DEBUG(0, ("cli_lsa_lookup_sids(): out of memory\n"));
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}
		
	for (i = 0; i < r.names->num_entries; i++) {
		fstring name, dom_name, full_name;
		uint32 dom_idx = t_names.name[i].domain_idx;

		/* Translate optimised name through domain index array */

		if (dom_idx != 0xffffffff) {

			rpcstr_pull_unistr2_fstring(
                                dom_name, &ref.ref_dom[dom_idx].uni_dom_name);
			rpcstr_pull_unistr2_fstring(
                                name, &t_names.uni_name[i]);

			slprintf(full_name, sizeof(full_name) - 1,
				 "%s%s%s", dom_name, 
                                 (dom_name[0] && name[0]) ? 
				 lp_winbind_separator() : "", name);

			(*names)[i] = talloc_strdup(mem_ctx, full_name);
			(*types)[i] = t_names.name[i].sid_name_use;

		} else {
			(*names)[i] = NULL;
			(*types)[i] = SID_NAME_UNKNOWN;
		}
	}

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Lookup a list of names */

uint32 cli_lsa_lookup_names(struct cli_state *cli, TALLOC_CTX *mem_ctx,
			    POLICY_HND *pol, int num_names, char **names, 
			    DOM_SID **sids, uint32 **types, int *num_sids)
{
	prs_struct qbuf, rbuf;
	LSA_Q_LOOKUP_NAMES q;
	LSA_R_LOOKUP_NAMES r;
	DOM_R_REF ref;
	uint32 result;
	int i;
	
	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Marshall data and send request */

	init_q_lookup_names(mem_ctx, &q, pol, num_names, names);

	if (!lsa_io_q_lookup_names("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, LSA_LOOKUPNAMES, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}
	
	/* Unmarshall response */

	ZERO_STRUCT(ref);
	r.dom_ref = &ref;

	if (!lsa_io_r_lookup_names("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	result = r.status;

	if (result != NT_STATUS_OK && 
	    result != (0xC0000000 | NT_STATUS_NONE_MAPPED)) {

		/* An actual error occured */

		goto done;
	}

	result = NT_STATUS_OK;

	/* Return output parameters */

	(*num_sids) = r.num_entries;

	if (!((*sids = (DOM_SID *)talloc(mem_ctx, sizeof(DOM_SID) *
					 r.num_entries)))) {
		DEBUG(0, ("cli_lsa_lookup_sids(): out of memory\n"));
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	if (!((*types = (uint32 *)talloc(mem_ctx, sizeof(uint32) *
					  r.num_entries)))) {
		DEBUG(0, ("cli_lsa_lookup_sids(): out of memory\n"));
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	for (i = 0; i < r.num_entries; i++) {
		DOM_RID2 *t_rids = r.dom_rid;
		uint32 dom_idx = t_rids[i].rid_idx;
		uint32 dom_rid = t_rids[i].rid;
		DOM_SID *sid = &(*sids)[i];

		/* Translate optimised sid through domain index array */

		if (dom_idx != 0xffffffff) {

			sid_copy(sid, &ref.ref_dom[dom_idx].ref_dom.sid);

			if (dom_rid != 0xffffffff) {
				sid_append_rid(sid, dom_rid);
			}

			(*types)[i] = t_rids[i].type;
		} else {
			ZERO_STRUCTP(sid);
			(*types)[i] = SID_NAME_UNKNOWN;
		}
	}

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Query info policy */

uint32 cli_lsa_query_info_policy(struct cli_state *cli, TALLOC_CTX *mem_ctx,
				 POLICY_HND *pol, uint16 info_class, 
				 fstring domain_name, DOM_SID *domain_sid)
{
	prs_struct qbuf, rbuf;
	LSA_Q_QUERY_INFO q;
	LSA_R_QUERY_INFO r;
	uint32 result;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Marshall data and send request */

	init_q_query(&q, pol, info_class);

	if (!lsa_io_q_query("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, LSA_QUERYINFOPOLICY, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!lsa_io_r_query("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	if ((result = r.status) != NT_STATUS_OK) {
		goto done;
	}

	/* Return output parameters */

	ZERO_STRUCTP(domain_sid);
	domain_name[0] = '\0';

	switch (info_class) {

	case 3:
		if (r.dom.id3.buffer_dom_name != 0) {
			unistr2_to_ascii(domain_name,
					 &r.dom.id3.
					 uni_domain_name,
					 sizeof (fstring) - 1);
		}

		if (r.dom.id3.buffer_dom_sid != 0) {
			*domain_sid = r.dom.id3.dom_sid.sid;
		}

		break;

	case 5:
		
		if (r.dom.id5.buffer_dom_name != 0) {
			unistr2_to_ascii(domain_name, &r.dom.id5.
					 uni_domain_name,
					 sizeof (fstring) - 1);
		}
			
		if (r.dom.id5.buffer_dom_sid != 0) {
			*domain_sid = r.dom.id5.dom_sid.sid;
		}

		break;
		
	default:
		DEBUG(3, ("unknown info class %d\n", info_class));
		break;		      
	}
	
 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}

/* Enumerate list of trusted domains */

uint32 cli_lsa_enum_trust_dom(struct cli_state *cli, TALLOC_CTX *mem_ctx,
			      POLICY_HND *pol, uint32 *enum_ctx, 
			      uint32 *num_domains, char ***domain_names, 
			      DOM_SID **domain_sids)
{
	prs_struct qbuf, rbuf;
	LSA_Q_ENUM_TRUST_DOM q;
	LSA_R_ENUM_TRUST_DOM r;
	uint32 result;
	int i;

	ZERO_STRUCT(q);
	ZERO_STRUCT(r);

	/* Initialise parse structures */

	prs_init(&qbuf, MAX_PDU_FRAG_LEN, mem_ctx, MARSHALL);
	prs_init(&rbuf, 0, mem_ctx, UNMARSHALL);

	/* Marshall data and send request */

        init_q_enum_trust_dom(&q, pol, *enum_ctx, 0xffffffff);

	if (!lsa_io_q_enum_trust_dom("", &q, &qbuf, 0) ||
	    !rpc_api_pipe_req(cli, LSA_ENUMTRUSTDOM, &qbuf, &rbuf)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	/* Unmarshall response */

	if (!lsa_io_r_enum_trust_dom("", &r, &rbuf, 0)) {
		result = NT_STATUS_UNSUCCESSFUL;
		goto done;
	}

	result = r.status;

	/* For some undocumented reason this function sometimes returns
	   0x8000001a (NT_STATUS_UNABLE_TO_FREE_VM) so we ignore it and
	   pretend everything is OK. */

	if (result != NT_STATUS_OK && 
	    result != NT_STATUS_UNABLE_TO_FREE_VM) {

		/* An actual error ocured */

		goto done;
	}

	result = NT_STATUS_OK;

	/* Return output parameters */

	if (r.num_domains) {

		/* Allocate memory for trusted domain names and sids */

		*domain_names = (char **)talloc(mem_ctx, sizeof(char *) *
						r.num_domains);

		if (!*domain_names) {
			DEBUG(0, ("cli_lsa_enum_trust_dom(): out of memory\n"));
			result = NT_STATUS_UNSUCCESSFUL;
			goto done;
		}

		*domain_sids = (DOM_SID *)talloc(mem_ctx, sizeof(DOM_SID) *
						 r.num_domains);
		if (!domain_sids) {
			DEBUG(0, ("cli_lsa_enum_trust_dom(): out of memory\n"));
			result = NT_STATUS_UNSUCCESSFUL;
			goto done;
		}

		/* Copy across names and sids */

		for (i = 0; i < r.num_domains; i++) {
			fstring tmp;

			unistr2_to_ascii(tmp, &r.uni_domain_name[i], 
					 sizeof(tmp) - 1);
			(*domain_names)[i] = strdup(tmp);
			sid_copy(&(*domain_sids)[i], &r.domain_sid[i].sid);
		}
	}

	*num_domains = r.num_domains;
	*enum_ctx = r.enum_context;

 done:
	prs_mem_free(&qbuf);
	prs_mem_free(&rbuf);

	return result;
}
