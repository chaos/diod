#!/bin/sh

test_description='Test Linux kernel 9p client with rsync

Run rsync against diod in multiuser mode.

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
if ! PATH_RSYNC=$(which rsync 2>/dev/null); then
	skip_all='rsync is not installed'
	test_done
fi

exportdir=$SHARNESS_TRASH_DIRECTORY/exp
test_under_diod unixsocketroot \
    --config-file=/dev/null \
    --no-auth \
    --export=$exportdir

# gnome probes for .Trash, autorun.inf, etc asynchronously on new mounts,
# causing umount to fail with EBUSY if still in progress.  Therefore --lazy.
umountcmd="$SUDO umount --lazy"
mountcmd="$SUDO mount -n -t 9p"
mountopts="trans=unix,uname=$(id -un)"

test_expect_success 'create export/mount directories' '
	mkdir -p exp mnt
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount filesystem with access=user' '
	$mountcmd -oaname=$exportdir,$mountopts,access=user \
	    $DIOD_SOCKET mnt
'
test_expect_success 'rsync /etc to mnt/etc' '
	mkdir -p mnt/etc &&
	$SUDO $PATH_RSYNC -av /etc mnt/etc/ 2>&1 >rsync.out
'
test_expect_success 'rsync /etc to mut/etc again should be silent' '
	$SUDO $PATH_RSYNC -avc /etc mnt/etc/ 2>&1 >rsync2.out &&
	test_must_fail grep etc/ rsync2.out
'
test_expect_success 'rsync /etc to exp/etc should be silent' '
	$SUDO $PATH_RSYNC -avc /etc exp/etc/ 2>&1 >rsync3.out &&
	test_must_fail grep etc/ rsync3.out
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
