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

#include <talloc.h>
#include "../lib/talloc/pytalloc.h"

void py_talloc_dealloc(PyObject* self)
{
	py_talloc_Object *obj = (py_talloc_Object *)self;
	talloc_free(obj->talloc_ctx);
	obj->talloc_ctx = NULL;
	PyObject_Del(self);
}

PyObject *py_talloc_import_ex(PyTypeObject *py_type, TALLOC_CTX *mem_ctx, 
						   void *ptr)
{
	py_talloc_Object *ret = PyObject_New(py_talloc_Object, py_type);
	ret->talloc_ctx = talloc_new(NULL);
	if (ret->talloc_ctx == NULL) {
		return NULL;
	}
	if (talloc_reference(ret->talloc_ctx, mem_ctx) == NULL) {
		return NULL;
	}
	ret->ptr = ptr;
	return (PyObject *)ret;
}

PyObject *py_talloc_default_repr(PyObject *py_obj)
{
	py_talloc_Object *obj = (py_talloc_Object *)py_obj;
	PyTypeObject *type = (PyTypeObject*)PyObject_Type((PyObject *)obj);

	return PyString_FromFormat("<%s talloc object at 0x%x>", type->tp_name, (intptr_t)py_obj);
}