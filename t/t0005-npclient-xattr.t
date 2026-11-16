#!/bin/sh

test_description='check extended attribute support with libnpclient'

. `dirname $0`/sharness.sh

if ! which getfattr setfattr >/dev/null 2>&1; then
	skip_all='getfattr/setfattr is not installed'
	test_done
fi

export DIOD_SERVER_ANAME=$SHARNESS_TRASH_DIRECTORY/export
test_under_diod socketpair \
    --config-file=/dev/null --debug=0x1 \
    --runas=$(id -u) --no-auth \
    --export=$DIOD_SERVER_ANAME --export=ctl

test_set_xattr_prereq
if ! test_have_prereq XATTR; then
	skip_all='user extended attributes do not work here'
	test_done
fi

test_expect_success '(re-)create export directory' '
	rm -rf export &&
	mkdir export &&
	chmod 777 export
'

test_expect_success 'create testfile in the diod export directory' '
	dd if=/dev/urandom of=export/xtestfile bs=4096 count=100
'
test_expect_success '9p setxattr user.foo works' '
	$PATH_DIODCLI setxattr xtestfile user.foo fooval
'
test_expect_success 'the attribute is set' '
	cat >xattr.exp <<-EOT &&
	fooval
	EOT
	getfattr --only-values -n user.foo export/xtestfile >xattr.out &&
	echo >>xattr.out &&
	test_cmp xattr.exp xattr.out
'
test_expect_success 'attributes can be listed' '
	$PATH_DIODCLI listxattr xtestfile
'
test_expect_success '9p getxattr user.foo works' '
	$PATH_DIODCLI getxattr xtestfile user.foo >xattr.out2 &&
	test_cmp xattr.exp xattr.out2
'
test_expect_success '9p delxattr user.foo works' '
	$PATH_DIODCLI delxattr xtestfile user.foo
'
test_expect_success '9p delxattr user.noexist fails with ENODATA' '
	test_must_fail $PATH_DIODCLI delxattr \
	    xtestfile user.noexist 2>noexist.err &&
	test_grep "No data available" noexist.err
'
test_expect_success '9p getxattr user.foo fails' '
	test_must_fail $PATH_DIODCLI getxattr xtestfile user.foo
'
test_expect_success '9p setxattr --create works' '
	$PATH_DIODCLI setxattr xtestfile user.create zzz &&
	test_must_fail $PATH_DIODCLI setxattr --create xtestfile \
	    user.create yyy &&
	$PATH_DIODCLI delxattr xtestfile user.create &&
	$PATH_DIODCLI setxattr --create xtestfile user.create xxx
'
test_expect_success '9p setxattr --replace works' '
	test_must_fail $PATH_DIODCLI setxattr --replace xtestfile \
	    user.replace zzz &&
	$PATH_DIODCLI setxattr xtestfile user.replace xxx &&
	$PATH_DIODCLI setxattr --replace xtestfile user.replace zzz
'

test_done

# vi: set ft=sh
