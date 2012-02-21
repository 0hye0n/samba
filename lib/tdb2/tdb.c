 /*
   Trivial Database 2: fetch, store and misc routines.
   Copyright (C) Rusty Russell 2010

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 3 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#include "private.h"
#ifndef HAVE_LIBREPLACE
#include <ccan/asprintf/asprintf.h>
#include <stdarg.h>
#endif

static enum TDB_ERROR update_rec_hdr(struct tdb_context *tdb,
				     tdb_off_t off,
				     tdb_len_t keylen,
				     tdb_len_t datalen,
				     struct tdb_used_record *rec,
				     uint64_t h)
{
	uint64_t dataroom = rec_data_length(rec) + rec_extra_padding(rec);
	enum TDB_ERROR ecode;

	ecode = set_header(tdb, rec, TDB_USED_MAGIC, keylen, datalen,
			   keylen + dataroom, h);
	if (ecode == TDB_SUCCESS) {
		ecode = tdb_write_convert(tdb, off, rec, sizeof(*rec));
	}
	return ecode;
}

static enum TDB_ERROR replace_data(struct tdb_context *tdb,
				   struct hash_info *h,
				   struct tdb_data key, struct tdb_data dbuf,
				   tdb_off_t old_off, tdb_len_t old_room,
				   bool growing)
{
	tdb_off_t new_off;
	enum TDB_ERROR ecode;

	/* Allocate a new record. */
	new_off = alloc(tdb, key.dsize, dbuf.dsize, h->h, TDB_USED_MAGIC,
			growing);
	if (TDB_OFF_IS_ERR(new_off)) {
		return TDB_OFF_TO_ERR(new_off);
	}

	/* We didn't like the existing one: remove it. */
	if (old_off) {
		tdb->stats.frees++;
		ecode = add_free_record(tdb, old_off,
					sizeof(struct tdb_used_record)
					+ key.dsize + old_room,
					TDB_LOCK_WAIT, true);
		if (ecode == TDB_SUCCESS)
			ecode = replace_in_hash(tdb, h, new_off);
	} else {
		ecode = add_to_hash(tdb, h, new_off);
	}
	if (ecode != TDB_SUCCESS) {
		return ecode;
	}

	new_off += sizeof(struct tdb_used_record);
	ecode = tdb->tdb2.io->twrite(tdb, new_off, key.dptr, key.dsize);
	if (ecode != TDB_SUCCESS) {
		return ecode;
	}

	new_off += key.dsize;
	ecode = tdb->tdb2.io->twrite(tdb, new_off, dbuf.dptr, dbuf.dsize);
	if (ecode != TDB_SUCCESS) {
		return ecode;
	}

	if (tdb->flags & TDB_SEQNUM)
		tdb_inc_seqnum(tdb);

	return TDB_SUCCESS;
}

static enum TDB_ERROR update_data(struct tdb_context *tdb,
				  tdb_off_t off,
				  struct tdb_data dbuf,
				  tdb_len_t extra)
{
	enum TDB_ERROR ecode;

	ecode = tdb->tdb2.io->twrite(tdb, off, dbuf.dptr, dbuf.dsize);
	if (ecode == TDB_SUCCESS && extra) {
		/* Put a zero in; future versions may append other data. */
		ecode = tdb->tdb2.io->twrite(tdb, off + dbuf.dsize, "", 1);
	}
	if (tdb->flags & TDB_SEQNUM)
		tdb_inc_seqnum(tdb);

	return ecode;
}

_PUBLIC_ enum TDB_ERROR tdb_store(struct tdb_context *tdb,
			 struct tdb_data key, struct tdb_data dbuf, int flag)
{
	struct hash_info h;
	tdb_off_t off;
	tdb_len_t old_room = 0;
	struct tdb_used_record rec;
	enum TDB_ERROR ecode;

	if (tdb->flags & TDB_VERSION1) {
		if (tdb1_store(tdb, key, dbuf, flag) == -1)
			return tdb->last_error;
		return TDB_SUCCESS;
	}

	off = find_and_lock(tdb, key, F_WRLCK, &h, &rec, NULL);
	if (TDB_OFF_IS_ERR(off)) {
		return tdb->last_error = TDB_OFF_TO_ERR(off);
	}

