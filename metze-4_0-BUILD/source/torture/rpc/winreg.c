/* 
   Unix SMB/CIFS implementation.
   test suite for winreg rpc operations

   Copyright (C) Tim Potter 2003
   
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

static void init_winreg_String(struct winreg_String *name, const char *s)
{
	name->name = s;
	if (s) {
		name->name_len = 2 * (strlen_m(s) + 1);
		name->name_size = name->name_len;
	} else {
		name->name_len = 0;
		name->name_size = 0;
	}
}

static BOOL test_GetVersion(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			    struct policy_handle *handle)
{
	NTSTATUS status;
	struct winreg_GetVersion r;

	printf("\ntesting GetVersion\n");

	r.in.handle = handle;

	status = dcerpc_winreg_GetVersion(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("GetVersion failed - %s\n", nt_errstr(status));
		return False;
	}

	return True;
}

static BOOL test_CreateKey(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			  struct policy_handle *handle, char *name, const char *class)
{
	struct winreg_CreateKey r;
	struct policy_handle newhandle;
	NTSTATUS status;
	uint32 sec_info = 0;

	printf("\ntesting CreateKey\n");

	r.in.handle = handle;
	r.out.handle = &newhandle;
	init_winreg_String(&r.in.key, name);	
	init_winreg_String(&r.in.class, class);
	r.in.reserved = 0x0;
	r.in.access_mask = 0x02000000;
	r.in.sec_info = &sec_info;
	r.in.sec_desc = NULL;

	status = dcerpc_winreg_CreateKey(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("CreateKey failed - %s\n", nt_errstr(status));
		return False;
	}

	if (!W_ERROR_IS_OK(r.out.result)) {
		printf("CreateKey failed - %s\n", win_errstr(r.out.result));
		return False;
	}

	return True;
}

static BOOL test_CloseKey(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			  struct policy_handle *handle)
{
	NTSTATUS status;
	struct winreg_CloseKey r;

	printf("\ntesting CloseKey\n");

	r.in.handle = r.out.handle = handle;

	status = dcerpc_winreg_CloseKey(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("CloseKey failed - %s\n", nt_errstr(status));
		return False;
	}

	return True;
}

static BOOL test_FlushKey(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			  struct policy_handle *handle)
{
	NTSTATUS status;
	struct winreg_FlushKey r;

	printf("\ntesting FlushKey\n");

	r.in.handle = handle;

	status = dcerpc_winreg_FlushKey(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("FlushKey failed - %s\n", nt_errstr(status));
		return False;
	}

	return True;
}

static BOOL test_OpenKey(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			 struct policy_handle *hive_handle,
			 const char *keyname, struct policy_handle *key_handle)
{
	NTSTATUS status;
	struct winreg_OpenKey r;

	printf("\ntesting OpenKey\n");

	r.in.handle = hive_handle;
	init_winreg_String(&r.in.keyname, keyname);
	r.in.unknown = 0x00000000;
	r.in.access_mask = 0x02000000;
	r.out.handle = key_handle;

	status = dcerpc_winreg_OpenKey(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenKey failed - %s\n", nt_errstr(status));
		return False;
	}

	if (!W_ERROR_IS_OK(r.out.result)) {
		printf("OpenKey failed - %s\n", win_errstr(r.out.result));
		return False;
	}

	return True;
}

static BOOL test_DeleteKey(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			   struct policy_handle *handle, const char *key)
{
	NTSTATUS status;
	struct winreg_DeleteKey r;

	printf("\ntesting DeleteKey\n");

	r.in.handle = handle;
	init_winreg_String(&r.in.key, key);	

	status = dcerpc_winreg_DeleteKey(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("DeleteKey failed - %s\n", nt_errstr(status));
		return False;
	}

	if (!W_ERROR_IS_OK(r.out.result)) {
		printf("DeleteKey failed - %s\n", win_errstr(r.out.result));
		return False;
	}

	return True;
}

static BOOL test_QueryInfoKey(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			      struct policy_handle *handle, char *class)
{
	NTSTATUS status;
	struct winreg_QueryInfoKey r;

	printf("\ntesting QueryInfoKey\n");

	r.in.handle = handle;
	init_winreg_String(&r.in.class, class);
	
	status = dcerpc_winreg_QueryInfoKey(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("QueryInfoKey failed - %s\n", nt_errstr(status));
		return False;
	}

	if (!W_ERROR_IS_OK(r.out.result)) {
		printf("QueryInfoKey failed - %s\n", win_errstr(r.out.result));
		return False;
	}

	return True;
}

static BOOL test_key(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
		     struct policy_handle *handle, int depth);

static BOOL test_EnumKey(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			 struct policy_handle *handle, int depth)
{
	struct winreg_EnumKey r;
	struct winreg_EnumKeyNameRequest keyname;
	struct winreg_String classname;
	struct winreg_Time tm;
	NTSTATUS status;

	r.in.handle = handle;
	r.in.enum_index = 0;
	r.in.key_name_len = r.out.key_name_len = 0;
	r.in.unknown = r.out.unknown = 0x0414;
	keyname.unknown = 0x0000020a;
	init_winreg_String(&keyname.key_name, NULL);
	init_winreg_String(&classname, NULL);
	r.in.in_name = &keyname;
	r.in.class = &classname;
	tm.low = tm.high = 0x7fffffff;
	r.in.last_changed_time = &tm;

	do {
		status = dcerpc_winreg_EnumKey(p, mem_ctx, &r);

		if (NT_STATUS_IS_OK(status) && W_ERROR_IS_OK(r.out.result)) {
			struct policy_handle key_handle;

			if (!test_OpenKey(
				    p, mem_ctx, handle, r.out.out_name->name,
				    &key_handle)) {
				printf("OpenKey(%s) failed - %s\n",
				       r.out.out_name->name, 
				       win_errstr(r.out.result));
				goto next_key;
			}

			test_key(p, mem_ctx, &key_handle, depth + 1);
		}

	next_key:

		r.in.enum_index++;

	} while (W_ERROR_IS_OK(r.out.result));

	return True;
}

static BOOL test_EnumValue(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
			   struct policy_handle *handle, int max_valnamelen, int max_valbufsize)
{
	struct winreg_EnumValue r;
    struct winreg_Uint8buf vb;
    struct winreg_EnumValueName vn;
	uint32 type = 0, requested_len = max_valbufsize, returned_len = 0;
	NTSTATUS status;

    r.in.handle = handle;
    r.in.enum_index = 0;
    r.in.type = &type;
    r.in.requested_len = &requested_len;
    r.in.returned_len = &returned_len;
    vn.max_len = 0;
    vn.len = 0;
    vn.buf = NULL;
    r.in.name = r.out.name = &vn;
    vb.max_len = max_valbufsize;
    vb.offset = 0x0;
    vb.len = 0x0;
    vb.buffer = NULL;
    r.in.value = &vb;

	do {
		status = dcerpc_winreg_EnumValue(p, mem_ctx, &r);
		if(NT_STATUS_IS_ERR(status)) {
			printf("EnumValue failed - %s\n", nt_errstr(status));
			return False;
		}
		r.in.enum_index++;
	} while (W_ERROR_IS_OK(r.out.result));

	if(!W_ERROR_EQUAL(r.out.result, WERR_NO_MORE_ITEMS)) {
		printf("EnumValue failed - %s\n", win_errstr(r.out.result));
		return False;
	}
			
	return True;
}

static BOOL test_OpenHKLM(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			  struct policy_handle *handle)
{
	NTSTATUS status;
	struct winreg_OpenHKLM r;
	struct winreg_OpenUnknown unknown;
	BOOL ret = True;

	printf("\ntesting OpenHKLM\n");

	unknown.unknown0 = 0x84e0;
	unknown.unknown1 = 0x0000;
	r.in.unknown = &unknown;
	r.in.access_required = SEC_RIGHTS_MAXIMUM_ALLOWED;
	r.out.handle = handle;

	status = dcerpc_winreg_OpenHKLM(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenHKLM failed - %s\n", nt_errstr(status));
		return False;
	}

	return ret;
}

static BOOL test_OpenHKU(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			 struct policy_handle *handle)
{
	NTSTATUS status;
	struct winreg_OpenHKU r;
	struct winreg_OpenUnknown unknown;
	BOOL ret = True;

	printf("\ntesting OpenHKU\n");

	unknown.unknown0 = 0x84e0;
	unknown.unknown1 = 0x0000;
	r.in.unknown = &unknown;
	r.in.access_required = SEC_RIGHTS_MAXIMUM_ALLOWED;
	r.out.handle = handle;

	status = dcerpc_winreg_OpenHKU(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenHKU failed - %s\n", nt_errstr(status));
		return False;
	}

	return ret;
}

static BOOL test_OpenHKCR(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			  struct policy_handle *handle)
{
	NTSTATUS status;
	struct winreg_OpenHKCR r;
	struct winreg_OpenUnknown unknown;
	BOOL ret = True;

	printf("\ntesting OpenHKCR\n");

	unknown.unknown0 = 0x84e0;
	unknown.unknown1 = 0x0000;
	r.in.unknown = &unknown;
	r.in.access_required = SEC_RIGHTS_MAXIMUM_ALLOWED;
	r.out.handle = handle;

	status = dcerpc_winreg_OpenHKCR(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenHKCR failed - %s\n", nt_errstr(status));
		return False;
	}

	return ret;
}

static BOOL test_InitiateSystemShutdown(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			const char *msg, uint32 timeout)
{
	struct winreg_InitiateSystemShutdown r;
	NTSTATUS status;
	
	init_winreg_String(&r.in.message, msg);
	r.in.flags = 0;
	r.in.timeout = timeout;

	status = dcerpc_winreg_InitiateSystemShutdown(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("InitiateSystemShutdown failed - %s\n", nt_errstr(status));
		return False;
	}

	if (!W_ERROR_IS_OK(r.out.result)) {
		printf("InitiateSystemShutdown failed - %s\n", win_errstr(r.out.result));
		return False;
	}

	return True;
}

static BOOL test_AbortSystemShutdown(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx)
{
	struct winreg_AbortSystemShutdown r;
	NTSTATUS status;
	uint16 server = 0x0;

	r.in.server = &server;
	
	status = dcerpc_winreg_AbortSystemShutdown(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("AbortSystemShutdown failed - %s\n", nt_errstr(status));
		return False;
	}

	if (!W_ERROR_IS_OK(r.out.result)) {
		printf("AbortSystemShutdown failed - %s\n", win_errstr(r.out.result));
		return False;
	}

	return True;
}

static BOOL test_OpenHKCU(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			  struct policy_handle *handle)
{
	NTSTATUS status;
	struct winreg_OpenHKCU r;
	struct winreg_OpenUnknown unknown;
	BOOL ret = True;

	printf("\ntesting OpenHKCU\n");

	unknown.unknown0 = 0x84e0;
	unknown.unknown1 = 0x0000;
	r.in.unknown = &unknown;
	r.in.access_required = SEC_RIGHTS_MAXIMUM_ALLOWED;
	r.out.handle = handle;

	status = dcerpc_winreg_OpenHKCU(p, mem_ctx, &r);

	if (!NT_STATUS_IS_OK(status)) {
		printf("OpenHKCU failed - %s\n", nt_errstr(status));
		return False;
	}

	return ret;
}

#define MAX_DEPTH 2		/* Only go this far down the tree */

