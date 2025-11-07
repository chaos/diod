#!/bin/sh

test_description='Test Linux kernel 9p client with diod in "allsquash" mode

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
    --debug=0x1 \
    --allsquash \
    --no-auth \
    --export=$exportdir

# gnome probes for .Trash, autorun.inf, etc asynchronously on new mounts,
# causing umount to fail with EBUSY if still in progress.  Therefore --lazy.
umountcmd="$SUDO umount -l"
mountcmd="$SUDO mount -n -t 9p"
mountopts="trans=unix,uname=$(id -un)"

test_expect_success 'create export/mount directories' '
	mkdir -p exp exp2 mnt mnt2 &&
	chmod 777 exp &&
	chmod 755 exp2
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount filesystem with access=user on mnt' '
	$mountcmd -oaname=$exportdir,$mountopts,access=user \
	    $DIOD_SOCKET mnt
'
test_expect_success 'user can create a directory, mode 755' '
	mkdir -m 755 mnt/userdir
'
test_expect_success SUDO,STAT 'root can create a directory, mode 755' '
	$SUDO mkdir -m 755 mnt/rootdir &&
	test "$($PATH_STAT -c "%u:%g" exp/rootdir)" = "$(id -u):$(id -g)"
'
# N.B. fails on alpine/busybox when mkdir -m 755 is used because alpine
# calls mkdir and chmod separately, so the chmod isn't allowed.
# Do the chmod as the squashuser instead.
test_expect_success NOBODY,STAT 'nobody can create a directory, mode 755' '
	$SUDO -u nobody mkdir mnt/nobodydir &&
	chmod 755 mnt/nobodydir &&
	test "$($PATH_STAT -c "%u:%g" exp/nobodydir)" = "$(id -u):$(id -g)"
'

test_expect_success 'user can create a file in rootdir' '
	dd if=/dev/zero count=1 of=mnt/rootdir/userfile
'
test_expect_success 'user can create a file in nobodydir' '
	dd if=/dev/zero count=1 of=mnt/nobodydir/userfile
'

test_expect_success SUDO 'root can create a file in user directory' '
	$SUDO dd if=/dev/zero count=1 of=mnt/userdir/rootfile
'
test_expect_success SUDO 'root can create a file in nobody directory' '
	$SUDO dd if=/dev/zero count=1 of=mnt/nobodydir/rootfile
'

test_expect_success NOBODY 'nobody cannot create a file in nobody directory' '
	test_must_fail sh -c "$SUDO -u nobody \
	    dd if=/dev/zero count=1 of=mnt/nobodydir/nobodyfile"
'
test_expect_success 'user can chmod 777 nobody directory' '
	chmod 777 mnt/nobodydir
'
test_expect_success NOBODY 'now nobody can create a file in nobody directory' '
	$SUDO -u nobody dd if=/dev/zero count=1 of=mnt/nobodydir/nobodyfile
'
test_expect_success 'unmount mnt' '
	$umountcmd mnt
'

# N.B. Server exits when conn count drops to zero.

test_done

# vi: set ft=sh
