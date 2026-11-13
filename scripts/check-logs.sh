#!/bin/sh
#
#  Dump the logs of failing tests after a CI failure.
#
#  Automake writes a .log and a .trs for every test, passing or not, so
#  select on the .trs global-test-result and dump only what belongs to a
#  test that did not pass.  For each of those, dump
#
#    <test>.log         automake's per-test output, which with verbose=t
#                       holds the full TAP stream.  Tests that do not run
#                       under test_under_diod(), like t0001 and t0002,
#                       leave no other trace of what they did.
#    <test>.diod.log    the 9P protocol trace, kept on failure
#
#  plus any trash directory diod.log, which diod_start() writes with a
#  relative --logdest, and the test-suite.log summaries.
#
#  Each file is wrapped in a GitHub Actions ::group:: fold and truncated
#  to the last MAXLOG bytes.  The tail is kept rather than the head
#  because the interesting part of a trace is where it stopped.  diod at
#  --debug=1 writes several MB/s, so t0020-dbench can leave tens of MB.
#
MAXLOG=${MAXLOG:-262144}

catfile() {
    test -f "$1" && test -s "$1" || return 0
    size=$(wc -c <"$1")
    if test "$size" -gt "$MAXLOG"; then
        printf "::group::%s (last %s of %s bytes)\n" "$1" "$MAXLOG" "$size"
        tail -c "$MAXLOG" "$1"
    else
        printf "::group::%s (%s bytes)\n" "$1" "$size"
        cat "$1"
    fi
    printf "::endgroup::\n"
    found=$((found + 1))
}

found=0
failed=0
total=0
faildirs=
for trs in $(find . -name '*.trs' | sort); do
    total=$((total + 1))
    result=$(sed -n 's/^.*global-test-result: *//p' "$trs")
    case "$result" in
        PASS|SKIP|XFAIL) continue ;;
    esac
    failed=$((failed + 1))
    dir=$(dirname "$trs")
    case " $faildirs " in
        *" $dir "*) ;;
        *) faildirs="$faildirs $dir" ;;
    esac
    base=${trs%.trs}
    catfile "$base.log"
    catfile "$base.diod.log"
    catfile "$dir/trash-directory.$(basename "$base")/diod.log"
done

#  The summary of a directory that had a failure, which names what
#  failed.  A directory where everything passed has nothing to report.
for dir in $faildirs; do
    catfile "$dir/test-suite.log"
done

printf "Dumped %d log file(s) from %d failing test(s) of %d\n" \
    "$found" "$failed" "$total"
