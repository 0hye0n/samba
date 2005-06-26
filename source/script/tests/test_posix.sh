#!/bin/sh

# this runs the file serving tests that are expected to pass with the
# current posix ntvfs backend

if [ $# -lt 3 ]; then
cat <<EOF
Usage: test_posix.sh UNC USERNAME PASSWORD <first> <smbtorture args>
EOF
exit 1;
fi

unc="$1"
username="$2"
password="$3"
start="$4"
shift 4
ADDARGS="$*"

incdir=`dirname $0`
. $incdir/test_functions.sh

tests="BASE-FDPASS BASE-LOCK1 BASE-LOCK2 BASE-LOCK3 BASE-LOCK4"
tests="$tests BASE-LOCK5 BASE-LOCK6 BASE-LOCK7 BASE-UNLINK BASE-ATTR"
tests="$tests BASE-NEGNOWAIT BASE-DIR1 BASE-DIR2 BASE-VUID"
tests="$tests BASE-DENY1 BASE-DENY2 BASE-TCON BASE-TCONDEV BASE-RW1"
tests="$tests BASE-DENY3 BASE-XCOPY BASE-OPEN BASE-DENYDOS"
tests="$tests BASE-DELETE BASE-PROPERTIES BASE-MANGLE"
tests="$tests BASE-CHKPATH BASE-SECLEAK BASE-TRANS2"
tests="$tests BASE-NTDENY1 BASE-NTDENY2  BASE-RENAME BASE-OPENATTR"
tests="$tests RAW-QFSINFO RAW-QFILEINFO RAW-SFILEINFO-BUG RAW-SFILEINFO"
tests="$tests RAW-LOCK RAW-MKDIR RAW-SEEK RAW-CONTEXT RAW-MUX RAW-OPEN RAW-WRITE"
tests="$tests RAW-UNLINK RAW-READ RAW-CLOSE RAW-IOCTL RAW-SEARCH RAW-CHKPATH RAW-RENAME"
tests="$tests RAW-EAS RAW-STREAMS RAW-ACLS"

soon="BASE-CHARSET RAW-OPLOCK RAW-NOTIFY BASE-DELAYWRITE"

failed=0
for t in $tests; do
    if [ ! -z "$start" -a "$start" != $t ]; then
	continue;
    fi
    start=""
    name="$t"
    testit "$name" $VALGRIND bin/smbtorture $TORTURE_OPTIONS $ADDARGS $unc -U"$username"%"$password" $t || failed=`expr $failed + 1`
done

testok $0 $failed
