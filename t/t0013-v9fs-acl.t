#!/bin/sh

test_description='Test ACLs on Linux 9P kernel client

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
if ! test_have_prereq MULTIUSER; then
        skip_all='diod was configured with --disable-multiuser'
        test_done
fi
if ! PATH_GETFACL=$(which getfacl) || ! PATH_SETFACL=$(which setfacl); then
        skip_all='getfacl/setfacl is not installed'
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
	mkdir -p exp mnt
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount filesystem with access=client,posixacl on mnt' '
	$mountcmd -oaname=$exportdir,$mountopts,access=client,posixacl \
	    $DIOD_SOCKET mnt
'
test_expect_success 'create a test file and setfacl -m u:root:r' '
	touch mnt/testfile &&
	chmod 644 mnt/testfile &&
	$PATH_SETFACL -m u:root:r mnt/testfile
'
test_expect_success 'getfacl confirms ACL is set' '
	cat >getfacl.exp <<-EOT &&
	user::rw-
	user:root:r--
	group::r--
	mask::r--
	other::r--

	EOT
	$PATH_GETFACL --omit-header mnt/testfile >getfacl.out &&
	test_cmp getfacl.exp getfacl.out
'
test_expect_success 'it is set on the server too' '
	$PATH_GETFACL --omit-header exp/testfile >exp_getfacl.out &&
	test_cmp getfacl.exp exp_getfacl.out
'
test_expect_success 'setfacl -x u:root' '
	$PATH_SETFACL -x u:root mnt/testfile
'
test_expect_success 'getfacl confirms ACL is unset' '
	cat >getfaclx.exp <<-EOT &&
	user::rw-
	group::r--
	mask::r--
	other::r--

	EOT
	$PATH_GETFACL --omit-header mnt/testfile >getfaclx.out &&
	test_cmp getfaclx.exp getfaclx.out
'
test_expect_success 'it is unset on the server too' '
	$PATH_GETFACL --omit-header exp/testfile >exp_getfaclx.out &&
	test_cmp getfaclx.exp exp_getfaclx.out
'
test_expect_success 'create a test directory and setfacl -d -m u:root:r' '
	mkdir mnt/testdir &&
	chmod 755 mnt/testdir &&
	$SUDO $PATH_SETFACL -d -m u:root:r mnt/testdir
'
test_expect_success 'getfacl confirms ACL is set' '
	cat >getfacld.exp <<-EOT &&
	user::rwx
	group::r-x
	other::r-x
	default:user::rwx
	default:user:root:r--
	default:group::r-x
	default:mask::r-x
	default:other::r-x

	EOT
	$PATH_GETFACL --omit-header mnt/testdir >getfacld.out &&
	test_cmp getfacld.exp getfacld.out
'
test_expect_success 'it is set on the server too' '
	$PATH_GETFACL --omit-header exp/testdir >exp_getfacld.out &&
	test_cmp getfacld.exp exp_getfacld.out
'
test_expect_success 'setfacl -d -x u:root' '
	$PATH_SETFACL -d -x u:root mnt/testdir
'
test_expect_success 'getfacl confirms ACL is unset' '
	cat >getfacldx.exp <<-EOT &&
	user::rwx
	group::r-x
	other::r-x
	default:user::rwx
	default:group::r-x
	default:mask::r-x
	default:other::r-x

	EOT
	$PATH_GETFACL --omit-header mnt/testdir >getfacldx.out &&
	test_cmp getfacldx.exp getfacldx.out
'
test_expect_success 'it is unset on the server too' '
	$PATH_GETFACL --omit-header exp/testdir >exp_getfacldx.out &&
	test_cmp getfacldx.exp exp_getfacldx.out
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
