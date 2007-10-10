#!/bin/sh

plantest() {
	name=$1
	env=$2
	shift 2
	cmdline="$*"
	echo "-- TEST --"
	echo $name
	echo $env
	echo $cmdline
}

samba4srcdir=$incdir/..
samba4bindir=$samba4srcdir/bin
