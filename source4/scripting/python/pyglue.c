/* 
   Unix SMB/CIFS implementation.
   Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2007
   Copyright (C) Matthias Dieter Wallnöfer          2009
   
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

#include <Python.h>
#include "includes.h"
#include "ldb.h"
#include "ldb_errors.h"
#include "ldb_wrap.h"
#include "param/param.h"
#include "auth/credentials/credentials.h"
#include "dsdb/samdb/samdb.h"
#include "lib/ldb-samba/ldif_handlers.h"
#include "librpc/ndr/libndr.h"
#include "version.h"
#include "lib/ldb/pyldb.h"
#include "libcli/util/pyerrors.h"
#include "libcli/security/security.h"
#include "auth/pyauth.h"
#include "param/pyparam.h"
#include "auth/credentials/pycredentials.h"
#include "lib/socket/netif.h"
#include "lib/socket/netif_proto.h"

/* FIXME: These should be in a header file somewhere, once we finish moving
 * away from SWIG .. */
#define PyErr_LDB_OR_RAISE(py_ldb, ldb) \
/*	if (!PyLdb_Check(py_ldb)) { \
		PyErr_SetString(py_ldb_get_exception(), "Ldb connection object required"); \
		return NULL; \
	} */\
	ldb = PyLdb_AsLdbContext(py_ldb);

static void PyErr_SetLdbError(PyObject *error, int ret, struct ldb_context *ldb_ctx)
{
	if (ret == LDB_ERR_PYTHON_EXCEPTION)
		return; /* Python exception should already be set, just keep that */

	PyErr_SetObject(error, 
			Py_BuildValue(discard_const_p(char, "(i,s)"), ret,
			ldb_ctx == NULL?ldb_strerror(ret):ldb_errstring(ldb_ctx)));
}

static PyObject *py_ldb_get_exception(void)
{
	PyObject *mod = PyImport_ImportModule("ldb");
	if (mod == NULL)
		return NULL;

	return PyObject_GetAttrString(mod, "LdbError");
}

static PyObject *py_generate_random_str(PyObject *self, PyObject *args)
{
	int len;
	PyObject *ret;
	char *retstr;
	if (!PyArg_ParseTuple(args, "i", &len))
		return NULL;

	retstr = generate_random_str(NULL, len);
	ret = PyString_FromString(retstr);
	talloc_free(retstr);
	return ret;
}

static PyObject *py_generate_random_password(PyObject *self, PyObject *args)
{
	int min, max;
	PyObject *ret;
	char *retstr;
	if (!PyArg_ParseTuple(args, "ii", &min, &max))
		return NULL;

	retstr = generate_random_password(NULL, min, max);
	if (retstr == NULL) {
		return NULL;
	}
	ret = PyString_FromString(retstr);
	talloc_free(retstr);
	return ret;
}

static PyObject *py_unix2nttime(PyObject *self, PyObject *args)
{
	time_t t;
	NTTIME nt;
	if (!PyArg_ParseTuple(args, "I", &t))
		return NULL;

	unix_to_nt_time(&nt, t);

	return PyInt_FromLong((uint64_t)nt);
}

static PyObject *py_set_debug_level(PyObject *self, PyObject *args)
{
	unsigned level;
	if (!PyArg_ParseTuple(args, "I", &level))
		return NULL;
	(DEBUGLEVEL) = level;
	Py_RETURN_NONE;
}

static PyObject *py_ldb_set_session_info(PyObject *self, PyObject *args)
{
	PyObject *py_session_info, *py_ldb;
	struct auth_session_info *info;
	struct ldb_context *ldb;
	if (!PyArg_ParseTuple(args, "OO", &py_ldb, &py_session_info))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);
	/*if (!PyAuthSession_Check(py_session_info)) {
		PyErr_SetString(PyExc_TypeError, "Expected session info object");
		return NULL;
	}*/

	info = PyAuthSession_AsSession(py_session_info);

	ldb_set_opaque(ldb, "sessionInfo", info);

	Py_RETURN_NONE;
}

