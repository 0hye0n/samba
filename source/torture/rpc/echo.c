/* 
   Unix SMB/CIFS implementation.
   test suite for echo rpc operations

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


/*
  test the AddOne interface
*/
static BOOL test_addone(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	int i;
	NTSTATUS status;

	printf("\nTesting AddOne\n");

	for (i=0;i<10;i++) {
		uint32 n = i;
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
	char *data_in, *data_out;
	int len = 1 + (random() % 5000);
	struct echo_EchoData r;

	printf("\nTesting EchoData\n");

	data_in = talloc(mem_ctx, len);
	data_out = talloc(mem_ctx, len);
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
	char *data_out;
	struct echo_SourceData r;

	printf("\nTesting SourceData\n");

	data_out = talloc(mem_ctx, len);

	r.in.len = len;
	r.out.data = data_out;

	status = dcerpc_echo_SourceData(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("SourceData(%d) failed - %s\n", len, nt_errstr(status));
		return False;
	}

	for (i=0;i<len;i++) {
		unsigned char *v = (unsigned char *)data_out;
		if (v[i] != (i & 0xFF)) {
			printf("bad data 0x%x at %d\n", (unsigned char)data_out[i], i);
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
	char *data_in;
	int len = 200000 + (random() % 5000);
	struct echo_SinkData r;

	printf("\nTesting SinkData\n");

	data_in = talloc(mem_ctx, len);
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
	struct TestCall r;

	printf("\nTesting TestCall\n");
	status = dcerpc_TestCall(p, mem_ctx, &r);
	if (!NT_STATUS_IS_OK(status)) {
		printf("TestCall failed - %s\n", nt_errstr(status));
		return False;
	}

	NDR_PRINT_DEBUG(Struct1, r.out.s1);	

	return True;
}

BOOL torture_rpc_echo(int dummy)
{
        NTSTATUS status;
        struct dcerpc_pipe *p;
	TALLOC_CTX *mem_ctx;
	BOOL ret = True;

	mem_ctx = talloc_init("torture_rpc_echo");

	status = torture_rpc_connection(&p, "rpcecho");
	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	if (!test_addone(p, mem_ctx)) {
		ret = False;
	}

	if (!test_echodata(p, mem_ctx)) {
		ret = False;
	}

	if (!test_sourcedata(p, mem_ctx)) {
		ret = False;
	}

	if (!test_sinkdata(p, mem_ctx)) {
		ret = False;
	}

	if (!test_testcall(p, mem_ctx)) {
		ret = False;
	}

	printf("\n");
	
        torture_rpc_close(p);
	return ret;
}
