/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   client string routines
   Copyright (C) Andrew Tridgell 2001
   
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

#define NO_SYSLOG

#include "includes.h"

/* we will delete this variable once our client side unicode support is complete */
extern int cli_use_unicode;

/****************************************************************************
copy a string from a char* src to a unicode or ascii
dos code page destination choosing unicode or ascii based on the 
cli->capabilities flag
return the number of bytes occupied by the string in the destination
flags can have:
  CLISTR_TERMINATE means include the null termination
  CLISTR_CONVERT   means convert from unix to dos codepage
  CLISTR_UPPER     means uppercase in the destination
dest_len is the maximum length allowed in the destination. If dest_len
is -1 then no maxiumum is used
****************************************************************************/
int clistr_push(struct cli_state *cli, void *dest, char *src, int dest_len, int flags)
{
	int len;

	/* treat a pstring as "unlimited" length */
	if (dest_len == -1) {
		dest_len = sizeof(pstring);
	}

	if (!cli_use_unicode || !(cli->capabilities & CAP_UNICODE)) {
		/* the server doesn't want unicode */
		safe_strcpy(dest, src, dest_len);
		len = strlen(dest);
		if (flags & CLISTR_TERMINATE) len++;
		if (flags & CLISTR_CONVERT) unix_to_dos(dest,True);
		if (flags & CLISTR_UPPER) strupper(dest);
		return len;
	}

	/* the server likes unicode. give it the works */
	if (flags & CLISTR_CONVERT) {
		dos_PutUniCode(dest, src, dest_len, flags & CLISTR_TERMINATE);
	} else {
		ascii_to_unistr(dest, src, dest_len);
	}
	if (flags & CLISTR_UPPER) {
		strupper_w(dest);
	}
	len = strlen(src)*2;
	if (flags & CLISTR_TERMINATE) len += 2;
	return len;
}


/****************************************************************************
return the length that a string would occupy when copied with clistr_push()
  CLISTR_TERMINATE means include the null termination
  CLISTR_CONVERT   means convert from unix to dos codepage
  CLISTR_UPPER     means uppercase in the destination
****************************************************************************/
int clistr_push_size(struct cli_state *cli, char *src, int dest_len, int flags)
{
	int len = strlen(src);
	if (flags & CLISTR_TERMINATE) len++;
	if (cli_use_unicode && (cli->capabilities & CAP_UNICODE)) len *= 2;
	return len;
}

/****************************************************************************
copy a string from a unicode or ascii source (depending on
cli->capabilities) to a char* destination
flags can have:
  CLISTR_CONVERT   means convert from dos to unix codepage
  CLISTR_TERMINATE means the string in src is null terminated
if CLISTR_TERMINATE is set then src_len is ignored
src_len is the length of the source area in bytes
return the number of bytes occupied by the string in src
****************************************************************************/
int clistr_pull(struct cli_state *cli, char *dest, void *src, int dest_len, int src_len, int flags)
{
	int len;

	if (dest_len == -1) {
		dest_len = sizeof(pstring);
	}

	if (!cli_use_unicode || !(cli->capabilities & CAP_UNICODE)) {
		/* the server doesn't want unicode */
		if (flags & CLISTR_TERMINATE) {
			safe_strcpy(dest, src, dest_len);
			len = strlen(src)+1;
		} else {
			if (src_len > dest_len) src_len = dest_len;
			len = src_len;
			memcpy(dest, src, len);
			dest[len] = 0;
		}
		if (flags & CLISTR_CONVERT) dos_to_unix(dest,True);
		return len;
	}

	if (flags & CLISTR_TERMINATE) {
		unistr_to_ascii(dest, src, dest_len);
		len = strlen(dest)*2 + 2;
	} else {
		int i, c;
		if (dest_len < src_len) src_len = dest_len;
		for (i=0; i < src_len; i += 2) {
			c = SVAL(src, i);
			*dest++ = c;
		}
		*dest++ = 0;
		len = src_len;
	}
	if (flags & CLISTR_CONVERT) dos_to_unix(dest,True);
	return len;
}

/****************************************************************************
return the length that a string would occupy (not including the null)
when copied with clistr_pull()
if src_len is -1 then assume the source is null terminated
****************************************************************************/
int clistr_pull_size(struct cli_state *cli, void *src, int src_len)
{
	if (!cli_use_unicode || !(cli->capabilities & CAP_UNICODE)) {
		return strlen(src);
	}	
	return strlen_w(src);
}

/****************************************************************************
return an alignment of either 0 or 1
if unicode is not negotiated then return 0
otherwise return 1 if offset is off
****************************************************************************/
int clistr_align(struct cli_state *cli, int offset)
{
	if (!cli_use_unicode || !(cli->capabilities & CAP_UNICODE)) return 0;
	return offset & 1;
}