	/* Now we have lock on this hash bucket. */
	if (flag == TDB_INSERT) {
		if (off) {
			ecode = TDB_ERR_EXISTS;
			goto out;
		}
	} else {
		if (off) {
			old_room = rec_data_length(&rec)
				+ rec_extra_padding(&rec);
			if (old_room >= dbuf.dsize) {
				/* Can modify in-place.  Easy! */
				ecode = update_rec_hdr(tdb, off,
						       key.dsize, dbuf.dsize,
						       &rec, h.h);
				if (ecode != TDB_SUCCESS) {
					goto out;
				}
				ecode = update_data(tdb,
						    off + sizeof(rec)
						    + key.dsize, dbuf,
						    old_room - dbuf.dsize);
				if (ecode != TDB_SUCCESS) {
					goto out;
				}
				tdb_unlock_hashes(tdb, h.hlock_start,
						  h.hlock_range, F_WRLCK);
				return tdb->last_error = TDB_SUCCESS;
			}
		} else {
			if (flag == TDB_MODIFY) {
				/* if the record doesn't exist and we
				   are in TDB_MODIFY mode then we should fail
				   the store */
				ecode = TDB_ERR_NOEXIST;
				goto out;
			}
		}
	}

	/* If we didn't use the old record, this implies we're growing. */
	ecode = replace_data(tdb, &h, key, dbuf, off, old_room, off);
out:
	tdb_unlock_hashes(tdb, h.hlock_start, h.hlock_range, F_WRLCK);
	return tdb->last_error = ecode;
}

_PUBLIC_ enum TDB_ERROR tdb_append(struct tdb_context *tdb,
			  struct tdb_data key, struct tdb_data dbuf)
{
	struct hash_info h;
	tdb_off_t off;
	struct tdb_used_record rec;
	tdb_len_t old_room = 0, old_dlen;
	unsigned char *newdata;
	struct tdb_data new_dbuf;
	enum TDB_ERROR ecode;

	if (tdb->flags & TDB_VERSION1) {
		if (tdb1_append(tdb, key, dbuf) == -1)
			return tdb->last_error;
		return TDB_SUCCESS;
	}

	off = find_and_lock(tdb, key, F_WRLCK, &h, &rec, NULL);
	if (TDB_OFF_IS_ERR(off)) {
		return tdb->last_error = TDB_OFF_TO_ERR(off);
	}

	if (off) {
		old_dlen = rec_data_length(&rec);
		old_room = old_dlen + rec_extra_padding(&rec);

		/* Fast path: can append in place. */
		if (rec_extra_padding(&rec) >= dbuf.dsize) {
			ecode = update_rec_hdr(tdb, off, key.dsize,
					       old_dlen + dbuf.dsize, &rec,
					       h.h);
			if (ecode != TDB_SUCCESS) {
				goto out;
			}

			off += sizeof(rec) + key.dsize + old_dlen;
			ecode = update_data(tdb, off, dbuf,
					    rec_extra_padding(&rec));
			goto out;
		}

		/* Slow path. */
		newdata = malloc(key.dsize + old_dlen + dbuf.dsize);
		if (!newdata) {
			ecode = tdb_logerr(tdb, TDB_ERR_OOM, TDB_LOG_ERROR,
					   "tdb_append:"
					   " failed to allocate %zu bytes",
					   (size_t)(key.dsize + old_dlen
						    + dbuf.dsize));
			goto out;
		}
		ecode = tdb->tdb2.io->tread(tdb, off + sizeof(rec) + key.dsize,
					    newdata, old_dlen);
		if (ecode != TDB_SUCCESS) {
			goto out_free_newdata;
		}
		memcpy(newdata + old_dlen, dbuf.dptr, dbuf.dsize);
		new_dbuf.dptr = newdata;
		new_dbuf.dsize = old_dlen + dbuf.dsize;
	} else {
		newdata = NULL;
		new_dbuf = dbuf;
	}

	/* If they're using tdb_append(), it implies they're growing record. */
	ecode = replace_data(tdb, &h, key, new_dbuf, off, old_room, true);

out_free_newdata:
	free(newdata);
out:
	tdb_unlock_hashes(tdb, h.hlock_start, h.hlock_range, F_WRLCK);
	return tdb->last_error = ecode;
}

_PUBLIC_ enum TDB_ERROR tdb_fetch(struct tdb_context *tdb, struct tdb_data key,
			 struct tdb_data *data)
{
	tdb_off_t off;
	struct tdb_used_record rec;
	struct hash_info h;
	enum TDB_ERROR ecode;

	if (tdb->flags & TDB_VERSION1)
		return tdb1_fetch(tdb, key, data);

