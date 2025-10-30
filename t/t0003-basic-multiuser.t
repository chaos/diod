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
	$PATH_DIODCLI --aname=ctl read version
'
test_expect_success 'the root user can access ctl:/version' '
	$SUDO -E $PATH_DIODCLI --aname=ctl read version
'
test_expect_success 'the nobody user can access ctl:/version' '
	$SUDO -E -u nobody $PATH_DIODCLI --aname=ctl read version
'

test_expect_success 'user can access net:/user' '
	$PATH_DIODCLI --aname=$exportdir read /user
'
test_expect_success 'nobody cannot access net:/user' '
	test_must_fail $SUDO -E -u nobody \
	    $PATH_DIODCLI --aname=$exportdir read /user
'
test_expect_success 'root can access net:/user' '
	$SUDO -E $PATH_DIODCLI --aname=$exportdir read /user
'

test_expect_success 'user cannot access net:/nobody' '
	test_must_fail $PATH_DIODCLI --aname=$exportdir read /nobody
'
test_expect_success 'nobody can access net:/nobody' '
	$SUDO -E -u nobody \
	    $PATH_DIODCLI --aname=$exportdir read /nobody
'
test_expect_success 'root can access net:/nobody' '
	$SUDO -E $PATH_DIODCLI --aname=$exportdir read /nobody
'

test_expect_success 'user cannot access net:/root' '
	test_must_fail $PATH_DIODCLI --aname=$exportdir read /root
'
test_expect_success 'nobody cannot access net:/root' '
	test_must_fail $SUDO -E -u nobody \
	    $PATH_DIODCLI --aname=$exportdir read /root
'
test_expect_success 'root can access net:/root' '
	$SUDO -E $PATH_DIODCLI --aname=$exportdir read /root
'

test_expect_success 'stop diod' '
	diod_term_asroot $DIOD_SOCKET
'

test_done

# vi: set ft=sh
