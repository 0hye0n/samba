dnl test whether dirent has a d_off member
AC_DEFUN(AC_DIRENT_D_OFF,
[AC_CACHE_CHECK([for d_off in dirent], ac_cv_dirent_d_off,
[AC_TRY_COMPILE([
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>], [struct dirent d; d.d_off;],
ac_cv_dirent_d_off=yes, ac_cv_dirent_d_off=no)])
if test $ac_cv_dirent_d_off = yes; then
  AC_DEFINE(HAVE_DIRENT_D_OFF,1,[Whether dirent has a d_off member])
fi
])

dnl AC_PROG_CC_FLAG(flag)
AC_DEFUN(AC_PROG_CC_FLAG,
[AC_CACHE_CHECK(whether ${CC-cc} accepts -$1, ac_cv_prog_cc_$1,
[echo 'void f(){}' > conftest.c
if test -z "`${CC-cc} -$1 -c conftest.c 2>&1`"; then
  ac_cv_prog_cc_$1=yes
else
  ac_cv_prog_cc_$1=no
fi
rm -f conftest*
])])

dnl see if a declaration exists for a function or variable
dnl defines HAVE_function_DECL if it exists
dnl AC_HAVE_DECL(var, includes)
AC_DEFUN(AC_HAVE_DECL,
[
 AC_CACHE_CHECK([for $1 declaration],ac_cv_have_$1_decl,[
    AC_TRY_COMPILE([$2],[int i = (int)$1],
        ac_cv_have_$1_decl=yes,ac_cv_have_$1_decl=no)])
 if test x"$ac_cv_have_$1_decl" = x"yes"; then
    AC_DEFINE([HAVE_]translit([$1], [a-z], [A-Z])[_DECL],1,[Whether $1() is available])
 fi
])


# AC_CHECK_LIB_EXT(LIBRARY, [EXT_LIBS], [FUNCTION],
#              [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND],
#              [ADD-ACTION-IF-FOUND],[OTHER-LIBRARIES])
# ------------------------------------------------------
#
# Use a cache variable name containing both the library and function name,
# because the test really is for library $1 defining function $3, not
# just for library $1.  Separate tests with the same $1 and different $3s
# may have different results.
#
# Note that using directly AS_VAR_PUSHDEF([ac_Lib], [ac_cv_lib_$1_$3])
# is asking for trouble, since AC_CHECK_LIB($lib, fun) would give
# ac_cv_lib_$lib_fun, which is definitely not what was meant.  Hence
# the AS_LITERAL_IF indirection.
#
# FIXME: This macro is extremely suspicious.  It DEFINEs unconditionally,
# whatever the FUNCTION, in addition to not being a *S macro.  Note
# that the cache does depend upon the function we are looking for.
#
# It is on purpose we used `ac_check_lib_ext_save_LIBS' and not just
# `ac_save_LIBS': there are many macros which don't want to see `LIBS'
# changed but still want to use AC_CHECK_LIB_EXT, so they save `LIBS'.
# And ``ac_save_LIBS' is too tempting a name, so let's leave them some
# freedom.
AC_DEFUN([AC_CHECK_LIB_EXT],
[
AH_CHECK_LIB_EXT([$1])
ac_check_lib_ext_save_LIBS=$LIBS
LIBS="-l$1 $$2 $7 $LIBS"
AS_LITERAL_IF([$1],
      [AS_VAR_PUSHDEF([ac_Lib_ext], [ac_cv_lib_ext_$1])],
      [AS_VAR_PUSHDEF([ac_Lib_ext], [ac_cv_lib_ext_$1''])])dnl

m4_ifval([$3],
 [
    AH_CHECK_FUNC_EXT([$3])
    AS_LITERAL_IF([$1],
              [AS_VAR_PUSHDEF([ac_Lib_func], [ac_cv_lib_ext_$1_$3])],
              [AS_VAR_PUSHDEF([ac_Lib_func], [ac_cv_lib_ext_$1''_$3])])dnl
    AC_CACHE_CHECK([for $3 in -l$1], ac_Lib_func,
	[AC_TRY_LINK_FUNC($3,
                 [AS_VAR_SET(ac_Lib_func, yes);
		  AS_VAR_SET(ac_Lib_ext, yes)],
                 [AS_VAR_SET(ac_Lib_func, no);
		  AS_VAR_SET(ac_Lib_ext, no)])
	])
    AS_IF([test AS_VAR_GET(ac_Lib_func) = yes],
        [AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_$3))])dnl
    AS_VAR_POPDEF([ac_Lib_func])dnl
 ],[
    AC_CACHE_CHECK([for -l$1], ac_Lib_ext,
	[AC_TRY_LINK_FUNC([main],
                 [AS_VAR_SET(ac_Lib_ext, yes)],
                 [AS_VAR_SET(ac_Lib_ext, no)])
	])
 ])
