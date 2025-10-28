#!/bin/sh

test_description='Test Linux kernel 9p client with diod in multiuser mode

Note: this test runs even with
  --disable-auth --disable-config
'

. `dirname $0`/sharness.sh

if ! test_have_prereq SUDO; then
        skip_all='passwordless udo is required'
        test_done
fi
if ! test_have_prereq V9FS_CLIENT; then
        skip_all='linux 9p kernel client is required'
        test_done
fi
if ! test_have_prereq MULTIUSER; then
        skip_all='diod was configured with --disable-multiuser'
        test_done
fi

exportdir=$SHARNESS_TRASH_DIRECTORY/exp
test_under_diod unixsocketroot \
    --config-file=/dev/null \
    --no-auth \
    --debug=0x1 \
    --export=$exportdir

# gnome probes for .Trash, autorun.inf, etc asynchronously on new mounts,
# causing umount to fail with EBUSY if still in progress.  Therefore --lazy.
umountcmd="$SUDO umount --lazy"
mountcmd="$SUDO mount -n -t 9p"
mountopts="trans=unix,uname=$(id -un)"

test_expect_success 'create export/mount directories' '
	mkdir -p exp mnt mnt2
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount filesystem with access=user on mnt' '
	$mountcmd -oaname=$exportdir,$mountopts,access=user \
	    $DIOD_SOCKET mnt
'
test_expect_success STAT 'create a file' '
	dd if=/dev/zero of=mnt/file count=10 &&
	test $($PATH_STAT -c %u exp/file) -eq $(id -u)
'
test_expect_success STAT,NOBODY 'chown it to nobody' '
	$SUDO chown nobody mnt/file &&
	test $($PATH_STAT -c %u exp/file) -eq $(id -u nobody)
'
test_expect_success STAT 'create a file owner 0 group 0' '
	touch exp/a &&
	$SUDO chown 0:0 exp/a &&
	test "$($PATH_STAT -c %u:%g mnt/a)" = "0:0"
'
test_expect_success STAT 'chgrp the file to gid 42' '
	$SUDO chgrp  42 mnt/a &&
	test "$($PATH_STAT -c %u:%g mnt/a)" = "0:42"
'
test_expect_success 'mount helper fails with -ouname but no -oaccess' '
	test_must_fail $SUDO $PATH_DIODMOUNT -ouname=fred foo mnt \
	    2>mounthelp.out &&
	grep "Common examples:" mounthelp.out
'
test_expect_success 'mount helper fails with -oaccess but no -ouname' '
	test_must_fail $SUDO $PATH_DIODMOUNT -oaccess=any foo mnt \
	    2>mounthelp.out &&
	grep "Common examples:" mounthelp.out
'
test_expect_success 'mount helper works with unix domain socket, no auth' '
	$SUDO $PATH_DIODMOUNT -n $DIOD_SOCKET:$exportdir mnt2
'
test_expect_success 'unmount mnt2' '
	$umountcmd mnt2
'
test_expect_success 'mount helper allows -oaccess=client' '
	$SUDO $PATH_DIODMOUNT -n -oaccess=client,uname=root \
	    $DIOD_SOCKET:$exportdir mnt2
'
test_expect_success 'unmount mnt2' '
	$umountcmd mnt2
'
test_expect_success 'unmount mnt' '
	$umountcmd mnt
'
test_expect_success 'chown files back to test runner or cleanup will fail' '
	$SUDO chown -R $(id -un) exp
'

# N.B. Server exits when conn count drops to zero.

test_done

# vi: set ft=sh
