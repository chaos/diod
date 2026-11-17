#!/bin/sh

test_description='Test mount(8) with diod in "runas" mode

Other v9fs tests mount with mount.diod.  Cover plain mount -t 9p here,
where mount(8) does not stat the new mount as root (#181).

Note: this test runs even with
  --disable-auth --disable-config --disable-multiuser --disable-diodmount
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
if ! test_have_prereq MOUNT_NOSTAT; then
	skip_all='mount(8) stats the new mount as root (#181)'
	test_done
fi

exportdir=$SHARNESS_TRASH_DIRECTORY/exp
test_under_diod unixsocket \
    --config-file=/dev/null \
    --runas=$(id -u) \
    --no-auth \
    --export=$exportdir

umountcmd="$SUDO umount -l"

# Usage: mount9p options mountpoint
mount9p() {
	$SUDO mount -n -t 9p -oaname=$exportdir,trans=unix,$1 $DIOD_SOCKET $2
}

# N.B. the server exits when its connection count drops to zero, so keep
# mnt mounted while the other modes are tried on mnt2.
test_expect_success 'create export/mount directories' '
	mkdir -p exp mnt mnt2 &&
	echo hello >exp/file &&
	echo hello >file.exp
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount with access=<uid>' '
	mount9p uname=$(id -un),access=$(id -u) mnt &&
	test_cmp file.exp mnt/file
'
test_expect_success 'mount with access=user' '
	mount9p uname=$(id -un),access=user mnt2 &&
	test_cmp file.exp mnt2/file &&
	$umountcmd mnt2
'
test_expect_success 'mount with access=any' '
	mount9p uname=$(id -un),access=any mnt2 &&
	test_cmp file.exp mnt2/file &&
	$umountcmd mnt2
'
test_expect_success 'mount with default access' '
	mount9p uname=$(id -un) mnt2 &&
	test_cmp file.exp mnt2/file &&
	$umountcmd mnt2
'
test_expect_success 'mount with uname=root,access=any fails' '
	test_must_fail mount9p uname=root,access=any mnt2
'
test_expect_success 'clean up mnt2, if mount unexpectedly succeeded' '
	$umountcmd mnt2 2>/dev/null || :
'
test_expect_success 'unmount mnt' '
	$umountcmd mnt
'

test_done

# vi: set ft=sh
