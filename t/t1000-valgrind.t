#!/bin/sh

test_description='Run diod under valgrind with test client'

. `dirname $0`/sharness.sh

loadgen=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_loadgen

if ! valgrind --version; then
        skip_all='skipping valgrind tests'
        test_done
fi

test_expect_success 'start diod under valgrind in runasuser mode' '
	DIOD_RUN_WRAPPER="valgrind \
	    --tool=memcheck \
	    --leak-check=full \
	    --error-exitcode=1 \
	    --gen-suppressions=all" &&
	diod_start --config-file=/dev/null --no-auth --export=ctl
'

test_expect_success 'copy ctl:/zero to ctl:null' '
        $loadgen --server=$DIOD_SOCKET --runtime=1
'

test_expect_success 'stop diod' '
        diod_term $DIOD_SOCKET
'
test_done

# vi: set ft=sh
