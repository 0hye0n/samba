/* 
   Unix SMB/CIFS implementation.
   test suite for echo rpc operations

   Copyright (C) Andrew Tridgell 2003
   Copyright (C) Stefan (metze) Metzmacher 2005
   Copyright (C) Jelmer Vernooij 2005
   
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
#include "lib/events/events.h"
#include "librpc/gen_ndr/ndr_echo.h"


/*
  test the AddOne interface
*/
static BOOL test_addone(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	int i;
	NTSTATUS status;

	printf("\nTesting AddOne\n");

	for (i=0;i<10;i++) {
		uint32_t n = i;
		struct echo_AddOne r;
		r.in.v = &n;
		r.out.v = &n;
		status = dcerpc_echo_AddOne(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("AddOne(%d) failed - %s\n", i, nt_errstr(status));
			return False;
		}
		printf("%d + 1 = %u\n", i, n);
	}

	return True;
}

/*
  test the EchoData interface
*/
static BOOL test_echodata(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	int i;
	NTSTATUS status;
	uint8_t *data_in, *data_out;
	int len = 1 + (random() % 5000);
	struct echo_EchoData r;

	printf("\nTesting EchoData\n");

	data_in = talloc_size(mem_ctx, len);
	data_out = talloc_size(mem_ctx, len);
	for (i=0;i<len;i++) {
		data_in[i] = i;
	}
	
	r.in.len = len;
	r.in.in_data = data_in;

	status = dcerpc_echo_EchoData(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("EchoData(%d) failed - %s\n", len, nt_errstr(status));
		return False;
	}

	data_out = r.out.out_data;

	for (i=0;i<len;i++) {
		if (data_in[i] != data_out[i]) {
			printf("Bad data returned for len %d at offset %d\n", 
			       len, i);
			printf("in:\n");
			dump_data(0, data_in+i, MIN(len-i, 16));
			printf("out:\n");
			dump_data(0, data_out+i, MIN(len-1, 16));
			return False;
		}
	}


	return True;
}


/*
  test the SourceData interface
*/
static BOOL test_sourcedata(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	int i;
	NTSTATUS status;
	int len = 200000 + (random() % 5000);
	uint8_t *data_out;
	struct echo_SourceData r;

	printf("\nTesting SourceData\n");

	data_out = talloc_size(mem_ctx, len);

	r.in.len = len;
	r.out.data = data_out;

	status = dcerpc_echo_SourceData(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SourceData(%d) failed - %s\n", len, nt_errstr(status));
		return False;
	}

	for (i=0;i<len;i++) {
		uint8_t *v = (uint8_t *)data_out;
		if (v[i] != (i & 0xFF)) {
			printf("bad data 0x%x at %d\n", (uint8_t)data_out[i], i);
			return False;
		}
	}

	return True;
}

/*
  test the SinkData interface
*/
static BOOL test_sinkdata(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	int i;
	NTSTATUS status;
	uint8_t *data_in;
	int len = 200000 + (random() % 5000);
	struct echo_SinkData r;

	printf("\nTesting SinkData\n");

	data_in = talloc_size(mem_ctx, len);
	for (i=0;i<len;i++) {
		data_in[i] = i+1;
	}

	r.in.len = len;
	r.in.data = data_in;

	status = dcerpc_echo_SinkData(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SinkData(%d) failed - %s\n", len, nt_errstr(status));
		return False;
	}

	printf("sunk %d bytes\n", len);

	return True;
}


/*
  test the testcall interface
*/
static BOOL test_testcall(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	struct echo_TestCall r;

	r.in.s1 = "input string";

	printf("\nTesting TestCall\n");
	status = dcerpc_echo_TestCall(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("TestCall failed - %s\n", nt_errstr(status));
		return False;
	}

	return True;
}

/*
  test the testcall interface
*/
static BOOL test_testcall2(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	struct echo_TestCall2 r;
	int i;
	BOOL ret = True;

	for (i=1;i<=7;i++) {
		r.in.level = i;

		printf("\nTesting TestCall2 level %d\n", i);
		status = dcerpc_echo_TestCall2(p, mem_ctx, &r);
		if (!NT_STATUS_IS_OK(status)) {
			printf("TestCall2 failed - %s\n", nt_errstr(status));
			ret = False;
		}
	}

	return ret;
}

