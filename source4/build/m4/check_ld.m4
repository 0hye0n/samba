dnl SMB Build Environment LD Checks
dnl -------------------------------------------------------
dnl  Copyright (C) Stefan (metze) Metzmacher 2004
dnl  Copyright (C) Jelmer Vernooij 2006
dnl  Released under the GNU GPL
dnl -------------------------------------------------------
dnl

AC_PATH_PROG(PROG_LD,ld)
LD=${PROG_LD}
AC_PROG_LD_GNU
LD=""
AC_PATH_PROG(PROG_AR, ar)

AC_SUBST(STLD)
AC_SUBST(STLD_FLAGS)
AC_SUBST(BLDSHARED)
AC_SUBST(LD)
AC_SUBST(LDFLAGS)
AC_SUBST(SHLD)
AC_SUBST(SHLD_UNDEF_FLAGS)
AC_SUBST(SONAMEFLAG)

# Assume non-shared by default and override below
# these are the defaults, good for lots of systems
STLD=${PROG_AR}
STLD_FLAGS="-rcs"
BLDSHARED="false"
LD="${CC}"
SHLD="${CC}"
SONAMEFLAG=""
PICFLAG=""

# allow for --with-hostld=gcc
AC_ARG_WITH(hostld,[  --with-hostld=linker    choose host linker],
[HOSTLD=$withval],
[
if test z"$cross_compiling" = "yes"; then
	HOSTLD='$(HOSTCC)'
else
	HOSTLD='$(LD)'
fi
])

AC_MSG_CHECKING([whether to try to build shared libraries on $host_os])

# and these are for particular systems
case "$host_os" in
	*linux*)
		BLDSHARED="true"
		SHLD_UNDEF_FLAGS="-Wl,--allow-shlib-undefined"
		LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
		SONAMEFLAG="-Wl,-soname="
		;;
	*solaris*)
		BLDSHARED="true"
		SONAMEFLAG="-h "
		if test "${GCC}" = "yes"; then
			SONAMEFLAG="-Wl,-soname="
			if test "${ac_cv_prog_gnu_ld}" = "yes"; then
				LDFLAGS="$LDFLAGS -Wl,-E"
			fi
		fi
		;;
	*sunos*)
		BLDSHARED="true"
		SONAMEFLAG="-Wl,-h,"
		;;
	*netbsd* | *freebsd* | *dragonfly* )  
		BLDSHARED="true"
		LDFLAGS="$LDFLAGS -Wl,--export-dynamic"
		SONAMEFLAG="-Wl,-soname,"
		;;
	*openbsd*)
		# disabled because us4 fails to link libtorture.so
		BLDSHARED="false"
		LDFLAGS="$LDFLAGS -Wl,-Bdynamic"
		SONAMEFLAG="-Wl,-soname,"
		;;
	*irix*)
		# disabled because us4 fails to link libtorture.so
		BLDSHARED="false"
		SONAMEFLAG="-soname "
		SHLD="${PROG_LD}"
		;;
	*aix*)
		# disabled because us4 fails to link libtorture.so
		BLDSHARED="false"
		LDFLAGS="$LDFLAGS -Wl,-brtl,-bexpall,-bbigtoc"
		;;
	*hpux*)
		# Use special PIC flags for the native HP-UX compiler.
		if test $ac_cv_prog_cc_Ae = yes; then
			BLDSHARED="true"
			SONAMEFLAG="-Wl,+h "
		elif test "${GCC}" = "yes"; then
			BLDSHARED="true" # I hope this is correct
		fi
		if test "$host_cpu" = "ia64"; then
			LDFLAGS="$LDFLAGS -Wl,-E,+b/usr/local/lib/hpux32:/usr/lib/hpux32"
		else
			LDFLAGS="$LDFLAGS -Wl,-E,+b/usr/local/lib:/usr/lib"
		fi
		;;
	*osf*)
		# disabled because tru64 fails to link libtorture.so
		BLDSHARED="false"
		SONAMEFLAG="-Wl,-soname,"
		;;
	*unixware*)
		BLDSHARED="true"
		SONAMEFLAG="-Wl,-soname,"
		;;
	*darwin*)
		BLDSHARED="true"
		;;
esac

