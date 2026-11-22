#!/bin/sh

test_description='Test MUNGE authentication

Run diod with authentication required, and check that clients holding a
valid MUNGE credential can attach while others cannot.  A munged daemon
must be running.
'

. `dirname $0`/sharness.sh

if ! test_have_prereq AUTH; then
	skip_all='diod was configured with --disable-auth'
	test_done
fi
if ! munge -n </dev/null 2>/dev/null | unmunge >/dev/null 2>&1; then
	skip_all='a working munged is required'
	test_done
fi

if id -u daemon >/dev/null 2>&1 && test $(id -u) -ne $(id -u daemon); then
	test_set_prereq DAEMONUSER
fi

authattach=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_authattach
umountcmd="$SUDO umount -l"
mountcmd="$SUDO $PATH_MOUNT_DIOD -n"
myuid=$(id -u)

# N.B. Do not grep diod.log for messages logged with np_logerr().  It
# truncates them to 128 bytes, which a long export path (e.g. under
# make distcheck) fills before the reason is reached.

# Usage: check_steps expected-output authattach-args...
# Run test_authattach, which prints one line per attach step, and compare
# its output to the expected output, given as a printf format.  The exit
# status is ignored since the output says which steps failed.
check_steps() {
	printf "$1\n" >steps.exp &&
	shift &&
	{ $authattach "$@" >steps.out; true; } &&
	test_cmp steps.exp steps.out
}

#
# runas mode
#

test_expect_success 'create export dirs' '
	mkdir -p exp noauth mnt &&
	echo hello >exp/file &&
	echo hello >noauth/file &&
	echo hello >file.exp &&
	exportdir=$(pwd)/exp &&
	noauthdir=$(pwd)/noauth
'
test_expect_success 'start diod with auth required' '
	diod_start \
	    --config-file=/dev/null \
	    --export=ctl \
	    --export=$exportdir \
	    --runas=$myuid
'
test_expect_success 'diod reports that MUNGE authentication is required' '
	test_grep "MUNGE authentication is required" diod.log
'
test_expect_success 'diodcli attach with a valid credential works' '
	$PATH_DIODCLI --aname=$exportdir read /file >file.out &&
	test_cmp file.exp file.out
'
test_expect_success DAEMONUSER 'diodcli attach as a uid the credential does not match fails' '
	test_must_fail $PATH_DIODCLI --uid=$(id -u daemon) \
	    --aname=$exportdir read /file 2>wronguid.err &&
	test_grep "Operation not permitted" wronguid.err &&
	test_grep "does not authenticate uid=$(id -u daemon)" diod.log
'
test_expect_success 'attach with a valid credential works' '
	check_steps "1: ok" -a $exportdir munge
'
test_expect_success 'attach with no afid on a new connection fails' '
	check_steps "1: Operation not permitted" -a $exportdir none
'
test_expect_success 'attach with no credential written to afid fails' '
	check_steps "1: Operation not permitted" -a $exportdir empty &&
	test_grep "munge cred missing" diod.log
'
test_expect_success 'attach with a malformed credential fails' '
	check_steps "1: Operation not permitted" -a $exportdir garbage &&
	test_grep "munge cred decode" diod.log
'
test_expect_success 'a credential cannot be replayed' '
	munge -n >cred &&
	check_steps "1: ok\n2: Operation not permitted" \
	    -a $exportdir --cred=cred cred cred &&
	test_grep "munge cred decode: .*[Rr]eplay" diod.log
'
test_expect_success 'a second attach on an authenticated connection works' '
	check_steps "1: ok\n2: ok" -a $exportdir munge none
'
test_expect_success DAEMONUSER 'an authenticated connection does not extend to another uid' '
	check_steps "1: ok\n2: Operation not permitted" \
	    -a $exportdir munge none:$(id -u daemon)
'
test_expect_success 'failed attaches do not create thread pools' '
	check_steps "1: tpools=1\n2: Operation not permitted\n3: Operation not permitted\n4: Operation not permitted\n5: tpools=1" \
	    -a $exportdir tpools none@/nope1 garbage@/nope2 munge@/nope3 tpools
'
test_expect_success 'a successful attach creates a thread pool' '
	check_steps "1: tpools=1\n2: ok\n3: tpools=2" \
	    -a $exportdir tpools munge tpools
'
test_expect_success 'ctl requires auth too' '
	check_steps "1: Operation not permitted" -a ctl none &&
	check_steps "1: ok" -a ctl munge
