/* 
   ldb database library

   Copyright (C) Andrew Tridgell  2004

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
 *  Component: ldbsearch
 *
 *  Description: utility for ldb search - modelled on ldapsearch
 *
 *  Author: Andrew Tridgell
 */

#include "includes.h"
#include "ldb/include/ldb.h"
#include "ldb/include/ldb_private.h"

#ifdef _SAMBA_BUILD_
#include "system/filesys.h"
#endif

static void usage(void)
{
	printf("Usage: ldbsearch <options> <expression> <attrs...>\n");
	printf("Options:\n");
	printf("  -H ldb_url       choose the database (or $LDB_URL)\n");
	printf("  -s base|sub|one  choose search scope\n");
	printf("  -b basedn        choose baseDN\n");
	printf("  -i               read search expressions from stdin\n");
        printf("  -S               sort returned attributes\n");
	printf("  -o options       pass options like modules to activate\n");
	printf("              e.g: -o modules:timestamps\n");
	exit(1);
}

static int do_compare_msg(struct ldb_message **el1,
		struct ldb_message **el2)
{
	return ldb_dn_cmp((*el1)->dn, (*el2)->dn);
}

static int do_search(struct ldb_context *ldb,
		     const char *basedn,
		     int scope,
                     int sort_attribs,
		     const char *expression,
		     const char * const *attrs)
{
	int ret, i;
	struct ldb_message **msgs;

	ret = ldb_search(ldb, basedn, scope, expression, attrs, &msgs);
	if (ret == -1) {
		printf("search failed - %s\n", ldb_errstring(ldb));
		return -1;
	}

	printf("# returned %d records\n", ret);

	if (sort_attribs) {
		qsort(msgs, ret, sizeof(struct ldb_message *),
				(comparison_fn_t)do_compare_msg);
	}

	for (i=0;i<ret;i++) {
		struct ldb_ldif ldif;
		printf("# record %d\n", i+1);

		ldif.changetype = LDB_CHANGETYPE_NONE;
		ldif.msg = msgs[i];

                if (sort_attribs) {
                        /*
                         * Ensure attributes are always returned in the same
                         * order.  For testing, this makes comparison of old
                         * vs. new much easier.
                         */
                        ldb_msg_sort_elements(ldif.msg);
                }
                
		ldb_ldif_write_file(ldb, stdout, &ldif);
	}

	if (ret > 0) {
		ret = talloc_free(msgs);
		if (ret == -1) {
			fprintf(stderr, "talloc_free failed\n");
			exit(1);
		}
	}

	return 0;
}

 int main(int argc, char * const argv[])
{
	struct ldb_context *ldb;
	const char * const * attrs = NULL;
	const char *ldb_url;
	const char *basedn = NULL;
	const char **options = NULL;
	int opt, ldbopts;
	enum ldb_scope scope = LDB_SCOPE_SUBTREE;
	int interactive = 0, sort_attribs=0, ret=0;

	ldb_url = getenv("LDB_URL");

	ldbopts = 0;
	while ((opt = getopt(argc, argv, "b:H:s:o:hiS")) != EOF) {
		switch (opt) {
		case 'b':
			basedn = optarg;
			break;

		case 'H':
			ldb_url = optarg;
			break;

		case 's':
			if (strcmp(optarg, "base") == 0) {
				scope = LDB_SCOPE_BASE;
			} else if (strcmp(optarg, "sub") == 0) {
				scope = LDB_SCOPE_SUBTREE;
			} else if (strcmp(optarg, "one") == 0) {
				scope = LDB_SCOPE_ONELEVEL;
			}
			break;

		case 'i':
			interactive = 1;
			break;

                case 'S':
                        sort_attribs = 1;
                        break;

		case 'o':
			options = ldb_options_parse(options, &ldbopts, optarg);
			break;

		case 'h':
		default:
			usage();
			break;
		}
	}

	if (!ldb_url) {
		fprintf(stderr, "You must specify a ldb URL\n\n");
		usage();
	}

	argc -= optind;
	argv += optind;

	if (argc < 1 && !interactive) {
		usage();
		exit(1);
	}

	if (argc > 1) {
		attrs = (const char * const *)(argv+1);
	}

	ldb = ldb_connect(ldb_url, LDB_FLG_RDONLY, options);
	if (!ldb) {
		perror("ldb_connect");
		exit(1);
	}

	ldb_set_debug_stderr(ldb);

	if (interactive) {
		char line[1024];
		while (fgets(line, sizeof(line), stdin)) {
			if (do_search(ldb, basedn, scope, sort_attribs, line, attrs) == -1) {
				ret = -1;
			}
		}
	} else {
		ret = do_search(ldb, basedn, scope, sort_attribs, argv[0], attrs);
	}

	talloc_free(ldb);
	return ret;
}