	off = find_and_lock(tdb, key, F_RDLCK, &h, &rec, NULL);
	if (TDB_OFF_IS_ERR(off)) {
		return tdb->last_error = TDB_OFF_TO_ERR(off);
	}

	if (!off) {
		ecode = TDB_ERR_NOEXIST;
	} else {
		data->dsize = rec_data_length(&rec);
		data->dptr = tdb_alloc_read(tdb, off + sizeof(rec) + key.dsize,
					    data->dsize);
		if (TDB_PTR_IS_ERR(data->dptr)) {
			ecode = TDB_PTR_ERR(data->dptr);
		} else
			ecode = TDB_SUCCESS;
	}

	tdb_unlock_hashes(tdb, h.hlock_start, h.hlock_range, F_RDLCK);
	return tdb->last_error = ecode;
}

_PUBLIC_ bool tdb_exists(struct tdb_context *tdb, TDB_DATA key)
{
	tdb_off_t off;
	struct tdb_used_record rec;
	struct hash_info h;

	if (tdb->flags & TDB_VERSION1) {
		return tdb1_exists(tdb, key);
	}

	off = find_and_lock(tdb, key, F_RDLCK, &h, &rec, NULL);
	if (TDB_OFF_IS_ERR(off)) {
		tdb->last_error = TDB_OFF_TO_ERR(off);
		return false;
	}
	tdb_unlock_hashes(tdb, h.hlock_start, h.hlock_range, F_RDLCK);

	tdb->last_error = TDB_SUCCESS;
	return off ? true : false;
}

_PUBLIC_ enum TDB_ERROR tdb_delete(struct tdb_context *tdb, struct tdb_data key)
{
	tdb_off_t off;
	struct tdb_used_record rec;
	struct hash_info h;
	enum TDB_ERROR ecode;

	if (tdb->flags & TDB_VERSION1) {
		if (tdb1_delete(tdb, key) == -1)
			return tdb->last_error;
		return TDB_SUCCESS;
	}

	off = find_and_lock(tdb, key, F_WRLCK, &h, &rec, NULL);
	if (TDB_OFF_IS_ERR(off)) {
		return tdb->last_error = TDB_OFF_TO_ERR(off);
	}

	if (!off) {
		ecode = TDB_ERR_NOEXIST;
		goto unlock;
	}

	ecode = delete_from_hash(tdb, &h);
	if (ecode != TDB_SUCCESS) {
		goto unlock;
	}

	/* Free the deleted entry. */
	tdb->stats.frees++;
	ecode = add_free_record(tdb, off,
				sizeof(struct tdb_used_record)
				+ rec_key_length(&rec)
				+ rec_data_length(&rec)
				+ rec_extra_padding(&rec),
				TDB_LOCK_WAIT, true);

	if (tdb->flags & TDB_SEQNUM)
		tdb_inc_seqnum(tdb);

unlock:
	tdb_unlock_hashes(tdb, h.hlock_start, h.hlock_range, F_WRLCK);
	return tdb->last_error = ecode;
}

_PUBLIC_ unsigned int tdb_get_flags(struct tdb_context *tdb)
{
	return tdb->flags;
}

static bool inside_transaction(const struct tdb_context *tdb)
{
	if (tdb->flags & TDB_VERSION1)
		return tdb->tdb1.transaction != NULL;
	else
		return tdb->tdb2.transaction != NULL;
}

static bool readonly_changable(struct tdb_context *tdb, const char *caller)
{
	if (inside_transaction(tdb)) {
		tdb->last_error = tdb_logerr(tdb, TDB_ERR_EINVAL,
					     TDB_LOG_USE_ERROR,
					     "%s: can't change"
					     " TDB_RDONLY inside transaction",
					     caller);
		return false;
	}
	return true;
}