AC_MSG_RESULT($BLDSHARED)

AC_MSG_CHECKING([LD])
AC_MSG_RESULT([$LD])
AC_MSG_CHECKING([LDFLAGS])
AC_MSG_RESULT([$LDFLAGS])

AC_SUBST(HOSTLD)

AC_MSG_CHECKING([STLD])
AC_MSG_RESULT([$STLD])
AC_MSG_CHECKING([STLD_FLAGS])
AC_MSG_RESULT([$STLD_FLAGS])

AC_LD_PICFLAG
AC_LD_EXPORT_DYNAMIC
AC_LD_SHLDFLAGS
AC_LD_SHLIBEXT

AC_ARG_ENABLE(shared,
[  --disable-shared        Disable testing for building shared libraries],
[],[enable_shared=yes])

if test x"$enable_shared" = x"no" -o x"$enable_shared" = x"false"; then
	BLDSHARED=false
fi

#######################################################
# test whether building a shared library actually works
if test $BLDSHARED = true; then

	AC_MSG_CHECKING([SHLD])
	AC_MSG_RESULT([$SHLD])
	AC_MSG_CHECKING([SHLD_FLAGS])
	AC_MSG_RESULT([$SHLD_FLAGS])

	AC_MSG_CHECKING([SHLIBEXT])
	AC_MSG_RESULT([$SHLIBEXT])
	AC_MSG_CHECKING([SONAMEFLAG])
	AC_MSG_RESULT([$SONAMEFLAG])

	AC_MSG_CHECKING([PICFLAG])
	AC_MSG_RESULT([$PICFLAG])

	AC_CACHE_CHECK([whether building shared libraries actually works], 
	               [ac_cv_shlib_works],[
			ac_cv_shlib_works=no
			# try building a trivial shared library
			${CC} ${CFLAGS} ${PICFLAG} -c ${srcdir-.}/build/tests/shlib.c -o shlib.o &&
				${SHLD} `eval echo ${SHLD_FLAGS} ` -o shlib.${SHLIBEXT} shlib.o && 
				ac_cv_shlib_works=yes
			rm -f shlib.${SHLIBEXT} shlib.o
	])
	if test $ac_cv_shlib_works = no; then
		BLDSHARED=false
	fi
fi

if test $BLDSHARED != true; then
	SHLD="shared-libraries-disabled"
	SHLD_FLAGS="shared-libraries-disabled"
	SHLIBEXT="shared_libraries_disabled"
	SONAMEFLAG="shared-libraries-disabled"
	PICFLAG=""
	AC_MSG_CHECKING([SHLD])
	AC_MSG_RESULT([$SHLD])
	AC_MSG_CHECKING([SHLD_FLAGS])
	AC_MSG_RESULT([$SHLD_FLAGS])

	AC_MSG_CHECKING([SHLIBEXT])
	AC_MSG_RESULT([$SHLIBEXT])
	AC_MSG_CHECKING([SONAMEFLAG])
	AC_MSG_RESULT([$SONAMEFLAG])

	AC_MSG_CHECKING([PICFLAG])
	AC_MSG_RESULT([$PICFLAG])
fi

AC_DEFINE_UNQUOTED(SHLIBEXT, "$SHLIBEXT", [Shared library extension])

AC_MSG_CHECKING([if we can link using the selected flags])
AC_TRY_RUN([#include "${srcdir-.}/build/tests/trivial.c"],
	    AC_MSG_RESULT(yes),
	    AC_MSG_ERROR([we cannot link with the selected cc and ld flags. Aborting configure]),
	    AC_MSG_WARN([cannot run when cross-compiling]))


USESHARED=false
AC_SUBST(USESHARED)

AC_ARG_ENABLE(dso,
[  --enable-dso 	          Enable using shared libraries internally (experimental)],
[],[enable_dso=no])

if test x"$enable_dso" = x"yes" -a x"$BLDSHARED" != x"true"; then
	AC_MSG_ERROR([--enable-dso: no support for shared libraries])
fi

if test x"$enable_dso" != x"no"; then
	USESHARED=$BLDSHARED
fi

AC_MSG_CHECKING([if binaries will use shared libraries])
AC_MSG_RESULT([$USESHARED])