'
test_expect_success SUDO,V9FS_CLIENT,DIODMOUNT 'mount.diod authenticates and mounts' '
	$mountcmd -ouname=$(id -un),access=$myuid \
	    $DIOD_SOCKET:$exportdir mnt &&
	test_cmp file.exp mnt/file
'
test_expect_success SUDO,V9FS_CLIENT,DIODMOUNT 'unmount' '
	$umountcmd mnt
'
test_expect_success 'stop diod' '
	diod_term $DIOD_SOCKET
'

#
# noauth export option
#

test_expect_success CONFIG 'start diod with a noauth export' '
	confdir=$(mktemp -d /tmp/diod-t0026.XXXXXX) &&
	test_when_finished "rm -rf $confdir" &&
	cat >$confdir/noauth.conf <<-EOT &&
	exports = {
	    "$exportdir",
	    { path="$noauthdir", opts="noauth" },
	}
	EOT
	diod_start --config-file=$confdir/noauth.conf --runas=$myuid
'
test_expect_success CONFIG 'attach to the noauth export without auth works' '
	check_steps "1: ok" -a $noauthdir none
'
test_expect_success CONFIG 'attach to the noauth export with auth works' '
	check_steps "1: ok" -a $noauthdir munge
'
test_expect_success CONFIG 'attach to the other export still requires auth' '
	check_steps "1: Operation not permitted\n2: ok" \
	    -a $exportdir none munge
'
test_expect_success CONFIG 'stop diod' '
	diod_term $DIOD_SOCKET
'

#
# allsquash mode
#

test_expect_success 'start diod in allsquash mode with auth required' '
	diod_start \
	    --config-file=/dev/null \
	    --export=$exportdir \
	    --allsquash
'
test_expect_success 'allsquash: attach with a valid credential works' '
	check_steps "1: ok" -a $exportdir munge
'
test_expect_success 'allsquash: attach without auth fails' '
	check_steps "1: Operation not permitted" -a $exportdir none
'
test_expect_success 'allsquash: attach with a malformed credential fails' '
	check_steps "1: Operation not permitted" -a $exportdir garbage
'
test_expect_success 'stop diod' '
	diod_term $DIOD_SOCKET
'

#
# multiuser mode (diod runs as root)
#

test_expect_success SUDO,MULTIUSER 'start diod as root in multiuser mode with auth required' '
	diod_start_asroot \
	    --config-file=/dev/null \
	    --export=$exportdir
'
test_expect_success SUDO,MULTIUSER 'multiuser: user attach with a valid credential works' '
	check_steps "1: ok" -a $exportdir munge
'
test_expect_success SUDO,MULTIUSER 'multiuser: user attach without auth fails' '
	check_steps "1: Operation not permitted" -a $exportdir none
'
test_expect_success SUDO,MULTIUSER,NOBODY 'multiuser: nobody attach with own credential works' '
	printf "1: ok\n" >nobody.exp &&
	$SUDO -u nobody $authattach -s $DIOD_SOCKET -a $exportdir munge \
	    >nobody.out &&
	test_cmp nobody.exp nobody.out
'
test_expect_success SUDO,MULTIUSER,NOBODY 'multiuser: nobody cannot authenticate as user' '
	printf "1: Operation not permitted\n" >nobody2.exp &&
	test_must_fail $SUDO -u nobody $authattach -s $DIOD_SOCKET \
	    -a $exportdir munge:$myuid >nobody2.out &&
	test_cmp nobody2.exp nobody2.out
'
test_expect_success SUDO,MULTIUSER 'multiuser: root credential lets connection attach as user' '
	printf "1: ok\n2: ok\n" >root.exp &&
	$SUDO $authattach -s $DIOD_SOCKET -a $exportdir munge none:$myuid \
	    >root.out &&
	test_cmp root.exp root.out
'
test_expect_success SUDO,MULTIUSER 'multiuser: root attach without auth fails' '
	printf "1: Operation not permitted\n" >root2.exp &&
	test_must_fail $SUDO $authattach -s $DIOD_SOCKET -a $exportdir none \
	    >root2.out &&
	test_cmp root2.exp root2.out
'
test_expect_success SUDO,MULTIUSER 'stop diod' '
	diod_term_asroot $DIOD_SOCKET
'

test_done

# vi: set ft=sh