_PUBLIC_ void tdb_add_flag(struct tdb_context *tdb, unsigned flag)
{
	if (tdb->flags & TDB_INTERNAL) {
		tdb->last_error = tdb_logerr(tdb, TDB_ERR_EINVAL,
					     TDB_LOG_USE_ERROR,
					     "tdb_add_flag: internal db");
		return;
	}
	switch (flag) {
	case TDB_NOLOCK:
		tdb->flags |= TDB_NOLOCK;
		break;
	case TDB_NOMMAP:
		tdb->flags |= TDB_NOMMAP;
		tdb_munmap(tdb->file);
		break;
	case TDB_NOSYNC:
		tdb->flags |= TDB_NOSYNC;
		break;
	case TDB_SEQNUM:
		tdb->flags |= TDB_SEQNUM;
		break;
	case TDB_ALLOW_NESTING:
		tdb->flags |= TDB_ALLOW_NESTING;
		break;
	case TDB_RDONLY:
		if (readonly_changable(tdb, "tdb_add_flag"))
			tdb->flags |= TDB_RDONLY;
		break;
	default:
		tdb->last_error = tdb_logerr(tdb, TDB_ERR_EINVAL,
					     TDB_LOG_USE_ERROR,
					     "tdb_add_flag: Unknown flag %u",
					     flag);
	}
}

_PUBLIC_ void tdb_remove_flag(struct tdb_context *tdb, unsigned flag)
{
	if (tdb->flags & TDB_INTERNAL) {
		tdb->last_error = tdb_logerr(tdb, TDB_ERR_EINVAL,
					     TDB_LOG_USE_ERROR,
					     "tdb_remove_flag: internal db");
		return;
	}
	switch (flag) {
	case TDB_NOLOCK:
		tdb->flags &= ~TDB_NOLOCK;
		break;
	case TDB_NOMMAP:
		tdb->flags &= ~TDB_NOMMAP;
		tdb_mmap(tdb);
		break;
	case TDB_NOSYNC:
		tdb->flags &= ~TDB_NOSYNC;
		break;
	case TDB_SEQNUM:
		tdb->flags &= ~TDB_SEQNUM;
		break;
	case TDB_ALLOW_NESTING:
		tdb->flags &= ~TDB_ALLOW_NESTING;
		break;
	case TDB_RDONLY:
		if ((tdb->open_flags & O_ACCMODE) == O_RDONLY) {
			tdb->last_error = tdb_logerr(tdb, TDB_ERR_EINVAL,
						     TDB_LOG_USE_ERROR,
						     "tdb_remove_flag: can't"
						     " remove TDB_RDONLY on tdb"
						     " opened with O_RDONLY");
			break;
		}
		if (readonly_changable(tdb, "tdb_remove_flag"))
			tdb->flags &= ~TDB_RDONLY;
		break;
	default:
		tdb->last_error = tdb_logerr(tdb, TDB_ERR_EINVAL,
					     TDB_LOG_USE_ERROR,
					     "tdb_remove_flag: Unknown flag %u",
					     flag);
	}
}

_PUBLIC_ const char *tdb_errorstr(enum TDB_ERROR ecode)
{
	/* Gcc warns if you miss a case in the switch, so use that. */
	switch (TDB_ERR_TO_OFF(ecode)) {
	case TDB_ERR_TO_OFF(TDB_SUCCESS): return "Success";
	case TDB_ERR_TO_OFF(TDB_ERR_CORRUPT): return "Corrupt database";
	case TDB_ERR_TO_OFF(TDB_ERR_IO): return "IO Error";
	case TDB_ERR_TO_OFF(TDB_ERR_LOCK): return "Locking error";
	case TDB_ERR_TO_OFF(TDB_ERR_OOM): return "Out of memory";
	case TDB_ERR_TO_OFF(TDB_ERR_EXISTS): return "Record exists";
	case TDB_ERR_TO_OFF(TDB_ERR_EINVAL): return "Invalid parameter";
	case TDB_ERR_TO_OFF(TDB_ERR_NOEXIST): return "Record does not exist";
	case TDB_ERR_TO_OFF(TDB_ERR_RDONLY): return "write not permitted";
	}
	return "Invalid error code";
}

_PUBLIC_ enum TDB_ERROR tdb_error(struct tdb_context *tdb)
{
	return tdb->last_error;
}

enum TDB_ERROR COLD tdb_logerr(struct tdb_context *tdb,
			       enum TDB_ERROR ecode,
			       enum tdb_log_level level,
			       const char *fmt, ...)
{
	char *message;
	va_list ap;
	size_t len;
	/* tdb_open paths care about errno, so save it. */
	int saved_errno = errno;

	if (!tdb->log_fn)
		return ecode;

	va_start(ap, fmt);
	len = vasprintf(&message, fmt, ap);
	va_end(ap);

	if (len < 0) {
		tdb->log_fn(tdb, TDB_LOG_ERROR, TDB_ERR_OOM,
			    "out of memory formatting message:", tdb->log_data);
		tdb->log_fn(tdb, level, ecode, fmt, tdb->log_data);
	} else {
		tdb->log_fn(tdb, level, ecode, message, tdb->log_data);
		free(message);
	}
	errno = saved_errno;
	return ecode;
}

