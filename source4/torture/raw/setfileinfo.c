/* 
   Unix SMB/CIFS implementation.
   RAW_SFILEINFO_* individual test suite
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

#define BASEDIR "\\testsfileinfo"

/* basic testing of all RAW_SFILEINFO_* calls 
   for each call we test that it succeeds, and where possible test 
   for consistency between the calls. 
*/
BOOL torture_raw_sfileinfo(int dummy)
{
	struct cli_state *cli;
	BOOL ret = True;
	TALLOC_CTX *mem_ctx;
	int fnum_saved, d_fnum, fnum2, fnum = -1;
	char *fnum_fname;
	char *fnum_fname_new;
	char *path_fname;
	char *path_fname_new;
	union smb_fileinfo finfo1, finfo2;
	union smb_setfileinfo sfinfo;
	NTSTATUS status, status2;
	const char *call_name;
	time_t basetime = (time(NULL) - 86400) & ~1;
	BOOL check_fnum;
	int n = time(NULL) % 100;
	
	asprintf(&path_fname, BASEDIR "\\fname_test_%d.txt", n);
	asprintf(&path_fname_new, BASEDIR "\\fname_test_new_%d.txt", n);
	asprintf(&fnum_fname, BASEDIR "\\fnum_test_%d.txt", n);
	asprintf(&fnum_fname_new, BASEDIR "\\fnum_test_new_%d.txt", n);

	if (!torture_open_connection(&cli)) {
		return False;
	}

	mem_ctx = talloc_init("torture_sfileinfo");

	cli_deltree(cli, BASEDIR);
	cli_mkdir(cli, BASEDIR);

#define RECREATE_FILE(fname) do { \
	if (fnum != -1) cli_close(cli, fnum); \
	fnum = create_complex_file(cli, mem_ctx, fname); \
	if (fnum == -1) { \
		printf("(%d) ERROR: open of %s failed (%s)\n", \
		       __LINE__, fname, cli_errstr(cli)); \
		ret = False; \
		goto done; \
	}} while (0)

#define RECREATE_BOTH do { \
		RECREATE_FILE(path_fname); \
		cli_close(cli, fnum); \
		RECREATE_FILE(fnum_fname); \
	} while (0)

	RECREATE_BOTH;
	
#define CHECK_CALL_FNUM(call, rightstatus) do { \
	check_fnum = True; \
	call_name = #call; \
	sfinfo.generic.level = RAW_SFILEINFO_ ## call; \
	sfinfo.generic.file.fnum = fnum; \
	status = smb_raw_setfileinfo(cli->tree, &sfinfo); \
	if (!NT_STATUS_EQUAL(status, rightstatus)) { \
		printf("(%d) %s - %s (should be %s)\n", __LINE__, #call, \
			nt_errstr(status), nt_errstr(rightstatus)); \
		ret = False; \
	} \
	finfo1.generic.level = RAW_FILEINFO_ALL_INFO; \
	finfo1.generic.in.fnum = fnum; \
	status2 = smb_raw_fileinfo(cli->tree, mem_ctx, &finfo1); \
	if (!NT_STATUS_IS_OK(status2)) { \
		printf("(%d) %s pathinfo - %s\n", __LINE__, #call, nt_errstr(status)); \
		ret = False; \
	}} while (0)

