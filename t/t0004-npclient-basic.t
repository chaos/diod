#!/bin/sh

test_description='check read, write, mkdir, stat using libnpclient'

. `dirname $0`/sharness.sh

export DIOD_SERVER_ANAME=$SHARNESS_TRASH_DIRECTORY/export
test_under_diod socketpair \
    --config-file=/dev/null --debug=0x1 \
    --runas=$(id -u) --no-auth \
    --export=$DIOD_SERVER_ANAME --export=ctl

if test $(id -u) -ne 0; then
    test_set_prereq UNPRIVILEGED
fi
if id -u daemon >/dev/null 2>&1; then
    test_set_prereq DAEMONUSER
fi

test_expect_success '(re-)create export directory' '
	rm -rf export &&
	mkdir export &&
	chmod 777 export
'

test_expect_success 'mount/umount works on ctl' '
	sh -c "DIOD_SERVER_ANAME=ctl $PATH_DIODCLI null"
'
test_expect_success 'mount/umount works on export directory' '
	$PATH_DIODCLI null
'
test_expect_success 'mount fails on aname that is not exported' '
	test_must_fail $PATH_DIODCLI --aname=/bogus null
'
test_expect_success UNPRIVILEGED,DAEMONUSER 'mount fails as daemon uid' '
	test_must_fail $PATH_DIODCLI --uid=$(id -u daemon) null
'
test_expect_success 'create testfile in the diod export directory' '
	dd if=/dev/urandom of=export/testfile bs=4096 count=100
'
test_expect_success '9p read of testfile works' '
	$PATH_DIODCLI read testfile >testfile.copy
'
test_expect_success 'read copy is the same as the original' '
	cmp export/testfile testfile.copy
'
test_expect_success '9p read of bogus file fails' '
	test_must_fail $PATH_DIODCLI read bogus
'
test_expect_success 'create testfile2' '
	dd if=/dev/urandom of=testfile2 bs=4096 count=100 &&
	chmod 644 testfile2
'
test_expect_success '9p write of testfile2 works' '
	$PATH_DIODCLI write testfile2.copy <testfile2
'
test_expect_success STAT '9p stat reports expected values' '
	$PATH_STAT -c "mode=%f owner=%u:%g size=%s blocks=%b blocksize=%o links=%h device=%t:%T mtime=%Y ctime=%Z atime=%X" testfile2 >stat.exp &&
	$PATH_DIODCLI stat testfile2.copy >stat.out &&
	test_cmp stat.exp stat.out
'
test_expect_success 'write copy is the same as the original' '
	cmp testfile2 export/testfile2.copy
'
test_expect_success '9p write to bogus directory fails' '
	test_must_fail $PATH_DIODCLI write bogus/t <testfile2
'
test_expect_success '9p mkdir foo' '
	$PATH_DIODCLI mkdir foo
'
test_expect_success 'foo directory exists' '
	test -d export/foo
'
test_expect_success '9p stat works on new directory' '
	$PATH_DIODCLI stat foo
'
test_expect_success '9p mkdir in a bogus directory fails' '
	test_must_fail $PATH_DIODCLI mkdir bogus/foo
'
test_expect_success '9p stat on bogus file fails' '
	test_must_fail $PATH_DIODCLI stat bogus
'

test_done

# vi: set ft=sh