_PUBLIC_ enum TDB_ERROR tdb_parse_record_(struct tdb_context *tdb,
				 TDB_DATA key,
				 enum TDB_ERROR (*parse)(TDB_DATA k,
							 TDB_DATA d,
							 void *data),
				 void *data)
{
	tdb_off_t off;
	struct tdb_used_record rec;
	struct hash_info h;
	enum TDB_ERROR ecode;

	if (tdb->flags & TDB_VERSION1) {
		return tdb->last_error = tdb1_parse_record(tdb, key, parse,
							   data);
	}

	off = find_and_lock(tdb, key, F_RDLCK, &h, &rec, NULL);
	if (TDB_OFF_IS_ERR(off)) {
		return tdb->last_error = TDB_OFF_TO_ERR(off);
	}

	if (!off) {
		ecode = TDB_ERR_NOEXIST;
	} else {
		const void *dptr;
		dptr = tdb_access_read(tdb, off + sizeof(rec) + key.dsize,
				       rec_data_length(&rec), false);
		if (TDB_PTR_IS_ERR(dptr)) {
			ecode = TDB_PTR_ERR(dptr);
		} else {
			TDB_DATA d = tdb_mkdata(dptr, rec_data_length(&rec));

			ecode = parse(key, d, data);
			tdb_access_release(tdb, dptr);
		}
	}

	tdb_unlock_hashes(tdb, h.hlock_start, h.hlock_range, F_RDLCK);
	return tdb->last_error = ecode;
}

_PUBLIC_ const char *tdb_name(const struct tdb_context *tdb)
{
	return tdb->name;
}

_PUBLIC_ int64_t tdb_get_seqnum(struct tdb_context *tdb)
{
	tdb_off_t off;

	if (tdb->flags & TDB_VERSION1) {
		tdb1_off_t val;
		tdb->last_error = TDB_SUCCESS;
		val = tdb1_get_seqnum(tdb);

		if (tdb->last_error != TDB_SUCCESS)
			return TDB_ERR_TO_OFF(tdb->last_error);
		else
			return val;
	}

	off = tdb_read_off(tdb, offsetof(struct tdb_header, seqnum));
	if (TDB_OFF_IS_ERR(off))
		tdb->last_error = TDB_OFF_TO_ERR(off);
	else
		tdb->last_error = TDB_SUCCESS;
	return off;
}


_PUBLIC_ int tdb_fd(const struct tdb_context *tdb)
{
	return tdb->file->fd;
}

struct traverse_state {
	enum TDB_ERROR error;
	struct tdb_context *dest_db;
};

/*
  traverse function for repacking
 */
static int repack_traverse(struct tdb_context *tdb, TDB_DATA key, TDB_DATA data,
			   struct traverse_state *state)
{
	state->error = tdb_store(state->dest_db, key, data, TDB_INSERT);
	if (state->error != TDB_SUCCESS) {
		return -1;
	}
	return 0;
}

_PUBLIC_ enum TDB_ERROR tdb_repack(struct tdb_context *tdb)
{
	struct tdb_context *tmp_db;
	struct traverse_state state;

	state.error = tdb_transaction_start(tdb);
	if (state.error != TDB_SUCCESS) {
		return state.error;
	}

	tmp_db = tdb_open("tmpdb", TDB_INTERNAL, O_RDWR|O_CREAT, 0, NULL);
	if (tmp_db == NULL) {
		state.error = tdb_logerr(tdb, TDB_ERR_OOM, TDB_LOG_ERROR,
					 __location__
					 " Failed to create tmp_db");
		tdb_transaction_cancel(tdb);
		return tdb->last_error = state.error;
	}

	state.dest_db = tmp_db;
	if (tdb_traverse(tdb, repack_traverse, &state) < 0) {
		goto fail;
	}

	state.error = tdb_wipe_all(tdb);
	if (state.error != TDB_SUCCESS) {
		goto fail;
	}

	state.dest_db = tdb;
	if (tdb_traverse(tmp_db, repack_traverse, &state) < 0) {
		goto fail;
	}

	tdb_close(tmp_db);
	return tdb_transaction_commit(tdb);

fail:
	tdb_transaction_cancel(tdb);
	tdb_close(tmp_db);
	return state.error;
}