static BOOL test_key(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, 
		     struct policy_handle *handle, int depth)
{
	if (depth == MAX_DEPTH)
		return True;

	if (!test_QueryInfoKey(p, mem_ctx, handle, NULL)) {
	}

	if (!test_EnumKey(p, mem_ctx, handle, depth)) {
	}

	if (!test_EnumValue(p, mem_ctx, handle, 200, 200)) {
	}

	/* Enumerate values */

	test_CloseKey(p, mem_ctx, handle);

	return True;
}

typedef BOOL winreg_open_fn(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx,
			    struct policy_handle *handle);

static BOOL test_Open(struct dcerpc_pipe *p, TALLOC_CTX *mem_ctx, void *fn)
{
	struct policy_handle handle, newhandle;
	BOOL ret = True;
	winreg_open_fn *open_fn = (winreg_open_fn *)fn;

	if (!open_fn(p, mem_ctx, &handle))
		return False;

	if (!test_CreateKey(p, mem_ctx, &handle, "spottyfoot", NULL)) {
		printf("CreateKey failed\n");
		ret = False;
	}

	if (!test_FlushKey(p, mem_ctx, &handle)) {
		printf("FlushKey failed\n");
		ret = False;
	}

	if (!test_OpenKey(p, mem_ctx, &handle, "spottyfoot", &newhandle)) {
		printf("CreateKey failed (OpenKey after Create didn't work)\n");
		ret = False;
	}

	if (!test_DeleteKey(p, mem_ctx, &handle, "spottyfoot")) {
		printf("DeleteKey failed\n");
		ret = False;
	}

	if (!test_FlushKey(p, mem_ctx, &handle)) {
		printf("FlushKey failed\n");
		ret = False;
	}

	if (test_OpenKey(p, mem_ctx, &handle, "spottyfoot", &newhandle)) {
		printf("DeleteKey failed (OpenKey after Delete didn't work)\n");
		ret = False;
	}

	if (!test_GetVersion(p, mem_ctx, &handle)) {
		printf("GetVersion failed\n");
		ret = False;
	}

	/* The HKCR hive has a very large fanout */

	if (open_fn == test_OpenHKCR) {
		if(!test_key(p, mem_ctx, &handle, MAX_DEPTH - 1)) {
			ret = False;
		}
	}

	if(!test_key(p, mem_ctx, &handle, 0)) {
		ret = False;
	}

	return ret;
}

BOOL torture_rpc_winreg(int dummy)
{
        NTSTATUS status;
       struct dcerpc_pipe *p;
	TALLOC_CTX *mem_ctx;
	BOOL ret = True;
	winreg_open_fn *open_fns[] = { test_OpenHKLM, test_OpenHKU,
				       test_OpenHKCR, test_OpenHKCU };
	int i;

	mem_ctx = talloc_init("torture_rpc_winreg");

	status = torture_rpc_connection(&p, 
					DCERPC_WINREG_NAME, 
					DCERPC_WINREG_UUID, 
					DCERPC_WINREG_VERSION);

	if (!NT_STATUS_IS_OK(status)) {
		return False;
	}

	if(!test_InitiateSystemShutdown(p, mem_ctx, "spottyfood", 30))
		ret = False;

	if(!test_AbortSystemShutdown(p, mem_ctx))
		ret = False;

	for (i = 0; i < ARRAY_SIZE(open_fns); i++) {
		if (!test_Open(p, mem_ctx, open_fns[i]))
			ret = False;
	}

	talloc_destroy(mem_ctx);

        torture_rpc_close(p);

	return ret;
}
