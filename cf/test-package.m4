dnl $Id$
dnl
dnl AC_TEST_PACKAGE(package,header,lib,linkline)
AC_DEFUN(AC_TEST_PACKAGE,
[
AC_MSG_CHECKING(for $1)
AC_ARG_WITH($1,
[  --with-$1=dir                use $1 in dir],
[if test "$with_$1" = "no"; then
  with_$1=
fi]
)
AC_ARG_WITH($1-lib,
[  --with-$1-lib=dir            use $1-lib in dir],
[if test "$withval" = "yes" -o "$withval" = "no"; then
  AC_MSG_ERROR([No argument for --with-$1-lib])
elif test "X$with_$1" = "X"; then
  with_$1=yes
fi]
)
AC_ARG_WITH($1-include,
[  --with-$1-include=dir        use $1-include in dir],
[if test "$withval" = "yes" -o "$withval" = "no"; then
  AC_MSG_ERROR([No argument for --with-$1-include])
elif test "X$with_$1" = "X"; then
  with_$1=yes
fi]
)

define([foo], translit($1, [a-z], [A-Z]))
: << END
@@@syms="$syms foo"@@@
END

if test -n "$with_$1"; then
  AC_DEFINE([foo])
  if test "$with_$1" != "yes"; then
    $1_dir=$with_$1
  fi
dnl Try to find include
  if test -n "$with_$1_include"; then
    trydir=$with_$1_include
  elif test "$with_$1" != "yes"; then
    trydir="$with_$1 $with_$1/include"
  else
    trydir=
  fi
  found=
  for i in $trydir ""; do
    if test -n "$i"; then
      if test -f $i/$2; then
        found=yes; res=$i; break
      fi
    else
      AC_TRY_CPP([#include <$2>], [found=yes; res=$i; break])
    fi
  done
  if test -n "$found"; then
    $1_include=$res
  else
    AC_MSG_ERROR(Cannot find $2)
  fi
dnl Try to find lib
  if test -n "$with_$1_lib"; then
    trydir=$with_$1_lib
  elif test "$with_$1" != "yes"; then
    trydir="$with_$1 $with_$1/lib"
  else
    trydir=
  fi
  found=
  for i in $trydir ""; do
    if test -n "$i"; then
      if test -f $i/$3; then
        found=yes; res=$i; break
      fi
    else
      old_LIBS=$LIBS
      LIBS="$4 $LIBS"
      AC_TRY_LINK([], [], [found=yes; res=$i; LIBS=$old_LIBS; break])
      LIBS=$old_LIBS
    fi
  done
  if test -n "$found"; then
    $1_lib=$res
  else
    AC_MSG_ERROR(Cannot find $3)
  fi
  AC_MSG_RESULT([headers $$1_include, libraries $$1_lib])
  AC_DEFINE_UNQUOTED(foo)
  if test -n "$$1_include"; then
    foo[INCLUDE]="-I$$1_include"
  fi
  AC_SUBST(foo[INCLUDE])
  if test -n "$$1_lib"; then
    foo[LIB]="-L$$1_lib"
  fi
  foo[LIB]="$foo[LIB] $4"
  AC_SUBST(foo[LIB])
else
  AC_MSG_RESULT(no)
fi
undefine([foo])
])

