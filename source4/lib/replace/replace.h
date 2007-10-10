/* 
   Unix SMB/CIFS implementation.

   macros to go along with the lib/replace/ portability layer code

   Copyright (C) Andrew Tridgell 2005
   
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

#ifndef _replace_h
#define _replace_h

#if defined(_MSC_VER) || defined(__MINGW32__)
#include "lib/replace/win32/replace.h"
#endif

#ifdef __COMPAR_FN_T
#define QSORT_CAST (__compar_fn_t)
#endif

#ifndef QSORT_CAST
#define QSORT_CAST (int (*)(const void *, const void *))
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif

#ifndef HAVE_STRERROR
extern char *sys_errlist[];
#define strerror(i) sys_errlist[i]
#endif

#ifndef HAVE_ERRNO_DECL
extern int errno;
#endif

#ifndef HAVE_STRDUP
char *strdup(const char *s);
#endif

#ifndef HAVE_MEMMOVE
void *memmove(void *dest,const void *src,int size);
#endif

#ifndef HAVE_MKTIME
time_t mktime(struct tm *t);
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *d, const char *s, size_t bufsize);
#endif

#ifndef HAVE_STRLCAT
size_t strlcat(char *d, const char *s, size_t bufsize);
#endif

#ifndef HAVE_STRNDUP
char *strndup(const char *s, size_t n);
#endif

#ifndef HAVE_STRNLEN
size_t strnlen(const char *s, size_t n);
#endif

#ifndef HAVE_STRTOUL
unsigned long strtoul(const char *nptr, char **endptr, int base);
#endif

#ifndef HAVE_SETENV
int setenv(const char *name, const char *value, int overwrite); 
#endif

#ifndef HAVE_RENAME
int rename(const char *zfrom, const char *zto);
#endif

#ifndef HAVE_STRCASESTR
char *strcasestr(const char *haystack, const char *needle);
#endif

#ifndef HAVE_FTRUNCATE
int ftruncate(int f,long l);
#endif

#ifndef HAVE_VASPRINTF_DECL
int vasprintf(char **ptr, const char *format, va_list ap);
#endif

#if !defined(HAVE_BZERO) && defined(HAVE_MEMSET)
#define bzero(a,b) memset((a),'\0',(b))
#endif

#ifndef PRINTF_ATTRIBUTE
#if __GNUC__ >= 3
/** Use gcc attribute to check printf fns.  a1 is the 1-based index of
 * the parameter containing the format, and a2 the index of the first
 * argument. Note that some gcc 2.x versions don't handle this
 * properly **/
#define PRINTF_ATTRIBUTE(a1, a2) __attribute__ ((format (__printf__, a1, a2)))
#else
#define PRINTF_ATTRIBUTE(a1, a2)
#endif
#endif

/* add varargs prototypes with printf checking */
#ifndef HAVE_SNPRINTF_DECL
int snprintf(char *,size_t ,const char *, ...) PRINTF_ATTRIBUTE(3,4);
#endif
#ifndef HAVE_ASPRINTF_DECL
int asprintf(char **,const char *, ...) PRINTF_ATTRIBUTE(2,3);
#endif


/* we used to use these fns, but now we have good replacements
   for snprintf and vsnprintf */
#define slprintf snprintf


#ifndef HAVE_VA_COPY
#ifdef HAVE___VA_COPY
#define va_copy(dest, src) __va_copy(dest, src)
#else
#define va_copy(dest, src) (dest) = (src)
#endif
#endif

#ifndef HAVE_VOLATILE
#define volatile
#endif

#ifndef HAVE_COMPARISON_FN_T
typedef int (*comparison_fn_t)(const void *, const void *);
#endif

/* Load header file for dynamic linking stuff */
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#ifndef RTLD_LAZY
#define RTLD_LAZY 0
#endif

#ifndef HAVE_SECURE_MKSTEMP
#define mkstemp(path) rep_mkstemp(path)
int rep_mkstemp(char *temp);
#endif

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

/* The extra casts work around common compiler bugs.  */
#define _TYPE_SIGNED(t) (! ((t) 0 < (t) -1))
/* The outer cast is needed to work around a bug in Cray C 5.0.3.0.
   It is necessary at least when t == time_t.  */
#define _TYPE_MINIMUM(t) ((t) (_TYPE_SIGNED (t) \
  			      ? ~ (t) 0 << (sizeof (t) * CHAR_BIT - 1) : (t) 0))
#define _TYPE_MAXIMUM(t) ((t) (~ (t) 0 - _TYPE_MINIMUM (t)))

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 64
#endif

#ifndef UINT16_MAX
#define UINT16_MAX 65535
#endif

#ifndef UINT32_MAX
#define UINT32_MAX (4294967295U)
#endif

#ifndef UINT64_MAX
#define UINT64_MAX ((uint64_t)-1)
#endif

#ifndef CHAR_BIT
#define CHAR_BIT 8
#endif

#ifndef INT32_MAX
#define INT32_MAX _TYPE_MAXIMUM(int32_t)
#endif

#ifdef HAVE_STDBOOL_H
#include <stdbool.h>
#endif

#ifndef HAVE_BOOL
#define __bool_true_false_are_defined
typedef int bool;
#define false (0)
#define true (1)
#endif

#ifndef HAVE_FUNCTION_MACRO
#define __FUNCTION__ ("")
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif



#endif