#define CHECK_CALL_PATH(call, rightstatus) do { \
	check_fnum = False; \
	call_name = #call; \
	sfinfo.generic.level = RAW_SFILEINFO_ ## call; \
	sfinfo.generic.file.fname = path_fname; \
	status = smb_raw_setpathinfo(cli->tree, &sfinfo); \
	if (NT_STATUS_EQUAL(status, NT_STATUS_OBJECT_NAME_NOT_FOUND)) { \
		sfinfo.generic.file.fname = path_fname_new; \
		status = smb_raw_setpathinfo(cli->tree, &sfinfo); \
	} \
	if (!NT_STATUS_EQUAL(status, rightstatus)) { \
		printf("(%d) %s - %s (should be %s)\n", __LINE__, #call, \
			nt_errstr(status), nt_errstr(rightstatus)); \
		ret = False; \
	} \
	finfo1.generic.level = RAW_FILEINFO_ALL_INFO; \
	finfo1.generic.in.fname = path_fname; \
	status2 = smb_raw_pathinfo(cli->tree, mem_ctx, &finfo1); \
	if (NT_STATUS_EQUAL(status2, NT_STATUS_OBJECT_NAME_NOT_FOUND)) { \
		finfo1.generic.in.fname = path_fname_new; \
		status2 = smb_raw_pathinfo(cli->tree, mem_ctx, &finfo1); \
	} \
	if (!NT_STATUS_IS_OK(status2)) { \
		printf("(%d) %s pathinfo - %s\n", __LINE__, #call, nt_errstr(status2)); \
		ret = False; \
	}} while (0)

#define CHECK1(call) \
	do { if (NT_STATUS_IS_OK(status)) { \
		finfo2.generic.level = RAW_FILEINFO_ ## call; \
		if (check_fnum) { \
			finfo2.generic.in.fnum = fnum; \
			status2 = smb_raw_fileinfo(cli->tree, mem_ctx, &finfo2); \
		} else { \
			finfo2.generic.in.fname = path_fname; \
			status2 = smb_raw_pathinfo(cli->tree, mem_ctx, &finfo2); \
			if (NT_STATUS_EQUAL(status2, NT_STATUS_OBJECT_NAME_NOT_FOUND)) { \
				finfo2.generic.in.fname = path_fname_new; \
				status2 = smb_raw_pathinfo(cli->tree, mem_ctx, &finfo2); \
			} \
		} \
		if (!NT_STATUS_IS_OK(status2)) { \
			printf("%s - %s\n", #call, nt_errstr(status2)); \
		} \
	}} while (0)

#define CHECK_VALUE(call, stype, field, value) do { \
 	CHECK1(call); \
	if (NT_STATUS_IS_OK(status) && finfo2.stype.out.field != value) { \
		printf("(%d) %s - %s/%s should be 0x%x - 0x%x\n", __LINE__, \
		       call_name, #stype, #field, \
		       (uint_t)value, (uint_t)finfo2.stype.out.field); \
		dump_all_info(mem_ctx, &finfo1); \
	}} while (0)

#define CHECK_TIME(call, stype, field, value) do { \
 	CHECK1(call); \
	if (NT_STATUS_IS_OK(status) && nt_time_to_unix(&finfo2.stype.out.field) != value) { \
		printf("(%d) %s - %s/%s should be 0x%x - 0x%x\n", __LINE__, \
		        call_name, #stype, #field, \
		        (uint_t)value, \
			(uint_t)nt_time_to_unix(&finfo2.stype.out.field)); \
		printf("\t%s", http_timestring(mem_ctx, value)); \
		printf("\t%s\n", nt_time_string(mem_ctx, &finfo2.stype.out.field)); \
		dump_all_info(mem_ctx, &finfo1); \
	}} while (0)