static PyObject *py_ldb_set_credentials(PyObject *self, PyObject *args)
{
	PyObject *py_creds, *py_ldb;
	struct cli_credentials *creds;
	struct ldb_context *ldb;
	if (!PyArg_ParseTuple(args, "OO", &py_ldb, &py_creds))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);
	
	creds = cli_credentials_from_py_object(py_creds);
	if (creds == NULL) {
		PyErr_SetString(PyExc_TypeError, "Expected credentials object");
		return NULL;
	}

	ldb_set_opaque(ldb, "credentials", creds);

	Py_RETURN_NONE;
}

static PyObject *py_ldb_set_loadparm(PyObject *self, PyObject *args)
{
	PyObject *py_lp_ctx, *py_ldb;
	struct loadparm_context *lp_ctx;
	struct ldb_context *ldb;
	if (!PyArg_ParseTuple(args, "OO", &py_ldb, &py_lp_ctx))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	lp_ctx = lp_from_py_object(py_lp_ctx);
	if (lp_ctx == NULL) {
		PyErr_SetString(PyExc_TypeError, "Expected loadparm object");
		return NULL;
	}

    	ldb_set_opaque(ldb, "loadparm", lp_ctx);

	Py_RETURN_NONE;
}

static PyObject *py_ldb_set_utf8_casefold(PyObject *self, PyObject *args)
{
	PyObject *py_ldb;
	struct ldb_context *ldb;

	if (!PyArg_ParseTuple(args, "O", &py_ldb))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	ldb_set_utf8_fns(ldb, NULL, wrap_casefold);

	Py_RETURN_NONE;
}

static PyObject *py_samdb_set_domain_sid(PyLdbObject *self, PyObject *args)
{ 
	PyObject *py_ldb, *py_sid;
	struct ldb_context *ldb;
	struct dom_sid *sid;
	bool ret;

	if (!PyArg_ParseTuple(args, "OO", &py_ldb, &py_sid))
		return NULL;
	
	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	sid = dom_sid_parse_talloc(NULL, PyString_AsString(py_sid));

	ret = samdb_set_domain_sid(ldb, sid);
	if (!ret) {
		PyErr_SetString(PyExc_RuntimeError, "set_domain_sid failed");
		return NULL;
	} 
	Py_RETURN_NONE;
}

static PyObject *py_samdb_get_domain_sid(PyLdbObject *self, PyObject *args)
{ 
	PyObject *py_ldb;
	struct ldb_context *ldb;
	const struct dom_sid *sid;
	PyObject *ret;
	char *retstr;

	if (!PyArg_ParseTuple(args, "O", &py_ldb))
		return NULL;
	
	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	sid = samdb_domain_sid(ldb);
	if (!sid) {
		PyErr_SetString(PyExc_RuntimeError, "samdb_domain_sid failed");
		return NULL;
	} 
	retstr = dom_sid_string(NULL, sid);
	ret = PyString_FromString(retstr);
	talloc_free(retstr);
	return ret;
}

static PyObject *py_ldb_register_samba_handlers(PyObject *self, PyObject *args)
{
	PyObject *py_ldb;
	struct ldb_context *ldb;
	int ret;

	if (!PyArg_ParseTuple(args, "O", &py_ldb))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);
	ret = ldb_register_samba_handlers(ldb);

	PyErr_LDB_ERROR_IS_ERR_RAISE(py_ldb_get_exception(), ret, ldb);
	Py_RETURN_NONE;
}

