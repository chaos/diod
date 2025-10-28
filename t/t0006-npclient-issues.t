#!/bin/sh

test_description='regression tests for past issues'

. `dirname $0`/sharness.sh

export DIOD_SERVER_ANAME=$SHARNESS_TRASH_DIRECTORY/export
test_under_diod socketpair \
    --config-file=/dev/null --debug=0x1 \
    --allsquash --no-auth \
    --export $DIOD_SERVER_ANAME --export=ctl

touch probe.file
$PATH_NPCLIENT sysgetxattr notafile probe.file 2>probe.err
if ! grep -q "Operation not supported" probe.err; then
	test_set_prereq XATTR
fi

diodlog="${SHARNESS_TEST_DIRECTORY}/${SHARNESS_TEST_NAME}.diod.log"

test_expect_success '(re-)create export directory' '
	rm -rf export &&
	mkdir export &&
	chmod 777 export
'

test_expect_success XATTR 'setxattr with wild offset is rejected' '
	touch export/xtestfile &&
        test_must_fail $PATH_NPCLIENT bug-setxattr-offsetcheck \
            xtestfile user.wildoffset zzz 2>wildoffset.err &&
        grep "pwrite xtestfile: Invalid argument" wildoffset.err
'
test_expect_success 'Twalk on an open fid works' '
	mkdir -p export/subdir &&
        $PATH_NPCLIENT bug-open-walk subdir
'
test_expect_success 'Tread on an open fid works after removal' '
	dd if=/dev/urandom count=1 of=export/testfile &&
        $PATH_NPCLIENT bug-open-rm-read testfile
'
test_expect_success 'Tgetattr on an open fid works after removal' '
	dd if=/dev/urandom count=1 of=export/testfile2 &&
        $PATH_NPCLIENT bug-open-rm-getattr testfile2
'
test_expect_success 'Tsetattr on an open fid works after removal' '
	dd if=/dev/urandom count=1 of=export/testfile3 &&
        $PATH_NPCLIENT bug-open-rm-setattr testfile3
'
test_expect_success 'Tsetattr on an open fid works after move' '
	dd if=/dev/urandom count=1 of=export/testfile4 &&
        $PATH_NPCLIENT bug-open-move-setattr testfile4
'
# chaos/diod#93
test_expect_success 'Trename on fid from Tcreate works' '
        $PATH_NPCLIENT bug-create-rename testfile5 testfile5b
'
# chaos/diod#146 - diod logs a crap stderr message
test_expect_success 'access to an unexported aname fails with EPERM' '
	test_must_fail $PATH_NPCLIENT -a badaname null &&
	grep "access denied for export" $diodlog >badaname.log &&
	test_must_fail grep Success badaname.log
'
test_expect_success 'Tflush works' '
	$SHARNESS_BUILD_DIRECTORY/src/cmd/test_tflush
'

# Usage: test_tattach users threads getattrs iterations
test_tattach=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_tattach \

test_expect_success 'Tattach with 1 thread, 1 user' '
	$test_tattach 1 1 64 64
'

test_done

# vi: set ft=sh
