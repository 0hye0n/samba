#!/bin/sh
# Bootstrap Samba and run a number of tests against it.

DOMAIN=SAMBADOMAIN
USERNAME=administrator
REALM=SAMBA.EXAMPLE.COM
PASSWORD=penguin
SRCDIR=`pwd`
ROOT=$USER
if test -z "$ROOT"; then
    ROOT=$LOGNAME
fi
if test -z "$ROOT"; then
    ROOT=`whoami`
fi

if [ $# -lt 1 ]
then
	echo "$0 PREFIX"
	exit
fi


PREFIX=$1
PREFIX=`echo $PREFIX | sed s+//+/+`
export PREFIX

# allow selection of the test lists
TESTS=$2

if [ $TESTS = "all" ]; then
    TLS_ENABLED="yes"
else
    TLS_ENABLED="no"
fi

mkdir -p $PREFIX || exit $?
OLD_PWD=`pwd`
cd $PREFIX || exit $?
PREFIX_ABS=`pwd`
export PREFIX_ABS
cd $OLD_PWD

TMPDIR=$PREFIX_ABS/tmp
LIBDIR=$PREFIX_ABS/lib
PIDDIR=$PREFIX_ABS/pid
CONFFILE=$LIBDIR/smb.conf
KRB5_CONFIG=$LIBDIR/krb5.conf
PRIVATEDIR=$PREFIX_ABS/private
NCALRPCDIR=$PREFIX_ABS/ncalrpc
LOCKDIR=$PREFIX_ABS/lockdir
TLSDIR=$PRIVATEDIR/tls
CONFIGURATION="--configfile=$CONFFILE"
export CONFIGURATION
export CONFFILE

SMBD_TEST_FIFO="$PREFIX/smbd_test.fifo"
export SMBD_TEST_FIFO
SMBD_TEST_LOG="$PREFIX/smbd_test.log"
export SMBD_TEST_LOG

DO_SOCKET_WRAPPER=$3
if [ x"$DO_SOCKET_WRAPPER" = x"SOCKET_WRAPPER" ];then
	SOCKET_WRAPPER_DIR="$PREFIX/sw"
	export SOCKET_WRAPPER_DIR
	echo "SOCKET_WRAPPER_DIR=$SOCKET_WRAPPER_DIR"
fi

# start off with 0 failures
failed=0
export failed

incdir=`dirname $0`
. $incdir/test_functions.sh

PATH=bin:$PATH
export PATH

rm -rf $PREFIX/*
mkdir -p $PRIVATEDIR $LIBDIR $PIDDIR $NCALRPCDIR $LOCKDIR $TMPDIR $TLSDIR

cat >$CONFFILE<<EOF
[global]
	netbios name = LOCALHOST
	workgroup = $DOMAIN
	realm = $REALM
	private dir = $PRIVATEDIR
	pid directory = $PIDDIR
	ncalrpc dir = $NCALRPCDIR
	lock dir = $LOCKDIR
	setup directory = $SRCDIR/setup
	js include = $SRCDIR/scripting/libjs
	name resolve order = bcast
	interfaces = lo*
	tls enabled = $TLS_ENABLED
	panic action = $SRCDIR/script/gdb_backtrace %PID% %PROG%
	wins support = yes
	server role = pdc

[tmp]
	path = $TMPDIR
	read only = no
	ntvfs handler = posix
	posix:sharedelay = 100000
	posix:eadb = $LOCKDIR/eadb.tdb

[cifs]
	read only = no
	ntvfs handler = cifs
	cifs:server = localhost
	cifs:user = $USERNAME
	cifs:password = $PASSWORD
	cifs:domain = $DOMAIN
	cifs:share = tmp
EOF

cat >$KRB5_CONFIG<<EOF
[libdefaults]
 default_realm = SAMBA.EXAMPLE.COM
 dns_lookup_realm = false
 dns_lookup_kdc = false
 ticket_lifetime = 24h
 forwardable = yes

[realms]
 SAMBA.EXAMPLE.COM = {
  kdc = 127.0.0.1
  admin_server = 127.0.0.1
  default_domain = samba.example.com
 }
[domain_realm]
 .samba.example.com = SAMBA.EXAMPLE.COM
EOF

export KRB5_CONFIG

./setup/provision $CONFIGURATION --quiet --domain $DOMAIN --realm $REALM \
    --adminpass $PASSWORD --root=$ROOT || exit 1

if [ x"$RUN_FROM_BUILD_FARM" = x"yes" ];then
	CONFIGURATION="$CONFIGURATION --option=\"torture:progress=no\""
fi

smbd_check_or_start

# ensure any one smbtorture call doesn't run too long
TORTURE_OPTIONS="--maximum-runtime=300 $CONFIGURATION"
export TORTURE_OPTIONS


START=`date`
(
 # give time for nbt server to register its names
 echo delaying for nbt name registration
 sleep 4
 bin/nmblookup $CONFIGURATION -U localhost localhost 

 failed=0

 . script/tests/tests_$TESTS.sh
 exit $failed
) 9>$SMBD_TEST_FIFO
failed=$?

kill `cat $PIDDIR/smbd.pid`

END=`date`
echo "START: $START ($0)";
echo "END:   $END ($0)";

# if there were any valgrind failures, show them
count=`find $PREFIX -name 'valgrind.log*' | wc -l`
if [ "$count" != 0 ]; then
    for f in $PREFIX/valgrind.log*; do
	if [ -s $f ]; then
	    echo "VALGRIND FAILURE";
	    failed=`expr $failed + 1`
	    cat $f
	fi
    done
fi

teststatus $0 $failed