LIBS=$ac_check_lib_ext_save_LIBS

AS_IF([test AS_VAR_GET(ac_Lib_ext) = yes],
    [m4_default([$4], 
        [AC_DEFINE_UNQUOTED(AS_TR_CPP(HAVE_LIB$1))
		case "$$2" in
		    *-l$1*)
			;;
		    *)
			$2="-l$1 $$2"
			;;
		esac])
		[$6]
	    ],
	    [$5])dnl
AS_VAR_POPDEF([ac_Lib_ext])dnl
])# AC_CHECK_LIB_EXT

# AH_CHECK_LIB_EXT(LIBNAME)
# ---------------------
m4_define([AH_CHECK_LIB_EXT],
[AH_TEMPLATE(AS_TR_CPP(HAVE_LIB$1),
             [Define to 1 if you have the `]$1[' library (-l]$1[).])])

dnl AC_SEARCH_LIBS_EXT(FUNCTION, SEARCH-LIBS, EXT_LIBS,
dnl                    [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND],
dnl                    [OTHER-LIBRARIES])
dnl --------------------------------------------------------
dnl Search for a library defining FUNC, if it's not already available.
AC_DEFUN([AC_SEARCH_LIBS_EXT],
[AC_CACHE_CHECK([for library containing $1], [ac_cv_search_ext_$1],
[
ac_func_search_ext_save_LIBS=$LIBS
ac_cv_search_ext_$1=no
AC_LINK_IFELSE([AC_LANG_CALL([], [$1])],
	       [ac_cv_search_ext_$1="none required"])
if test "$ac_cv_search_ext_$1" = no; then
  for ac_lib in $2; do
    LIBS="-l$ac_lib $$3 $6 $ac_func_search_save_ext_LIBS"
    AC_LINK_IFELSE([AC_LANG_CALL([], [$1])],
		   [ac_cv_search_ext_$1="-l$ac_lib"
break])
  done
fi
LIBS=$ac_func_search_ext_save_LIBS])
AS_IF([test "$ac_cv_search_ext_$1" != no],
  [test "$ac_cv_search_ext_$1" = "none required" || $3="$ac_cv_search_ext_$1"
  $4],
      [$5])dnl
])

dnl check for a function in a $LIBS and $OTHER_LIBS libraries variable.
dnl AC_CHECK_FUNC_EXT(func,OTHER_LIBS,IF-TRUE,IF-FALSE)
AC_DEFUN([AC_CHECK_FUNC_EXT],
[
    AH_CHECK_FUNC_EXT($1)	
    ac_check_func_ext_save_LIBS=$LIBS
    LIBS="$2 $LIBS"
    AS_VAR_PUSHDEF([ac_var], [ac_cv_func_ext_$1])dnl
    AC_CACHE_CHECK([for $1], ac_var,
	[AC_LINK_IFELSE([AC_LANG_FUNC_LINK_TRY([$1])],
                [AS_VAR_SET(ac_var, yes)],
                [AS_VAR_SET(ac_var, no)])])
    LIBS=$ac_check_func_ext_save_LIBS
    AS_IF([test AS_VAR_GET(ac_var) = yes], 
	    [AC_DEFINE_UNQUOTED(AS_TR_CPP([HAVE_$1])) $3], 
	    [$4])dnl
AS_VAR_POPDEF([ac_var])dnl
])# AC_CHECK_FUNC

# AH_CHECK_FUNC_EXT(FUNCNAME)
# ---------------------
m4_define([AH_CHECK_FUNC_EXT],
[AH_TEMPLATE(AS_TR_CPP(HAVE_$1),
             [Define to 1 if you have the `]$1[' function.])])

dnl Define an AC_DEFINE with ifndef guard.
dnl AC_N_DEFINE(VARIABLE [, VALUE])
define(AC_N_DEFINE,
[cat >> confdefs.h <<\EOF
[#ifndef] $1
[#define] $1 ifelse($#, 2, [$2], $#, 3, [$2], 1)
[#endif]
EOF
])

dnl Add an #include
dnl AC_ADD_INCLUDE(VARIABLE)
define(AC_ADD_INCLUDE,
[cat >> confdefs.h <<\EOF
[#include] $1
EOF
])

dnl Copied from libtool.m4
AC_DEFUN(AC_PROG_LD_GNU,
[AC_CACHE_CHECK([if the linker ($LD) is GNU ld], ac_cv_prog_gnu_ld,
[# I'd rather use --version here, but apparently some GNU ld's only accept -v.
if $LD -v 2>&1 </dev/null | egrep '(GNU|with BFD)' 1>&5; then
  ac_cv_prog_gnu_ld=yes
else
  ac_cv_prog_gnu_ld=no
fi])
])

dnl Removes -I/usr/include/? from given variable
AC_DEFUN(CFLAGS_REMOVE_USR_INCLUDE,[
  ac_new_flags=""
  for i in [$]$1; do
    case [$]i in
    -I/usr/include|-I/usr/include/) ;;
    *) ac_new_flags="[$]ac_new_flags [$]i" ;;
    esac
  done
  $1=[$]ac_new_flags
])
    
dnl Removes -L/usr/lib/? from given variable
AC_DEFUN(LIB_REMOVE_USR_LIB,[
  ac_new_flags=""
  for i in [$]$1; do
    case [$]i in
    -L/usr/lib|-L/usr/lib/) ;;
    *) ac_new_flags="[$]ac_new_flags [$]i" ;;
    esac
  done
  $1=[$]ac_new_flags
])

dnl From Bruno Haible.

AC_DEFUN(jm_ICONV,
[
  dnl Some systems have iconv in libc, some have it in libiconv (OSF/1 and
  dnl those with the standalone portable libiconv installed).
  AC_MSG_CHECKING(for iconv in $1)
    jm_cv_func_iconv="no"
    jm_cv_lib_iconv=no
    jm_cv_giconv=no
    AC_TRY_LINK([#include <stdlib.h>
#include <giconv.h>],
      [iconv_t cd = iconv_open("","");
       iconv(cd,NULL,NULL,NULL,NULL);
       iconv_close(cd);],
      jm_cv_func_iconv=yes
      jm_cv_giconv=yes)

    if test "$jm_cv_func_iconv" != yes; then
      AC_TRY_LINK([#include <stdlib.h>
#include <iconv.h>],
        [iconv_t cd = iconv_open("","");
         iconv(cd,NULL,NULL,NULL,NULL);
         iconv_close(cd);],
        jm_cv_func_iconv=yes)

        if test "$jm_cv_lib_iconv" != yes; then
          jm_save_LIBS="$LIBS"
          LIBS="$LIBS -lgiconv"
          AC_TRY_LINK([#include <stdlib.h>
#include <giconv.h>],
            [iconv_t cd = iconv_open("","");
             iconv(cd,NULL,NULL,NULL,NULL);
             iconv_close(cd);],
            jm_cv_lib_iconv=yes
            jm_cv_func_iconv=yes
            jm_cv_giconv=yes)
          LIBS="$jm_save_LIBS"

      if test "$jm_cv_func_iconv" != yes; then
        jm_save_LIBS="$LIBS"
        LIBS="$LIBS -liconv"
        AC_TRY_LINK([#include <stdlib.h>
#include <iconv.h>],
          [iconv_t cd = iconv_open("","");
           iconv(cd,NULL,NULL,NULL,NULL);
           iconv_close(cd);],
          jm_cv_lib_iconv=yes
          jm_cv_func_iconv=yes)
        LIBS="$jm_save_LIBS"
        fi
      fi
    fi

  if test "$jm_cv_func_iconv" = yes; then
    if test "$jm_cv_giconv" = yes; then
      AC_DEFINE(HAVE_GICONV, 1, [What header to include for iconv() function: giconv.h])
      AC_MSG_RESULT(yes)
      ICONV_FOUND=yes
    else
      AC_DEFINE(HAVE_ICONV, 1, [What header to include for iconv() function: iconv.h])
      AC_MSG_RESULT(yes)
      ICONV_FOUND=yes
    fi
  else
    AC_MSG_RESULT(no)
  fi
  if test "$jm_cv_lib_iconv" = yes; then
    if test "$jm_cv_giconv" = yes; then
      LIBS="$LIBS -lgiconv"
    else
      LIBS="$LIBS -liconv"
    fi
  fi
])

dnl CFLAGS_ADD_DIR(CFLAGS, $INCDIR)
dnl This function doesn't add -I/usr/include into CFLAGS
AC_DEFUN(CFLAGS_ADD_DIR,[
if test "$2" != "/usr/include" ; then
    $1="$$1 -I$2"
fi
])

dnl LIB_ADD_DIR(LDFLAGS, $LIBDIR)
dnl This function doesn't add -L/usr/lib into LDFLAGS
AC_DEFUN(LIB_ADD_DIR,[
if test "$2" != "/usr/lib" ; then
    $1="$$1 -L$2"
fi
])

sinclude(build/m4/public.m4)
sinclude(build/m4/core.m4)
