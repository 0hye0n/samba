/* 
 *  Unix SMB/Netbios implementation.
 *  Version 1.9.
 *  RPC Pipe client / server routines
 *  Copyright (C) Andrew Tridgell              1992-1997,
 *  Copyright (C) Luke Kenneth Casson Leighton 1996-1997,
 *  Copyright (C) Paul Ashton                       1997.
 *  Copyright (C) Jeremy Allison                    2001.
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

/* This is the implementation of the lsa server code. */

#include "includes.h"

extern int DEBUGLEVEL;
extern DOM_SID global_sam_sid;
extern fstring global_myworkgroup;
extern pstring global_myname;

/***************************************************************************
Init dom_query
 ***************************************************************************/

static void init_dom_query(DOM_QUERY *d_q, char *dom_name, DOM_SID *dom_sid)
{
	int domlen = (dom_name != NULL) ? strlen(dom_name) : 0;

	/*
	 * I'm not sure why this really odd combination of length
	 * values works, but it does appear to. I need to look at
	 * this *much* more closely - but at the moment leave alone
	 * until it's understood. This allows a W2k client to join
	 * a domain with both odd and even length names... JRA.
	 */

	d_q->uni_dom_str_len = domlen ? ((domlen + 1) * 2) : 0;
	d_q->uni_dom_max_len = domlen * 2;
	d_q->buffer_dom_name = domlen != 0 ? 1 : 0; /* domain buffer pointer */
	d_q->buffer_dom_sid = dom_sid != NULL ? 1 : 0;  /* domain sid pointer */

	/* this string is supposed to be character short */
	init_unistr2(&d_q->uni_domain_name, dom_name, domlen);
	d_q->uni_domain_name.uni_max_len++;

	if (dom_sid != NULL)
		init_dom_sid2(&d_q->dom_sid, dom_sid);
}

/***************************************************************************
 init_dom_ref - adds a domain if it's not already in, returns the index.
***************************************************************************/

static int init_dom_ref(DOM_R_REF *ref, char *dom_name, DOM_SID *dom_sid)
{
	int num = 0;
	int len;

	if (dom_name != NULL) {
		for (num = 0; num < ref->num_ref_doms_1; num++) {
			fstring domname;
			fstrcpy(domname, dos_unistr2_to_str(&ref->ref_dom[num].uni_dom_name));
			if (strequal(domname, dom_name))
				return num;
		}
	} else {
		num = ref->num_ref_doms_1;
	}

	if (num >= MAX_REF_DOMAINS) {
		/* index not found, already at maximum domain limit */
		return -1;
	}

	ref->num_ref_doms_1 = num+1;
	ref->ptr_ref_dom  = 1;
	ref->max_entries = MAX_REF_DOMAINS;
	ref->num_ref_doms_2 = num+1;

	len = (dom_name != NULL) ? strlen(dom_name) : 0;
	if(dom_name != NULL && len == 0)
		len = 1;

	init_uni_hdr(&ref->hdr_ref_dom[num].hdr_dom_name, len);
	ref->hdr_ref_dom[num].ptr_dom_sid = dom_sid != NULL ? 1 : 0;

	init_unistr2(&ref->ref_dom[num].uni_dom_name, dom_name, len);
	init_dom_sid2(&ref->ref_dom[num].ref_dom, dom_sid );

	return num;
}

/***************************************************************************
 init_lsa_rid2s
 ***************************************************************************/

