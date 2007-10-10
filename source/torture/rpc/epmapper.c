/* 
   Unix SMB/CIFS implementation.
   test suite for epmapper rpc operations

   Copyright (C) Andrew Tridgell 2003
   
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
#include "system/network.h"
#include "librpc/gen_ndr/ndr_epmapper.h"


/*
  display any protocol tower
 */
static void display_tower(TALLOC_CTX *mem_ctx, struct epm_tower *twr)
{
	int i;
	const char *uuid;

	for (i=0;i<twr->num_floors;i++) {
		struct epm_lhs *lhs = &twr->floors[i].lhs;
		union epm_rhs *rhs = &twr->floors[i].rhs;

		switch(lhs->protocol) {
		case EPM_PROTOCOL_UUID:
			uuid = GUID_string(mem_ctx, &lhs->info.uuid.uuid);
			if (strcasecmp(uuid, NDR_GUID) == 0) {
				printf(" NDR");
			} else {
				printf(" uuid %s/0x%02x", uuid, lhs->info.uuid.version);
			}
			break;

		case EPM_PROTOCOL_NCACN:
			printf(" RPC-C");
			break;

		case EPM_PROTOCOL_NCADG:
			printf(" RPC");
			break;

		case EPM_PROTOCOL_NCALRPC:
			printf(" NCALRPC");
			break;

		case EPM_PROTOCOL_DNET_NSP:
			printf(" DNET/NSP");
			break;

		case EPM_PROTOCOL_IP:
			printf(" IP:%s", rhs->ip.ipaddr);
			break;

		case EPM_PROTOCOL_PIPE:
			printf(" PIPE:%s", rhs->pipe.path);
			break;

		case EPM_PROTOCOL_SMB:
			printf(" SMB:%s", rhs->smb.unc);
			break;

		case EPM_PROTOCOL_UNIX_DS:
			printf(" Unix:%s", rhs->unix_ds.path);
			break;

		case EPM_PROTOCOL_NETBIOS:
			printf(" NetBIOS:%s", rhs->netbios.name);
			break;

		case EPM_PROTOCOL_NETBEUI:
			printf(" NETBeui");
			break;

		case EPM_PROTOCOL_SPX:
			printf(" SPX");
			break;

		case EPM_PROTOCOL_NB_IPX:
			printf(" NB_IPX");
			break;

		case EPM_PROTOCOL_HTTP:
			printf(" HTTP:%d", rhs->http.port);
			break;

		case EPM_PROTOCOL_TCP:
			/* what is the difference between this and 0x1f? */
			printf(" TCP:%d", rhs->tcp.port);
			break;

		case EPM_PROTOCOL_UDP:
			printf(" UDP:%d", rhs->udp.port);
			break;

		default:
			printf(" UNK(%02x):", lhs->protocol);
			if (rhs->unknown.length == 2) {
				printf("%d", RSVAL(rhs->unknown.data, 0));
			}
			break;
		}
	}
	printf("\n");
}


static BOOL test_Map(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
		     struct epm_twr_t *twr)
{
	NTSTATUS status;
	struct epm_Map r;
	struct GUID uuid;
	const char *uuid_str;
	struct policy_handle handle;
	int i;

	ZERO_STRUCT(uuid);
	ZERO_STRUCT(handle);

	r.in.object = &uuid;
	r.in.map_tower = twr;
	r.in.entry_handle = &handle;	
	r.out.entry_handle = &handle;
	r.in.max_towers = 100;

	uuid_str = GUID_string(mem_ctx, &twr->tower.floors[0].lhs.info.uuid.uuid);

	printf("epm_Map results for '%s':\n", 
	       idl_pipe_name(uuid_str, twr->tower.floors[0].lhs.info.uuid.version));

	twr->tower.floors[2].lhs.protocol = EPM_PROTOCOL_NCACN;
	twr->tower.floors[2].lhs.info.lhs_data = data_blob(NULL, 0);
	twr->tower.floors[2].rhs.ncacn.minor_version = 0;

