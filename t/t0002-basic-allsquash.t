#!/bin/sh

test_description='A few easy tests for diod in "allsquash" mode

Note: this test runs even with
  --disable-auth --disable-config --disable-multiuser
'

. `dirname $0`/sharness.sh

diodcat=$SHARNESS_BUILD_DIRECTORY/src/cmd/diodcat
diodls=$SHARNESS_BUILD_DIRECTORY/src/cmd/diodls
diodload=$SHARNESS_BUILD_DIRECTORY/src/cmd/diodload

test_expect_success 'create export dir' '
	mkdir -p net &&
	echo TESTING >net/a &&
	chmod 600 net/a &&
	exportdir=$(pwd)/net
'

test_expect_success 'start diod in allsquash mode with implied squashuser' '
	diod_start \
	    --config-file=/dev/null \
	    --no-auth \
	    --export=ctl \
	    --export=$exportdir \
	    --allsquash
'

test_expect_success 'the squash user can access ctl:/version' '
	$diodcat --server=$DIOD_SOCKET --aname=ctl version
'

test_expect_success SUDO 'the root user can access ctl:/version' '
	$SUDO $diodcat --server=$DIOD_SOCKET --aname=ctl version
'

test_expect_success NOBODY 'the nobody user can access ctl:/version' '
	$SUDO -u nobody $diodcat --server=$DIOD_SOCKET --aname=ctl version
'

test_expect_success 'the squash user can access net:/a' '
	$diodcat --server=$DIOD_SOCKET --aname=$exportdir /a
'
test_expect_success SUDO 'the root user can access net:/a' '
	$SUDO $diodcat --server=$DIOD_SOCKET --aname=$exportdir /a
'
test_expect_success NOBODY 'the nobody user can access net:/a' '
	$SUDO -u nobody $diodcat --server=$DIOD_SOCKET --aname=$exportdir /a
'

test_expect_success 'stop diod' '
	diod_term $DIOD_SOCKET
'

test_done

# vi: set ft=sh
