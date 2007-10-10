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
 *  Component: ldb timestamps module
 *
 *  Description: add object timestamping functionality
 *
 *  Author: Simo Sorce
 */

#include "includes.h"
#include "ldb/include/ldb.h"
#include "ldb/include/ldb_private.h"
#include <time.h>

struct private_data {
	const char *error_string;
};

static int timestamps_search(struct ldb_module *module, const struct ldb_dn *base,
				  enum ldb_scope scope, const char *expression,
				  const char * const *attrs, struct ldb_message ***res)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_search\n");
	return ldb_next_search(module, base, scope, expression, attrs, res);
}

static int timestamps_search_bytree(struct ldb_module *module, const struct ldb_dn *base,
				    enum ldb_scope scope, struct ldb_parse_tree *tree,
				    const char * const *attrs, struct ldb_message ***res)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_search\n");
	return ldb_next_search_bytree(module, base, scope, tree, attrs, res);
}

static int add_time_element(struct ldb_module *module, struct ldb_message *msg, 
			    const char *attr_name, const char *time_string, unsigned int flags)
{
	struct ldb_message_element *attribute = NULL;

	int i;

	for (i = 0; i < msg->num_elements; i++) {
		if (ldb_attr_cmp(msg->elements[i].name, attr_name) == 0) {
			return 0;
		}
	}

	if (ldb_msg_add_string(module->ldb, msg, attr_name, time_string) != 0) {
		return -1;
	}

	for (i = 0; i < msg->num_elements; i++) {
		if (ldb_attr_cmp(attr_name, msg->elements[i].name) == 0) {
			attribute = &msg->elements[i];
			break;
		}
	}

	if (!attribute) {
		return -1;
	}

	attribute->flags = flags;

	return 0;
}

/* add_record: add crateTimestamp/modifyTimestamp attributes */
static int timestamps_add_record(struct ldb_module *module, const struct ldb_message *msg)
{
	struct ldb_message *msg2 = NULL;
	struct tm *tm;
	char *timestr;
	time_t timeval;
	int ret, i;

	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_add_record\n");

	/* do not manipulate our control entries */
	if (ldb_dn_is_special(msg->dn)) {
		return ldb_next_add_record(module, msg);
	}

	timeval = time(NULL);
 	tm = gmtime(&timeval);
	if (!tm) {
		return -1;
	}

	msg2 = talloc(module, struct ldb_message);
	if (!msg2) {
		return -1;
	}

	/* formatted like: 20040408072012.0Z */
	timestr = talloc_asprintf(msg2, "%04u%02u%02u%02u%02u%02u.0Z",
				  tm->tm_year+1900, tm->tm_mon+1,
				  tm->tm_mday, tm->tm_hour, tm->tm_min,
				  tm->tm_sec);
	if (!timestr) {
		return -1;
	}

	msg2->dn = msg->dn;
	msg2->num_elements = msg->num_elements;
	msg2->private_data = msg->private_data;
	msg2->elements = talloc_array(msg2, struct ldb_message_element, msg2->num_elements);
	for (i = 0; i < msg2->num_elements; i++) {
		msg2->elements[i] = msg->elements[i];
	}

	add_time_element(module, msg2, "createTimestamp", timestr, LDB_FLAG_MOD_ADD);
	add_time_element(module, msg2, "modifyTimestamp", timestr, LDB_FLAG_MOD_ADD);
	add_time_element(module, msg2, "whenCreated", timestr, LDB_FLAG_MOD_ADD);
	add_time_element(module, msg2, "whenChanged", timestr, LDB_FLAG_MOD_ADD);

	if (msg2) {
		ret = ldb_next_add_record(module, msg2);
		talloc_free(msg2);
	} else {
		ret = ldb_next_add_record(module, msg);
	}

	return ret;
}

