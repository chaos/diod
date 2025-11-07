#!/bin/sh

test_description='Test Linux kernel 9p client with scrub

Run scrub against diod in "runas" mode.

See also: https://github.com/chaos/scrub

Note: this test runs even with
  --disable-auth --disable-config
'

. `dirname $0`/sharness.sh

if ! test_have_prereq SUDO; then
	skip_all='passwordless sudo is required'
	test_done
fi
if ! test_have_prereq V9FS_CLIENT; then
	skip_all='linux 9p kernel client is required'
	test_done
fi
if ! PATH_SCRUB=$(which scrub 2>/dev/null); then
	skip_all='scrub is not installed'
	test_done
fi

exportdir=$SHARNESS_TRASH_DIRECTORY/exp
test_under_diod unixsocket \
    --config-file=/dev/null \
    --runas=$(id -u) \
    --no-auth \
    --export=$exportdir

# gnome probes for .Trash, autorun.inf, etc asynchronously on new mounts,
# causing umount to fail with EBUSY if still in progress.  Therefore --lazy.
umountcmd="$SUDO umount -l"
mountcmd="$SUDO mount -n -t 9p"
mountopts="trans=unix,uname=$(id -un)"

test_expect_success 'create export/mount directories' '
	mkdir -p exp mnt
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount filesystem with access=<uid> mnt' '
	$mountcmd -oaname=$exportdir,$mountopts,access=$(id -u) \
	    $DIOD_SOCKET mnt
'
test_expect_success 'create a 20mb file' '
	dd if=/dev/zero of=mnt/file bs=1024k count=20 status=noxfer
'
test_expect_success 'scrub using default pattern' '
	$PATH_SCRUB mnt/file
'
test_expect_success 'unmount mnt' '
	$umountcmd mnt
'

# N.B. Server exits when conn count drops to zero.

test_done

# vi: set ft=sh
