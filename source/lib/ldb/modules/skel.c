/* 
   ldb database library

   Copyright (C) Simo Sorce  2004

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
 *  Component: ldb skel module
 *
 *  Description: example module
 *
 *  Author: Simo Sorce
 */

#include "includes.h"
#include "ldb/include/ldb.h"
#include "ldb/include/ldb_private.h"

struct private_data {

	char *errstring;
};

/* search */
static int skel_search(struct ldb_module *module, const struct ldb_dn *base,
		       enum ldb_scope scope, const char *expression,
		       const char * const *attrs, struct ldb_message ***res)
{
	return ldb_next_search(module, base, scope, expression, attrs, res); 
}

/* add_record */
static int skel_add_record(struct ldb_module *module, const struct ldb_message *msg)
{
	return ldb_next_add_record(module, msg);
}

/* modify_record */
static int skel_modify_record(struct ldb_module *module, const struct ldb_message *msg)
{
	return ldb_next_modify_record(module, msg);
}

/* delete_record */
static int skel_delete_record(struct ldb_module *module, const struct ldb_dn *dn)
{
	return ldb_next_delete_record(module, dn);
}

/* rename_record */
static int skel_rename_record(struct ldb_module *module, const struct ldb_dn *olddn, const struct ldb_dn *newdn)
{
	return ldb_next_rename_record(module, olddn, newdn);
}

/* start a transaction */
static int skel_start_trans(struct ldb_module *module)
{
	return ldb_next_start_trans(module);
}

/* end a transaction */
static int skel_end_trans(struct ldb_module *module, int status)
{
	return ldb_next_end_trans(module, status);
}

/* return extended error information */
static const char *skel_errstring(struct ldb_module *module)
{
	return ldb_next_errstring(module);
}

static int skel_destructor(void *module_ctx)
{
	struct ldb_module *ctx = talloc_get_type(module_ctx, struct ldb_module);
	struct private_data *data = talloc_get_type(ctx->private_data, struct private_data);
	/* put your clean-up functions here */
	if (data->errstring) talloc_free(data->errstring);
	return 0;
}

static const struct ldb_module_ops skel_ops = {
	.name		   = "skel",
	.search		   = skel_search,
	.search_bytree	   = skel_search_bytree,
	.add_record	   = skel_add_record,
	.modify_record	   = skel_modify_record,
	.delete_record	   = skel_delete_record,
	.rename_record	   = skel_rename_record,
	.start_transaction = skel_start_trans,
	.end_transaction   = skel_end_trans,
	.errstring	   = skel_errstring
};

#ifdef HAVE_DLOPEN_DISABLED
struct ldb_module *init_module(struct ldb_context *ldb, const char *options[])
#else
struct ldb_module *skel_module_init(struct ldb_context *ldb, const char *options[])
#endif
{
	struct ldb_module *ctx;
	struct private_data *data;

	ctx = talloc(ldb, struct ldb_module);
	if (!ctx)
		return NULL;

	data = talloc(ctx, struct private_data);
	if (data == NULL) {
		talloc_free(ctx);
		return NULL;
	}

	data->errstring = NULL;
	ctx->private_data = data;

	ctx->ldb = ldb;
	ctx->prev = ctx->next = NULL;
	ctx->ops = &skel_ops;

	talloc_set_destructor (ctx, skel_destructor);

	return ctx;
}
