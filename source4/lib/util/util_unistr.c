/* 
   Unix SMB/CIFS implementation.
   Samba utility functions
   Copyright (C) Andrew Tridgell 1992-2001
   Copyright (C) Simo Sorce 2001
   
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
#include "system/iconv.h"

/**
 * @file
 * @brief Unicode string manipulation
 */

/* these 2 tables define the unicode case handling.  They are loaded
   at startup either via mmap() or read() from the lib directory */
static void *upcase_table;
static void *lowcase_table;


/*******************************************************************
load the case handling tables
********************************************************************/
static void load_case_tables(void)
{
	TALLOC_CTX *mem_ctx;

	mem_ctx = talloc_init("load_case_tables");
	if (!mem_ctx) {
		smb_panic("No memory for case_tables");
	}
	upcase_table = map_file(lib_path(mem_ctx, "upcase.dat"), 0x20000);
	lowcase_table = map_file(lib_path(mem_ctx, "lowcase.dat"), 0x20000);
	talloc_free(mem_ctx);
	if (upcase_table == NULL) {
		/* try also under codepages for testing purposes */
		upcase_table = map_file("codepages/upcase.dat", 0x20000);
		if (upcase_table == NULL) {
			upcase_table = (void *)-1;
		}
	}
	if (lowcase_table == NULL) {
		/* try also under codepages for testing purposes */
		lowcase_table = map_file("codepages/lowcase.dat", 0x20000);
		if (lowcase_table == NULL) {
			lowcase_table = (void *)-1;
		}
	}
}

/**
 Convert a codepoint_t to upper case.
**/
codepoint_t toupper_w(codepoint_t val)
{
	if (val < 128) {
		return toupper(val);
	}
	if (upcase_table == NULL) {
		load_case_tables();
	}
	if (upcase_table == (void *)-1) {
		return val;
	}
	if (val & 0xFFFF0000) {
		return val;
	}
	return SVAL(upcase_table, val*2);
}

/**
 Convert a codepoint_t to lower case.
**/
codepoint_t tolower_w(codepoint_t val)
{
	if (val < 128) {
		return tolower(val);
	}
	if (lowcase_table == NULL) {
		load_case_tables();
	}
	if (lowcase_table == (void *)-1) {
		return val;
	}
	if (val & 0xFFFF0000) {
		return val;
	}
	return SVAL(lowcase_table, val*2);
}

/**
return the number of bytes occupied by a buffer in CH_UTF16 format
the result includes the null termination
**/
size_t utf16_len(const void *buf)
{
	size_t len;

	for (len = 0; SVAL(buf,len); len += 2) ;

	return len + 2;
}

/**
return the number of bytes occupied by a buffer in CH_UTF16 format
the result includes the null termination
limited by 'n' bytes
**/
size_t utf16_len_n(const void *src, size_t n)
{
	size_t len;

	for (len = 0; (len+2 < n) && SVAL(src, len); len += 2) ;

	if (len+2 <= n) {
		len += 2;
	}

	return len;
}


size_t ucs2_align(const void *base_ptr, const void *p, int flags)
{
	if (flags & (STR_NOALIGN|STR_ASCII))
		return 0;
	return PTR_DIFF(p, base_ptr) & 1;
}

/**
  compare two codepoints case insensitively
*/
int codepoint_cmpi(codepoint_t c1, codepoint_t c2)
{
	if (c1 == c2 ||
	    toupper_w(c1) == toupper_w(c2)) {
		return 0;
	}
	return c1 - c2;
}
