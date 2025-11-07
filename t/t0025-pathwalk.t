#!/bin/sh

test_description='Run pathwalk test in diod

Note: this test runs even with
  --disable-auth --disable-config --disable-multiuser
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

pathwalk=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_pathwalk

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
test_expect_success 'run pathwalk create' '
	$pathwalk mnt/pw --create --files=500 --quiet
'
test_expect_success 'run pathwalk test' '
	$pathwalk mnt/pw --test --files=500 --quiet
'
test_expect_success 'run pathwalk remove' '
	$pathwalk mnt/pw --remove --files=500 --quiet
'
test_expect_success 'unmount mnt' '
	$umountcmd mnt
'

# N.B. Server exits when conn count drops to zero.

test_done

# vi: set ft=sh
