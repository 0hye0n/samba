/* 
   Unix SMB/Netbios implementation.
   Version 3.0
   filename matching routine
   Copyright (C) Andrew Tridgell 1992-1998 

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/*
   This module was originally based on fnmatch.c copyright by the Free
   Software Foundation. It bears little resemblence to that code now 
*/  


#if FNMATCH_TEST
#include <stdio.h>
#include <stdlib.h>
#else
#include "includes.h"
#endif



/* 
   bugger. we need a separate wildcard routine for older versions
   of the protocol. This is not yet perfect, but its a lot
   better thaan what we had */
static int ms_fnmatch_lanman_core(const char *pattern, const char *string)
{
	const char *p = pattern, *n = string;
	char c;

	if (strcmp(p,"?")==0 && strcmp(n,".")==0) goto match;

	while ((c = *p++)) {
		switch (c) {
		case '.':
			if (! *n) goto next;
			/* if (! *n && ! *p) goto match; */
			if (*n != '.') goto nomatch;
			n++;
			break;

		case '?':
			if (! *n) goto next;
			if ((*n == '.' && n[1] != '.') || ! *n) goto next;
			n++;
			break;

		case '>':
			if (! *n) goto next;
			if (n[0] == '.') {
				if (! n[1] && ms_fnmatch_lanman_core(p, n+1) == 0) goto match;
				if (ms_fnmatch_lanman_core(p, n) == 0) goto match;
				goto nomatch;
			}
			n++;
			break;

		case '*':
			if (! *n) goto next;
			if (! *p) goto match;
			for (; *n; n++) {
				if (ms_fnmatch_lanman_core(p, n) == 0) goto match;
			}
			break;

		case '<':
			for (; *n; n++) {
				if (ms_fnmatch_lanman_core(p, n) == 0) goto match;
				if (*n == '.' && !strchr(n+1,'.')) {
					n++;
					break;
				}
			}
			break;

		case '"':
			if (*n == 0 && ms_fnmatch_lanman_core(p, n) == 0) goto match;
			if (*n != '.') goto nomatch;
			n++;
			break;

		default:
			if (c != *n) goto nomatch;
			n++;
		}
	}
	
	if (! *n) goto match;
	
 nomatch:
	/*
	if (verbose) printf("NOMATCH pattern=[%s] string=[%s]\n", pattern, string);
	*/
	return -1;

next:
	if (ms_fnmatch_lanman_core(p, n) == 0) goto match;
        goto nomatch;

 match:
	/*
	if (verbose) printf("MATCH   pattern=[%s] string=[%s]\n", pattern, string);
	*/
	return 0;
}

static int ms_fnmatch_lanman1(const char *pattern, const char *string)
{
	if (!strpbrk(pattern, "?*<>\"")) {
		if (strcmp(string,"..") == 0) string = ".";
		return strcasecmp(pattern, string);
	}

	if (strcmp(string,"..") == 0 || strcmp(string,".") == 0) {
		return ms_fnmatch_lanman_core(pattern, "..") &&
			ms_fnmatch_lanman_core(pattern, ".");
	}

	return ms_fnmatch_lanman_core(pattern, string);
}


/* the following function was derived using the masktest utility -
   after years of effort we finally have a perfect MS wildcard
   matching routine! 

   NOTE: this matches only filenames with no directory component

   Returns 0 on match, -1 on fail.
*/
int ms_fnmatch(const char *pattern, const char *string)
{
	const char *p = pattern, *n = string;
	char c;
	extern int Protocol;

	if (Protocol <= PROTOCOL_LANMAN2) {
		return ms_fnmatch_lanman1(pattern, string);
	}

	while ((c = *p++)) {
		switch (c) {
		case '?':
			if (! *n) return -1;
			n++;
			break;

		case '>':
			if (n[0] == '.') {
				if (! n[1] && ms_fnmatch(p, n+1) == 0) return 0;
				if (ms_fnmatch(p, n) == 0) return 0;
				return -1;
			}
			if (! *n) return ms_fnmatch(p, n);
			n++;
			break;

		case '*':
			for (; *n; n++) {
				if (ms_fnmatch(p, n) == 0) return 0;
			}
			break;

		case '<':
			for (; *n; n++) {
				if (ms_fnmatch(p, n) == 0) return 0;
				if (*n == '.' && !strchr(n+1,'.')) {
					n++;
					break;
				}
			}
			break;

		case '"':
			if (*n == 0 && ms_fnmatch(p, n) == 0) return 0;
			if (*n != '.') return -1;
			n++;
			break;

		default:
			if (c != *n) return -1;
			n++;
		}
	}
	
	if (! *n) return 0;
	
	return -1;
}


#if FNMATCH_TEST

static int match_one(char *pattern, char *file)
{
	if (strcmp(file,"..") == 0) file = ".";
	if (strcmp(pattern,".") == 0) return -1;

	return ms_fnmatch(pattern, file);
}

static char *match_test(char *pattern, char *file, char *short_name)
{
	static char ret[4];
	strncpy(ret, "---", 3);

	if (match_one(pattern, ".") == 0) ret[0] = '+';
	if (match_one(pattern, "..") == 0) ret[1] = '+';
	if (match_one(pattern, file) == 0 || 
	    (*short_name && match_one(pattern, short_name)==0)) ret[2] = '+';
	return ret;
}

 int main(int argc, char *argv[])
{
	int ret;
	char ans[4], mask[100], file[100], mfile[100];
	char *ans2;
	int n, i=0;
	char line[200];

	if (argc == 3) {
		ret = ms_fnmatch(argv[1], argv[2]);
		if (ret == 0) 
			printf("YES\n");
		else printf("NO\n");
		return ret;
	}
	mfile[0] = 0;

	while (fgets(line, sizeof(line)-1, stdin)) {
		n = sscanf(line, "%3s %s %s %s\n", ans, mask, file, mfile);
		if (n < 3) continue;
		ans2 = match_test(mask, file, mfile);
		if (strcmp(ans2, ans)) {
			printf("%s %s %d mask=[%s]  file=[%s]  mfile=[%s]\n",
			       ans, ans2, i, mask, file, mfile);
		}
		i++;
		mfile[0] = 0;
	}
	return 0;
}
#endif /* FNMATCH_TEST */

