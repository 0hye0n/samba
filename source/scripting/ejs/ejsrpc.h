/* 
   Unix SMB/CIFS implementation.

   ejs <-> rpc interface definitions

   Copyright (C) Andrew Tridgell 2005
   
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

struct ejs_rpc {
	int eid;
	const char *callname;
	/* as ejs does only one pass, we can use a single var for switch 
	   handling */
	uint32_t switch_var;
};

typedef NTSTATUS (*ejs_pull_t)(struct ejs_rpc *, struct MprVar *, const char *, void *);
typedef NTSTATUS (*ejs_push_t)(struct ejs_rpc *, struct MprVar *, const char *, const void *);
typedef NTSTATUS (*ejs_pull_function_t)(struct ejs_rpc *, struct MprVar *, void *);
typedef NTSTATUS (*ejs_push_function_t)(struct ejs_rpc *, struct MprVar *, const void *);

NTSTATUS ejs_panic(struct ejs_rpc *ejs, const char *why);
void ejs_set_switch(struct ejs_rpc *ejs, uint32_t switch_var);

NTSTATUS smbcalls_register_ejs(const char *name, MprCFunction fn);


int ejs_rpc_call(int eid, int argc, struct MprVar **argv,
		 const struct dcerpc_interface_table *iface, int callnum,
		 ejs_pull_function_t ejs_pull, ejs_push_function_t ejs_push);

NTSTATUS ejs_pull_struct_start(struct ejs_rpc *ejs, struct MprVar **v, const char *name);
NTSTATUS ejs_push_struct_start(struct ejs_rpc *ejs, struct MprVar **v, const char *name);

NTSTATUS ejs_pull_uint8(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, uint8_t *r);
NTSTATUS ejs_push_uint8(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, const uint8_t *r);
NTSTATUS ejs_pull_uint16(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, uint16_t *r);
NTSTATUS ejs_push_uint16(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, const uint16_t *r);
NTSTATUS ejs_pull_uint32(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, uint32_t *r);
NTSTATUS ejs_push_int32(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, const int32_t *r);
NTSTATUS ejs_pull_int32(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, int32_t *r);
NTSTATUS ejs_push_uint32(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, const uint32_t *r);
NTSTATUS ejs_pull_hyper(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, uint64_t *r);
NTSTATUS ejs_push_hyper(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, const uint64_t *r);
NTSTATUS ejs_pull_dlong(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, uint64_t *r);
NTSTATUS ejs_push_dlong(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, const uint64_t *r);
NTSTATUS ejs_pull_udlong(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, uint64_t *r);
NTSTATUS ejs_push_udlong(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, const uint64_t *r);
NTSTATUS ejs_pull_NTTIME(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, uint64_t *r);
NTSTATUS ejs_push_NTTIME(struct ejs_rpc *ejs, 
			struct MprVar *v, const char *name, const uint64_t *r);
NTSTATUS ejs_pull_enum(struct ejs_rpc *ejs, 
		       struct MprVar *v, const char *name, unsigned *r);
NTSTATUS ejs_push_enum(struct ejs_rpc *ejs, 
		       struct MprVar *v, const char *name, const unsigned *r);
NTSTATUS ejs_pull_string(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, const char **s);
NTSTATUS ejs_push_string(struct ejs_rpc *ejs, 
			 struct MprVar *v, const char *name, const char *s);
void ejs_set_constant_int(int eid, const char *name, int value);
void ejs_set_constant_string(int eid, const char *name, const char *value);

NTSTATUS ejs_pull_dom_sid(struct ejs_rpc *ejs, 
			  struct MprVar *v, const char *name, struct dom_sid *r);
NTSTATUS ejs_push_dom_sid(struct ejs_rpc *ejs, 
			  struct MprVar *v, const char *name, const struct dom_sid *r);
NTSTATUS ejs_push_null(struct ejs_rpc *ejs, struct MprVar *v, const char *name);
BOOL ejs_pull_null(struct ejs_rpc *ejs, struct MprVar *v, const char *name);
NTSTATUS ejs_pull_DATA_BLOB(struct ejs_rpc *ejs, 
			    struct MprVar *v, const char *name, DATA_BLOB *r);
NTSTATUS ejs_push_DATA_BLOB(struct ejs_rpc *ejs, 
			    struct MprVar *v, const char *name, const DATA_BLOB *r);
NTSTATUS ejs_pull_BOOL(struct ejs_rpc *ejs, 
		       struct MprVar *v, const char *name, BOOL *r);
NTSTATUS ejs_push_BOOL(struct ejs_rpc *ejs, 
		       struct MprVar *v, const char *name, const BOOL *r);

#define EJS_ALLOC_SIZE(ejs, s, size) do { \
  (s) = talloc_size(ejs, size); \
  if (!(s)) return ejs_panic(ejs, "out of memory"); \
} while (0)

#define EJS_ALLOC(ejs, s) EJS_ALLOC_SIZE(ejs, s, sizeof(*(s)))

#define EJS_ALLOC_N_SIZE(ejs, s, n, elsize) do { \
	(s) = talloc_array_size(ejs, elsize, n); \
	if (!(s)) return ejs_panic(ejs, "out of memory"); \
} while (0)

#define EJS_ALLOC_N(ejs, s, n) EJS_ALLOC_N_SIZE(ejs, s, n, sizeof(*(s)))

/* some types are equivalent for ejs */
#define ejs_pull_dom_sid2 ejs_pull_dom_sid
#define ejs_push_dom_sid2 ejs_push_dom_sid
#define ejs_pull_NTTIME_hyper ejs_pull_NTTIME
#define ejs_push_NTTIME_hyper ejs_push_NTTIME

