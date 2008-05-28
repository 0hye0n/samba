/* 
   Unix SMB/CIFS implementation.

   SMB2 create test suite

   Copyright (C) Andrew Tridgell 2008
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include "libcli/smb2/smb2.h"
#include "libcli/smb2/smb2_calls.h"
#include "torture/torture.h"
#include "torture/smb2/proto.h"
#include "param/param.h"
#include "librpc/gen_ndr/ndr_security.h"
#include "libcli/security/security.h"

#define FNAME "test_create.dat"

#define CHECK_STATUS(status, correct) do { \
	if (!NT_STATUS_EQUAL(status, correct)) { \
		printf("(%s) Incorrect status %s - should be %s\n", \
		       __location__, nt_errstr(status), nt_errstr(correct)); \
		return false; \
	}} while (0)

#define CHECK_EQUAL(v, correct) do { \
	if (v != correct) { \
		printf("(%s) Incorrect value for %s 0x%08llx - should be 0x%08llx\n", \
		       __location__, #v, (unsigned long long)v, (unsigned long long)correct); \
		return false; \
	}} while (0)

/*
  test some interesting combinations found by gentest
 */
static bool test_create_gentest(struct torture_context *torture, struct smb2_tree *tree)
{
	struct smb2_create io;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx = talloc_new(tree);
	uint32_t access_mask, file_attributes, denied_mask;

	ZERO_STRUCT(io);
	io.in.desired_access     = SEC_FLAG_MAXIMUM_ALLOWED;
	io.in.file_attributes    = FILE_ATTRIBUTE_NORMAL;
	io.in.create_disposition = NTCREATEX_DISP_OVERWRITE_IF;
	io.in.share_access = 
		NTCREATEX_SHARE_ACCESS_DELETE|
		NTCREATEX_SHARE_ACCESS_READ|
		NTCREATEX_SHARE_ACCESS_WRITE;
	io.in.create_options = 0;
	io.in.fname = FNAME;

	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	io.in.create_options = 0xF0000000;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_INVALID_PARAMETER);

	io.in.create_options = 0x00100000;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_NOT_SUPPORTED);

	io.in.create_options = 0xF0100000;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_NOT_SUPPORTED);

	io.in.create_options = 0;

	io.in.file_attributes = FILE_ATTRIBUTE_DEVICE;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_INVALID_PARAMETER);

	io.in.file_attributes = FILE_ATTRIBUTE_VOLUME;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_INVALID_PARAMETER);

	io.in.create_disposition = NTCREATEX_DISP_OPEN;
	io.in.file_attributes = FILE_ATTRIBUTE_VOLUME;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_INVALID_PARAMETER);
	
	io.in.create_disposition = NTCREATEX_DISP_CREATE;
	io.in.desired_access = 0x08000000;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_ACCESS_DENIED);

	io.in.desired_access = 0x04000000;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_ACCESS_DENIED);

	io.in.create_disposition = NTCREATEX_DISP_OPEN_IF;
	io.in.file_attributes = 0;
	access_mask = 0;
	{
		int i;
		for (i=0;i<32;i++) {
			io.in.desired_access = 1<<i;
			status = smb2_create(tree, tmp_ctx, &io);
			if (NT_STATUS_EQUAL(status, NT_STATUS_ACCESS_DENIED)) {
				access_mask |= io.in.desired_access;
			} else {
				CHECK_STATUS(status, NT_STATUS_OK);
				status = smb2_util_close(tree, io.out.file.handle);
				CHECK_STATUS(status, NT_STATUS_OK);
			}
		}
	}

	CHECK_EQUAL(access_mask, 0x0df0fe00);

	io.in.create_disposition = NTCREATEX_DISP_OPEN_IF;
	io.in.desired_access = SEC_FLAG_MAXIMUM_ALLOWED;
	io.in.file_attributes = 0;
	file_attributes = 0;
	denied_mask = 0;
	{
		int i;
		for (i=0;i<32;i++) {
			io.in.file_attributes = 1<<i;
			smb2_deltree(tree, FNAME);
			status = smb2_create(tree, tmp_ctx, &io);
			if (NT_STATUS_EQUAL(status, NT_STATUS_INVALID_PARAMETER)) {
				file_attributes |= io.in.file_attributes;
			} else if (NT_STATUS_EQUAL(status, NT_STATUS_ACCESS_DENIED)) {
				denied_mask |= io.in.file_attributes;
			} else {
				CHECK_STATUS(status, NT_STATUS_OK);
				status = smb2_util_close(tree, io.out.file.handle);
				CHECK_STATUS(status, NT_STATUS_OK);
			}
		}
	}

	CHECK_EQUAL(file_attributes, 0xffff8048);
	CHECK_EQUAL(denied_mask, 0x4000);

	smb2_deltree(tree, FNAME);

	ZERO_STRUCT(io);
	io.in.desired_access     = SEC_FLAG_MAXIMUM_ALLOWED;
	io.in.file_attributes    = 0;
	io.in.create_disposition = NTCREATEX_DISP_OVERWRITE_IF;
	io.in.share_access = 
		NTCREATEX_SHARE_ACCESS_READ|
		NTCREATEX_SHARE_ACCESS_WRITE;
	io.in.create_options = 0;
	io.in.fname = FNAME ":stream1";
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	io.in.fname = FNAME;
	io.in.file_attributes = 0x8040;
	io.in.share_access = 
		NTCREATEX_SHARE_ACCESS_READ;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_INVALID_PARAMETER);

	talloc_free(tmp_ctx);

	smb2_deltree(tree, FNAME);
	
	return true;
}


