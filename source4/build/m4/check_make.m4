dnl SMB Build Environment make Checks
dnl -------------------------------------------------------
dnl  Copyright (C) Stefan (metze) Metzmacher 2004
dnl  Copyright (C) Jelmer Vernooij 2005
dnl  Released under the GNU GPL
dnl -------------------------------------------------------
dnl

AC_DEFUN([AC_SAMBA_GNU_MAKE],
[
AC_CACHE_CHECK([whether we have GNU make], samba_cv_gnu_make, [
AC_PATH_PROGS(MAKE,gmake make)

if ! $ac_cv_path_MAKE --version | head -1 | grep GNU 2>/dev/null >/dev/null
then
	samba_cv_gnu_make=no
else
	samba_cv_gnu_make=yes
fi
])
if test x$samba_cv_gnu_make = xyes; then
	$1
else
	$2
fi
])

AC_SAMBA_GNU_MAKE([AC_MSG_RESULT(found)], [AC_MSG_ERROR([Unable to find GNU make])])

AC_CACHE_CHECK([GNU make version], samba_cv_gnu_make_version,[
		samba_cv_gnu_make_version=`$ac_cv_path_MAKE --version | head -1 | cut -d " " -f 3 2>/dev/null`
	])
	GNU_MAKE_VERSION=$samba_cv_gnu_make_version
	AC_SUBST(GNU_MAKE_VERSION)


new_make=no
AC_MSG_CHECKING([for GNU make >= 3.81])
if $PERL -e " \$_ = '$GNU_MAKE_VERSION'; s/@<:@^\d\.@:>@.*//g; exit (\$_ < 3.81);"; then
	new_make=yes
fi
AC_MSG_RESULT($new_make)
automatic_dependencies=no
AX_CFLAGS_GCC_OPTION([-M -MT conftest.d -MF conftest.o], [], [ automatic_dependencies=$new_make ], [])
AC_MSG_CHECKING([Whether to use automatic dependencies])
AC_ARG_ENABLE(automatic-dependencies,
[  --enable-automatic-dependencies Enable automatic dependencies],
[ automatic_dependencies=$enableval ], 
[ automatic_dependencies=no ])
AC_MSG_RESULT($automatic_dependencies)
AC_SUBST(automatic_dependencies)
