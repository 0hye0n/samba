/*
   Unix SMB/CIFS implementation.

   Swig interface to ldb.

   Copyright (C) 2005,2006 Tim Potter <tpot@samba.org>
   Copyright (C) 2006 Simo Sorce <idra@samba.org>
   Copyright (C) 2007 Jelmer Vernooij <jelmer@samba.org>

     ** NOTE! The following LGPL license applies to the ldb
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

%module(package="ldb") ldb

%{

/* Include headers */

#include <stdint.h>
#include <stdbool.h>
#include "talloc.h"
#include "ldb.h"
#include "ldb_errors.h"

typedef struct ldb_message ldb_msg;
typedef struct ldb_context ldb;
typedef struct ldb_dn ldb_dn;
typedef struct ldb_ldif ldb_ldif;
typedef struct ldb_message_element ldb_msg_element;
typedef int ldb_error;

%}

%import "carrays.i"
%import "typemaps.i"
%include "exception.i"
%import "stdint.i"

%constant int SCOPE_DEFAULT = LDB_SCOPE_DEFAULT;
%constant int SCOPE_BASE = LDB_SCOPE_BASE;
%constant int SCOPE_ONELEVEL = LDB_SCOPE_ONELEVEL;
%constant int SCOPE_SUBTREE = LDB_SCOPE_SUBTREE;

%constant int CHANGETYPE_NONE = LDB_CHANGETYPE_NONE;
%constant int CHANGETYPE_ADD = LDB_CHANGETYPE_ADD;
%constant int CHANGETYPE_DELETE = LDB_CHANGETYPE_DELETE;
%constant int CHANGETYPE_MODIFY = LDB_CHANGETYPE_MODIFY;

/* 
 * Wrap struct ldb_context
 */

/* The ldb functions will crash if a NULL ldb context is passed so
   catch this before it happens. */

%typemap(check) struct ldb_context* {
	if ($1 == NULL)
		SWIG_exception(SWIG_ValueError, 
			"ldb context must be non-NULL");
}

%typemap(check) ldb_msg * {
	if ($1 == NULL)
		SWIG_exception(SWIG_ValueError, 
			"Message can not be None");
}

/* 
 * Wrap a small bit of talloc
 */

/*
 * Wrap struct ldb_val
 */

%typemap(in) struct ldb_val *INPUT (struct ldb_val temp) {
	$1 = &temp;
	if (!PyString_Check($input)) {
		PyErr_SetString(PyExc_TypeError, "string arg expected");
		return NULL;
	}
	$1->length = PyString_Size($input);
	$1->data = PyString_AsString($input);
}

%typemap(out) struct ldb_val {
	$result = PyString_FromStringAndSize((const char *)$1.data, $1.length);
}

%typemap(in) ldb_msg *add_msg (int dict_pos, int msg_pos, PyObject *key, 
                               PyObject *value, ldb_msg_element *msgel) {
    if (PyDict_Check($input)) {
        $1 = ldb_msg_new(NULL);
        $1->num_elements = PyDict_Size($input) - 1; /* dn isn't in there */
        $1->elements = talloc_zero_array($1, struct ldb_message_element, $1->num_elements+1);
        msg_pos = dict_pos = 0;
        while (PyDict_Next($input, &dict_pos, &key, &value)) {
            if (!strcmp(PyString_AsString(key), "dn")) {
                if (ldb_dn_from_pyobject(value, &$1->dn) != 0)
                    SWIG_exception(SWIG_TypeError, "unable to convert dn");
            } else {
                msgel = ldb_msg_element_from_pyobject(value, 0, PyString_AsString(key));
                memcpy(&$1->elements[msg_pos], msgel, sizeof(*msgel));
                msg_pos++;
            }
            dict_pos++;
        }

        if ($1->dn == NULL)
            SWIG_exception(SWIG_TypeError, "no dn set");
    } else {
        if (SWIG_ConvertPtr($input, &$1, SWIGTYPE_p_ldb_message, 0) != 0)
            return NULL;
    }
}

%typemap(freearg) ldb_msg *add_msg {
//talloc_free($1);
}


/*
 * Wrap struct ldb_result
 */

%typemap(in, numinputs=0) struct ldb_result **OUT (struct ldb_result *temp_ldb_result) {
	$1 = &temp_ldb_result;
}