static void init_lsa_rid2s(DOM_R_REF *ref, DOM_RID2 *rid2,
				int num_entries, UNISTR2 name[MAX_LOOKUP_SIDS],
				uint32 *mapped_count)
{
	int i;
	int total = 0;
	*mapped_count = 0;

	SMB_ASSERT(num_entries <= MAX_LOOKUP_SIDS);

	for (i = 0; i < num_entries; i++) {
		BOOL status = False;
		DOM_SID sid;
		uint32 rid = 0xffffffff;
		int dom_idx = -1;
		pstring full_name;
		fstring dom_name, user;
		enum SID_NAME_USE name_type = SID_NAME_UNKNOWN;

		/* Split name into domain and user component */

		pstrcpy(full_name, dos_unistr2_to_str(&name[i]));
		split_domain_name(full_name, dom_name, user);

		/* Lookup name */

		DEBUG(5, ("init_lsa_rid2s: looking up name %s\n", full_name));

		status = lookup_name(full_name, &sid, &name_type);

		DEBUG(5, ("init_lsa_rid2s: %s\n", status ? "found" : 
			  "not found"));

		if (status) {
			sid_split_rid(&sid, &rid);
			dom_idx = init_dom_ref(ref, dom_name, &sid);
			(*mapped_count)++;
		} else {
			dom_idx = -1;
			rid = 0xffffffff;
			name_type = SID_NAME_UNKNOWN;
		}

		init_dom_rid2(&rid2[total], rid, name_type, dom_idx);
		total++;
	}
}

/***************************************************************************
 init_reply_lookup_names
 ***************************************************************************/

static void init_reply_lookup_names(LSA_R_LOOKUP_NAMES *r_l,
                DOM_R_REF *ref, uint32 num_entries,
                DOM_RID2 *rid2, uint32 mapped_count)
{
	r_l->ptr_dom_ref  = 1;
	r_l->dom_ref      = ref;

	r_l->num_entries  = num_entries;
	r_l->ptr_entries  = 1;
	r_l->num_entries2 = num_entries;
	r_l->dom_rid      = rid2;

	r_l->mapped_count = mapped_count;

	if (mapped_count == 0)
		r_l->status = NT_STATUS_NONE_MAPPED;
	else
		r_l->status = NT_STATUS_NO_PROBLEMO;
}

/***************************************************************************
 Init lsa_trans_names.
 ***************************************************************************/

static void init_lsa_trans_names(TALLOC_CTX *ctx, DOM_R_REF *ref, LSA_TRANS_NAME_ENUM *trn,
				 int num_entries, DOM_SID2 *sid,
				 uint32 *mapped_count)
{
	int i;
	int total = 0;
	*mapped_count = 0;

	/* Allocate memory for list of names */

	if (num_entries > 0) {
		if (!(trn->name = (LSA_TRANS_NAME *)talloc(ctx, sizeof(LSA_TRANS_NAME) *
							  num_entries))) {
			DEBUG(0, ("init_lsa_trans_names(): out of memory\n"));
			return;
		}

		if (!(trn->uni_name = (UNISTR2 *)talloc(ctx, sizeof(UNISTR2) * 
							num_entries))) {
			DEBUG(0, ("init_lsa_trans_names(): out of memory\n"));
			return;
		}
	}

	for (i = 0; i < num_entries; i++) {
		BOOL status = False;
		DOM_SID find_sid = sid[i].sid;
		uint32 rid = 0xffffffff;
		int dom_idx = -1;
		fstring name, dom_name;
		enum SID_NAME_USE sid_name_use = (enum SID_NAME_USE)0;

		sid_to_string(name, &find_sid);
		DEBUG(5, ("init_lsa_trans_names: looking up sid %s\n", name));

		/* Lookup sid from winbindd */

		memset(dom_name, '\0', sizeof(dom_name));
		memset(name, '\0', sizeof(name));

		status = lookup_sid(&find_sid, dom_name, name, &sid_name_use);

		DEBUG(5, ("init_lsa_trans_names: %s\n", status ? "found" : 
			  "not found"));

		if (!status) {
			sid_name_use = SID_NAME_UNKNOWN;
		}

		/* Store domain sid in ref array */

		if (find_sid.num_auths == 5) {
			sid_split_rid(&find_sid, &rid);
		}

		dom_idx = init_dom_ref(ref, dom_name, &find_sid);

		DEBUG(10,("init_lsa_trans_names: added user '%s\\%s' to "
			  "referenced list.\n", dom_name, name ));

		(*mapped_count)++;

		init_lsa_trans_name(&trn->name[total], &trn->uni_name[total],
					sid_name_use, name, dom_idx);
		total++;
	}

	trn->num_entries = total;
	trn->ptr_trans_names = 1;
	trn->num_entries2 = total;
}