/*
  try the various request blobs
 */
static bool test_create_blob(struct torture_context *torture, struct smb2_tree *tree)
{
	struct smb2_create io;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx = talloc_new(tree);

	smb2_deltree(tree, FNAME);

	ZERO_STRUCT(io);
	io.in.desired_access     = SEC_FLAG_MAXIMUM_ALLOWED;
	io.in.file_attributes    = FILE_ATTRIBUTE_NORMAL;
	io.in.create_disposition = NTCREATEX_DISP_OVERWRITE_IF;
	io.in.share_access = 
		NTCREATEX_SHARE_ACCESS_DELETE|
		NTCREATEX_SHARE_ACCESS_READ|
		NTCREATEX_SHARE_ACCESS_WRITE;
	io.in.create_options		= NTCREATEX_OPTIONS_SEQUENTIAL_ONLY |
					  NTCREATEX_OPTIONS_ASYNC_ALERT	|
					  NTCREATEX_OPTIONS_NON_DIRECTORY_FILE |
					  0x00200000;
	io.in.fname = FNAME;

	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("testing alloc size\n");
	io.in.alloc_size = 4096;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);
	CHECK_EQUAL(io.out.alloc_size, io.in.alloc_size);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("testing durable open\n");
	io.in.durable_open = true;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("testing query maximal access\n");
	io.in.query_maximal_access = true;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("testing timewarp\n");
	io.in.timewarp = 10000;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OBJECT_NAME_NOT_FOUND);
	io.in.timewarp = 0;

	printf("testing query_on_disk\n");
	io.in.query_on_disk_id = true;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("testing unknown tag\n");
	status = smb2_create_blob_add(tmp_ctx, &io.in.blobs,
				      "FooO", data_blob(NULL, 0));
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	printf("testing bad tag length\n");
	status = smb2_create_blob_add(tmp_ctx, &io.in.blobs,
				      "xxx", data_blob(NULL, 0));
	CHECK_STATUS(status, NT_STATUS_OK);

	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_INVALID_PARAMETER);

	talloc_free(tmp_ctx);

	smb2_deltree(tree, FNAME);
	
	return true;
}

