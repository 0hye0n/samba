/* 
   ldb database library

   Copyright (C) Andrew Tridgell  2004
   Copyright (C) Stefan Metzmacher  2004

     ** NOTE! The following LGPL license applies to the ldb
     ** library. This does NOT imply that all of Samba is released
     ** under the LGPL
   
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
 *  Name: ldb
 *
 *  Component: ldbrename
 *
 *  Description: utility to rename records - modelled on ldapmodrdn
 *
 *  Author: Andrew Tridgell
 *  Author: Stefan Metzmacher
 */

#include "includes.h"
#include "ldb/include/ldb.h"
#include "ldb/include/ldb_private.h"
#include "ldb/tools/cmdline.h"

#ifdef _SAMBA_BUILD_
#include "system/filesys.h"
#endif

static void usage(void)
{
	printf("Usage: ldbrename [<options>] <olddn> <newdn>\n");
	printf("Options:\n");
	printf("  -H ldb_url       choose the database (or $LDB_URL)\n");
	printf("  -o options       pass options like modules to activate\n");
	printf("              e.g: -o modules:timestamps\n");
	printf("\n");
	printf("Renames records in a ldb\n\n");
	exit(1);
}


 int main(int argc, const char **argv)
{
	struct ldb_context *ldb;
	int ret;
	struct ldb_cmdline *options;
	const char *dn1, *dn2;

	ldb = ldb_init(NULL);

	options = ldb_cmdline_process(ldb, argc, argv, usage);

	ret = ldb_connect(ldb, options->url, 0, options->options);
	if (ret != 0) {
		fprintf(stderr, "Failed to connect to %s - %s\n", 
			options->url, ldb_errstring(ldb));
		talloc_free(ldb);
		exit(1);
	}

	if (options->argc < 2) {
		usage();
	}

	dn1 = options->argv[0];
	dn2 = options->argv[1];

	ret = ldb_rename(ldb, dn1, dn2);
	if (ret == 0) {
		printf("Renamed 1 record\n");
	} else  {
		printf("rename of '%s' to '%s' failed - %s\n", 
			dn1, dn2, ldb_errstring(ldb));
	}

	talloc_free(ldb);
	
	return ret;
}
