/* 
   Unix SMB/CIFS implementation.

   Swig interface to tdb.

   Copyright (C) 2004-2006 Tim Potter <tpot@samba.org>
   Copyright (C) 2007 Jelmer Vernooij <jelmer@samba.org>

     ** NOTE! The following LGPL license applies to the tdb
     ** library. This does NOT imply that all of Samba is released
     ** under the LGPL
   
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

%module tdb

%{

/* This symbol is used in both includes.h and Python.h which causes an
   annoying compiler warning. */

#ifdef HAVE_FSTAT
#undef HAVE_FSTAT
#endif

/* Include tdb headers */
#include <tdb.h>
#include <fcntl.h>

typedef TDB_CONTEXT tdb;
%}

/* The tdb functions will crash if a NULL tdb context is passed */

%include exception.i

%typemap(check) TDB_CONTEXT* {
	if ($1 == NULL)
		SWIG_exception(SWIG_ValueError, 
			"tdb context must be non-NULL");
}

/* In and out typemaps for the TDB_DATA structure.  This is converted to
   and from the Python string type which can contain arbitrary binary
   data.. */

%typemap(in) TDB_DATA {
	if (!PyString_Check($input)) {
		PyErr_SetString(PyExc_TypeError, "string arg expected");
		return NULL;
	}
	$1.dsize = PyString_Size($input);
	$1.dptr = (uint8_t *)PyString_AsString($input);
}

%typemap(out) TDB_DATA {
	if ($1.dptr == NULL && $1.dsize == 0) {
		$result = Py_None;
	} else {
		$result = PyString_FromStringAndSize((const char *)$1.dptr, $1.dsize);
		free($1.dptr);
	}
}

/* Treat a mode_t as an unsigned integer */
typedef int mode_t;

/* flags to tdb_store() */
%constant int REPLACE = TDB_REPLACE;
%constant int INSERT = TDB_INSERT;
%constant int MODIFY = TDB_MODIFY;

/* flags for tdb_open() */
%constant int DEFAULT = TDB_DEFAULT;
%constant int CLEAR_IF_FIRST = TDB_CLEAR_IF_FIRST;
%constant int INTERNAL = TDB_INTERNAL;
%constant int NOLOCK = TDB_NOLOCK;
%constant int NOMMAP = TDB_NOMMAP;
%constant int CONVERT = TDB_CONVERT;
%constant int BIGENDIAN = TDB_BIGENDIAN;

enum TDB_ERROR {
     TDB_SUCCESS=0, 
     TDB_ERR_CORRUPT, 
     TDB_ERR_IO, 
     TDB_ERR_LOCK, 
     TDB_ERR_OOM, 
     TDB_ERR_EXISTS, 
     TDB_ERR_NOLOCK, 
     TDB_ERR_LOCK_TIMEOUT,
     TDB_ERR_NOEXIST, 
     TDB_ERR_EINVAL, 
     TDB_ERR_RDONLY
};

%rename(Tdb) tdb;
%rename(lock_all) tdb_context::lockall;
%rename(unlock_all) tdb_context::unlockall;

%rename(read_lock_all) tdb_context::lockall_read;
%rename(read_unlock_all) tdb_context::unlockall_read;

%typemap(default) int tdb_flags {
    $1 = TDB_DEFAULT;
}

%typemap(default) int open_flags {
    $1 = O_RDWR;
}

%typemap(default) int hash_size {
    $1 = 0;
}

%typemap(default) mode_t mode {
    $1 = 0600;
}

%typemap(default) int flag {
    $1 = TDB_REPLACE;
}

typedef struct tdb_context {
    %extend {
        tdb(const char *name, int hash_size,
                    int tdb_flags,
                    int open_flags, mode_t mode)
        {
            tdb *ret = tdb_open(name, hash_size, tdb_flags, open_flags, mode);

            /* Throw an IOError exception from errno if tdb_open() returns 
               NULL */
            if (ret == NULL) {
                PyErr_SetFromErrno(PyExc_IOError);
                SWIG_fail;
            }

fail:
            return ret;
        }
        enum TDB_ERROR error();
        ~tdb() { tdb_close($self); }
        int close();
        int append(TDB_DATA key, TDB_DATA new_dbuf);
        const char *errorstr();
        TDB_DATA fetch(TDB_DATA key);
        int delete(TDB_DATA key);
        int store(TDB_DATA key, TDB_DATA dbuf, int flag);
        int exists(TDB_DATA key);
        TDB_DATA firstkey();
        TDB_DATA nextkey(TDB_DATA key);
        int lockall();
        int unlockall();
        int lockall_read();
        int unlockall_read();
        int reopen();
        int transaction_start();
        int transaction_commit();
        int transaction_cancel();
        int transaction_recover();
        int hash_size();
        size_t map_size();
        int get_flags();
        void set_max_dead(int max_dead);
        const char *name();
    }

    %pythoncode {
    def __str__(self):
        return self.name()

    # Random access to keys, values
    def __getitem__(self, key):
        result = self.fetch(key)
        if result is None:
            raise KeyError, '%s: %s' % (key, self.errorstr())
        return result

    def __setitem__(self, key, item):
        if self.store(key, item) == -1:
            raise IOError, self.errorstr()

    def __delitem__(self, key):
        if not self.exists(key):
            raise KeyError, '%s: %s' % (key, self.errorstr())
        self.delete(key)

    def __contains__(self, key):
        return self.exists(key) != 0

    def has_key(self, key):
        return self.exists(key) != 0

    # Tdb iterator
    class TdbIterator:
        def __init__(self, tdb):
            self.tdb = tdb
            self.key = None

        def __iter__(self):
            return self
            
        def next(self):
            if self.key is None:
                self.key = self.tdb.firstkey()
                if self.key is None:
                    raise StopIteration
                return self.key
            else:
                self.key = self.tdb.nextkey(self.key)
                if self.key is None:
                    raise StopIteration
                return self.key

    def __iter__(self):
        return self.TdbIterator(self)

    # Implement other dict functions using TdbIterator

    def keys(self):
        return [k for k in iter(self)]

    def values(self):
        return [self[k] for k in iter(self)]

    def items(self):
        return [(k, self[k]) for k in iter(self)]

    def __len__(self):
        return len(self.keys())

    def clear(self):
        for k in iter(self):
            del(self[k])

    # TODO: iterkeys, itervalues, iteritems

    # TODO: any other missing methods for container types
    }
} tdb;