	twr->tower.floors[3].lhs.protocol = EPM_PROTOCOL_TCP;
	twr->tower.floors[3].lhs.info.lhs_data = data_blob(NULL, 0);
	twr->tower.floors[3].rhs.tcp.port = 0;

	twr->tower.floors[4].lhs.protocol = EPM_PROTOCOL_IP;
	twr->tower.floors[4].lhs.info.lhs_data = data_blob(NULL, 0);
	twr->tower.floors[4].rhs.ip.ipaddr = "0.0.0.0";

	status = dcerpc_epm_Map(p, mem_ctx, &r);
	if (NT_STATUS_IS_OK(status) && r.out.result == 0) {
		for (i=0;i<r.out.num_towers;i++) {
			if (r.out.towers[i].twr) {
				display_tower(mem_ctx, &r.out.towers[i].twr->tower);
			}
		}
	}

	twr->tower.floors[3].lhs.protocol = EPM_PROTOCOL_HTTP;
	twr->tower.floors[3].lhs.info.lhs_data = data_blob(NULL, 0);
	twr->tower.floors[3].rhs.http.port = 0;

	status = dcerpc_epm_Map(p, mem_ctx, &r);
	if (NT_STATUS_IS_OK(status) && r.out.result == 0) {
		for (i=0;i<r.out.num_towers;i++) {
			if (r.out.towers[i].twr) {
				display_tower(mem_ctx, &r.out.towers[i].twr->tower);
			}
		}
	}

	twr->tower.floors[3].lhs.protocol = EPM_PROTOCOL_UDP;
	twr->tower.floors[3].lhs.info.lhs_data = data_blob(NULL, 0);
	twr->tower.floors[3].rhs.http.port = 0;

	status = dcerpc_epm_Map(p, mem_ctx, &r);
	if (NT_STATUS_IS_OK(status) && r.out.result == 0) {
		for (i=0;i<r.out.num_towers;i++) {
			if (r.out.towers[i].twr) {
				display_tower(mem_ctx, &r.out.towers[i].twr->tower);
			}
		}
	}

	twr->tower.floors[3].lhs.protocol = EPM_PROTOCOL_SMB;
	twr->tower.floors[3].lhs.info.lhs_data = data_blob(NULL, 0);
	twr->tower.floors[3].rhs.smb.unc = "";

	twr->tower.floors[4].lhs.protocol = EPM_PROTOCOL_NETBIOS;
	twr->tower.floors[4].lhs.info.lhs_data = data_blob(NULL, 0);
	twr->tower.floors[4].rhs.netbios.name = "";

	status = dcerpc_epm_Map(p, mem_ctx, &r);
	if (NT_STATUS_IS_OK(status) && r.out.result == 0) {
		for (i=0;i<r.out.num_towers;i++) {
			if (r.out.towers[i].twr) {
				display_tower(mem_ctx, &r.out.towers[i].twr->tower);
			}
		}
	}

	/* FIXME: Extend to do other protocols as well (ncacn_unix_stream, ncalrpc) */
	
	return True;
}

static BOOL test_Lookup(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	struct epm_Lookup r;
	struct GUID uuid;
	struct rpc_if_id_t iface;
	struct policy_handle handle;

	ZERO_STRUCT(handle);

	r.in.inquiry_type = 0;
	r.in.object = &uuid;
	r.in.interface_id = &iface;
	r.in.vers_option = 0;
	r.in.entry_handle = &handle;
	r.out.entry_handle = &handle;
	r.in.max_ents = 10;

	do {
		int i;

		ZERO_STRUCT(uuid);
		ZERO_STRUCT(iface);

		status = dcerpc_epm_Lookup(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status) || r.out.result != 0) {
			break;
		}

		printf("epm_Lookup returned %d events GUID %s\n", 
		       r.out.num_ents, GUID_string(mem_ctx, &handle.uuid));

		for (i=0;i<r.out.num_ents;i++) {
			printf("\nFound '%s'\n", r.out.entries[i].annotation);
			display_tower(mem_ctx, &r.out.entries[i].tower->tower);
			if (r.out.entries[i].tower->tower.num_floors == 5) {
				test_Map(p, mem_ctx, r.out.entries[i].tower);
			}
		}
	} while (NT_STATUS_IS_OK(status) && 
		 r.out.result == 0 && 
		 r.out.num_ents == r.in.max_ents &&
		 !policy_handle_empty(&handle));

	if (!NT_STATUS_IS_OK(status)) {
		printf("Lookup failed - %s\n", nt_errstr(status));
		return False;
	}


	return True;
}

