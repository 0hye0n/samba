/* 
   Unix SMB/CIFS implementation.

   WINS database routines

   Copyright (C) Andrew Tridgell	2005
   
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
#include "nbt_server/nbt_server.h"
#include "nbt_server/wins/winsdb.h"
#include "lib/ldb/include/ldb.h"
#include "lib/ldb/include/ldb_errors.h"
#include "db_wrap.h"
#include "system/time.h"

/*
  return the new maxVersion and save it
*/
static uint64_t winsdb_allocate_version(struct wins_server *winssrv)
{
	int trans;
	int ret;
	struct ldb_context *ldb = winssrv->wins_db;
	struct ldb_dn *dn;
	struct ldb_message **res = NULL;
	struct ldb_message *msg = NULL;
	TALLOC_CTX *tmp_ctx = talloc_new(winssrv);
	uint64_t maxVersion = 0;

	trans = ldb_transaction_start(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	dn = ldb_dn_explode(tmp_ctx, "CN=VERSION");
	if (!dn) goto failed;

	/* find the record in the WINS database */
	ret = ldb_search(ldb, dn, LDB_SCOPE_BASE, 
			 NULL, NULL, &res);
	if (res != NULL) {
		talloc_steal(tmp_ctx, res);
	}
	if (ret < 0) goto failed;
	if (ret > 1) goto failed;

	if (ret == 1) {
		maxVersion = ldb_msg_find_uint64(res[0], "maxVersion", 0);
	}
	maxVersion++;

	msg = ldb_msg_new(tmp_ctx);
	if (!msg) goto failed;
	msg->dn = dn;


	ret = ldb_msg_add_empty(ldb, msg, "objectClass", LDB_FLAG_MOD_REPLACE);
	if (ret != 0) goto failed;
	ret = ldb_msg_add_string(ldb, msg, "objectClass", "winsMaxVersion");
	if (ret != 0) goto failed;
	ret = ldb_msg_add_empty(ldb, msg, "maxVersion", LDB_FLAG_MOD_REPLACE);
	if (ret != 0) goto failed;
	ret = ldb_msg_add_fmt(ldb, msg, "maxVersion", "%llu", maxVersion);
	if (ret != 0) goto failed;

	ret = ldb_modify(ldb, msg);
	if (ret != 0) ret = ldb_add(ldb, msg);
	if (ret != 0) goto failed;

	trans = ldb_transaction_commit(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	talloc_free(tmp_ctx);
	return maxVersion;

failed:
	if (trans == LDB_SUCCESS) ldb_transaction_cancel(ldb);
	talloc_free(tmp_ctx);
	return 0;
}

/*
  return a DN for a nbt_name
*/
static struct ldb_dn *winsdb_dn(TALLOC_CTX *mem_ctx, struct nbt_name *name)
{
	struct ldb_dn *dn;

	dn = ldb_dn_string_compose(mem_ctx, NULL, "type=0x%02X", name->type);
	if (dn && name->name && *name->name) {
		dn = ldb_dn_string_compose(mem_ctx, dn, "name=%s", name->name);
	}
	if (dn && name->scope && *name->scope) {
		dn = ldb_dn_string_compose(mem_ctx, dn, "scope=%s", name->scope);
	}
	return dn;
}

static NTSTATUS winsdb_nbt_name(TALLOC_CTX *mem_ctx, struct ldb_dn *dn, struct nbt_name **_name)
{
	NTSTATUS status;
	struct nbt_name *name;
	uint32_t cur = 0;

	name = talloc(mem_ctx, struct nbt_name);
	if (!name) {
		status = NT_STATUS_NO_MEMORY;
		goto failed;
	}

	if (dn->comp_num > 3) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	if (dn->comp_num > cur && strcasecmp("scope", dn->components[cur].name) == 0) {
		name->scope	= talloc_steal(name, dn->components[cur].value.data);
		cur++;
	} else {
		name->scope	= NULL;
	}

	if (dn->comp_num > cur && strcasecmp("name", dn->components[cur].name) == 0) {
		name->name	= talloc_steal(name, dn->components[cur].value.data);
		cur++;
	} else {
		name->name	= talloc_strdup(name, "");
		if (!name->name) {
			status = NT_STATUS_NO_MEMORY;
			goto failed;
		}
	}

	if (dn->comp_num > cur && strcasecmp("type", dn->components[cur].name) == 0) {
		name->type	= strtoul((char *)dn->components[cur].value.data, NULL, 0);
		cur++;
	} else {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	*_name = name;
	return NT_STATUS_OK;
failed:
	talloc_free(name);
	return status;
}

/*
 decode the winsdb_addr("address") attribute:
 "172.31.1.1" or 
 "172.31.1.1;winsOwner:172.31.9.202;expireTime:20050923032330.0Z;"
 are valid records
*/
static NTSTATUS winsdb_addr_decode(struct winsdb_record *rec, struct ldb_val *val,
				   TALLOC_CTX *mem_ctx, struct winsdb_addr **_addr)
{
	NTSTATUS status;
	struct winsdb_addr *addr;
	char *address;
	char *wins_owner;
	char *expire_time;
	char *p;

	addr = talloc(mem_ctx, struct winsdb_addr);
	if (!addr) {
		status = NT_STATUS_NO_MEMORY;
		goto failed;
	}

	address = (char *)val->data;

	p = strchr(address, ';');
	if (!p) {
		/* support old entries, with only the address */
		addr->address		= talloc_steal(addr, val->data);
		addr->wins_owner	= talloc_reference(addr, rec->wins_owner);
		if (!addr->wins_owner) {
			status = NT_STATUS_NO_MEMORY;
			goto failed;
		}
		addr->expire_time	= rec->expire_time;
		*_addr = addr;
		return NT_STATUS_OK;
	}

	*p = '\0';p++;
	addr->address = talloc_strdup(addr, address);
	if (!addr->address) {
		status = NT_STATUS_NO_MEMORY;
		goto failed;
	}

	if (strncmp("winsOwner:", p, 10) != 0) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}
	wins_owner = p + 10;
	p = strchr(wins_owner, ';');
	if (!p) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	*p = '\0';p++;
	addr->wins_owner = talloc_strdup(addr, wins_owner);
	if (!addr->wins_owner) {
		status = NT_STATUS_NO_MEMORY;
		goto failed;
	}

	if (strncmp("expireTime:", p, 11) != 0) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	expire_time = p + 11;
	p = strchr(expire_time, ';');
	if (!p) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	*p = '\0';p++;
	addr->expire_time = ldap_string_to_time(expire_time);

	*_addr = addr;
	return NT_STATUS_OK;
failed:
	talloc_free(addr);
	return status;
}

/*
 encode the winsdb_addr("address") attribute like this:
 "172.31.1.1;winsOwner:172.31.9.202;expireTime:20050923032330.0Z;"
*/
static int ldb_msg_add_winsdb_addr(struct ldb_context *ldb, struct ldb_message *msg, 
				   const char *attr_name, struct winsdb_addr *addr)
{
	struct ldb_val val;
	const char *str;

	str = talloc_asprintf(msg, "%s;winsOwner:%s;expireTime:%s;",
			      addr->address, addr->wins_owner,
			      ldap_timestring(msg, addr->expire_time));
	if (!str) return -1;

	val.data = discard_const_p(uint8_t, str);
	val.length = strlen(str);

	return ldb_msg_add_value(ldb, msg, attr_name, &val);
}

struct winsdb_addr **winsdb_addr_list_make(TALLOC_CTX *mem_ctx)
{
	struct winsdb_addr **addresses;

	addresses = talloc_array(mem_ctx, struct winsdb_addr *, 1);
	if (!addresses) return NULL;

	addresses[0] = NULL;

	return addresses;
}

struct winsdb_addr **winsdb_addr_list_add(struct winsdb_addr **addresses, const char *address,
					  const char *wins_owner, time_t expire_time)
{
	size_t len = winsdb_addr_list_length(addresses);

	addresses = talloc_realloc(addresses, addresses, struct winsdb_addr *, len + 2);
	if (!addresses) return NULL;

	addresses[len] = talloc(addresses, struct winsdb_addr);
	if (!addresses[len]) {
		talloc_free(addresses);
		return NULL;
	}

	addresses[len]->address = talloc_strdup(addresses[len], address);
	if (!addresses[len]->address) {
		talloc_free(addresses);
		return NULL;
	}

	addresses[len]->wins_owner = talloc_strdup(addresses[len], wins_owner);
	if (!addresses[len]->wins_owner) {
		talloc_free(addresses);
		return NULL;
	}

	addresses[len]->expire_time = expire_time;

	addresses[len+1] = NULL;

	return addresses;
}

void winsdb_addr_list_remove(struct winsdb_addr **addresses, const char *address)
{
	size_t i;

	for (i=0; addresses[i]; i++) {
		if (strcmp(addresses[i]->address, address) == 0) {
			break;
		}
	}
	if (!addresses[i]) return;

	for (; addresses[i]; i++) {
		addresses[i] = addresses[i+1];
	}

	return;
}

struct winsdb_addr *winsdb_addr_list_check(struct winsdb_addr **addresses, const char *address)
{
	size_t i;

	for (i=0; addresses[i]; i++) {
		if (strcmp(addresses[i]->address, address) == 0) {
			return addresses[i];
		}
	}

	return NULL;
}

size_t winsdb_addr_list_length(struct winsdb_addr **addresses)
{
	size_t i;
	for (i=0; addresses[i]; i++);
	return i;
}

const char **winsdb_addr_string_list(TALLOC_CTX *mem_ctx, struct winsdb_addr **addresses)
{
	size_t len = winsdb_addr_list_length(addresses);
	const char **str_list;
	size_t i;

	str_list = talloc_array(mem_ctx, const char *, len + 1);
	if (!str_list) return NULL;

	for (i=0; i < len; i++) {
		str_list[i] = talloc_strdup(str_list, addresses[i]->address);
		if (!str_list[i]) {
			talloc_free(str_list);
			return NULL;
		}
	}

	str_list[len] = NULL;
	return str_list;
}

/*
  load a WINS entry from the database
*/
NTSTATUS winsdb_lookup(struct ldb_context *wins_db, 
		       struct nbt_name *name,
		       TALLOC_CTX *mem_ctx,
		       struct winsdb_record **_rec)
{
	NTSTATUS status;
	struct ldb_message **res = NULL;
	int ret;
	struct winsdb_record *rec;
	TALLOC_CTX *tmp_ctx = talloc_new(mem_ctx);

	/* find the record in the WINS database */
	ret = ldb_search(wins_db, winsdb_dn(tmp_ctx, name), LDB_SCOPE_BASE, 
			 NULL, NULL, &res);
	if (res != NULL) {
		talloc_steal(tmp_ctx, res);
	}
	if (ret == 0) {
		status = NT_STATUS_OBJECT_NAME_NOT_FOUND;
		goto failed;
	} else if (ret != 1) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	status = winsdb_record(res[0], name, tmp_ctx, &rec);
	if (!NT_STATUS_IS_OK(status)) goto failed;

	/* see if it has already expired */
	if (rec->state == WREPL_STATE_ACTIVE &&
	    rec->expire_time <= time(NULL)) {
		DEBUG(5,("WINS: expiring name %s (expired at %s)\n", 
			 nbt_name_string(tmp_ctx, rec->name), timestring(tmp_ctx, rec->expire_time)));
		rec->state = WREPL_STATE_RELEASED;
	}

	talloc_steal(mem_ctx, rec);
	talloc_free(tmp_ctx);
	*_rec = rec;
	return NT_STATUS_OK;

failed:
	talloc_free(tmp_ctx);
	return status;
}

NTSTATUS winsdb_record(struct ldb_message *msg, struct nbt_name *name, TALLOC_CTX *mem_ctx, struct winsdb_record **_rec)
{
	NTSTATUS status;
	struct winsdb_record *rec;
	struct ldb_message_element *el;
	uint32_t i;

	rec = talloc(mem_ctx, struct winsdb_record);
	if (rec == NULL) {
		status = NT_STATUS_NO_MEMORY;
		goto failed;
	}

	if (!name) {
		status = winsdb_nbt_name(rec, msg->dn, &name);
		if (!NT_STATUS_IS_OK(status)) goto failed;
	}

	if (strlen(name->name) > 15) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}
	if (name->scope && strlen(name->scope) > 238) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	/* parse it into a more convenient winsdb_record structure */
	rec->name		= name;
	rec->type		= ldb_msg_find_int(msg, "recordType", WREPL_TYPE_UNIQUE);
	rec->state		= ldb_msg_find_int(msg, "recordState", WREPL_STATE_RELEASED);
	rec->node		= ldb_msg_find_int(msg, "nodeType", WREPL_NODE_B);
	rec->is_static		= ldb_msg_find_int(msg, "isStatic", 0);
	rec->expire_time	= ldap_string_to_time(ldb_msg_find_string(msg, "expireTime", NULL));
	rec->version		= ldb_msg_find_uint64(msg, "versionID", 0);
	rec->wins_owner		= ldb_msg_find_string(msg, "winsOwner", NULL);
	rec->registered_by	= ldb_msg_find_string(msg, "registeredBy", NULL);
	talloc_steal(rec, rec->wins_owner);
	talloc_steal(rec, rec->registered_by);

	if (!rec->wins_owner) {
		rec->wins_owner = talloc_strdup(rec, WINSDB_OWNER_LOCAL);
		if (rec->wins_owner == NULL) {
			status = NT_STATUS_NO_MEMORY;
			goto failed;
		}
	}

	el = ldb_msg_find_element(msg, "address");
	if (el == NULL) {
		status = NT_STATUS_INTERNAL_DB_CORRUPTION;
		goto failed;
	}

	if (rec->type == WREPL_TYPE_UNIQUE || rec->type == WREPL_TYPE_GROUP) {
		if (el->num_values != 1) {
			status = NT_STATUS_INTERNAL_DB_CORRUPTION;
			goto failed;
		}
	}

	rec->addresses     = talloc_array(rec, struct winsdb_addr *, el->num_values+1);
	if (rec->addresses == NULL) {
		status = NT_STATUS_NO_MEMORY;
		goto failed;
	}

	for (i=0;i<el->num_values;i++) {
		status = winsdb_addr_decode(rec, &el->values[i], rec->addresses, &rec->addresses[i]);
		if (!NT_STATUS_IS_OK(status)) goto failed;
	}
	rec->addresses[i] = NULL;

	*_rec = rec;
	return NT_STATUS_OK;
failed:
	if (NT_STATUS_EQUAL(NT_STATUS_INTERNAL_DB_CORRUPTION, status)) {
		DEBUG(1,("winsdb_record: corrupted record: %s\n", ldb_dn_linearize(rec, msg->dn)));
	}
	talloc_free(rec);
	return status;
}

/*
  form a ldb_message from a winsdb_record
*/
struct ldb_message *winsdb_message(struct ldb_context *ldb, 
				   struct winsdb_record *rec, TALLOC_CTX *mem_ctx)
{
	int i, ret=0;
	struct ldb_message *msg = ldb_msg_new(mem_ctx);
	if (msg == NULL) goto failed;

	msg->dn = winsdb_dn(msg, rec->name);
	if (msg->dn == NULL) goto failed;
	ret |= ldb_msg_add_fmt(ldb, msg, "objectClass", "winsRecord");
	ret |= ldb_msg_add_fmt(ldb, msg, "recordType", "%u", rec->type);
	ret |= ldb_msg_add_fmt(ldb, msg, "recordState", "%u", rec->state);
	ret |= ldb_msg_add_fmt(ldb, msg, "nodeType", "%u", rec->node);
	ret |= ldb_msg_add_fmt(ldb, msg, "isStatic", "%u", rec->is_static);
	ret |= ldb_msg_add_string(ldb, msg, "expireTime", 
				  ldap_timestring(msg, rec->expire_time));
	ret |= ldb_msg_add_fmt(ldb, msg, "versionID", "%llu", rec->version);
	ret |= ldb_msg_add_string(ldb, msg, "winsOwner", rec->wins_owner);
	for (i=0;rec->addresses[i];i++) {
		ret |= ldb_msg_add_winsdb_addr(ldb, msg, "address", rec->addresses[i]);
	}
	ret |= ldb_msg_add_string(ldb, msg, "registeredBy", rec->registered_by);
	if (ret != 0) goto failed;
	return msg;

failed:
	talloc_free(msg);
	return NULL;
}

/*
  save a WINS record into the database
*/
uint8_t winsdb_add(struct wins_server *winssrv, struct winsdb_record *rec)
{
	struct ldb_context *ldb = winssrv->wins_db;
	struct ldb_message *msg;
	TALLOC_CTX *tmp_ctx = talloc_new(winssrv);
	int trans = -1;
	int ret = 0;


	trans = ldb_transaction_start(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	rec->version = winsdb_allocate_version(winssrv);
	if (rec->version == 0) goto failed;
	rec->wins_owner = WINSDB_OWNER_LOCAL;

	msg = winsdb_message(winssrv->wins_db, rec, tmp_ctx);
	if (msg == NULL) goto failed;
	ret = ldb_add(ldb, msg);
	if (ret != 0) goto failed;

	trans = ldb_transaction_commit(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	talloc_free(tmp_ctx);
	return NBT_RCODE_OK;

failed:
	if (trans == LDB_SUCCESS) ldb_transaction_cancel(ldb);
	talloc_free(tmp_ctx);
	return NBT_RCODE_SVR;
}


/*
  modify a WINS record in the database
*/
uint8_t winsdb_modify(struct wins_server *winssrv, struct winsdb_record *rec)
{
	struct ldb_context *ldb = winssrv->wins_db;
	struct ldb_message *msg;
	TALLOC_CTX *tmp_ctx = talloc_new(winssrv);
	int trans;
	int ret;
	int i;

	trans = ldb_transaction_start(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	rec->version = winsdb_allocate_version(winssrv);
	if (rec->version == 0) goto failed;
	rec->wins_owner = WINSDB_OWNER_LOCAL;

	msg = winsdb_message(winssrv->wins_db, rec, tmp_ctx);
	if (msg == NULL) goto failed;

	for (i=0;i<msg->num_elements;i++) {
		msg->elements[i].flags = LDB_FLAG_MOD_REPLACE;
	}

	ret = ldb_modify(ldb, msg);
	if (ret != 0) goto failed;

	trans = ldb_transaction_commit(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	talloc_free(tmp_ctx);
	return NBT_RCODE_OK;

failed:
	if (trans == LDB_SUCCESS) ldb_transaction_cancel(ldb);
	talloc_free(tmp_ctx);
	return NBT_RCODE_SVR;
}


/*
  delete a WINS record from the database
*/
uint8_t winsdb_delete(struct wins_server *winssrv, struct winsdb_record *rec)
{
	struct ldb_context *ldb = winssrv->wins_db;
	TALLOC_CTX *tmp_ctx = talloc_new(winssrv);
	const struct ldb_dn *dn;
	int trans;
	int ret;

	trans = ldb_transaction_start(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	dn = winsdb_dn(tmp_ctx, rec->name);
	if (dn == NULL) goto failed;

	ret = ldb_delete(ldb, dn);
	if (ret != 0) goto failed;

	trans = ldb_transaction_commit(ldb);
	if (trans != LDB_SUCCESS) goto failed;

	talloc_free(tmp_ctx);
	return NBT_RCODE_OK;

failed:
	if (trans == LDB_SUCCESS) ldb_transaction_cancel(ldb);
	talloc_free(tmp_ctx);
	return NBT_RCODE_SVR;
}

struct ldb_context *winsdb_connect(TALLOC_CTX *mem_ctx)
{
	return ldb_wrap_connect(mem_ctx, lp_wins_url(), 0, NULL);
}
