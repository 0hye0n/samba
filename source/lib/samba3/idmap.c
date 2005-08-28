/* 
   Unix SMB/CIFS implementation.

   idmap TDB backend

   Copyright (C) Tim Potter 2000
   Copyright (C) Jim McDonough <jmcd@us.ibm.com> 2003
   Copyright (C) Simo Sorce 2003
   Copyright (C) Jelmer Vernooij 2005
   
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
#include "lib/tdb/include/tdbutil.h"
#include "lib/samba3/samba3.h"
#include "system/filesys.h"

/* High water mark keys */
#define HWM_GROUP  "GROUP HWM"
#define HWM_USER   "USER HWM"

/* idmap version determines auto-conversion */
#define IDMAP_VERSION 2

/*****************************************************************************
 Initialise idmap database. 
*****************************************************************************/

NTSTATUS samba3_read_idmap(const char *fn, TALLOC_CTX *ctx, struct samba3_idmapdb *idmap)
{
	TDB_CONTEXT *tdb;
	TDB_DATA key, val;

	/* Open idmap repository */
	if (!(tdb = tdb_open(fn, 0, TDB_DEFAULT, O_RDONLY, 0644))) {
		DEBUG(0, ("idmap_init: Unable to open idmap database '%s'\n", fn));
		return NT_STATUS_UNSUCCESSFUL;
	}

	idmap->mapping_count = 0;
	idmap->mappings = NULL;
	idmap->user_hwm = tdb_fetch_int32(tdb, HWM_USER);
	idmap->group_hwm = tdb_fetch_int32(tdb, HWM_GROUP);

	for (key = tdb_firstkey(tdb); key.dptr; key = tdb_nextkey(tdb, key)) 
	{
		struct samba3_idmap_mapping map;
		
		if (strncmp(key.dptr, "GID ", 4) == 0) {
			map.type = IDMAP_GROUP;
			map.unix_id = atoi(key.dptr+4);
			val = tdb_fetch(tdb, key);
			map.sid = dom_sid_parse_talloc(ctx, val.dptr);
		} else if (strncmp(key.dptr, "UID ", 4) == 0) {
			map.type = IDMAP_USER;
			map.unix_id = atoi(key.dptr+4);
			val = tdb_fetch(tdb, key);
			map.sid = dom_sid_parse_talloc(ctx, val.dptr);
		} else {
			continue;
		}

		idmap->mappings = talloc_realloc(ctx, idmap->mappings, struct samba3_idmap_mapping, idmap->mapping_count+1);

		idmap->mappings[idmap->mapping_count] = map;
		idmap->mapping_count++;
	}

	tdb_close(tdb);

	return NT_STATUS_OK;
}
