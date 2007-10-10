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
 *  Component: ldb header
 *
 *  Description: defines for base ldb API
 *
 *  Author: Andrew Tridgell
 */

/*
  major restrictions as compared to normal LDAP:

     - no async calls.
     - each record must have a unique key field
     - the key must be representable as a NULL terminated C string and may not 
       contain a comma or braces

  major restrictions as compared to tdb:

     - no explicit locking calls

*/


/*
  an individual lump of data in a result comes in this format. The
  pointer will usually be to a UTF-8 string if the application is
  sensible, but it can be to anything you like, including binary data
  blobs of arbitrary size.
*/
struct ldb_val {
	unsigned int length;
	void *data;
};

/* these flags are used in ldd_message_element.flags fields. The
   LDA_FLAGS_MOD_* flags are used in ldap_modify() calls to specify
   whether attributes are being added, deleted or modified */
#define LDB_FLAG_MOD_MASK  0x3
#define LDB_FLAG_MOD_ADD     1
#define LDB_FLAG_MOD_REPLACE 2
#define LDB_FLAG_MOD_DELETE  3


/*
  results are given back as arrays of ldb_message_element
*/
struct ldb_message_element {
	unsigned int flags;
	char *name;
	unsigned int num_values;
	struct ldb_val *values;
};


/*
  a ldb_message represents all or part of a record. It can contain an arbitrary
  number of elements. 
*/
struct ldb_message {
	char *dn;
	unsigned int num_elements;
	struct ldb_message_element *elements;
	void *private; /* private to the backend */
};

enum ldb_changetype {
	LDB_CHANGETYPE_NONE=0,
	LDB_CHANGETYPE_ADD,
	LDB_CHANGETYPE_DELETE,
	LDB_CHANGETYPE_MODIFY
};

/*
  a ldif record - from ldif_read
*/
struct ldb_ldif {
	enum ldb_changetype changetype;
	struct ldb_message msg;
};

enum ldb_scope {LDB_SCOPE_DEFAULT=-1, 
		LDB_SCOPE_BASE=0, 
		LDB_SCOPE_ONELEVEL=1,
		LDB_SCOPE_SUBTREE=2};

struct ldb_context;

/*
  the fuction type for the callback used in traversing the database
*/
typedef int (*ldb_traverse_fn)(struct ldb_context *, const struct ldb_message *);


/* 
   these function pointers define the operations that a ldb backend must perform
   they correspond exactly to the ldb_*() interface 
*/
struct ldb_backend_ops {
	int (*close)(struct ldb_context *);
	int (*search)(struct ldb_context *, const char *, enum ldb_scope,
		      const char *, const char *[], struct ldb_message ***);
	int (*search_free)(struct ldb_context *, struct ldb_message **);
	int (*add)(struct ldb_context *, const struct ldb_message *);
	int (*modify)(struct ldb_context *, const struct ldb_message *);
	int (*delete)(struct ldb_context *, const char *);
	const char * (*errstring)(struct ldb_context *);
};

/*
  every ldb connection is started by establishing a ldb_context
*/
struct ldb_context {
	/* a private pointer for the backend to use */
	void *private;

	/* the operations provided by the backend */
	const struct ldb_backend_ops *ops;
};


#define LDB_FLG_RDONLY 1

/* 
 connect to a database. The URL can either be one of the following forms
   ldb://path
   ldapi://path

   flags is made up of LDB_FLG_*

   the options are passed uninterpreted to the backend, and are
   backend specific
*/
struct ldb_context *ldb_connect(const char *url, unsigned int flags,
				const char *options[]);

/*
  close the connection to the database
*/
int ldb_close(struct ldb_context *ldb);


/*
  search the database given a LDAP-like search expression

  return the number of records found, or -1 on error
*/
int ldb_search(struct ldb_context *ldb, 
	       const char *base,
	       enum ldb_scope scope,
	       const char *expression,
	       char * const attrs[], struct ldb_message ***res);

/* 
   free a set of messages returned by ldb_search
*/
int ldb_search_free(struct ldb_context *ldb, struct ldb_message **msgs);


/*
  add a record to the database. Will fail if a record with the given class and key
  already exists
*/
int ldb_add(struct ldb_context *ldb, 
	    const struct ldb_message *message);

/*
  modify the specified attributes of a record
*/
int ldb_modify(struct ldb_context *ldb, 
	       const struct ldb_message *message);

/*
  delete a record from the database
*/
int ldb_delete(struct ldb_context *ldb, const char *dn);


/*
  return extended error information from the last call
*/
const char *ldb_errstring(struct ldb_context *ldb);

/*
  ldif manipulation functions
*/
int ldif_write(int (*fprintf_fn)(void *, const char *, ...), 
	       void *private,
	       const struct ldb_ldif *ldif);
void ldif_read_free(struct ldb_ldif *);
struct ldb_ldif *ldif_read(int (*fgetc_fn)(void *), void *private);
struct ldb_ldif *ldif_read_file(FILE *f);
struct ldb_ldif *ldif_read_string(const char *s);
int ldif_write_file(FILE *f, const struct ldb_ldif *msg);