#define CHECK_STR(call, stype, field, value) do { \
 	CHECK1(call); \
	if (NT_STATUS_IS_OK(status) && strcmp(finfo2.stype.out.field, value) != 0) { \
		printf("(%d) %s - %s/%s should be '%s' - '%s'\n", __LINE__, \
		        call_name, #stype, #field, \
		        value, \
			finfo2.stype.out.field); \
		dump_all_info(mem_ctx, &finfo1); \
	}} while (0)

	
	printf("test setattr\n");
	sfinfo.setattr.in.attrib = FILE_ATTRIBUTE_READONLY;
	sfinfo.setattr.in.write_time = basetime;
	CHECK_CALL_PATH(SETATTR, NT_STATUS_OK);
	CHECK_VALUE  (ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_READONLY);
	CHECK_TIME   (ALL_INFO, all_info, write_time, basetime);

	printf("setting to NORMAL doesn't do anything\n");
	sfinfo.setattr.in.attrib = FILE_ATTRIBUTE_NORMAL;
	sfinfo.setattr.in.write_time = 0;
	CHECK_CALL_PATH(SETATTR, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_READONLY);
	CHECK_TIME (ALL_INFO, all_info, write_time, basetime);

	printf("a zero write_time means don't change\n");
	sfinfo.setattr.in.attrib = 0;
	sfinfo.setattr.in.write_time = 0;
	CHECK_CALL_PATH(SETATTR, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_NORMAL);
	CHECK_TIME (ALL_INFO, all_info, write_time, basetime);

	printf("test setattre\n");
	sfinfo.setattre.in.create_time = basetime + 20;
	sfinfo.setattre.in.access_time = basetime + 30;
	sfinfo.setattre.in.write_time  = basetime + 40;
	CHECK_CALL_FNUM(SETATTRE, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 20);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 30);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 40);

	sfinfo.setattre.in.create_time = 0;
	sfinfo.setattre.in.access_time = 0;
	sfinfo.setattre.in.write_time  = 0;
	CHECK_CALL_FNUM(SETATTRE, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 20);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 30);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 40);

	printf("test standard level\n");
	sfinfo.standard.in.create_time = basetime + 100;
	sfinfo.standard.in.access_time = basetime + 200;
	sfinfo.standard.in.write_time  = basetime + 300;
	CHECK_CALL_FNUM(STANDARD, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 100);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 200);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 300);

	printf("test basic_info level\n");
	basetime += 86400;
	unix_to_nt_time(&sfinfo.basic_info.in.create_time, basetime + 100);
	unix_to_nt_time(&sfinfo.basic_info.in.access_time, basetime + 200);
	unix_to_nt_time(&sfinfo.basic_info.in.write_time,  basetime + 300);
	unix_to_nt_time(&sfinfo.basic_info.in.change_time, basetime + 400);
	sfinfo.basic_info.in.attrib = FILE_ATTRIBUTE_READONLY;
	CHECK_CALL_FNUM(BASIC_INFO, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 100);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 200);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 300);
	CHECK_TIME(ALL_INFO, all_info, change_time, basetime + 400);
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_READONLY);

	printf("a zero time means don't change\n");
	unix_to_nt_time(&sfinfo.basic_info.in.create_time, 0);
	unix_to_nt_time(&sfinfo.basic_info.in.access_time, 0);
	unix_to_nt_time(&sfinfo.basic_info.in.write_time,  0);
	unix_to_nt_time(&sfinfo.basic_info.in.change_time, 0);
	sfinfo.basic_info.in.attrib = FILE_ATTRIBUTE_NORMAL;
	CHECK_CALL_FNUM(BASIC_INFO, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 100);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 200);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 300);
	CHECK_TIME(ALL_INFO, all_info, change_time, basetime + 400);
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_NORMAL);

	printf("test basic_information level\n");
	basetime += 86400;
	unix_to_nt_time(&sfinfo.basic_info.in.create_time, basetime + 100);
	unix_to_nt_time(&sfinfo.basic_info.in.access_time, basetime + 200);
	unix_to_nt_time(&sfinfo.basic_info.in.write_time,  basetime + 300);
	unix_to_nt_time(&sfinfo.basic_info.in.change_time, basetime + 400);
	sfinfo.basic_info.in.attrib = FILE_ATTRIBUTE_READONLY;
	CHECK_CALL_FNUM(BASIC_INFORMATION, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 100);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 200);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 300);
	CHECK_TIME(ALL_INFO, all_info, change_time, basetime + 400);
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_READONLY);

	CHECK_CALL_PATH(BASIC_INFORMATION, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 100);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 200);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 300);
	CHECK_TIME(ALL_INFO, all_info, change_time, basetime + 400);
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_READONLY);

	printf("a zero time means don't change\n");
	unix_to_nt_time(&sfinfo.basic_info.in.create_time, 0);
	unix_to_nt_time(&sfinfo.basic_info.in.access_time, 0);
	unix_to_nt_time(&sfinfo.basic_info.in.write_time,  0);
	unix_to_nt_time(&sfinfo.basic_info.in.change_time, 0);
	sfinfo.basic_info.in.attrib = FILE_ATTRIBUTE_NORMAL;
	CHECK_CALL_FNUM(BASIC_INFORMATION, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 100);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 200);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 300);
	CHECK_TIME(ALL_INFO, all_info, change_time, basetime + 400);
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_NORMAL);

	CHECK_CALL_PATH(BASIC_INFORMATION, NT_STATUS_OK);
	CHECK_TIME(ALL_INFO, all_info, create_time, basetime + 100);
	CHECK_TIME(ALL_INFO, all_info, access_time, basetime + 200);
	CHECK_TIME(ALL_INFO, all_info, write_time,  basetime + 300);

	/* interesting - w2k3 leaves change_time as current time for 0 change time
	   in setpathinfo
	  CHECK_TIME(ALL_INFO, all_info, change_time, basetime + 400);
	*/
	CHECK_VALUE(ALL_INFO, all_info, attrib,     FILE_ATTRIBUTE_NORMAL);

	printf("test disposition_info level\n");
	sfinfo.disposition_info.in.delete_on_close = 1;
	CHECK_CALL_FNUM(DISPOSITION_INFO, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, delete_pending, 1);
	CHECK_VALUE(ALL_INFO, all_info, nlink, 0);

	sfinfo.disposition_info.in.delete_on_close = 0;
	CHECK_CALL_FNUM(DISPOSITION_INFO, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, delete_pending, 0);
	CHECK_VALUE(ALL_INFO, all_info, nlink, 1);

	printf("test disposition_information level\n");
	sfinfo.disposition_info.in.delete_on_close = 1;
	CHECK_CALL_FNUM(DISPOSITION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, delete_pending, 1);
	CHECK_VALUE(ALL_INFO, all_info, nlink, 0);

	/* this would delete the file! */
	/*
	  CHECK_CALL_PATH(DISPOSITION_INFORMATION, NT_STATUS_OK);
	  CHECK_VALUE(ALL_INFO, all_info, delete_pending, 1);
	  CHECK_VALUE(ALL_INFO, all_info, nlink, 0);
	*/

	sfinfo.disposition_info.in.delete_on_close = 0;
	CHECK_CALL_FNUM(DISPOSITION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, delete_pending, 0);
	CHECK_VALUE(ALL_INFO, all_info, nlink, 1);

	CHECK_CALL_PATH(DISPOSITION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, delete_pending, 0);
	CHECK_VALUE(ALL_INFO, all_info, nlink, 1);

	printf("test allocation_info level\n");
	sfinfo.allocation_info.in.alloc_size = 0;
	CHECK_CALL_FNUM(ALLOCATION_INFO, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 0);
	CHECK_VALUE(ALL_INFO, all_info, alloc_size, 0);

	sfinfo.allocation_info.in.alloc_size = 4096;
	CHECK_CALL_FNUM(ALLOCATION_INFO, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, alloc_size, 4096);
	CHECK_VALUE(ALL_INFO, all_info, size, 0);

	RECREATE_BOTH;
	sfinfo.allocation_info.in.alloc_size = 0;
	CHECK_CALL_FNUM(ALLOCATION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 0);
	CHECK_VALUE(ALL_INFO, all_info, alloc_size, 0);

	CHECK_CALL_PATH(ALLOCATION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 0);
	CHECK_VALUE(ALL_INFO, all_info, alloc_size, 0);

	sfinfo.allocation_info.in.alloc_size = 4096;
	CHECK_CALL_FNUM(ALLOCATION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, alloc_size, 4096);
	CHECK_VALUE(ALL_INFO, all_info, size, 0);

	/* setting the allocation size up via setpathinfo seems
	   to be broken in w2k3 */
	CHECK_CALL_PATH(ALLOCATION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, alloc_size, 0);
	CHECK_VALUE(ALL_INFO, all_info, size, 0);

	printf("test end_of_file_info level\n");
	sfinfo.end_of_file_info.in.size = 37;
	CHECK_CALL_FNUM(END_OF_FILE_INFO, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 37);

	sfinfo.end_of_file_info.in.size = 7;
	CHECK_CALL_FNUM(END_OF_FILE_INFO, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 7);

	sfinfo.end_of_file_info.in.size = 37;
	CHECK_CALL_FNUM(END_OF_FILE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 37);

	CHECK_CALL_PATH(END_OF_FILE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 37);

	sfinfo.end_of_file_info.in.size = 7;
	CHECK_CALL_FNUM(END_OF_FILE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 7);

	CHECK_CALL_PATH(END_OF_FILE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(ALL_INFO, all_info, size, 7);

	printf("test position_information level\n");
	sfinfo.position_information.in.position = 123456;
	CHECK_CALL_FNUM(POSITION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(POSITION_INFORMATION, position_information, position, 123456);

	CHECK_CALL_PATH(POSITION_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(POSITION_INFORMATION, position_information, position, 0);

	printf("test mode_information level\n");
	sfinfo.mode_information.in.mode = 2;
	CHECK_CALL_FNUM(MODE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(MODE_INFORMATION, mode_information, mode, 2);

	CHECK_CALL_PATH(MODE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(MODE_INFORMATION, mode_information, mode, 0);

	sfinfo.mode_information.in.mode = 1;
	CHECK_CALL_FNUM(MODE_INFORMATION, NT_STATUS_INVALID_PARAMETER);
	CHECK_CALL_PATH(MODE_INFORMATION, NT_STATUS_INVALID_PARAMETER);

	sfinfo.mode_information.in.mode = 0;
	CHECK_CALL_FNUM(MODE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(MODE_INFORMATION, mode_information, mode, 0);

	CHECK_CALL_PATH(MODE_INFORMATION, NT_STATUS_OK);
	CHECK_VALUE(MODE_INFORMATION, mode_information, mode, 0);
#if 1
	printf("finally the rename_information level\n");
	cli_close(cli, create_complex_file(cli, mem_ctx, fnum_fname_new));
	cli_close(cli, create_complex_file(cli, mem_ctx, path_fname_new));

	sfinfo.rename_information.in.overwrite = 0;
	sfinfo.rename_information.in.root_fid  = 0;
	sfinfo.rename_information.in.new_name  = fnum_fname_new+strlen(BASEDIR)+1;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_OBJECT_NAME_COLLISION);

	sfinfo.rename_information.in.new_name  = path_fname_new+strlen(BASEDIR)+1;
	CHECK_CALL_PATH(RENAME_INFORMATION, NT_STATUS_OBJECT_NAME_COLLISION);

	sfinfo.rename_information.in.new_name  = fnum_fname_new;
	sfinfo.rename_information.in.overwrite = 1;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_NOT_SUPPORTED);

	sfinfo.rename_information.in.new_name  = fnum_fname_new+strlen(BASEDIR)+1;
	sfinfo.rename_information.in.overwrite = 1;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname_new);

	printf("Trying rename with dest file open\n");
	fnum2 = create_complex_file(cli, mem_ctx, fnum_fname);
	sfinfo.rename_information.in.new_name  = fnum_fname+strlen(BASEDIR)+1;
	sfinfo.rename_information.in.overwrite = 1;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_ACCESS_DENIED);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname_new);

	fnum_saved = fnum;
	fnum = fnum2;
	sfinfo.disposition_info.in.delete_on_close = 1;
	CHECK_CALL_FNUM(DISPOSITION_INFO, NT_STATUS_OK);
	fnum = fnum_saved;

	printf("Trying rename with dest file open and delete_on_close\n");
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_ACCESS_DENIED);

	cli_close(cli, fnum2);
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname);

	printf("Trying rename with source file open twice\n");
	sfinfo.rename_information.in.new_name  = fnum_fname+strlen(BASEDIR)+1;
	sfinfo.rename_information.in.overwrite = 1;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname);

	fnum2 = create_complex_file(cli, mem_ctx, fnum_fname);
	sfinfo.rename_information.in.new_name  = fnum_fname_new+strlen(BASEDIR)+1;
	sfinfo.rename_information.in.overwrite = 0;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname_new);
	cli_close(cli, fnum2);

	sfinfo.rename_information.in.new_name  = fnum_fname+strlen(BASEDIR)+1;
	sfinfo.rename_information.in.overwrite = 0;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname);

	sfinfo.rename_information.in.new_name  = path_fname_new+strlen(BASEDIR)+1;
	sfinfo.rename_information.in.overwrite = 1;
	CHECK_CALL_PATH(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, path_fname_new);

	sfinfo.rename_information.in.new_name  = fnum_fname+strlen(BASEDIR)+1;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname);

	sfinfo.rename_information.in.new_name  = path_fname+strlen(BASEDIR)+1;
	CHECK_CALL_PATH(RENAME_INFORMATION, NT_STATUS_OK);
	CHECK_STR(NAME_INFO, name_info, fname.s, path_fname);

	printf("Trying rename with a root fid\n");
	d_fnum = create_directory_handle(cli->tree, BASEDIR);
	sfinfo.rename_information.in.new_name  = fnum_fname_new+strlen(BASEDIR)+1;
	sfinfo.rename_information.in.root_fid = d_fnum;
	CHECK_CALL_FNUM(RENAME_INFORMATION, NT_STATUS_INVALID_PARAMETER);
	CHECK_STR(NAME_INFO, name_info, fname.s, fnum_fname);