#ifdef SWIGPYTHON
%typemap(argout) struct ldb_result ** (int i) {
	$result = PyList_New((*$1)->count);
    for (i = 0; i < (*$1)->count; i++) {
        PyList_SetItem($result, i, 
            SWIG_NewPointerObj((*$1)->msgs[i], SWIGTYPE_p_ldb_message, 0)
        );
    }
}

%typemap(in, numinputs=1) const char * const *attrs {
    if ($input == Py_None) {
        $1 = NULL;
    } else if (PySequence_Check($input)) {
        int i;
        $1 = talloc_array(NULL, char *, PySequence_Size($input)+1);
        for(i = 0; i < PySequence_Size($input); i++)
            $1[i] = PyString_AsString(PySequence_GetItem($input, i));
        $1[i] = NULL;
    } else {
        SWIG_exception(SWIG_TypeError, "expected sequence");
    }
}

%typemap(freearg) const char * const *attrs {
    talloc_free($1);
}
#endif

%types(struct ldb_result *);

/*
 * Wrap struct ldb_dn
 */

%rename(__str__) ldb_dn::get_linearized;
%rename(__cmp__) ldb_dn::compare;
%rename(__len__) ldb_dn::get_comp_num;
%rename(Dn) ldb_dn;
typedef struct ldb_dn {
    %extend {
        ldb_dn(ldb *ldb, const char *str)
        {
            ldb_dn *ret = ldb_dn_new(ldb, ldb, str);
            /* ldb_dn_new() doesn't accept NULL as memory context, so 
               we do it this way... */
            talloc_steal(NULL, ret);

            if (ret == NULL)
                SWIG_exception(SWIG_ValueError, 
                                "unable to parse dn string");
fail:
            return ret;
        }
        ~ldb_dn() { talloc_free($self); }
        bool validate();
        const char *get_casefold();
        const char *get_linearized();
        ldb_dn *parent() { return ldb_dn_get_parent(NULL, $self); }
        int compare(ldb_dn *other);
        bool is_valid();
        bool is_special();
        bool is_null();
        bool check_special(const char *name);
        int get_comp_num();
        bool add_child(ldb_dn *child);
        bool add_base(ldb_dn *base);
        const char *canonical_str() {
            return ldb_dn_canonical_string($self, $self);
        }
        const char *canonical_ex_str() {
            return ldb_dn_canonical_ex_string($self, $self);
        }
#ifdef SWIGPYTHON
        ldb_dn *__add__(ldb_dn *other)
        {
            ldb_dn *ret = ldb_dn_copy(NULL, $self);
            ldb_dn_add_child(ret, other);
            return ret;
        }

        /* FIXME: implement __getslice__ */
#endif
    }
} ldb_dn;

#ifdef SWIGPYTHON
%inline {
int ldb_dn_from_pyobject(PyObject *object, ldb_dn **dn)
{
    return SWIG_ConvertPtr(object, dn, SWIGTYPE_p_ldb_dn, 0);
}

ldb_msg_element *ldb_msg_element_from_pyobject(PyObject *set_obj, int flags,
                                               const char *attr_name)
{
    struct ldb_message_element *me = talloc(NULL, struct ldb_message_element);
    me->name = attr_name;
    me->flags = flags;
    if (PyString_Check(set_obj)) {
        me->num_values = 1;
        me->values = talloc_array(me, struct ldb_val, me->num_values);
        me->values[0].length = PyString_Size(set_obj);
        me->values[0].data = (uint8_t *)talloc_strdup(me->values, 
                                           PyString_AsString(set_obj));
    } else if (PySequence_Check(set_obj)) {
        int i;
        me->num_values = PySequence_Size(set_obj);
        me->values = talloc_array(me, struct ldb_val, me->num_values);
        for (i = 0; i < me->num_values; i++) {
            PyObject *obj = PySequence_GetItem(set_obj, i);
            me->values[i].length = PyString_Size(obj);
            me->values[i].data = (uint8_t *)PyString_AsString(obj);
        }
    } else {
        talloc_free(me);
        me = NULL;
    }

    return me;
}

PyObject *ldb_msg_element_to_set(ldb_msg_element *me)
{
    int i;
    PyObject *result;

    /* Python << 2.5 doesn't have PySet_New and PySet_Add. */
    result = PyList_New(me->num_values);

    for (i = 0; i < me->num_values; i++) {
        PyList_SetItem(result, i,
            PyString_FromStringAndSize((const char *)me->values[i].data, 
                                       me->values[i].length));
    }

    return result;
}

}
#endif

