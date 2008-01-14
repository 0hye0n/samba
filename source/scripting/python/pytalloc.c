/* 
   Unix SMB/CIFS implementation.
   Python/Talloc glue
   Copyright (C) Jelmer Vernooij <jelmer@samba.org> 2008
   
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

void py_talloc_dealloc(PyObject* self)
{
	py_talloc_Object *obj = (py_talloc_Object *)self;
	talloc_free(obj->talloc_ctx);
	PyObject_Del(self);
}

PyObject *py_talloc_import(PyTypeObject *py_type, TALLOC_CTX *mem_ctx, 
						   void *ptr)
{
	PyObject *ret = PyObject_New(py_talloc_Object, &py_type);
	ret->talloc_ctx = talloc_reference(mem_ctx, ptr); 
	ret->ptr = ptr;
	return ret;
}

PyObject *py_talloc_default_repr(PyObject *py_obj)
{
	py_talloc_Object *obj = (py_talloc_Object *)py_obj;

	return PyString_FromFormat("<talloc: %s>", 
							   talloc_get_name(obj->talloc_ctx));
}