#endif

#if 0
	printf("test unix_basic level\n");
	CHECK_CALL_FNUM(UNIX_BASIC, NT_STATUS_OK);
	CHECK_CALL_PATH(UNIX_BASIC, NT_STATUS_OK);

	printf("test unix_link level\n");
	CHECK_CALL_FNUM(UNIX_LINK, NT_STATUS_OK);
	CHECK_CALL_PATH(UNIX_LINK, NT_STATUS_OK);
#endif

done:
	smb_raw_exit(cli->session);
	cli_close(cli, fnum);
	if (!cli_unlink(cli, fnum_fname)) {
		printf("Failed to delete %s - %s\n", fnum_fname, cli_errstr(cli));
	}
	if (!cli_unlink(cli, path_fname)) {
		printf("Failed to delete %s - %s\n", path_fname, cli_errstr(cli));
	}

	torture_close_connection(cli);
	talloc_destroy(mem_ctx);
	return ret;
}


/* 
   look for the w2k3 setpathinfo STANDARD bug
*/
BOOL torture_raw_sfileinfo_bug(int dummy)
{
	struct cli_state *cli;
	TALLOC_CTX *mem_ctx;
	const char *fname = "\\bug3.txt";
	union smb_setfileinfo sfinfo;
	NTSTATUS status;
	int fnum;

	if (!torture_open_connection(&cli)) {
		return False;
	}

	mem_ctx = talloc_init("torture_sfileinfo");

	fnum = create_complex_file(cli, mem_ctx, fname);
	cli_close(cli, fnum);

	sfinfo.generic.level = RAW_SFILEINFO_STANDARD;
	sfinfo.generic.file.fname = fname;

	sfinfo.standard.in.create_time = 0;
	sfinfo.standard.in.access_time = 0;
	sfinfo.standard.in.write_time  = 0;

	status = smb_raw_setpathinfo(cli->tree, &sfinfo);
	printf("%s - %s\n", fname, nt_errstr(status));

	printf("now try and delete %s\n", fname);

	return True;
}
