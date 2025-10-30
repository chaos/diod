#!/bin/sh

test_description='A few easy tests for diod in "allsquash" mode

Note: this test runs even with
  --disable-auth --disable-config --disable-multiuser
'

. `dirname $0`/sharness.sh

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
	$PATH_DIODCLI --aname=ctl read version
'

test_expect_success SUDO 'the root user can access ctl:/version' '
	$SUDO -E $PATH_DIODCLI --aname=ctl read version
'

test_expect_success NOBODY 'the nobody user can access ctl:/version' '
	$SUDO -E -u nobody $PATH_DIODCLI  --aname=ctl read version
'

test_expect_success 'the squash user can access net:/a' '
	$PATH_DIODCLI --aname=$exportdir read /a
'
test_expect_success SUDO 'the root user can access net:/a' '
	$SUDO -E $PATH_DIODCLI --aname=$exportdir read /a
'
test_expect_success NOBODY 'the nobody user can access net:/a' '
	$SUDO -E -u nobody $PATH_DIODCLI --aname=$exportdir read /a
'

test_expect_success 'stop diod' '
	diod_term $DIOD_SOCKET
'

test_done

# vi: set ft=sh
