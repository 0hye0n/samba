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
 *  Component: ldb core API
 *
 *  Description: core API routines interfacing to ldb backends
 *
 *  Author: Andrew Tridgell
 */

#include "includes.h"

/* 
 connect to a database. The URL can either be one of the following forms
   ldb://path
   ldapi://path

   flags is made up of LDB_FLG_*

   the options are passed uninterpreted to the backend, and are
   backend specific
*/
struct ldb_context *ldb_connect(const char *url, unsigned int flags,
				const char *options[])
{
	struct ldb_context *ldb_ctx = NULL;

	if (strncmp(url, "tdb:", 4) == 0 ||
	    strchr(url, ':') == NULL) {
		ldb_ctx = ltdb_connect(url, flags, options);
	}

#if HAVE_LDAP
	if (strncmp(url, "ldap", 4) == 0) {
		ldb_ctx = lldb_connect(url, flags, options);
	}
#endif

	if (ldb_ctx) {
		if (register_ldb_modules(ldb_ctx, options) == 0) {
			return ldb_ctx;
		}
	}

	errno = EINVAL;
	return NULL;
}

/*
  close the connection to the database
*/
int ldb_close(struct ldb_context *ldb)
{
	int ret;

	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_close");

	return ldb->module->ops->close(ldb->module);
}


/*
  search the database given a LDAP-like search expression

  return the number of records found, or -1 on error
*/
int ldb_search(struct ldb_context *ldb, 
	       const char *base,
	       enum ldb_scope scope,
	       const char *expression,
	       const char * const *attrs, struct ldb_message ***res)
{
	int ret, count;

	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_search(%s)\n", expression);

	count = ldb->module->ops->search(ldb->module, base, scope, expression, attrs, res);

	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_search found %d records\n", ret);
	return count;
}

/* 
   free a set of messages returned by ldb_search
*/
int ldb_search_free(struct ldb_context *ldb, struct ldb_message **msgs)
{
	return ldb->module->ops->search_free(ldb->module, msgs);
}


/*
  add a record to the database. Will fail if a record with the given class and key
  already exists
*/
int ldb_add(struct ldb_context *ldb, 
	    const struct ldb_message *message)
{
	int ret;

	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_add(%s)\n", message->dn);

	ret = ldb->module->ops->add_record(ldb->module, message);

done:
	ldb_debug(ldb, LDB_DEBUG_TRACE, "return: %d\n", ret);
	return ret;
}

/*
  modify the specified attributes of a record
*/
int ldb_modify(struct ldb_context *ldb, 
	       const struct ldb_message *message)
{
	int ret;

	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_modify(%s)\n", message->dn);

	ret = ldb->module->ops->modify_record(ldb->module, message);

done:
	ldb_debug(ldb, LDB_DEBUG_TRACE, "return: %d\n", ret);
	return ret;
}


/*
  delete a record from the database
*/
int ldb_delete(struct ldb_context *ldb, const char *dn)
{
	int ret;

	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_delete(%s)\n", dn);

	ret = ldb->module->ops->delete_record(ldb->module, dn);

done:
	ldb_debug(ldb, LDB_DEBUG_TRACE, "return: %d\n", ret);
	return ret;
}

/*
  rename a record in the database
*/
int ldb_rename(struct ldb_context *ldb, const char *olddn, const char *newdn)
{
	int ret;
	ret = ldb->module->ops->rename_record(ldb->module, olddn, newdn);
	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_rename(%s,%s) -> %d\n", olddn, newdn);
	return ret;
}

/*
  return extended error information 
*/
const char *ldb_errstring(struct ldb_context *ldb)
{
	const char *err;

	ldb_debug(ldb, LDB_DEBUG_TRACE, "ldb_errstring\n");

	err = ldb->module->ops->errstring(ldb->module);

done:
	return err;
}