/* ldb_message_element */
%rename(__cmp__) ldb_message_element::compare;
%rename(MessageElement) ldb_msg_element;
typedef struct ldb_message_element {
    %extend {
#ifdef SWIGPYTHON
        PyObject *__iter__(void)
        {
            return PyObject_GetIter(ldb_msg_element_to_set($self));
        }

        PyObject *__set__(void)
        {
            return ldb_msg_element_to_set($self);
        }

        ldb_msg_element(PyObject *set_obj, int flags=0, const char *name = NULL)
        {
            return ldb_msg_element_from_pyobject(set_obj, flags, name);
        }
#endif
        ~ldb_msg_element() { talloc_free($self); }
        int compare(ldb_msg_element *);
    }
} ldb_msg_element;

/* ldb_message */

%rename(Message) ldb_message;
#ifdef SWIGPYTHON
%rename(__delitem__) ldb_message::remove_attr;
%typemap(out) ldb_msg_element * {
	if ($1 == NULL)
		PyErr_SetString(PyExc_KeyError, "no such element");
    else
        $result = SWIG_NewPointerObj($1, SWIGTYPE_p_ldb_message_element, 0);
}
%rename(__getitem__) ldb_message::find_element;
//%typemap(out) ldb_msg_element *;


%inline {
    PyObject *ldb_msg_list_elements(ldb_msg *msg)
    {
        int i;
        PyObject *obj = PyList_New(msg->num_elements);
        for (i = 0; i < msg->num_elements; i++)
            PyList_SetItem(obj, i, PyString_FromString(msg->elements[i].name));
        return obj;
    }
}

#endif

typedef struct ldb_message {
	ldb_dn *dn;

    %extend {
        ldb_msg(ldb_dn *dn = NULL) { 
            ldb_msg *ret = ldb_msg_new(NULL); 
            ret->dn = talloc_reference(ret, dn);
            return ret;
        }
        ~ldb_msg() { talloc_free($self); }

        ldb_msg_element *find_element(const char *name);
        
#ifdef SWIGPYTHON
        void __setitem__(const char *attr_name, ldb_msg_element *val)
        {
            struct ldb_message_element *el;
            
            ldb_msg_remove_attr($self, attr_name);

            el = talloc($self, struct ldb_message_element);
            el->name = talloc_strdup(el, attr_name);
            el->num_values = val->num_values;
            el->values = talloc_reference(el, val->values);

            ldb_msg_add($self, el, val->flags);
        }

        void __setitem__(const char *attr_name, PyObject *val)
        {
            struct ldb_message_element *el = ldb_msg_element_from_pyobject(
                                                val, 0, attr_name);
            talloc_steal($self, el);
            ldb_msg_remove_attr($self, attr_name);
            ldb_msg_add($self, el, el->flags);
        }

        unsigned int __len__() { return $self->num_elements; }

        PyObject *keys(void)
        {
            return ldb_msg_list_elements($self);
        }

        PyObject *__iter__(void)
        {
            return PyObject_GetIter(ldb_msg_list_elements($self));
        }
#endif
        void remove_attr(const char *name);
    }
} ldb_msg;

/* FIXME: Convert ldb_result to 3-tuple:
   (msgs, refs, controls)
 */

typedef struct ldb_ldif ldb_ldif;

#ifdef SWIGPYTHON
%{
static void py_ldb_debug(void *context, enum ldb_debug_level level, const char *fmt, va_list ap)
{
    char *text;
    PyObject *fn = context;

    vasprintf(&text, fmt, ap);
    PyObject_CallFunction(fn, "(i,s)", level, text);
    free(text);
}
%}

%typemap(in,numinputs=1) (void (*debug)(void *context, enum ldb_debug_level level, const char *fmt, va_list ap),
                            void *context) {
    $1 = py_ldb_debug;
    /* FIXME: Should be decreased somewhere as well. Perhaps register a destructor and 
       tie it to the ldb context ? */
    Py_INCREF($input);
    $2 = $input;
}
#endif

