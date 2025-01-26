#!/bin/sh

test_description='A few easy tests for diod in "multiuser" mode

Note: this test runs even with
  --disable-auth --disable-config
'

. `dirname $0`/sharness.sh

if ! test_have_prereq MULTIUSER; then
        skip_all='diod was configured with --disable-multiuser'
        test_done
fi
if ! test_have_prereq SUDO; then
        skip_all='passwordless sudo is required'
        test_done
fi
if ! test_have_prereq NOBODY; then
        skip_all='the nobody user is required'
        test_done
fi

diodcat=$SHARNESS_BUILD_DIRECTORY/src/cmd/diodcat

test_expect_success 'create export dir' '
	mkdir -p net &&
	chmod 755 net &&
	echo TESTING >net/nobody &&
	$SUDO chown nobody net/nobody &&
	$SUDO chmod 600 net/nobody &&
	echo TESTING >net/user &&
	chmod 600 net/user &&
	echo TESTING >net/root &&
	$SUDO chown root net/root &&
	$SUDO chmod 600 net/root &&
	exportdir=$(pwd)/net
'

test_expect_success 'start diod as root in multiuser mode' '
	diod_start_asroot \
	    --config-file=/dev/null \
	    --no-auth \
	    --export=ctl \
	    --export=$exportdir
'

test_expect_success 'the squash user can access ctl:/version' '
	$diodcat --server=$DIOD_SOCKET --aname=ctl version
'
test_expect_success 'the root user can access ctl:/version' '
	$SUDO $diodcat --server=$DIOD_SOCKET --aname=ctl version
'
test_expect_success 'the nobody user can access ctl:/version' '
	$SUDO -u nobody $diodcat --server=$DIOD_SOCKET --aname=ctl version
'

test_expect_success 'user can access net:/user' '
	$diodcat --server=$DIOD_SOCKET --aname=$exportdir /user
'
test_expect_success 'nobody cannot access net:/user' '
	test_must_fail $SUDO -u nobody \
	    $diodcat --server=$DIOD_SOCKET --aname=$exportdir /user
'
test_expect_success 'root can access net:/user' '
	$SUDO $diodcat --server=$DIOD_SOCKET --aname=$exportdir /user
'

test_expect_success 'user cannot access net:/nobody' '
	test_must_fail $diodcat --server=$DIOD_SOCKET --aname=$exportdir /nobody
'
test_expect_success 'nobody can access net:/nobody' '
	$SUDO -u nobody \
	    $diodcat --server=$DIOD_SOCKET --aname=$exportdir /nobody
'
test_expect_success 'root can access net:/nobody' '
	$SUDO $diodcat --server=$DIOD_SOCKET --aname=$exportdir /nobody
'

test_expect_success 'user cannot access net:/root' '
	test_must_fail $diodcat --server=$DIOD_SOCKET --aname=$exportdir /root
'
test_expect_success 'nobody cannot access net:/root' '
	test_must_fail $SUDO -u nobody \
	    $diodcat --server=$DIOD_SOCKET --aname=$exportdir /root
'
test_expect_success 'root can access net:/root' '
	$SUDO $diodcat --server=$DIOD_SOCKET --aname=$exportdir /root
'

test_expect_success 'stop diod' '
	diod_term_asroot $DIOD_SOCKET
'

test_done

# vi: set ft=sh