/*
  try creating with acls
 */
static bool test_create_acl(struct torture_context *torture, struct smb2_tree *tree)
{
	struct smb2_create io;
	NTSTATUS status;
	TALLOC_CTX *tmp_ctx = talloc_new(tree);
	struct security_ace ace;
	struct security_descriptor *sd, *sd2;
	struct dom_sid *test_sid;
	union smb_fileinfo q;

	smb2_deltree(tree, FNAME);

	ZERO_STRUCT(io);
	io.in.desired_access     = SEC_FLAG_MAXIMUM_ALLOWED;
	io.in.file_attributes    = FILE_ATTRIBUTE_NORMAL;
	io.in.create_disposition = NTCREATEX_DISP_OVERWRITE_IF;
	io.in.share_access = 
		NTCREATEX_SHARE_ACCESS_DELETE|
		NTCREATEX_SHARE_ACCESS_READ|
		NTCREATEX_SHARE_ACCESS_WRITE;
	io.in.create_options		= NTCREATEX_OPTIONS_SEQUENTIAL_ONLY |
					  NTCREATEX_OPTIONS_ASYNC_ALERT	|
					  NTCREATEX_OPTIONS_NON_DIRECTORY_FILE |
					  0x00200000;
	io.in.fname = FNAME;

	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	q.query_secdesc.level = RAW_FILEINFO_SEC_DESC;
	q.query_secdesc.in.file.handle = io.out.file.handle;
	q.query_secdesc.in.secinfo_flags = 
		SECINFO_OWNER |
		SECINFO_GROUP |
		SECINFO_DACL;
	status = smb2_getinfo_file(tree, tmp_ctx, &q);
	CHECK_STATUS(status, NT_STATUS_OK);
	sd = q.query_secdesc.out.sd;

	status = smb2_util_close(tree, io.out.file.handle);
	CHECK_STATUS(status, NT_STATUS_OK);

	smb2_util_unlink(tree, FNAME);

	printf("adding a new ACE\n");
	test_sid = dom_sid_parse_talloc(tmp_ctx, "S-1-5-32-1234-54321");

	ace.type = SEC_ACE_TYPE_ACCESS_ALLOWED;
	ace.flags = 0;
	ace.access_mask = SEC_STD_ALL;
	ace.trustee = *test_sid;

	status = security_descriptor_dacl_add(sd, &ace);
	CHECK_STATUS(status, NT_STATUS_OK);
	
	printf("creating a file with an initial ACL\n");

	io.in.sec_desc = sd;
	status = smb2_create(tree, tmp_ctx, &io);
	CHECK_STATUS(status, NT_STATUS_OK);

	q.query_secdesc.in.file.handle = io.out.file.handle;
	status = smb2_getinfo_file(tree, tmp_ctx, &q);
	CHECK_STATUS(status, NT_STATUS_OK);
	sd2 = q.query_secdesc.out.sd;

	if (!security_acl_equal(sd->dacl, sd2->dacl)) {
		printf("%s: security descriptors don't match!\n", __location__);
		printf("got:\n");
		NDR_PRINT_DEBUG(security_descriptor, sd2);
		printf("expected:\n");
		NDR_PRINT_DEBUG(security_descriptor, sd);
		return false;
	}

	talloc_free(tmp_ctx);
	
	return true;
}

/* 
   basic testing of SMB2 read
*/
struct torture_suite *torture_smb2_create_init(void)
{
	struct torture_suite *suite = torture_suite_create(talloc_autofree_context(), "CREATE");

	torture_suite_add_1smb2_test(suite, "GENTEST", test_create_gentest);
	torture_suite_add_1smb2_test(suite, "BLOB", test_create_blob);
	torture_suite_add_1smb2_test(suite, "ACL", test_create_acl);

	suite->description = talloc_strdup(suite, "SMB2-CREATE tests");

	return suite;
}
