#!/bin/sh
srcdir=$1
LIBDIR=$2
CODEPAGEDIR=$3
BINDIR=$4

shift
shift
shift
shift

echo Installing codepage files in $CODEPAGEDIR
for d in $LIBDIR $CODEPAGEDIR; do
if [ ! -d $d ]; then
mkdir $d
if [ ! -d $d ]; then
  echo Failed to make directory $d
  exit 1
fi
fi
done

for p in $*; do
 echo Creating codepage file $CODEPAGEDIR/codepage.$p
 $BINDIR/make_smbcodepage c $p ${srcdir}/codepages/codepage_def.$p $CODEPAGEDIR/codepage.$p
done


cat << EOF
======================================================================
The code pages have been installed. You may uninstall them using the
command "make uninstallcp" or make "uninstall" to uninstall binaries,
man pages, shell scripts and code pages.
======================================================================
EOF

exit 0

