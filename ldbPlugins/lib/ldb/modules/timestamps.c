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
#include <time.h>

static int timestamps_close(struct ldb_module *module)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_close\n");
	return ldb_next_close(module);
}

static int timestamps_search(struct ldb_module *module, const char *base,
				  enum ldb_scope scope, const char *expression,
				  const char * const *attrs, struct ldb_message ***res)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_search\n");
	return ldb_next_search(module, base, scope, expression, attrs, res);
}

static int timestamps_search_free(struct ldb_module *module, struct ldb_message **res)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_search_free\n");
	return ldb_next_search_free(module, res);
}

static int add_time_element(struct ldb_context *ldb, struct ldb_message *msg, char *attr_name, char *time_string, unsigned int flags)
{
	struct ldb_val *values;
	char *name, *time;
	int i;

	for (i = 0; i < msg->num_elements; i++) {
		if (strcasecmp(msg->elements[i].name, attr_name) == 0) {
			return 0;
		}
	}

	msg->elements = ldb_realloc_array(ldb, msg->elements, sizeof(struct ldb_message_element), msg->num_elements + 1);
	name = ldb_strdup(ldb, attr_name);
	time = ldb_strdup(ldb, time_string);
	values = ldb_malloc(ldb, sizeof(struct ldb_val));
	if (!msg->elements || !name || !time || !values) {
		return -1;
	}

	msg->elements[msg->num_elements].name = name;
	msg->elements[msg->num_elements].flags = flags;
	msg->elements[msg->num_elements].num_values = 1;
	msg->elements[msg->num_elements].values = values;
	msg->elements[msg->num_elements].values[0].data = time;
	msg->elements[msg->num_elements].values[0].length = strlen(time);

	msg->num_elements += 1;

	return 0;
}

static void free_elements(struct ldb_context *ldb, struct ldb_message *msg, int real_el_num)
{
	int i;

	for (i = real_el_num; i < msg->num_elements; i++) {
		ldb_free(ldb, msg->elements[i].name);
		ldb_free(ldb, msg->elements[i].values[0].data);
		ldb_free(ldb, msg->elements[i].values);
	}
	ldb_free(ldb, msg->elements);
	ldb_free(ldb, msg);
}

/* add_record: add crateTimestamp/modifyTimestamp attributes */
static int timestamps_add_record(struct ldb_module *module, const struct ldb_message *msg)
{
	struct ldb_message *msg2;
	struct tm *tm;
	char *timestr;
	time_t timeval;
	int ret, i;

	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_add_record\n");

	timeval = time(NULL);
 	tm = gmtime(&timeval);
	if (!tm) {
		return -1;
	}

	/* formatted like: 20040408072012.0Z */
	ldb_asprintf(module->ldb, &timestr,
			"%04u%02u%02u%02u%02u%02u.0Z",
			tm->tm_year+1900, tm->tm_mon+1,
			tm->tm_mday, tm->tm_hour, tm->tm_min,
			tm->tm_sec);

	if (!timestr) {
		return -1;
	}

	msg2 = ldb_malloc_p(module->ldb, struct ldb_message);
	if (!msg2) {
		return -1;
	}
	msg2->dn = msg->dn;
	msg2->num_elements = msg->num_elements;
	msg2->private_data = msg->private_data;
	msg2->elements = ldb_malloc_array_p(module->ldb, struct ldb_message_element, msg2->num_elements);
	for (i = 0; i < msg2->num_elements; i++) {
		msg2->elements[i] = msg->elements[i];
	}

	add_time_element(module->ldb, msg2, "createTimestamp", timestr, LDB_FLAG_MOD_ADD);
	add_time_element(module->ldb, msg2, "modifyTimestamp", timestr, LDB_FLAG_MOD_ADD);
	add_time_element(module->ldb, msg2, "whenCreated", timestr, LDB_FLAG_MOD_ADD);
	add_time_element(module->ldb, msg2, "whenChanged", timestr, LDB_FLAG_MOD_ADD);
	
	ldb_free(module->ldb, timestr);

	ret = ldb_next_add_record(module, msg2);

	free_elements(module->ldb, msg2, msg->num_elements);

	return ret;
}

/* modify_record: change modifyTimestamp as well */
static int timestamps_modify_record(struct ldb_module *module, const struct ldb_message *msg)
{
	struct ldb_message *msg2;
	struct tm *tm;
	char *timestr;
	time_t timeval;
	int ret, i;

	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_modify_record\n");

	timeval = time(NULL);
 	tm = gmtime(&timeval);
	if (!tm) {
		return -1;
	}

	/* formatted like: 20040408072012.0Z */
	ldb_asprintf(module->ldb, &timestr,
			"%04u%02u%02u%02u%02u%02u.0Z",
			tm->tm_year+1900, tm->tm_mon+1,
			tm->tm_mday, tm->tm_hour, tm->tm_min,
			tm->tm_sec);

	if (!timestr) {
		return -1;
	}

	msg2 = ldb_malloc_p(module->ldb, struct ldb_message);
	if (!msg2) {
		return -1;
	}
	msg2->dn = msg->dn;
	msg2->num_elements = msg->num_elements;
	msg2->private_data = msg->private_data;
	msg2->elements = ldb_malloc_array_p(module->ldb, struct ldb_message_element, msg2->num_elements);
	for (i = 0; i < msg2->num_elements; i++) {
		msg2->elements[i] = msg->elements[i];
	}

	add_time_element(module->ldb, msg2, "modifyTimestamp", timestr, LDB_FLAG_MOD_REPLACE);
	add_time_element(module->ldb, msg2, "whenChanged", timestr, LDB_FLAG_MOD_REPLACE);
	
	ldb_free(module->ldb, timestr);

	ret = ldb_next_modify_record(module, msg2);

	free_elements(module->ldb, msg2, msg->num_elements);

	return ret;
}

static int timestamps_delete_record(struct ldb_module *module, const char *dn)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_delete_record\n");
	return ldb_next_delete_record(module, dn);
}

static int timestamps_rename_record(struct ldb_module *module, const char *olddn, const char *newdn)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_rename_record\n");
	return ldb_next_rename_record(module, olddn, newdn);
}

/* return extended error information */
static const char *timestamps_errstring(struct ldb_module *module)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_errstring\n");
	return ldb_next_errstring(module);
}

static void timestamps_cache_free(struct ldb_module *module)
{
	ldb_debug(module->ldb, LDB_DEBUG_TRACE, "timestamps_cache_free\n");
	ldb_next_cache_free(module);
}

static const struct ldb_module_ops timestamps_ops = {
	timestamps_close, 
	timestamps_search,
	timestamps_search_free,
	timestamps_add_record,
	timestamps_modify_record,
	timestamps_delete_record,
	timestamps_rename_record,
	timestamps_errstring,
	timestamps_cache_free
};


/* the init function */
#ifdef HAVE_DLOPEN_DISABLED
struct ldb_module *init_module(struct ldb_context *ldb, const char *options[])
#else
struct ldb_module *timestamps_module_init(struct ldb_context *ldb, const char *options[])
#endif
{
	struct ldb_module *ctx;

	ctx = (struct ldb_module *)malloc(sizeof(struct ldb_module));
	if (!ctx)
		return NULL;

	ctx->name = "timestamps";
	ctx->private_data = NULL;
	ctx->next = NULL;
	ctx->ldb = ldb;
	ctx->ops = &timestamps_ops;

	return ctx;
}
