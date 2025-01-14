#!/bin/sh

die () {
    echo "$@" >&2
    exit 1
}

which valgrind >/dev/null 2>&1 || die "valgrind is missing"

TESTS="src/libnpfs/test_encoding.t \
    src/libnpfs/test_fidpool.t \
    src/libdiod/test_configfile.t"

exit_rc=0

for test in $TESTS; do
    valgrind \
        --tool=memcheck \
	--leak-check=full \
	--error-exitcode=1 \
	--leak-resolution=med \
	--trace-children=no \
	--child-silent-after-fork=yes \
	$test
    test $? -eq 0 || exit_rc=1
done

exit $exit_rc
