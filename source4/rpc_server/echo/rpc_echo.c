/* 
   Unix SMB/CIFS implementation.

   endpoint server for the echo pipe

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


static NTSTATUS echo_AddOne(struct dcesrv_state *dce, TALLOC_CTX *mem_ctx, struct echo_AddOne *r)
{
	*r->out.v = *r->in.v + 1;
	return NT_STATUS_OK;
}

static NTSTATUS echo_EchoData(struct dcesrv_state *dce, TALLOC_CTX *mem_ctx, struct echo_EchoData *r)
{
	if (!r->in.len) {
		return NT_STATUS_OK;
	}

	r->out.out_data = talloc(mem_ctx, r->in.len);
	if (!r->out.out_data) {
		return NT_STATUS_NO_MEMORY;
	}
	memcpy(r->out.out_data, r->in.in_data, r->in.len);

	return NT_STATUS_OK;
}

static NTSTATUS echo_SinkData(struct dcesrv_state *dce, TALLOC_CTX *mem_ctx, struct echo_SinkData *r)
{
	return NT_STATUS_OK;
}

static NTSTATUS echo_SourceData(struct dcesrv_state *dce, TALLOC_CTX *mem_ctx, struct echo_SourceData *r)
{
	int i;
	for (i=0;i<r->in.len;i++) {
		r->out.data[i] = i;
	}

	return NT_STATUS_OK;
}

static NTSTATUS echo_TestCall(struct dcesrv_state *dce, TALLOC_CTX *mem_ctx, struct echo_TestCall *r)
{
	r->out.s2 = "this is a test string";
	
	return NT_STATUS_OK;
}

static NTSTATUS echo_TestCall2(struct dcesrv_state *dce, TALLOC_CTX *mem_ctx, struct echo_TestCall2 *r)
{
	r->out.info = talloc(mem_ctx, sizeof(*r->out.info));
	if (!r->out.info) {
		r->out.result = NT_STATUS_NO_MEMORY;
		return NT_STATUS_OK;
	}

	r->out.result = NT_STATUS_OK;

	switch (r->in.level) {
	case 1:
		r->out.info->info1.v = 10;
		break;
	case 2:
		r->out.info->info2.v = 20;
		break;
	case 3:
		r->out.info->info3.v = 30;
		break;
	case 4:
		r->out.info->info4.v.low = 40;
		r->out.info->info4.v.high = 0;
		break;
	case 5:
		r->out.info->info5.v1 = 50;
		r->out.info->info5.v2.low = 60;
		r->out.info->info5.v2.high = 0;
		break;
	case 6:
		r->out.info->info6.v1 = 70;
		r->out.info->info6.info1.v= 80;
		break;
	case 7:
		r->out.info->info7.v1 = 80;
		r->out.info->info7.info4.v.low = 90;
		r->out.info->info7.info4.v.high = 0;
		break;
	default:
		r->out.result = NT_STATUS_INVALID_LEVEL;
		break;
	}

	return NT_STATUS_OK;
}


/* include the generated boilerplate */
#include "librpc/gen_ndr/ndr_echo_s.c"