static BOOL test_Delete(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, struct epm_entry_t *entries)
{
	NTSTATUS status;
	struct epm_Delete r;

	r.in.num_ents = 1;
	r.in.entries = entries;
	
	status = dcerpc_epm_Delete(p, mem_ctx, &r);
	if (NT_STATUS_IS_ERR(status)) {
		printf("Delete failed - %s\n", nt_errstr(status));
		return False;
	}

	if (r.out.result != 0) {
		printf("Delete failed - %d\n", r.out.result);
		return False;
	}

	return True;
}

static BOOL test_Insert(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	struct epm_Insert r;
	struct dcerpc_binding bd;

	r.in.num_ents = 1;

	r.in.entries = talloc_array(mem_ctx, struct epm_entry_t, 1);
	ZERO_STRUCT(r.in.entries[0].object);
	r.in.entries[0].annotation = "smbtorture endpoint";
	status = dcerpc_parse_binding(mem_ctx, "ncalrpc:[SMBTORTURE]", &bd);
	if (NT_STATUS_IS_ERR(status)) {
		printf("Unable to generate dcerpc_binding struct\n");
		return False;
	}

	r.in.entries[0].tower = talloc(mem_ctx, struct epm_twr_t);

	status = dcerpc_binding_build_tower(mem_ctx, &bd, &r.in.entries[0].tower->tower);
	if (NT_STATUS_IS_ERR(status)) {
		printf("Unable to build tower from binding struct\n");
		return False;
	}
	
	r.in.replace = 0;

	status = dcerpc_epm_Insert(p, mem_ctx, &r);
	if (NT_STATUS_IS_ERR(status)) {
		printf("Insert failed - %s\n", nt_errstr(status));
		return False;
	}

	if (r.out.result != 0) {
		printf("Insert failed - %d\n", r.out.result);
		printf("NOT CONSIDERING AS A FAILURE\n");
		return True;
	}

	if (!test_Delete(p, mem_ctx, r.in.entries)) {
		return False; 
	}

	return True;
}

static BOOL test_InqObject(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	struct epm_InqObject r;

	r.in.epm_object = talloc(mem_ctx, struct GUID);
	GUID_from_string(DCERPC_EPMAPPER_UUID, r.in.epm_object);

	status = dcerpc_epm_InqObject(p, mem_ctx, &r);
	if (NT_STATUS_IS_ERR(status)) {
		printf("InqObject failed - %s\n", nt_errstr(status));
		return False;
	}

	return True;
}

BOOL torture_rpc_epmapper(void)
{
        NTSTATUS status;
        struct dcerpc_pipe *p;
	TALLOC_CTX *mem_ctx;
	BOOL ret = True;

	mem_ctx = talloc_init("torture_rpc_epmapper");

	status = torture_rpc_connection(&p, 
					DCERPC_EPMAPPER_NAME,
					DCERPC_EPMAPPER_UUID,
					DCERPC_EPMAPPER_VERSION);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	if (!test_Lookup(p, mem_ctx)) {
		ret = False;
	}

	if (!test_Insert(p, mem_ctx)) {
		ret = False;
	}

	if (!test_InqObject(p, mem_ctx)) {
		ret = False;
	}

	talloc_free(mem_ctx);

	torture_rpc_close(p);

	return ret;
}
