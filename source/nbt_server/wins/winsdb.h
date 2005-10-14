/* 
   Unix SMB/CIFS implementation.

   WINS server structures

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

#define WINSDB_OWNER_LOCAL	"0.0.0.0"
#define WINSDB_GROUP_ADDRESS	"255.255.255.255"

struct winsdb_addr {
	const char *address;
	const char *wins_owner;
	time_t expire_time;
};

/*
  each record in the database contains the following information
*/
struct winsdb_record {
	struct nbt_name *name;
	enum wrepl_name_type type;
	enum wrepl_name_state state;
	enum wrepl_name_node node;
	BOOL is_static;
	time_t expire_time;
	uint64_t version;
	const char *wins_owner;
	struct winsdb_addr **addresses;

	/* only needed for debugging problems */
	const char *registered_by;
};

struct wins_server {
	/* wins server database handle */
	struct ldb_context *wins_db;

	uint32_t min_ttl;
	uint32_t max_ttl;
};
