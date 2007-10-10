/* 
   Unix SMB/CIFS implementation.

   kerberos system include wrappers

   Copyright (C) Andrew Tridgell 2004
   
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


#ifdef HAVE_KRB5_H
#include <krb5.h>
#else
#undef HAVE_KRB5
#endif

#ifdef HAVE_GSSAPI_KRB5_H
#include <gssapi_krb5.h>
#elif defined(HAVE_GSSAPI_GSSAPI_KRB5_H)
#include <gssapi/gssapi_krb5.h>
#elif defined(HAVE_GSSAPI_H)
#include <gssapi.h>
#elif defined(HAVE_GSSAPI_GSSAPI_H)
#include <gssapi/gssapi.h>
#elif defined(HAVE_GSSAPI_GSSAPI_GENERIC_H)
#include <gssapi/gssapi_generic.h>
#endif

#ifdef HAVE_COM_ERR_H
#include <com_err.h>
#endif
