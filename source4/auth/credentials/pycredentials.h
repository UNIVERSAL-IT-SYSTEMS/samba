/* 
   Unix SMB/CIFS implementation.
   Samba utility functions
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
#ifndef _PYCREDENTIALS_H_
#define _PYCREDENTIALS_H_

#include "auth/credentials/credentials.h"
#include "pytalloc.h"

PyAPI_DATA(PyTypeObject) PyCredentials;
struct cli_credentials *cli_credentials_from_py_object(PyObject *py_obj);
#define PyCredentials_Check(py_obj) PyObject_TypeCheck(py_obj, &PyCredentials)
#define PyCredentials_AsCliCredentials(py_obj) py_talloc_get_type(py_obj, struct cli_credentials)

#endif /*  _PYCREDENTIALS_H_ */
