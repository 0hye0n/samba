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
 *  Component: ldb utf8 handling
 *
 *  Description: case folding and case comparison for UTF8 strings
 *
 *  Author: Andrew Tridgell
 */

#include "includes.h"
#include "ldb/include/includes.h"


/*
  this allow the user to pass in a caseless comparison
  function to handle utf8 caseless comparisons
 */
void ldb_set_utf8_fns(struct ldb_context *ldb,
			void *context,
			int (*cmp)(void *, const char *, const char *),
			char *(*casefold)(void *, void *, const char *))
{
	if (context)
		ldb->utf8_fns.context = context;
	if (cmp)
		ldb->utf8_fns.caseless_cmp = cmp;
	if (casefold)
		ldb->utf8_fns.casefold = casefold;
}

/*
  a simple case folding function
  NOTE: does not handle UTF8
*/
char *ldb_casefold_default(void *context, void *mem_ctx, const char *s)
{
	int i;
	char *ret = talloc_strdup(mem_ctx, s);
	if (!s) {
		errno = ENOMEM;
		return NULL;
	}
	for (i=0;ret[i];i++) {
		ret[i] = toupper((unsigned char)ret[i]);
	}
	return ret;
}

/*
  a caseless compare, optimised for 7 bit
  NOTE: doesn't handle UTF8
*/

int ldb_caseless_cmp_default(void *context, const char *s1, const char *s2)
{
	return strcasecmp(s1,s2);
}

void ldb_set_utf8_default(struct ldb_context *ldb)
{
	ldb_set_utf8_fns(ldb, NULL, ldb_caseless_cmp_default, ldb_casefold_default);
}

char *ldb_casefold(struct ldb_context *ldb, void *mem_ctx, const char *s)
{
	return ldb->utf8_fns.casefold(ldb->utf8_fns.context, mem_ctx, s);
}

int ldb_caseless_cmp(struct ldb_context *ldb, const char *s1, const char *s2)
{
	return ldb->utf8_fns.caseless_cmp(ldb->utf8_fns.context, s1, s2);
}

/*
  check the attribute name is valid according to rfc2251
  returns 1 if the name is ok
 */

int ldb_valid_attr_name(const char *s)
{
	int i;

	if (!s || !s[0])
		return 0;

	/* handle special ldb_tdb wildcard */
	if (strcmp(s, "*") == 0) return 1;

	for (i = 0; s[i]; i++) {
		if (! isascii(s[i])) {
			return 0;
		}
		if (i == 0) { /* first char must be an alpha (or our special '@' identifier) */
			if (! (isalpha(s[i]) || (s[i] == '@'))) {
				return 0;
			}
		} else {
			if (! (isalnum(s[i]) || (s[i] == '-'))) {
				return 0;
			}
		}
	}
	return 1;
}

/*
  compare two attribute names
  attribute names are restricted by rfc2251 so using strcasecmp here is ok.
  return 0 for match
*/
int ldb_attr_cmp(const char *attr1, const char *attr2)
{
	return strcasecmp(attr1, attr2);
}

/*
  we accept either 'dn' or 'distinguishedName' for a distinguishedName
*/
int ldb_attr_dn(const char *attr)
{
	if (ldb_attr_cmp(attr, "dn") == 0 ||
	    ldb_attr_cmp(attr, "distinguishedName") == 0) {
		return 0;
	}
	return -1;
}
