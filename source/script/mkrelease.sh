#!/bin/sh

TMPDIR=`mktemp samba-XXXXX`
rm $TMPDIR || exit 1
svn export . $TMPDIR || exit 1

( cd $TMPDIR/source
 ./autogen.sh || exit 1
 ./configure || exit 1
 make dist  || exit 1
) || exit 1

VERSION=`sed -n 's/^SAMBA_VERSION_STRING=//p' $TMPDIR/source/version.h`
mv $TMPDIR samba-$VERSION || exit 1
tar -cf samba-$VERSION.tar samba-$VERSION || exit 1
echo "Now run: "
echo "gpg --detach-sign --armor samba-$VERSION.tar"
echo "gzip samba-$VERSION.tar" 
echo "And then upload "
echo "samba-$VERSION.tar.gz samba-$VERSION.tar.asc" 
echo "to pub/samba/samba4/ on samba.org"