static PyObject *py_dsdb_set_ntds_invocation_id(PyObject *self, PyObject *args)
{
	PyObject *py_ldb, *py_guid;
	bool ret;
	struct GUID guid;
	struct ldb_context *ldb;
	if (!PyArg_ParseTuple(args, "OO", &py_ldb, &py_guid))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);
	GUID_from_string(PyString_AsString(py_guid), &guid);

	ret = samdb_set_ntds_invocation_id(ldb, &guid);
	if (!ret) {
		PyErr_SetString(PyExc_RuntimeError, "set_ntds_invocation_id failed");
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject *py_dsdb_set_opaque_integer(PyObject *self, PyObject *args)
{
	PyObject *py_ldb;
	int value;
	int *old_val, *new_val;
	char *py_opaque_name, *opaque_name_talloc;
	struct ldb_context *ldb;
	TALLOC_CTX *tmp_ctx;

	if (!PyArg_ParseTuple(args, "Osi", &py_ldb, &py_opaque_name, &value))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	/* see if we have a cached copy */
	old_val = (int *)ldb_get_opaque(ldb, 
					py_opaque_name);

	if (old_val) {
		*old_val = value;
		Py_RETURN_NONE;
	} 

	tmp_ctx = talloc_new(ldb);
	if (tmp_ctx == NULL) {
		goto failed;
	}
	
	new_val = talloc(tmp_ctx, int);
	if (!new_val) {
		goto failed;
	}
	
	opaque_name_talloc = talloc_strdup(tmp_ctx, py_opaque_name);
	if (!opaque_name_talloc) {
		goto failed;
	}
	
	*new_val = value;

	/* cache the domain_sid in the ldb */
	if (ldb_set_opaque(ldb, opaque_name_talloc, new_val) != LDB_SUCCESS) {
		goto failed;
	}

	talloc_steal(ldb, new_val);
	talloc_steal(ldb, opaque_name_talloc);
	talloc_free(tmp_ctx);

	Py_RETURN_NONE;

failed:
	talloc_free(tmp_ctx);
	PyErr_SetString(PyExc_RuntimeError, "Failed to set opaque integer into the ldb!\n");
	return NULL;
}

static PyObject *py_dsdb_set_global_schema(PyObject *self, PyObject *args)
{
	PyObject *py_ldb;
	struct ldb_context *ldb;
	int ret;
	if (!PyArg_ParseTuple(args, "O", &py_ldb))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	ret = dsdb_set_global_schema(ldb);
	PyErr_LDB_ERROR_IS_ERR_RAISE(py_ldb_get_exception(), ret, ldb);

	Py_RETURN_NONE;
}

static PyObject *py_dsdb_set_schema_from_ldif(PyObject *self, PyObject *args)
{
	WERROR result;
	char *pf, *df;
	PyObject *py_ldb;
	struct ldb_context *ldb;

	if (!PyArg_ParseTuple(args, "Oss", &py_ldb, &pf, &df))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	result = dsdb_set_schema_from_ldif(ldb, pf, df);
	PyErr_WERROR_IS_ERR_RAISE(result);

	Py_RETURN_NONE;
}

static PyObject *py_dsdb_convert_schema_to_openldap(PyObject *self, PyObject *args)
{
	char *target_str, *mapping;
	PyObject *py_ldb;
	struct ldb_context *ldb;
	PyObject *ret;
	char *retstr;

	if (!PyArg_ParseTuple(args, "Oss", &py_ldb, &target_str, &mapping))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	retstr = dsdb_convert_schema_to_openldap(ldb, target_str, mapping);
	if (!retstr) {
		PyErr_SetString(PyExc_RuntimeError, "dsdb_convert_schema_to_openldap failed");
		return NULL;
	} 
	ret = PyString_FromString(retstr);
	talloc_free(retstr);
	return ret;
}

static PyObject *py_dsdb_write_prefixes_from_schema_to_ldb(PyObject *self, PyObject *args)
{
	PyObject *py_ldb;
	struct ldb_context *ldb;
	WERROR result;
	struct dsdb_schema *schema;

	if (!PyArg_ParseTuple(args, "O", &py_ldb))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	schema = dsdb_get_schema(ldb, NULL);
	if (!schema) {
		PyErr_SetString(PyExc_RuntimeError, "Failed to set find a schema on ldb!\n");
		return NULL;
	}

	result = dsdb_write_prefixes_from_schema_to_ldb(NULL, ldb, schema);
	PyErr_WERROR_IS_ERR_RAISE(result);

	Py_RETURN_NONE;
}

static PyObject *py_dsdb_set_schema_from_ldb(PyObject *self, PyObject *args)
{
	PyObject *py_ldb;
	struct ldb_context *ldb;
	PyObject *py_from_ldb;
	struct ldb_context *from_ldb;
	struct dsdb_schema *schema;
	int ret;
	if (!PyArg_ParseTuple(args, "OO", &py_ldb, &py_from_ldb))
		return NULL;

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	PyErr_LDB_OR_RAISE(py_from_ldb, from_ldb);

	schema = dsdb_get_schema(from_ldb, NULL);
	if (!schema) {
		PyErr_SetString(PyExc_RuntimeError, "Failed to set find a schema on 'from' ldb!\n");
		return NULL;
	}

	ret = dsdb_reference_schema(ldb, schema, true);
	PyErr_LDB_ERROR_IS_ERR_RAISE(py_ldb_get_exception(), ret, ldb);

	Py_RETURN_NONE;
}

static PyObject *py_dsdb_load_partition_usn(PyObject *self, PyObject *args)
{
	PyObject *py_dn, *py_ldb, *result;
	struct ldb_dn *dn;
	uint64_t highest_uSN, urgent_uSN;
	struct ldb_context *ldb;
	TALLOC_CTX *mem_ctx;
	int ret;

	mem_ctx = talloc_new(NULL);
	if (mem_ctx == NULL) {
	   PyErr_NoMemory();
	   return NULL;
	}

	if (!PyArg_ParseTuple(args, "OO", &py_ldb, &py_dn)) {
	   talloc_free(mem_ctx);
	   return NULL;
	}

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	if (!PyObject_AsDn(mem_ctx, py_dn, ldb, &dn)) {
	   talloc_free(mem_ctx);
	   return NULL;
	}

	ret = dsdb_load_partition_usn(ldb, dn, &highest_uSN, &urgent_uSN);
	if (ret != LDB_SUCCESS) {
	   char *errstr = talloc_asprintf(mem_ctx, "Failed to load partition uSN - %s", ldb_errstring(ldb));
	   PyErr_SetString(PyExc_RuntimeError, errstr);
	   talloc_free(mem_ctx);
	   return NULL;
	}

	talloc_free(mem_ctx);

	result = PyDict_New();

	PyDict_SetItemString(result, "uSNHighest", PyInt_FromLong((uint64_t)highest_uSN));
	PyDict_SetItemString(result, "uSNUrgent", PyInt_FromLong((uint64_t)urgent_uSN));


	return result;

}



static PyObject *py_samdb_ntds_invocation_id(PyObject *self, PyObject *args)
{
	PyObject *py_ldb, *result;
	struct ldb_context *ldb;
	TALLOC_CTX *mem_ctx;
	const struct GUID *guid;

	mem_ctx = talloc_new(NULL);
	if (mem_ctx == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	if (!PyArg_ParseTuple(args, "O", &py_ldb)) {
		talloc_free(mem_ctx);
		return NULL;
	}

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	guid = samdb_ntds_invocation_id(ldb);
	if (guid == NULL) {
		PyErr_SetStringError("Failed to find NTDS invocation ID");
		talloc_free(mem_ctx);
		return NULL;
	}

	result = PyString_FromString(GUID_string(mem_ctx, guid));
	talloc_free(mem_ctx);
	return result;
}


static PyObject *py_samdb_ntds_objectGUID(PyObject *self, PyObject *args)
{
	PyObject *py_ldb, *result;
	struct ldb_context *ldb;
	TALLOC_CTX *mem_ctx;
	const struct GUID *guid;

	mem_ctx = talloc_new(NULL);
	if (mem_ctx == NULL) {
		PyErr_NoMemory();
		return NULL;
	}

	if (!PyArg_ParseTuple(args, "O", &py_ldb)) {
		talloc_free(mem_ctx);
		return NULL;
	}

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	guid = samdb_ntds_objectGUID(ldb);
	if (guid == NULL) {
		PyErr_SetStringError("Failed to find NTDS GUID");
		talloc_free(mem_ctx);
		return NULL;
	}

	result = PyString_FromString(GUID_string(mem_ctx, guid));
	talloc_free(mem_ctx);
	return result;
}


static PyObject *py_samdb_server_site_name(PyObject *self, PyObject *args)
{
	PyObject *py_ldb, *result;
	struct ldb_context *ldb;
	const char *site;
	TALLOC_CTX *mem_ctx = talloc_new(NULL);

	if (!PyArg_ParseTuple(args, "O", &py_ldb)) {
		talloc_free(mem_ctx);
		return NULL;
	}

	PyErr_LDB_OR_RAISE(py_ldb, ldb);

	site = samdb_server_site_name(ldb, mem_ctx);
	if (site == NULL) {
		PyErr_SetStringError("Failed to find server site");
		talloc_free(mem_ctx);
		return NULL;
	}

	result = PyString_FromString(site);
	talloc_free(mem_ctx);
	return result;
}


/*
  return the list of interface IPs we have configured
  takes an loadparm context, returns a list of IPs in string form

  Does not return addresses on 127.0.0.0/8
 */
static PyObject *py_interface_ips(PyObject *self, PyObject *args)
{
	PyObject *pylist;
	int count;
	TALLOC_CTX *tmp_ctx;
	PyObject *py_lp_ctx;
	struct loadparm_context *lp_ctx;
	struct interface *ifaces;
	int i, ifcount;
	int all_interfaces;

	if (!PyArg_ParseTuple(args, "Oi", &py_lp_ctx, &all_interfaces))
		return NULL;

	lp_ctx = lp_from_py_object(py_lp_ctx);
	if (lp_ctx == NULL) {
		PyErr_SetString(PyExc_TypeError, "Expected loadparm object");
		return NULL;
	}

	tmp_ctx = talloc_new(NULL);

	load_interfaces(tmp_ctx, lp_interfaces(lp_ctx), &ifaces);

	count = iface_count(ifaces);

	/* first count how many are not loopback addresses */
	for (ifcount = i = 0; i<count; i++) {
		const char *ip = iface_n_ip(ifaces, i);
		if (!(!all_interfaces && iface_same_net(ip, "127.0.0.1", "255.0.0.0"))) {
			ifcount++;
		}
	}

	pylist = PyList_New(ifcount);
	for (ifcount = i = 0; i<count; i++) {
		const char *ip = iface_n_ip(ifaces, i);
		if (!(!all_interfaces && iface_same_net(ip, "127.0.0.1", "255.0.0.0"))) {
			PyList_SetItem(pylist, ifcount, PyString_FromString(ip));
			ifcount++;
		}
	}
	talloc_free(tmp_ctx);
	return pylist;
}


static PyMethodDef py_misc_methods[] = {
	{ "generate_random_str", (PyCFunction)py_generate_random_str, METH_VARARGS,
		"generate_random_str(len) -> string\n"
		"Generate random string with specified length." },
	{ "generate_random_password", (PyCFunction)py_generate_random_password, METH_VARARGS,
		"generate_random_password(min, max) -> string\n"
		"Generate random password with a length >= min and <= max." },
	{ "unix2nttime", (PyCFunction)py_unix2nttime, METH_VARARGS,
		"unix2nttime(timestamp) -> nttime" },
	{ "ldb_set_session_info", (PyCFunction)py_ldb_set_session_info, METH_VARARGS,
		"ldb_set_session_info(ldb, session_info)\n"
		"Set session info to use when connecting." },
	{ "ldb_set_credentials", (PyCFunction)py_ldb_set_credentials, METH_VARARGS,
		"ldb_set_credentials(ldb, credentials)\n"
		"Set credentials to use when connecting." },
	{ "ldb_set_loadparm", (PyCFunction)py_ldb_set_loadparm, METH_VARARGS,
		"ldb_set_loadparm(ldb, session_info)\n"
		"Set loadparm context to use when connecting." },
	{ "samdb_set_domain_sid", (PyCFunction)py_samdb_set_domain_sid, METH_VARARGS,
		"samdb_set_domain_sid(samdb, sid)\n"
		"Set SID of domain to use." },
	{ "samdb_get_domain_sid", (PyCFunction)py_samdb_get_domain_sid, METH_VARARGS,
		"samdb_get_domain_sid(samdb)\n"
		"Get SID of domain in use." },
	{ "ldb_register_samba_handlers", (PyCFunction)py_ldb_register_samba_handlers, METH_VARARGS,
		"ldb_register_samba_handlers(ldb)\n"
		"Register Samba-specific LDB modules and schemas." },
	{ "ldb_set_utf8_casefold", (PyCFunction)py_ldb_set_utf8_casefold, METH_VARARGS,
		"ldb_set_utf8_casefold(ldb)\n"
		"Set the right Samba casefolding function for UTF8 charset." },
	{ "dsdb_set_ntds_invocation_id", (PyCFunction)py_dsdb_set_ntds_invocation_id, METH_VARARGS,
		NULL },
	{ "dsdb_set_opaque_integer", (PyCFunction)py_dsdb_set_opaque_integer, METH_VARARGS,
		NULL },
	{ "dsdb_set_global_schema", (PyCFunction)py_dsdb_set_global_schema, METH_VARARGS,
		NULL },
	{ "dsdb_set_schema_from_ldif", (PyCFunction)py_dsdb_set_schema_from_ldif, METH_VARARGS,
		NULL },
	{ "dsdb_write_prefixes_from_schema_to_ldb", (PyCFunction)py_dsdb_write_prefixes_from_schema_to_ldb, METH_VARARGS,
		NULL },
	{ "dsdb_set_schema_from_ldb", (PyCFunction)py_dsdb_set_schema_from_ldb, METH_VARARGS,
		NULL },
	{ "dsdb_convert_schema_to_openldap", (PyCFunction)py_dsdb_convert_schema_to_openldap, METH_VARARGS,
		NULL },
	{ "set_debug_level", (PyCFunction)py_set_debug_level, METH_VARARGS,
		"set debug level" },
	{ "dsdb_load_partition_usn", (PyCFunction)py_dsdb_load_partition_usn, METH_VARARGS,
		"get uSNHighest and uSNUrgent from the partition @REPLCHANGED"},
	{ "samdb_ntds_invocation_id", (PyCFunction)py_samdb_ntds_invocation_id, METH_VARARGS,
		"get the NTDS invocation ID GUID as a string"},
	{ "samdb_ntds_objectGUID", (PyCFunction)py_samdb_ntds_objectGUID, METH_VARARGS,
		"get the NTDS objectGUID as a string"},
	{ "samdb_server_site_name", (PyCFunction)py_samdb_server_site_name, METH_VARARGS,
		"get the server site name as a string"},
	{ "interface_ips", (PyCFunction)py_interface_ips, METH_VARARGS,
		"get interface IP address list"},
	{ NULL }
};

void initglue(void)
{
	PyObject *m;

	debug_setup_talloc_log();

	m = Py_InitModule3("glue", py_misc_methods, 
			   "Python bindings for miscellaneous Samba functions.");
	if (m == NULL)
		return;

	PyModule_AddObject(m, "version", PyString_FromString(SAMBA_VERSION_STRING));

	/* "userAccountControl" flags */
	PyModule_AddObject(m, "UF_NORMAL_ACCOUNT", PyInt_FromLong(UF_NORMAL_ACCOUNT));
	PyModule_AddObject(m, "UF_TEMP_DUPLICATE_ACCOUNT", PyInt_FromLong(UF_TEMP_DUPLICATE_ACCOUNT));
	PyModule_AddObject(m, "UF_SERVER_TRUST_ACCOUNT", PyInt_FromLong(UF_SERVER_TRUST_ACCOUNT));
	PyModule_AddObject(m, "UF_WORKSTATION_TRUST_ACCOUNT", PyInt_FromLong(UF_WORKSTATION_TRUST_ACCOUNT));
	PyModule_AddObject(m, "UF_INTERDOMAIN_TRUST_ACCOUNT", PyInt_FromLong(UF_INTERDOMAIN_TRUST_ACCOUNT));
	PyModule_AddObject(m, "UF_PASSWD_NOTREQD", PyInt_FromLong(UF_PASSWD_NOTREQD));
	PyModule_AddObject(m, "UF_ACCOUNTDISABLE", PyInt_FromLong(UF_ACCOUNTDISABLE));

	/* "groupType" flags */
	PyModule_AddObject(m, "GTYPE_SECURITY_BUILTIN_LOCAL_GROUP", PyInt_FromLong(GTYPE_SECURITY_BUILTIN_LOCAL_GROUP));
	PyModule_AddObject(m, "GTYPE_SECURITY_GLOBAL_GROUP", PyInt_FromLong(GTYPE_SECURITY_GLOBAL_GROUP));
	PyModule_AddObject(m, "GTYPE_SECURITY_DOMAIN_LOCAL_GROUP", PyInt_FromLong(GTYPE_SECURITY_DOMAIN_LOCAL_GROUP));
	PyModule_AddObject(m, "GTYPE_SECURITY_UNIVERSAL_GROUP", PyInt_FromLong(GTYPE_SECURITY_UNIVERSAL_GROUP));
	PyModule_AddObject(m, "GTYPE_DISTRIBUTION_GLOBAL_GROUP", PyInt_FromLong(GTYPE_DISTRIBUTION_GLOBAL_GROUP));
	PyModule_AddObject(m, "GTYPE_DISTRIBUTION_DOMAIN_LOCAL_GROUP", PyInt_FromLong(GTYPE_DISTRIBUTION_DOMAIN_LOCAL_GROUP));
	PyModule_AddObject(m, "GTYPE_DISTRIBUTION_UNIVERSAL_GROUP", PyInt_FromLong(GTYPE_DISTRIBUTION_UNIVERSAL_GROUP));

	/* "sAMAccountType" flags */
	PyModule_AddObject(m, "ATYPE_NORMAL_ACCOUNT", PyInt_FromLong(ATYPE_NORMAL_ACCOUNT));
	PyModule_AddObject(m, "ATYPE_WORKSTATION_TRUST", PyInt_FromLong(ATYPE_WORKSTATION_TRUST));
	PyModule_AddObject(m, "ATYPE_INTERDOMAIN_TRUST", PyInt_FromLong(ATYPE_INTERDOMAIN_TRUST));
	PyModule_AddObject(m, "ATYPE_SECURITY_GLOBAL_GROUP", PyInt_FromLong(ATYPE_SECURITY_GLOBAL_GROUP));
	PyModule_AddObject(m, "ATYPE_SECURITY_LOCAL_GROUP", PyInt_FromLong(ATYPE_SECURITY_LOCAL_GROUP));
	PyModule_AddObject(m, "ATYPE_SECURITY_UNIVERSAL_GROUP", PyInt_FromLong(ATYPE_SECURITY_UNIVERSAL_GROUP));
	PyModule_AddObject(m, "ATYPE_DISTRIBUTION_GLOBAL_GROUP", PyInt_FromLong(ATYPE_DISTRIBUTION_GLOBAL_GROUP));
	PyModule_AddObject(m, "ATYPE_DISTRIBUTION_LOCAL_GROUP", PyInt_FromLong(ATYPE_DISTRIBUTION_LOCAL_GROUP));
	PyModule_AddObject(m, "ATYPE_DISTRIBUTION_UNIVERSAL_GROUP", PyInt_FromLong(ATYPE_DISTRIBUTION_UNIVERSAL_GROUP));

	/* "domainFunctionality", "forestFunctionality" flags in the rootDSE */
	PyModule_AddObject(m, "DS_DOMAIN_FUNCTION_2000", PyInt_FromLong(DS_DOMAIN_FUNCTION_2000));
	PyModule_AddObject(m, "DS_DOMAIN_FUNCTION_2003_MIXED", PyInt_FromLong(DS_DOMAIN_FUNCTION_2003_MIXED));
	PyModule_AddObject(m, "DS_DOMAIN_FUNCTION_2003", PyInt_FromLong(DS_DOMAIN_FUNCTION_2003));
	PyModule_AddObject(m, "DS_DOMAIN_FUNCTION_2008", PyInt_FromLong(DS_DOMAIN_FUNCTION_2008));
	PyModule_AddObject(m, "DS_DOMAIN_FUNCTION_2008_R2", PyInt_FromLong(DS_DOMAIN_FUNCTION_2008_R2));

	/* "domainControllerFunctionality" flags in the rootDSE */
	PyModule_AddObject(m, "DS_DC_FUNCTION_2000", PyInt_FromLong(DS_DC_FUNCTION_2000));
	PyModule_AddObject(m, "DS_DC_FUNCTION_2003", PyInt_FromLong(DS_DC_FUNCTION_2003));
	PyModule_AddObject(m, "DS_DC_FUNCTION_2008", PyInt_FromLong(DS_DC_FUNCTION_2008));
	PyModule_AddObject(m, "DS_DC_FUNCTION_2008_R2", PyInt_FromLong(DS_DC_FUNCTION_2008_R2));

	/* "LDAP_SERVER_SD_FLAGS_OID" */
	PyModule_AddObject(m, "SECINFO_OWNER", PyInt_FromLong(SECINFO_OWNER));
	PyModule_AddObject(m, "SECINFO_GROUP", PyInt_FromLong(SECINFO_GROUP));
	PyModule_AddObject(m, "SECINFO_DACL", PyInt_FromLong(SECINFO_DACL));
	PyModule_AddObject(m, "SECINFO_SACL", PyInt_FromLong(SECINFO_SACL));

	/* control access rights guids */
	PyModule_AddObject(m, "GUID_DRS_ALLOCATE_RIDS", PyString_FromString(GUID_DRS_ALLOCATE_RIDS));
	PyModule_AddObject(m, "GUID_DRS_CHANGE_DOMAIN_MASTER", PyString_FromString(GUID_DRS_CHANGE_DOMAIN_MASTER));
	PyModule_AddObject(m, "GUID_DRS_CHANGE_INFR_MASTER", PyString_FromString(GUID_DRS_CHANGE_INFR_MASTER));
	PyModule_AddObject(m, "GUID_DRS_CHANGE_PDC", PyString_FromString(GUID_DRS_CHANGE_PDC));
	PyModule_AddObject(m, "GUID_DRS_CHANGE_RID_MASTER", PyString_FromString(GUID_DRS_CHANGE_RID_MASTER));
	PyModule_AddObject(m, "GUID_DRS_CHANGE_SCHEMA_MASTER", PyString_FromString(GUID_DRS_CHANGE_SCHEMA_MASTER));
	PyModule_AddObject(m, "GUID_DRS_GET_CHANGES", PyString_FromString(GUID_DRS_GET_CHANGES));
	PyModule_AddObject(m, "GUID_DRS_GET_ALL_CHANGES", PyString_FromString(GUID_DRS_GET_ALL_CHANGES));
	PyModule_AddObject(m, "GUID_DRS_GET_FILTERED_ATTRIBUTES", PyString_FromString(GUID_DRS_GET_FILTERED_ATTRIBUTES));
	PyModule_AddObject(m, "GUID_DRS_MANAGE_TOPOLOGY", PyString_FromString(GUID_DRS_MANAGE_TOPOLOGY));
	PyModule_AddObject(m, "GUID_DRS_MONITOR_TOPOLOGY", PyString_FromString(GUID_DRS_MONITOR_TOPOLOGY));
	PyModule_AddObject(m, "GUID_DRS_REPL_SYNCRONIZE", PyString_FromString(GUID_DRS_REPL_SYNCRONIZE));
	PyModule_AddObject(m, "GUID_DRS_RO_REPL_SECRET_SYNC", PyString_FromString(GUID_DRS_RO_REPL_SECRET_SYNC));

	/* one of the most annoying things about python scripts is
 	   that they don't die when you hit control-C. This fixes that
 	   sillyness. As we do all database operations using
 	   transactions, this is also safe. In fact, not dying
 	   immediately is unsafe as we could end up treating the
 	   control-C exception as a different error and try to modify
 	   as database incorrectly 
	*/
	signal(SIGINT, SIG_DFL);
}

