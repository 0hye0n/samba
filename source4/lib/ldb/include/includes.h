#ifndef _LDB_PRIVATE_INCLUDES_H_
#define _LDB_PRIVATE_INCLUDES_H_
/*
  a temporary includes file until I work on the ldb build system
*/

#ifdef _SAMBA_BUILD_

#include "system/filesys.h"
#include "system/locale.h"
#include "system/time.h"

/* tell ldb we have the internal ldap code */
#define HAVE_ILDAP 1

#else /*_SAMBA_BUILD_*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif
#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#define discard_const(ptr) ((void *)((intptr_t)(ptr)))
#define discard_const_p(type, ptr) ((type *)discard_const(ptr))
#ifndef HAVE_COMPARISON_FN_T
typedef int (*comparison_fn_t)(const void *, const void *);
#endif

#include "talloc.h"

#endif /*_SAMBA_BUILD_*/

#include "ldb.h"
#include "ldb_errors.h"
#include "ldb_private.h"
#include "dlinklist.h"

#endif /*_LDB_PRIVATE_INCLUDES_H_*/
