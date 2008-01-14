/* 
   Unix SMB/CIFS implementation.
   Samba utility functions
   Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2007
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "includes.h"
#include <Python.h>
#include "build.h"

extern void init_ldb(void);
extern void init_security(void);
extern void init_registry(void);
extern void init_param(void);
extern void init_misc(void);
extern void init_ldb(void);
extern void init_auth(void);
extern void init_credentials(void);
extern void init_tdb(void);
extern void init_dcerpc(void);
extern void init_events(void);
extern void inituuid(void);
extern void init_net(void);
extern void initecho(void);
extern void initwinreg(void);
extern void initepmapper(void);
extern void initinitshutdown(void);
static void initdcerpc_misc(void) {} 
extern void initmgmt(void);
extern void initatsvc(void);
extern void initsamr(void);
static void initdcerpc_security(void) {}
extern void initlsa(void);
extern void initsvcctl(void);
extern void initwkssvc(void);

static struct _inittab py_modules[] = { STATIC_LIBPYTHON_MODULES };

void py_load_samba_modules(void)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(py_modules); i++) {
		PyImport_ExtendInittab(&py_modules[i]);
	}
}

void py_update_path(const char *bindir)
{
	char *newpath;
	asprintf(&newpath, "%s:%s/python:%s/../scripting/python", Py_GetPath(), bindir, bindir);
	PySys_SetPath(newpath);
	free(newpath);
}
