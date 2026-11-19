#!/bin/sh

test_description='check diod over vsock using libnpclient

Uses the vsock loopback transport (VMADDR_CID_LOCAL), so no virtual
machine is needed.  The test is skipped if loopback is unavailable,
e.g. when the vsock_loopback kernel module is not loaded.
'

. `dirname $0`/sharness.sh

# Pick a port above the privileged range that is unlikely to collide.
port=$((20000 + $$ % 20000))
addr=vsock:1:$port

if ! test_have_prereq VSOCK; then
	skip_all='vsock loopback is not available'
	test_done
fi

if test_have_prereq SUDO,V9FS_CLIENT,DIODMOUNT; then
	test_set_prereq MOUNT
fi

exportdir=$SHARNESS_TRASH_DIRECTORY/export
diodlog=$SHARNESS_TRASH_DIRECTORY/diod.log

# Usage: wait_vsock ADDR
# Retry until a connection to the diod server succeeds.
wait_vsock() {
	local retry=100
	while ! $PATH_DIODCLI -s $1 null 2>/dev/null; do
		test $retry -gt 0 || return 1
		sleep 0.2
		retry=$(($retry-1))
	done
}

test_expect_success 'create export directory' '
	mkdir export &&
	echo hello >export/testfile
'
test_expect_success 'diod rejects a malformed vsock listen address' '
	test_must_fail $PATH_DIOD --listen=vsock:bogus 2>badlisten.err &&
	test_grep "invalid vsock address" badlisten.err
'
test_expect_success 'start diod listening on vsock' '
	$PATH_DIOD --foreground --config-file=/dev/null \
	    --logdest=$diodlog --debug=1 \
	    --runas-uid=$(id -u) --no-auth \
	    --export=$exportdir --export=ctl \
	    --listen=vsock:$port &
	echo $! >diod.pid &&
	wait_vsock $addr
'
test_expect_success 'diod logged the vsock listen address' '
	test_grep "Listening on vsock:$port" $diodlog
'
test_expect_success '9p read of testfile over vsock works' '
	DIOD_SERVER_ANAME=$exportdir \
	    $PATH_DIODCLI -s $addr read testfile >testfile.out &&
	test_cmp export/testfile testfile.out
'
test_expect_success '9p write over vsock works' '
	echo world >testfile2 &&
	DIOD_SERVER_ANAME=$exportdir \
	    $PATH_DIODCLI -s $addr write testfile2 <testfile2 &&
	test_cmp testfile2 export/testfile2
'
test_expect_success 'server identifies the client by its vsock CID' '
	$PATH_DIODCLI -s $addr showmount >showmount.out &&
	test_grep "^vsock:1$" showmount.out
'
test_expect_success 'diodcli rejects a malformed vsock address' '
	test_must_fail $PATH_DIODCLI -s vsock:x:$port null 2>badaddr.err &&
	test_grep "invalid vsock address" badaddr.err
'
test_expect_success 'diodcli fails to connect to an unused vsock port' '
	test_must_fail $PATH_DIODCLI -s vsock:1:$(($port + 1)) null
'
test_expect_success MOUNT 'mount.diod over vsock works' '
	mkdir mnt &&
	$SUDO $PATH_MOUNT_DIOD -n \
	    -oport=$port,uname=$(id -un),access=$(id -u) \
	    vsock:1:$exportdir mnt
'
test_expect_success MOUNT 'mount source shows the vsock CID' '
	grep "^vsock:1:$exportdir $SHARNESS_TRASH_DIRECTORY/mnt 9p " /proc/mounts
'
test_expect_success MOUNT 'read and write through the vsock mount works' '
	test_cmp export/testfile mnt/testfile &&
	echo again >mnt/testfile3 &&
	echo again >testfile3.exp &&
	test_cmp testfile3.exp export/testfile3
'
test_expect_success MOUNT 'unmount the vsock mount' '
	$SUDO umount -l mnt
'
test_expect_success MOUNT 'mount.diod rejects --privport with vsock' '
	test_must_fail $SUDO $PATH_MOUNT_DIOD -n --privport \
	    -oport=$port,uname=$(id -un),access=$(id -u) \
	    vsock:1:$exportdir mnt 2>privport.err &&
	test_grep "privport won.t work with vsock" privport.err
'
test_expect_success 'stop diod' '
	kill $(cat diod.pid) &&
	wait $(cat diod.pid)
'

test_done