/***************************************************************************
 Init_reply_lookup_sids.
 ***************************************************************************/

static void init_reply_lookup_sids(LSA_R_LOOKUP_SIDS *r_l,
                DOM_R_REF *ref, LSA_TRANS_NAME_ENUM *names,
                uint32 mapped_count)
{
	r_l->ptr_dom_ref  = 1;
	r_l->dom_ref      = ref;
	r_l->names        = names;
	r_l->mapped_count = mapped_count;

	if (mapped_count == 0)
		r_l->status = NT_STATUS_NONE_MAPPED;
	else
		r_l->status = NT_STATUS_NO_PROBLEMO;
}

/***************************************************************************
 _lsa_open_policy2.
 ***************************************************************************/

uint32 _lsa_open_policy2(pipes_struct *p, LSA_Q_OPEN_POL2 *q_u, LSA_R_OPEN_POL2 *r_u)
{
	int i;

	/* lkclXXXX having decoded it, ignore all fields in the open policy! */

	/* set up the LSA QUERY INFO response */

	for (i = 4; i < POL_HND_SIZE; i++)
		r_u->pol.data[i] = i;

	return NT_STATUS_NOPROBLEMO;
}

/***************************************************************************
 _lsa_open_policy
 ***************************************************************************/

uint32 _lsa_open_policy(pipes_struct *p, LSA_Q_OPEN_POL *q_u, LSA_R_OPEN_POL *r_u)
{
	int i;

	/* lkclXXXX having decoded it, ignore all fields in the open policy! */

	/* set up the LSA QUERY INFO response */

	for (i = 4; i < POL_HND_SIZE; i++)
		r_u->pol.data[i] = i;

	return NT_STATUS_NOPROBLEMO;
}

/***************************************************************************
 _lsa_enum_trust_dom - this needs fixing to do more than return NULL ! JRA.
 ***************************************************************************/

uint32 _lsa_enum_trust_dom(pipes_struct *p, LSA_Q_ENUM_TRUST_DOM *q_u, LSA_R_ENUM_TRUST_DOM *r_u)
{
	uint32 enum_context = 0;
	char *dom_name = NULL;
	DOM_SID *dom_sid = NULL;

	/* set up the LSA QUERY INFO response */
	init_r_enum_trust_dom(r_u, enum_context, dom_name, dom_sid,
	      dom_name != NULL ? NT_STATUS_NO_PROBLEMO : NT_STATUS_UNABLE_TO_FREE_VM);

	return r_u->status;
}

/***************************************************************************
 _lsa_query_info
 ***************************************************************************/