%inline {
    static PyObject *ldb_ldif_to_pyobject(ldb_ldif *ldif)
    {
        if (ldif == NULL) {
            return Py_None;
        } else {
            return Py_BuildValue("(iO)", ldif->changetype, 
                   SWIG_NewPointerObj(ldif->msg, SWIGTYPE_p_ldb_message, 0));
        }
    }
}

/*
 * Wrap ldb errors
 */

%{
PyObject *PyExc_LdbError;
%}

%pythoncode %{
    LdbError = _ldb.LdbError
%}

%init %{
    PyExc_LdbError = PyErr_NewException("_ldb.LdbError", NULL, NULL);
    PyDict_SetItemString(d, "LdbError", PyExc_LdbError);
%}

%ignore _LDB_ERRORS_H_;
%ignore LDB_SUCCESS;
%include "include/ldb_errors.h"

/*
 * Wrap ldb functions 
 */

%rename(Ldb) ldb;
/* Top-level ldb operations */
typedef struct ldb_context {
    %typemap(out) ldb_error {
        if ($1 != LDB_SUCCESS) {
            PyErr_SetObject(PyExc_LdbError, Py_BuildValue("(i,s)", $1, ldb_strerror($1)));
            SWIG_fail;
        }
        $result = Py_None;
    };
    %extend {
        ldb(const char *url=NULL, unsigned int flags = 0, 
            const char *options[] = NULL)
        {
            ldb *ldb = ldb_init(NULL);
            
            if (url != NULL) {
                int ret;

                ret = ldb_connect(ldb, url, flags, options);
                if (ret != LDB_SUCCESS)
                    SWIG_exception(SWIG_ValueError, ldb_errstring(ldb));
            }

            return ldb;

fail:
            talloc_free(ldb);
            return NULL;
        }

        ldb_error connect(const char *url, unsigned int flags = 0, 
            const char *options[] = NULL);

        ~ldb() { talloc_free($self); }
        ldb_error search(ldb_dn *base = NULL, 
                   enum ldb_scope scope = LDB_SCOPE_DEFAULT, 
                   const char *expression = NULL, 
                   const char * const *attrs = NULL, 
                   struct ldb_result **OUT);
        ldb_error delete(ldb_dn *dn);
        ldb_error rename(ldb_dn *olddn, ldb_dn *newdn);
        ldb_error add(ldb_msg *add_msg);
        ldb_error modify(ldb_msg *message);
        ldb_dn *get_config_basedn();
        ldb_dn *get_root_basedn();
        ldb_dn *get_schema_basedn();
        ldb_dn *get_default_basedn();
        const char *errstring();
        void set_create_perms(unsigned int perms);
        void set_modules_dir(const char *path);
        ldb_error set_debug(void (*debug)(void *context, enum ldb_debug_level level, 
                                          const char *fmt, va_list ap),
                            void *context);
        ldb_error set_opaque(const char *name, void *value);
        void *get_opaque(const char *name);
        ldb_error transaction_start();
        ldb_error transaction_commit();
        ldb_error transaction_cancel();

#ifdef SWIGPYTHON
        bool __contains__(ldb_dn *dn)
        {
            struct ldb_result *result;
            
            int ret = ldb_search($self, dn, LDB_SCOPE_BASE, NULL, NULL, 
                             &result);

            /* FIXME: Check ret and set exception if necessary */

            return result->count > 0;
        }

        PyObject *parse_ldif(const char *s)
        {
            PyObject *list = PyList_New(0);
            struct ldb_ldif *ldif;
            while ((ldif = ldb_ldif_read_string($self, &s)) != NULL) {
                PyList_Append(list, ldb_ldif_to_pyobject(ldif));
            }
            return PyObject_GetIter(list);
        }

#endif
    }
} ldb;

%nodefault ldb_message;
%nodefault Ldb;
%nodefault Dn;

%rename(valid_attr_name) ldb_valid_attr_name;
int ldb_valid_attr_name(const char *s);

typedef unsigned long time_t;

%{
char *timestring(time_t t)
{
    char *tresult = ldb_timestring(NULL, t);
    char *result = strdup(tresult);
    talloc_free(tresult);
    return result; 
}
%}
char *timestring(time_t t);

%rename(string_to_time) ldb_string_to_time;
time_t ldb_string_to_time(const char *s);
