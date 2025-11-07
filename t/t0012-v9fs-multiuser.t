#!/bin/sh

test_description='Test Linux kernel 9p client with diod in multiuser mode

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

exportdir=$SHARNESS_TRASH_DIRECTORY/exp
test_under_diod unixsocketroot \
    --config-file=/dev/null \
    --no-auth \
    --debug=0x1 \
    --export=$exportdir

if PATH_GETFATTR=$(which getfattr) && PATH_SETFATTR=$(which setfattr); then
	touch probe.file
	$PATH_GETFATTR -n user.badname probe.file 2>probe.err
	if ! grep -q "Operation not supported" probe.err; then
		test_set_prereq XATTR
	fi
fi

# gnome probes for .Trash, autorun.inf, etc asynchronously on new mounts,
# causing umount to fail with EBUSY if still in progress.  Therefore --lazy.
umountcmd="$SUDO umount -l"
mountcmd="$SUDO mount -n -t 9p"
mountopts="trans=unix,uname=$(id -un)"

test_expect_success 'create export/mount directories' '
	mkdir -p exp mnt mnt2
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount filesystem with access=client on mnt' '
	$mountcmd -oaname=$exportdir,$mountopts,access=client \
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
test_expect_success 'create a directory with gid 44 and setgid bit' '
	mkdir mnt/dir &&
	$SUDO chgrp 44 mnt/dir &&
	$SUDO chmod g+s mnt/dir
'
test_expect_success STAT 'a file in that directory gets gid 44' '
	touch mnt/dir/a &&
	test "$($PATH_STAT -c %g mnt/dir/a)" = 44
'
test_expect_success XATTR 'set trusted extended attributes' '
	touch mnt/file.xattr &&
	$SUDO $PATH_SETFATTR -n trusted.foo -v fooval mnt/file.xattr &&
	$SUDO $PATH_SETFATTR -n trusted.bar -v barval mnt/file.xattr &&
	$SUDO $PATH_SETFATTR -n trusted.baz -v bazval mnt/file.xattr &&
	cat >trusted.exp <<-EOT &&
	# file: mnt/file.xattr
	trusted.bar="barval"
	trusted.baz="bazval"
	trusted.foo="fooval"

	EOT
	$SUDO $PATH_GETFATTR -m- -d  mnt/file.xattr >trusted.out &&
	test_cmp trusted.exp trusted.out
'
test_expect_success XATTR 'clear trusted extended attributes' '
	$SUDO $PATH_SETFATTR -x trusted.foo mnt/file.xattr &&
	$SUDO $PATH_SETFATTR -x trusted.bar mnt/file.xattr &&
	$SUDO $PATH_SETFATTR -x trusted.baz mnt/file.xattr &&
	$SUDO $PATH_GETFATTR -m- -d mnt/file.xattr >trustedx.out &&
	test_must_fail test $(wc -l <trustedx.out) -gt 0
'
test_expect_success XATTR,SECURITY 'set security extended attributes' '
	touch mnt/file2.xattr &&
	$SUDO $PATH_SETFATTR -n security.foo -v fooval mnt/file2.xattr &&
	$SUDO $PATH_SETFATTR -n security.bar -v barval mnt/file2.xattr &&
	$SUDO $PATH_SETFATTR -n security.baz -v bazval mnt/file2.xattr &&
	cat >security.exp <<-EOT &&
	# file: mnt/file2.xattr
	security.bar="barval"
	security.baz="bazval"
	security.foo="fooval"

	EOT
	$SUDO $PATH_GETFATTR -m- -d  mnt/file2.xattr >security.out &&
	test_cmp security.exp security.out
'
test_expect_success XATTR,SECURITY 'clear security extended attributes' '
	$SUDO $PATH_SETFATTR -x security.foo mnt/file2.xattr &&
	$SUDO $PATH_SETFATTR -x security.bar mnt/file2.xattr &&
	$SUDO $PATH_SETFATTR -x security.baz mnt/file2.xattr &&
	$SUDO $PATH_GETFATTR -m- -d mnt/file2.xattr >securityx.out &&
	test_must_fail test $(wc -l <securityx.out) -gt 0
'
test_expect_success XATTR 'set user extended attributes' '
	touch mnt/file3.xattr &&
	$PATH_SETFATTR -n user.foo -v fooval mnt/file3.xattr &&
	$PATH_SETFATTR -n user.bar -v barval mnt/file3.xattr &&
	$PATH_SETFATTR -n user.baz -v bazval mnt/file3.xattr &&
	cat >user.exp <<-EOT &&
	# file: mnt/file3.xattr
	user.bar="barval"
	user.baz="bazval"
	user.foo="fooval"

	EOT
	$PATH_GETFATTR -m- -d  mnt/file3.xattr >user.out &&
	test_cmp user.exp user.out
'
test_expect_success XATTR 'clear user extended attributes' '
	$PATH_SETFATTR -x user.foo mnt/file3.xattr &&
	$PATH_SETFATTR -x user.bar mnt/file3.xattr &&
	$PATH_SETFATTR -x user.baz mnt/file3.xattr &&
	$PATH_GETFATTR -m- -d mnt/file3.xattr >userx.out &&
	test_must_fail test $(wc -l <userx.out) -gt 0
'
test_expect_success DIODMOUNT 'mount helper fails with -ouname but no -oaccess' '
	test_must_fail $SUDO $PATH_MOUNT_DIOD -ouname=fred foo mnt \
	    2>mounthelp.out &&
	grep "Common examples:" mounthelp.out
'
test_expect_success DIODMOUNT 'mount helper fails with -oaccess but no -ouname' '
	test_must_fail $SUDO $PATH_MOUNT_DIOD -oaccess=any foo mnt \
	    2>mounthelp.out &&
	grep "Common examples:" mounthelp.out
'
test_expect_success DIODMOUNT 'mount helper works with unix domain socket, no auth' '
	$SUDO $PATH_MOUNT_DIOD -n $DIOD_SOCKET:$exportdir mnt2
'
test_expect_success DIODMOUNT 'unmount mnt2' '
	$umountcmd mnt2
'
test_expect_success DIODMOUNT 'mount helper allows -oaccess=user' '
	$SUDO $PATH_MOUNT_DIOD -n -oaccess=user,uname=root \
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