/* modify_record: change modifyTimestamp as well */
static int timestamps_modify_record(struct ldb_module *module, const struct ldb_message *msg)
{
	struct ldb_message *msg2 = NULL;
	struct tm *tm;
	char *timestr;
	time_t timeval;
	int ret, i;

	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_modify_record\n");

	/* do not manipulate our control entries */
	if (ldb_dn_is_special(msg->dn)) {
		return ldb_next_modify_record(module, msg);
	}

	timeval = time(NULL);
 	tm = gmtime(&timeval);
	if (!tm) {
		return -1;
	}

	msg2 = talloc(module, struct ldb_message);
	if (!msg2) {
		return -1;
	}

	/* formatted like: 20040408072012.0Z */
	timestr = talloc_asprintf(msg2, 
				"%04u%02u%02u%02u%02u%02u.0Z",
				tm->tm_year+1900, tm->tm_mon+1,
				tm->tm_mday, tm->tm_hour, tm->tm_min,
				tm->tm_sec);
	if (!timestr) {
		return -1;
	}

	msg2->dn = msg->dn;
	msg2->num_elements = msg->num_elements;
	msg2->private_data = msg->private_data;
	msg2->elements = talloc_array(msg2, struct ldb_message_element, msg2->num_elements);
	for (i = 0; i < msg2->num_elements; i++) {
		msg2->elements[i] = msg->elements[i];
	}

	add_time_element(module, msg2, "modifyTimestamp", timestr, LDB_FLAG_MOD_REPLACE);
	add_time_element(module, msg2, "whenChanged", timestr, LDB_FLAG_MOD_REPLACE);

	ret = ldb_next_modify_record(module, msg2);
	talloc_free(msg2);

	return ret;
}

static int timestamps_delete_record(struct ldb_module *module, const struct ldb_dn *dn)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_delete_record\n");
	return ldb_next_delete_record(module, dn);
}

static int timestamps_rename_record(struct ldb_module *module, const struct ldb_dn *olddn, const struct ldb_dn *newdn)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_rename_record\n");
	return ldb_next_rename_record(module, olddn, newdn);
}

static int timestamps_start_trans(struct ldb_module *module)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_start_trans\n");
	return ldb_next_start_trans(module);
}

static int timestamps_end_trans(struct ldb_module *module, int status)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_end_trans\n");
	return ldb_next_end_trans(module, status);
}

/* return extended error information */
static const char *timestamps_errstring(struct ldb_module *module)
{
	struct private_data *data = (struct private_data *)module->private_data;

	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_errstring\n");
	if (data->error_string) {
		const char *error;

		error = data->error_string;
		data->error_string = NULL;
		return error;
	}

	return ldb_next_errstring(module);
}

static int timestamps_destructor(void *module_ctx)
{
	/* struct ldb_module *ctx = module_ctx; */
	/* put your clean-up functions here */
	return 0;
}

static const struct ldb_module_ops timestamps_ops = {
	.name              = "timestamps",
	.search            = timestamps_search,
	.search_bytree     = timestamps_search_bytree,
	.add_record        = timestamps_add_record,
	.modify_record     = timestamps_modify_record,
	.delete_record     = timestamps_delete_record,
	.rename_record     = timestamps_rename_record,
	.start_transaction = timestamps_start_trans,
	.end_transaction   = timestamps_end_trans,
	.errstring         = timestamps_errstring
};


/* the init function */
#ifdef HAVE_DLOPEN_DISABLED
 struct ldb_module *init_module(struct ldb_context *ldb, const char *options[])
#else
struct ldb_module *timestamps_module_init(struct ldb_context *ldb, const char *options[])
#endif
{
	struct ldb_module *ctx;
	struct private_data *data;

	ctx = talloc(ldb, struct ldb_module);
	if (!ctx)
		return NULL;

	data = talloc(ctx, struct private_data);
	if (!data) {
		talloc_free(ctx);
		return NULL;
	}

	data->error_string = NULL;
	ctx->private_data = data;
	ctx->ldb = ldb;
	ctx->prev = ctx->next = NULL;
	ctx->ops = &timestamps_ops;

	talloc_set_destructor (ctx, timestamps_destructor);

	return ctx;
}
