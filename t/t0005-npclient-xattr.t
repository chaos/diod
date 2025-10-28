#!/bin/sh

test_description='check extended attribute support with libnpclient'

. `dirname $0`/sharness.sh

export DIOD_SERVER_ANAME=$SHARNESS_TRASH_DIRECTORY/export
test_under_diod socketpair \
    --config-file=/dev/null --debug=0x1 \
    --runas=$(id -u) --no-auth \
    --export=$DIOD_SERVER_ANAME --export=ctl

touch probe.file
$PATH_NPCLIENT sysgetxattr notafile probe.file 2>probe.err
if grep -q "Operation not supported" probe.err; then
	skip_all='skipping xattr tests - no support in test file system'
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
	$PATH_NPCLIENT setxattr xtestfile user.foo fooval
'
test_expect_success 'the attribute is set' '
	cat >xattr.exp <<-EOT &&
	fooval
	EOT
	$PATH_NPCLIENT sysgetxattr export/xtestfile user.foo >xattr.out &&
	test_cmp xattr.exp xattr.out
'
test_expect_success 'attributes can be listed' '
	$PATH_NPCLIENT listxattr xtestfile
'
test_expect_success '9p getxattr user.foo works' '
	$PATH_NPCLIENT getxattr xtestfile user.foo >xattr.out2 &&
	test_cmp xattr.exp xattr.out2
'
test_expect_success '9p delxattr user.foo works' '
	$PATH_NPCLIENT delxattr xtestfile user.foo
'
test_expect_success '9p delxattr user.noexist fails with ENODATA' '
	test_must_fail $PATH_NPCLIENT delxattr \
	    xtestfile user.noexist 2>noexist.err &&
	grep "No data available" noexist.err
'
test_expect_success '9p getxattr user.foo fails' '
	test_must_fail $PATH_NPCLIENT getxattr xtestfile user.foo
'
test_expect_success '9p setxattr --create works' '
	$PATH_NPCLIENT setxattr xtestfile user.create zzz &&
	test_must_fail $PATH_NPCLIENT setxattr --create xtestfile \
	    user.create yyy &&
	$PATH_NPCLIENT delxattr xtestfile user.create &&
	$PATH_NPCLIENT setxattr --create xtestfile user.create xxx
'
test_expect_success '9p setxattr --replace works' '
	test_must_fail $PATH_NPCLIENT setxattr --replace xtestfile \
	    user.replace zzz &&
	$PATH_NPCLIENT setxattr xtestfile user.replace xxx &&
	$PATH_NPCLIENT setxattr --replace xtestfile user.replace zzz
'

test_done

# vi: set ft=sh
