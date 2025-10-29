#!/bin/sh

test_description='Build diod in diod

Checkout a copy of git locally, then configure and build it

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
clonefrom=$(realpath $SHARNESS_TEST_SRCDIR/..)
if ! test -d $clonefrom/.git; then
	skip_all='source tree is not a git clone'
	test_done
fi
if ! git version; then
	skip_all='git is unavailable'
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
umountcmd="$SUDO umount --lazy"
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
test_expect_success 'clone git repository' '
	(cd mnt && git clone $clonefrom)
'
test_expect_success 'autogen' '
	(cd mnt/diod && ./autogen.sh)
'
# minicheck ci builder will fail if we depend on packages
# that aren't installed there, hence the --disable options below
test_expect_success 'configure' '
	(cd mnt/diod && \
	    ./configure --disable-auth --disable-config --disable-multiuser)
'
test_expect_success 'make' '
	(cd mnt/diod && make)
'
test_expect_success 'diod --version works' '
	mnt/diod/src/cmd/diod --version
'
test_expect_success 'unmount mnt' '
	$umountcmd mnt
'

# N.B. Server exits when conn count drops to zero.

test_done

# vi: set ft=sh
