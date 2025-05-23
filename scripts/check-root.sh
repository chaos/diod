#!/bin/sh

die () {
    echo "$@" >&2
    exit 1
}

sudo -n /bin/true || die "passwordless sudo is required to run privileged tests"

sudo -n make -C src/libnpfs check TESTS="\
  test_capability.t \
  test_setfsuid.t \
  test_setreuid.t" || die "test failed"

sudo -n make -C src/libdiod check TESTS="\
  test_multiuser.t" || die "test failed"
