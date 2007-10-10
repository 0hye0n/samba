/* 
   Unix SMB/CIFS implementation.

   provide hooks into smbd C calls from ejs scripts

   Copyright (C) Tim Potter 2005
   
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
#include "scripting/ejs/smbcalls.h"
#include "lib/ejs/ejs.h"
#include "librpc/gen_ndr/ndr_nbt.h"

/*
  look up a netbios name

  syntax:
    resolveName(result, "frogurt");
    resolveName(result, "frogurt", 0x1c);
*/

static int ejs_resolve_name(MprVarHandle eid, int argc, struct MprVar **argv)
{
	int result = -1;
	struct nbt_name name;
	TALLOC_CTX *tmp_ctx = talloc_new(mprMemCtx());	
	NTSTATUS nt_status;
	const char *reply_addr;

	/* validate arguments */
	if (argc < 2 || argc > 3) {
		ejsSetErrorMsg(eid, "resolveName invalid arguments");
		goto done;
	}

	if (argv[0]->type != MPR_TYPE_OBJECT) {
		ejsSetErrorMsg(eid, "resolvename invalid arguments");
		goto done;
	}

	if (argv[1]->type != MPR_TYPE_STRING) {
		ejsSetErrorMsg(eid, "resolveName invalid arguments");
		goto done;
	}
	
	if (argc == 2) {
		make_nbt_name_client(&name, mprToString(argv[1]));
	} else {
		if (argv[1]->type != MPR_TYPE_INT) {
			ejsSetErrorMsg(eid, "resolveName invalid arguments");
			goto done;
		}
		make_nbt_name(&name, mprToString(argv[1]), mprToInt(argv[2]));
	}

	result = 0;

	nt_status = resolve_name(&name, tmp_ctx, &reply_addr);

	if (NT_STATUS_IS_OK(nt_status)) {
		mprSetPropertyValue(argv[0], "value", 
				    mprCreateStringVar(reply_addr, 1));
	}

	mpr_Return(eid, mprNTSTATUS(nt_status));

 done:
	talloc_free(tmp_ctx);
	return result;
}

/*
  setup C functions that be called from ejs
*/
void smb_setup_ejs_nbt(void)
{
	ejsDefineCFunction(-1, "resolveName", ejs_resolve_name, NULL, MPR_VAR_SCRIPT_HANDLE);
}
