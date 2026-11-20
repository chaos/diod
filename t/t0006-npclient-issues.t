#!/bin/sh

test_description='regression tests for past issues'

. `dirname $0`/sharness.sh

export DIOD_SERVER_ANAME=$SHARNESS_TRASH_DIRECTORY/export
test_under_diod socketpair \
    --config-file=/dev/null --debug=0x1 \
    --allsquash --no-auth \
    --export $DIOD_SERVER_ANAME --export=ctl

test_set_xattr_prereq

diodlog="${SHARNESS_TEST_DIRECTORY}/${SHARNESS_TEST_NAME}.diod.log"

test_expect_success '(re-)create export directory' '
	rm -rf export &&
	mkdir export &&
	chmod 777 export
'

test_expect_success XATTR 'setxattr with wild offset is rejected' '
	touch export/xtestfile &&
        test_must_fail $PATH_DIODCLI bug-setxattr-offsetcheck \
            xtestfile user.wildoffset zzz 2>wildoffset.err &&
        test_grep "pwrite xtestfile: Invalid argument" wildoffset.err
'
test_expect_success 'xattrcreate with attr_size over XATTR_SIZE_MAX fails' '
	touch export/xtestfile &&
	test_must_fail $PATH_DIODCLI bug-setxattr-hugesize \
	    xtestfile user.huge 0xffffffffffffffff 2>huge.err &&
	test_grep "xattrcreate xtestfile: Argument list too long" huge.err &&
	test_must_fail $PATH_DIODCLI bug-setxattr-hugesize \
	    xtestfile user.huge 65537 2>huge2.err &&
	test_grep "Argument list too long" huge2.err
'
test_expect_success 'xattrcreate with attr_size of XATTR_SIZE_MAX works' '
	$PATH_DIODCLI bug-setxattr-hugesize xtestfile user.max 65536
'
test_expect_success 'Twalk on an open fid works' '
	mkdir -p export/subdir &&
        $PATH_DIODCLI bug-open-walk subdir
'
test_expect_success 'Tread on an open fid works after removal' '
	dd if=/dev/urandom count=1 of=export/testfile &&
        $PATH_DIODCLI bug-open-rm-read testfile
'
test_expect_success 'Tgetattr on an open fid works after removal' '
	dd if=/dev/urandom count=1 of=export/testfile2 &&
        $PATH_DIODCLI bug-open-rm-getattr testfile2
'
test_expect_success 'Tsetattr on an open fid works after removal' '
	dd if=/dev/urandom count=1 of=export/testfile3 &&
        $PATH_DIODCLI bug-open-rm-setattr testfile3
'
test_expect_success 'Tsetattr on an open fid works after move' '
	dd if=/dev/urandom count=1 of=export/testfile4 &&
        $PATH_DIODCLI bug-open-move-setattr testfile4
'
# chaos/diod#93
test_expect_success 'Trename on fid from Tcreate works' '
        $PATH_DIODCLI bug-create-rename testfile5 testfile5b
'
# chaos/diod#146 - diod logs a crap stderr message
test_expect_success 'access to an unexported aname fails with EPERM' '
	test_must_fail $PATH_DIODCLI -a badaname null &&
	grep "access denied for export" $diodlog >badaname.log &&
	test_must_fail grep Success badaname.log
'
test_expect_success 'Tflush works' '
	$SHARNESS_BUILD_DIRECTORY/src/cmd/test_tflush
'
test_expect_success 'Tflush of a request blocked in the server works' '
	mkfifo export/fifo &&
	$SHARNESS_BUILD_DIRECTORY/src/cmd/test_tflush --timeout=30 \
	    --fifo=fifo --unblock=export/fifo &&
	rm -f export/fifo
'

tversion=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_tversion
test_expect_success 'Tversion with msize 2^31 is clamped to the server max' '
	$tversion 0x80000000 >msize.out &&
	test $(cat msize.out) -lt 2147483648
'
test_expect_success 'Tversion with msize UINT32_MAX is clamped to the server max' '
	$tversion 0xffffffff >msize2.out &&
	test $(cat msize2.out) -lt 2147483648
'

# Usage: test_tattach users threads getattrs iterations
test_tattach=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_tattach \

test_expect_success 'Tattach with 1 thread, 1 user' '
	$test_tattach 1 1 64 64
'

test_done

# vi: set ft=sh
