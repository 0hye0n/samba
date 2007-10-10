/* 
   Unix SMB/CIFS implementation.
   RAP handlers

   Copyright (C) Volker Lendecke 2004

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
#include "libcli/rap/rap.h"
#include "librpc/gen_ndr/ndr_srvsvc.h"
#include "rpc_server/common/common.h"

/* At this moment these are just dummy functions, but you might get the
 * idea. */

NTSTATUS rap_netshareenum(TALLOC_CTX *mem_ctx,
			  struct rap_NetShareEnum *r)
{
	int i;
	r->out.status = 0;
	r->out.available = dcesrv_common_get_count_of_shares(mem_ctx, NULL);
	r->out.info = talloc_array(mem_ctx,
				   union rap_shareenum_info, r->out.available);

	for (i=0;i<r->out.available;i++) {
		strncpy(r->out.info[i].info1.name, 
			dcesrv_common_get_share_name(mem_ctx, NULL, i),
			sizeof(r->out.info[0].info1.name));
		r->out.info[i].info1.pad = 0;
		r->out.info[i].info1.type = dcesrv_common_get_share_type(mem_ctx, NULL, i);
		r->out.info[i].info1.comment = talloc_strdup(mem_ctx, 
							     dcesrv_common_get_share_comment(mem_ctx, NULL, i));
	}
	
	return NT_STATUS_OK;
}

NTSTATUS rap_netserverenum2(TALLOC_CTX *mem_ctx,
			    struct rap_NetServerEnum2 *r)
{
	r->out.status = 0;
	r->out.available = 0;
	return NT_STATUS_OK;
}
