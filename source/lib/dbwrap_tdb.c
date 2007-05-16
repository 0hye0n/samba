/* 
   Unix SMB/CIFS implementation.
   Database interface wrapper around tdb
   Copyright (C) Volker Lendecke 2005-2007
   
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

struct db_tdb_ctx {
	TDB_CONTEXT *tdb;
};

static NTSTATUS db_tdb_store(struct db_record *rec, TDB_DATA data, int flag);
static NTSTATUS db_tdb_delete(struct db_record *rec);

static int db_tdb_record_destr(struct db_record* data)
{
	struct db_tdb_ctx *ctx =
		talloc_get_type_abort(data->private_data, struct db_tdb_ctx);

	DEBUG(10, ("Unlocking key %s\n",
		   hex_encode(data, (unsigned char *)data->key.dptr,
			      data->key.dsize)));

	if (tdb_chainunlock(ctx->tdb, data->key) != 0) {
		DEBUG(0, ("tdb_chainunlock failed\n"));
		return -1;
	}
	return 0;
}

static struct db_record *db_tdb_fetch_locked(struct db_context *db,
				     TALLOC_CTX *mem_ctx, TDB_DATA key)
{
	struct db_tdb_ctx *ctx = talloc_get_type_abort(db->private_data,
						       struct db_tdb_ctx);
	struct db_record *result;
	TDB_DATA value;

	result = TALLOC_P(mem_ctx, struct db_record);
	if (result == NULL) {
		DEBUG(0, ("talloc failed\n"));
		return NULL;
	}

	result->key.dsize = key.dsize;
	result->key.dptr = talloc_memdup(result, key.dptr, key.dsize);
	if (result->key.dptr == NULL) {
		DEBUG(0, ("talloc failed\n"));
		TALLOC_FREE(result);
		return NULL;
	}

	result->value.dptr = NULL;
	result->value.dsize = 0;
	result->private_data = talloc_reference(result, ctx);
	result->store = db_tdb_store;
	result->delete_rec = db_tdb_delete;

	{
		char *keystr = hex_encode(NULL, key.dptr, key.dsize);
		DEBUG(10, ("Locking key %s\n", keystr));
		TALLOC_FREE(keystr);
	}

	if (tdb_chainlock(ctx->tdb, key) != 0) {
		DEBUG(3, ("tdb_chainlock failed\n"));
		TALLOC_FREE(result);
		return NULL;
	}

	talloc_set_destructor(result, db_tdb_record_destr);

	value = tdb_fetch(ctx->tdb, key);

	if (value.dptr == NULL) {
		return result;
	}

	result->value.dsize = value.dsize;
	result->value.dptr = talloc_memdup(result, value.dptr, value.dsize);
	if (result->value.dptr == NULL) {
		DEBUG(3, ("talloc failed\n"));
		TALLOC_FREE(result);
		return NULL;
	}

	SAFE_FREE(value.dptr);

	DEBUG(10, ("Allocated locked data 0x%p\n", result));

	return result;
}

static NTSTATUS db_tdb_store(struct db_record *rec, TDB_DATA data, int flag)
{
	struct db_tdb_ctx *ctx = talloc_get_type_abort(rec->private_data,
						       struct db_tdb_ctx);

	/*
	 * This has a bug: We need to replace rec->value for correct
	 * operation, but right now brlock and locking don't use the value
	 * anymore after it was stored.
	 */

	return (tdb_store(ctx->tdb, rec->key, data, flag) == 0) ?
		NT_STATUS_OK : NT_STATUS_UNSUCCESSFUL;
}

static NTSTATUS db_tdb_delete(struct db_record *rec)
{
	struct db_tdb_ctx *ctx = talloc_get_type_abort(rec->private_data,
						       struct db_tdb_ctx);

	return (tdb_delete(ctx->tdb, rec->key) == 0) ?
		NT_STATUS_OK : NT_STATUS_UNSUCCESSFUL;
}

struct db_tdb_traverse_ctx {
	struct db_context *db;
	int (*f)(struct db_record *rec, void *private_data);
	void *private_data;
};

static int db_tdb_traverse_func(TDB_CONTEXT *tdb, TDB_DATA kbuf, TDB_DATA dbuf,
				void *private_data)
{
	struct db_tdb_traverse_ctx *ctx =
		(struct db_tdb_traverse_ctx *)private_data;
	struct db_record rec;

	rec.key = kbuf;
	rec.value = dbuf;
	rec.store = db_tdb_store;
	rec.delete_rec = db_tdb_delete;
	rec.private_data = ctx->db->private_data;

	return ctx->f(&rec, ctx->private_data);
}

static int db_tdb_traverse(struct db_context *db,
			   int (*f)(struct db_record *rec, void *private_data),
			   void *private_data)
{
	struct db_tdb_ctx *db_ctx =
		talloc_get_type_abort(db->private_data, struct db_tdb_ctx);
	struct db_tdb_traverse_ctx ctx;

	ctx.db = db;
	ctx.f = f;
	ctx.private_data = private_data;
	return tdb_traverse(db_ctx->tdb, db_tdb_traverse_func, &ctx);
}

static int db_tdb_get_seqnum(struct db_context *db)

{
	struct db_tdb_ctx *db_ctx =
		talloc_get_type_abort(db->private_data, struct db_tdb_ctx);
	return tdb_get_seqnum(db_ctx->tdb);
}

static int db_tdb_ctx_destr(struct db_tdb_ctx *ctx)
{
	if (tdb_close(ctx->tdb) != 0) {
		DEBUG(0, ("Failed to close tdb: %s\n", strerror(errno)));
		return -1;
	}

	return 0;
}

struct db_context *db_open_tdb(TALLOC_CTX *mem_ctx,
			       const char *name,
			       int hash_size, int tdb_flags,
			       int open_flags, mode_t mode)
{
	struct db_context *result = NULL;
	struct db_tdb_ctx *db_tdb;

	result = TALLOC_ZERO_P(mem_ctx, struct db_context);
	if (result == NULL) {
		DEBUG(0, ("talloc failed\n"));
		goto fail;
	}

	result->private_data = db_tdb = TALLOC_P(result, struct db_tdb_ctx);
	if (db_tdb == NULL) {
		DEBUG(0, ("talloc failed\n"));
		goto fail;
	}

	db_tdb->tdb = tdb_open_log(name, hash_size, tdb_flags,
				   open_flags, mode);
	if (db_tdb->tdb == NULL) {
		DEBUG(3, ("Could not open tdb: %s\n", strerror(errno)));
		goto fail;
	}

	talloc_set_destructor(db_tdb, db_tdb_ctx_destr);
	result->fetch_locked = db_tdb_fetch_locked;
	result->traverse = db_tdb_traverse;
	result->get_seqnum = db_tdb_get_seqnum;
	return result;

 fail:
	if (result != NULL) {
		TALLOC_FREE(result);
	}
	return NULL;
}
