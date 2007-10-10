/* 
   Unix SMB/CIFS implementation.
   Registry interface
   Copyright (C) Gerald Carter                        2002.
   Copyright (C) Jelmer Vernooij					  2003-2004.
   
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

#ifndef _REGISTRY_H /* _REGISTRY_H */
#define _REGISTRY_H 

/* Handles for the predefined keys */
#define HKEY_CLASSES_ROOT		 0x80000000
#define HKEY_CURRENT_USER		 0x80000001
#define HKEY_LOCAL_MACHINE		 0x80000002
#define HKEY_USERS				 0x80000003
#define HKEY_PERFORMANCE_DATA	 0x80000004
#define HKEY_CURRENT_CONFIG		 0x80000005
#define HKEY_DYN_DATA			 0x80000006
#define HKEY_PERFORMANCE_TEXT	 0x80000050
#define HKEY_PERFORMANCE_NLSTEXT 0x80000060

#define	REG_DELETE								   -1

#if 0
/* FIXME */
typedef struct ace_struct_s {
  uint8_t type, flags;
  uint_t perms;   /* Perhaps a better def is in order */
  DOM_SID *trustee;
} ACE;
#endif

/*
 * The general idea here is that every backend provides a 'hive'. Combining
 * various hives gives you a complete registry like windows has
 */

#define REGISTRY_INTERFACE_VERSION 1

/* structure to store the registry handles */
struct registry_key {
  char *name;         /* Name of the key                    */
  const char *path;		  /* Full path to the key */
  char *class_name; /* Name of key class */
  NTTIME last_mod; /* Time last modified                 */
  struct registry_hive *hive;
  void *backend_data;
};

struct registry_value {
  char *name;
  unsigned int data_type;
  DATA_BLOB data;
};

/* FIXME */
typedef void (*key_notification_function) (void);
typedef void (*value_notification_function) (void);

/* 
 * Container for function pointers to enumeration routines
 * for virtual registry view 
 *
 * Backends can provide :
 *  - just one hive (example: nt4, w95)
 *  - several hives (example: rpc).
 * 
 * Backends should always do case-insensitive compares 
 * (everything is case-insensitive but case-preserving, 
 * just like the FS)
 */ 

struct hive_operations {
	const char *name;

	/* Implement this one */
	WERROR (*open_hive) (struct registry_hive *, struct registry_key **);

	/* Or this one */
	WERROR (*open_key) (TALLOC_CTX *, struct registry_key *, const char *name, struct registry_key **);

	WERROR (*num_subkeys) (struct registry_key *, uint32_t *count);
	WERROR (*num_values) (struct registry_key *, uint32_t *count);
	WERROR (*get_subkey_by_index) (TALLOC_CTX *, struct registry_key *, int idx, struct registry_key **);

	/* Can not contain more then one level */
	WERROR (*get_subkey_by_name) (TALLOC_CTX *, struct registry_key *, const char *name, struct registry_key **);
	WERROR (*get_value_by_index) (TALLOC_CTX *, struct registry_key *, int idx, struct registry_value **);

	/* Can not contain more then one level */
	WERROR (*get_value_by_name) (TALLOC_CTX *, struct registry_key *, const char *name, struct registry_value **);

	/* Security control */
	WERROR (*key_get_sec_desc) (TALLOC_CTX *, struct registry_key *, struct security_descriptor **);
	WERROR (*key_set_sec_desc) (struct registry_key *, struct security_descriptor *);

	/* Notification */
	WERROR (*request_key_change_notify) (struct registry_key *, key_notification_function);
	WERROR (*request_value_change_notify) (struct registry_value *, value_notification_function);

	/* Key management */
	WERROR (*add_key)(TALLOC_CTX *, struct registry_key *, const char *name, uint32_t access_mask, struct security_descriptor *, struct registry_key **);
	WERROR (*del_key)(struct registry_key *, const char *name);
	WERROR (*flush_key) (struct registry_key *);

	/* Value management */
	WERROR (*set_value)(struct registry_key *, const char *name, uint32_t type, DATA_BLOB data); 
	WERROR (*del_value)(struct registry_key *, const char *valname);
};

struct registry_hive {
	const struct hive_operations *functions;
	struct registry_key *root;
	void *backend_data;
	const char *location;
};

/* Handle to a full registry
 * contains zero or more hives */
struct registry_context {
    void *backend_data;
	WERROR (*get_predefined_key) (struct registry_context *, uint32_t hkey, struct registry_key **);
};

struct reg_init_function_entry {
	const struct hive_operations *hive_functions;
	struct reg_init_function_entry *prev, *next;
};

#endif /* _REGISTRY_H */
