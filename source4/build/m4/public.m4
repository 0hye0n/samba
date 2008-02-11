dnl SMB Build System
dnl ----------------
dnl Copyright (C) 2004 Stefan Metzmacher
dnl Copyright (C) 2004-2005 Jelmer Vernooij
dnl Published under the GPL
dnl
dnl SMB_SUBSYSTEM(name,obj_files,required_subsystems)
dnl
dnl SMB_EXT_LIB_FROM_PKGCONFIG(name,pkg-config name,[ACTION-IF-FOUND],[ACTION-IF-NOT-FOUND])
dnl
dnl SMB_EXT_LIB(name,libs,cflags,cppflags,ldflags,pcname)
dnl
dnl SMB_ENABLE(name,default_build)
dnl
dnl SMB_INCLUDE_MK(file)
dnl
dnl #######################################################
dnl ### And now the implementation			###
dnl #######################################################

dnl SMB_SUBSYSTEM(name,obj_files,required_subsystems,cflags)
AC_DEFUN([SMB_SUBSYSTEM],
[
MAKE_SETTINGS="$MAKE_SETTINGS
$1_OBJ_FILES = $2
$1_CFLAGS = $4
"

SMB_INFO_SUBSYSTEMS="$SMB_INFO_SUBSYSTEMS
###################################
# Start Subsystem $1
@<:@SUBSYSTEM::$1@:>@
OBJ_FILES = \$($1_OBJ_FILES)
PRIVATE_DEPENDENCIES = $3
CFLAGS = $4
ENABLE = YES
# End Subsystem $1
###################################
"
])

dnl SMB_LIBRARY(name,obj_files,required_subsystems,version,so_version,cflags,ldflags,pcname)
AC_DEFUN([SMB_LIBRARY],
[
MAKE_SETTINGS="$MAKE_SETTINGS
$1_OBJ_FILES = $2
$1_CFLAGS = $6
$1_LDFLAGS = $7
"

SMB_INFO_LIBRARIES="$SMB_INFO_LIBRARIES
###################################
# Start Library $1
@<:@LIBRARY::$1@:>@
OBJ_FILES = \$($1_OBJ_FILES)
PRIVATE_DEPENDENCIES = $3
VERSION = $4
SO_VERSION = $5 
CFLAGS = $6
LDFLAGS = \$($1_LDFLAGS)
PC_NAME = $8
ENABLE = YES
# End Library $1
###################################
"
])

dnl SMB_EXT_LIB_FROM_PKGCONFIG(name,pkg-config name,[ACTION-IF-FOUND],[ACTION-IF-NOT-FOUND])
AC_DEFUN([SMB_EXT_LIB_FROM_PKGCONFIG], 
[
	dnl Figure out the correct variables and call SMB_EXT_LIB()

	if test -z "$PKG_CONFIG"; then
		AC_PATH_PROG(PKG_CONFIG, pkg-config, no)
	fi

	if test "$PKG_CONFIG" = "no" ; then
		echo "*** The pkg-config script could not be found. Make sure it is"
		echo "*** in your path, or set the PKG_CONFIG environment variable"
		echo "*** to the full path to pkg-config."
		echo "*** Or see http://pkg-config.freedesktop.org/ to get pkg-config."
			ac_cv_$1_found=no
	else
		if $PKG_CONFIG --atleast-pkgconfig-version 0.9.0; then
			AC_MSG_CHECKING(for $2)

			if $PKG_CONFIG --exists '$2' ; then
				AC_MSG_RESULT(yes)

				$1_CFLAGS="`$PKG_CONFIG --cflags '$2'`"
				OLD_CFLAGS="$CFLAGS"
				CFLAGS="$CFLAGS $$1_CFLAGS"
				AC_MSG_CHECKING([that the C compiler can use the $1_CFLAGS])
				AC_TRY_RUN([#include "${srcdir-.}/build/tests/trivial.c"],
					SMB_ENABLE($1, YES)
					AC_MSG_RESULT(yes),
					AC_MSG_RESULT(no),
					AC_MSG_WARN([cannot run when cross-compiling]))
				CFLAGS="$OLD_CFLAGS"

				SMB_EXT_LIB($1, 
					[`$PKG_CONFIG --libs-only-l '$2'`], 
					[`$PKG_CONFIG --cflags-only-other '$2'`],
					[`$PKG_CONFIG --cflags-only-I '$2'`],
					[`$PKG_CONFIG --libs-only-other '$2'` `$PKG_CONFIG --libs-only-L '$2'`],
					[ $2 ])
				ac_cv_$1_found=yes

			else
				AC_MSG_RESULT(no)
				$PKG_CONFIG --errors-to-stdout --print-errors '$2'
				ac_cv_$1_found=no
			fi
		else
			echo "*** Your version of pkg-config is too old. You need version $PKG_CONFIG_MIN_VERSION or newer."
			echo "*** See http://pkg-config.freedesktop.org/"
			ac_cv_$1_found=no
		fi
	fi
	if test x$ac_cv_$1_found = x"yes"; then
		ifelse([$3], [], [echo -n ""], [$3])
	else
		ifelse([$4], [], [
			  SMB_EXT_LIB($1)
			  SMB_ENABLE($1, NO)
		], [$4])
	fi
])

dnl SMB_INCLUDE_MK(file)
AC_DEFUN([SMB_INCLUDE_MK],
[
SMB_INFO_EXT_LIBS="$SMB_INFO_EXT_LIBS
include $1
"
])

dnl SMB_EXT_LIB(name,libs,cflags,cppflags,ldflags,pcname)
AC_DEFUN([SMB_EXT_LIB],
[
MAKE_SETTINGS="$MAKE_SETTINGS
$1_LIBS = $2
$1_CFLAGS = $3
$1_CPPFLAGS = $4
$1_LDFLAGS = $5
"

SMB_INFO_EXT_LIBS="$SMB_INFO_EXT_LIBS
###################################
# Start Ext Lib $1
@<:@EXT_LIB::$1@:>@
LIBS = \$($1_LIBS)
CFLAGS = $3
CPPFLAGS = $4
LDFLAGS = \$($1_LDFLAGS)
PC_NAME = $6
# End Ext Lib $1
###################################
"
])

dnl SMB_ENABLE(name,default_build)
AC_DEFUN([SMB_ENABLE],
[
	[SMB_ENABLE_][$1]="$2";

SMB_INFO_ENABLES="$SMB_INFO_ENABLES
\$enabled{$1} = \"$2\";"
])