uint32 _lsa_query_info(pipes_struct *p, LSA_Q_QUERY_INFO *q_u, LSA_R_QUERY_INFO *r_u)
{
	LSA_INFO_UNION *info = &r_u->dom;
	DOM_SID domain_sid;
	char *name = NULL;
	DOM_SID *sid = NULL;

	r_u->status = NT_STATUS_NO_PROBLEMO;

	switch (q_u->info_class) {
	case 0x02:
		{
			unsigned int i;
			/* fake info: We audit everything. ;) */
			info->id2.auditing_enabled = 1;
            info->id2.count1 = 7;
            info->id2.count2 = 7;
			if ((info->id2.auditsettings = (uint32 *)talloc(p->mem_ctx,7*sizeof(uint32))) == NULL)
				return False;
            for (i = 0; i < 7; i++)
                info->id2.auditsettings[i] = 3;
            break;
		}
	case 0x03:
		switch (lp_server_role())
		{
			case ROLE_DOMAIN_PDC:
			case ROLE_DOMAIN_BDC:
				name = global_myworkgroup;
				sid = &global_sam_sid;
				break;
			case ROLE_DOMAIN_MEMBER:
				if (secrets_fetch_domain_sid(global_myworkgroup,
					&domain_sid))
				{
					name = global_myworkgroup;
					sid = &domain_sid;
				}
			default:
				break;
		}
		init_dom_query(&r_u->dom.id3, name, sid);
		break;
	case 0x05:
		name = global_myname;
		sid = &global_sam_sid;
		init_dom_query(&r_u->dom.id5, name, sid);
		break;
	case 0x06:
		switch (lp_server_role())
		{
			case ROLE_DOMAIN_BDC:
				/*
				 * only a BDC is a backup controller
				 * of the domain, it controls.
				 */
				info->id6.server_role = 2;
				break;
			default:
				/*
				 * any other role is a primary
				 * of the domain, it controls.
				 */
				info->id6.server_role = 3;
				break; 
		}
		break;
	default:
		DEBUG(0,("_lsa_query_info: unknown info level in Lsa Query: %d\n", q_u->info_class));
		r_u->status = NT_STATUS_INVALID_INFO_CLASS;
		break;
	}

	if(r_u->status == NT_STATUS_NO_PROBLEMO) {
		r_u->undoc_buffer = 0x22000000; /* bizarre */
		r_u->info_class = q_u->info_class;
	}

	return r_u->status;
}

/***************************************************************************
 _lsa_lookup_sids
 ***************************************************************************/

uint32 _lsa_lookup_sids(pipes_struct *p, LSA_Q_LOOKUP_SIDS *q_u, LSA_R_LOOKUP_SIDS *r_u)
{
	DOM_SID2 *sid = q_u->sids.sid;
	int num_entries = q_u->sids.num_entries;
	DOM_R_REF ref;
	LSA_TRANS_NAME_ENUM names;
	uint32 mapped_count = 0;

	ZERO_STRUCT(ref);
	ZERO_STRUCT(names);

	/* set up the LSA Lookup SIDs response */
	init_lsa_trans_names(p->mem_ctx, &ref, &names, num_entries, sid, &mapped_count);
	init_reply_lookup_sids(r_u, &ref, &names, mapped_count);

	return r_u->status;
}

/***************************************************************************
lsa_reply_lookup_names
 ***************************************************************************/

uint32 _lsa_lookup_names(pipes_struct *p,LSA_Q_LOOKUP_NAMES *q_u, LSA_R_LOOKUP_NAMES *r_u)
{
	UNISTR2 *names = q_u->uni_name;
	int num_entries = q_u->num_entries;
	DOM_R_REF ref;
	DOM_RID2 rids[MAX_LOOKUP_SIDS];
	uint32 mapped_count = 0;

	ZERO_STRUCT(ref);
	ZERO_ARRAY(rids);

	/* set up the LSA Lookup RIDs response */
	init_lsa_rid2s(&ref, rids, num_entries, names, &mapped_count);
	init_reply_lookup_names(r_u, &ref, num_entries, rids, mapped_count);

	return r_u->status;
}

/***************************************************************************
 _lsa_close. Also weird - needs to check if lsa handle is correct. JRA.
 ***************************************************************************/

uint32 _lsa_close(pipes_struct *p, LSA_Q_CLOSE *q_u, LSA_R_CLOSE *r_u)
{
	return NT_STATUS_NO_PROBLEMO;
}

/***************************************************************************
  "No more secrets Marty...." :-).
 ***************************************************************************/

uint32 _lsa_open_secret(pipes_struct *p, LSA_Q_OPEN_SECRET *q_u, LSA_R_OPEN_SECRET *r_u)
{
	return NT_STATUS_OBJECT_NAME_NOT_FOUND;
}
