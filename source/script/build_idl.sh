#!/bin/sh

PIDL_ARGS="--outputdir librpc/gen_ndr --header --ndr-parser --samba3-ndr-server --samba3-ndr-client --"
PIDL_EXTRA_ARGS="$*"

oldpwd=`pwd`
cd ${srcdir}

[ -d librpc/gen_ndr ] || mkdir -p librpc/gen_ndr || exit 1

PIDL="pidl ${PIDL_ARGS} ${PIDL_EXTRA_ARGS}"

##
## Find newer files rather than rebuild all of them
##

list=""
for f in ${IDL_FILES}; do
	basename=`basename $f .idl`
	ndr="librpc/gen_ndr/ndr_$basename.c"

	if [ -f $ndr ] && false; then
		if [ "x`find librpc/idl/$f -newer $ndr -print`" = "xlibrpc/idl/$f" ]; then
			list="$list librpc/idl/$f"
		fi
	else 
		list="$list librpc/idl/$f"
	fi
done

##
## generate the ndr stubs
##

if [ "x$list" != x ]; then
	# echo "${PIDL} ${list}"
	$PIDL $list || exit 1
fi

##
## Do miscellaneous cleanup
##

for f in librpc/gen_ndr/ndr_*.c; do
	cat $f | sed -e 's/^static //g' > $f.new
	/bin/mv -f $f.new $f
done

touch librpc/gen_ndr/ndr_dcerpc.h

cd ${oldpwd}

exit 0