/*
  test the TestSleep interface
*/
static BOOL test_sleep(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	int i;
	NTSTATUS status;
#define ASYNC_COUNT 3
	struct rpc_request *req[ASYNC_COUNT];
	struct echo_TestSleep r[ASYNC_COUNT];
	BOOL done[ASYNC_COUNT];
	struct timeval snd[ASYNC_COUNT];
	struct timeval rcv[ASYNC_COUNT];
	struct timeval diff[ASYNC_COUNT];
	struct event_context *ctx;
	int total_done = 0;
	BOOL ret = True;

	printf("\nTesting TestSleep\n");

	for (i=0;i<ASYNC_COUNT;i++) {
		done[i]		= False;
		snd[i]		= timeval_current();
		rcv[i]		= timeval_zero();
		r[i].in.seconds = ASYNC_COUNT-i;
		req[i] = dcerpc_echo_TestSleep_send(p, mem_ctx, &r[i]);
		if (!req[i]) {
			printf("Failed to send async sleep request\n");
			return False;
		}
	}

	ctx = dcerpc_event_context(p);
	while (total_done < ASYNC_COUNT) {
		if (event_loop_once(ctx) != 0) {
			return False;
		}
		for (i=0;i<ASYNC_COUNT;i++) {
			if (done[i] == False && req[i]->state == RPC_REQUEST_DONE) {
				total_done++;
				done[i] = True;
				rcv[i]	= timeval_current();
				diff[i]	= timeval_diff(&rcv[i], &snd[i]);
				status	= dcerpc_ndr_request_recv(req[i]);
				if (!NT_STATUS_IS_OK(status)) {
					printf("TestSleep(%d) failed - %s\n",
					       i, nt_errstr(status));
					ret = False;
				} else if (r[i].out.result != r[i].in.seconds) {
					printf("Failed - Sleeped for %u seconds (but we said %u seconds and the reply takes only %u seconds)\n", 
					       	r[i].out.result, r[i].in.seconds, (uint_t)diff[i].tv_sec);
					ret = False;
				} else {
					if (r[i].out.result > diff[i].tv_sec) {
						printf("Failed - Sleeped for %u seconds (but reply takes only %u.%06u seconds)\n", 
					       		r[i].out.result, (uint_t)diff[i].tv_sec, (uint_t)diff[i].tv_usec);
					} else if (r[i].out.result+1 == diff[i].tv_sec) {
						printf("Sleeped for %u seconds (but reply takes %u.%06u seconds - busy server?)\n", 
					       		r[i].out.result, (uint_t)diff[i].tv_sec, (uint_t)diff[i].tv_usec);
					} else if (r[i].out.result == diff[i].tv_sec) {
						printf("Sleeped for %u seconds (reply takes %u.%06u seconds - ok)\n", 
					       		r[i].out.result, (uint_t)diff[i].tv_sec, (uint_t)diff[i].tv_usec);
					} else {
					       	printf("(Failed) - Not async - Sleeped for %u seconds (but reply takes %u.%06u seconds)\n", 
					       		r[i].out.result, (uint_t)diff[i].tv_sec, (uint_t)diff[i].tv_usec);
						/* TODO: let the test fail here, when we support async rpc on ncacn_np
						ret = False;*/
					}
				}
			}
		}
	}

	return ret;
}

/*
  test enum handling
*/
static BOOL test_enum(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	struct echo_TestEnum r;
	BOOL ret = True;
	enum echo_Enum1 v = ECHO_ENUM1;
	struct echo_Enum2 e2;
	union echo_Enum3 e3;

	r.in.foo1 = &v;
	r.in.foo2 = &e2;
	r.in.foo3 = &e3;
	r.out.foo1 = &v;
	r.out.foo2 = &e2;
	r.out.foo3 = &e3;

	e2.e1 = 76;
	e2.e2 = ECHO_ENUM1_32;
	e3.e1 = ECHO_ENUM2;

	printf("\nTesting TestEnum\n");
	status = dcerpc_echo_TestEnum(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("TestEnum failed - %s\n", nt_errstr(status));
		ret = False;
	}

	return ret;
}

/*
  test surrounding conformant array handling
*/
static BOOL test_surrounding(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	NTSTATUS status;
	struct echo_TestSurrounding r;
	BOOL ret = True;

	ZERO_STRUCT(r);
	r.in.data = talloc(mem_ctx, struct echo_Surrounding);

	r.in.data->x = 20;
	r.in.data->surrounding = talloc_zero_array(mem_ctx, uint16_t, r.in.data->x);

	r.out.data = talloc(mem_ctx, struct echo_Surrounding);

	printf("\nTesting TestSurrounding\n");
	status = dcerpc_echo_TestSurrounding(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("TestSurrounding failed - %s\n", nt_errstr(status));
		ret = False;
	}
	
	if (r.out.data->x != 2 * r.in.data->x) {
		printf("TestSurrounding did not make the array twice as large\n");
		ret = False;
	}

	return ret;
}

BOOL torture_rpc_echo(void)
{
	NTSTATUS status;
    struct dcerpc_pipe *p;
	TALLOC_CTX *mem_ctx;
	BOOL ret = True;

	mem_ctx = talloc_init("torture_rpc_echo");

	status = torture_rpc_connection(&p, 
					DCERPC_RPCECHO_NAME,
					DCERPC_RPCECHO_UUID,
					DCERPC_RPCECHO_VERSION);
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	ret &= test_addone(p, mem_ctx);
	ret &= test_sinkdata(p, mem_ctx);
	ret &= test_echodata(p, mem_ctx);
	ret &= test_sourcedata(p, mem_ctx);
	ret &= test_testcall(p, mem_ctx);
	ret &= test_testcall2(p, mem_ctx);
	ret &= test_enum(p, mem_ctx);
	ret &= test_surrounding(p, mem_ctx);
	ret &= test_sleep(p, mem_ctx);

	printf("\n");
	
	talloc_free(mem_ctx);

    torture_rpc_close(p);
	return ret;
}
