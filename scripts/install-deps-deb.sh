#!/bin/sh

# Note:
# stat(1) is in coreutils
# flock(1) is in util-linux
# These are probably already installed

EXTRA_PKGS=""
for pkgset in $*; do
	case $pkgset in
	test)
	    EXTRA_PKGS="$EXTRA_PKGS valgrind acl attr scrub rsync postmark dbench"
	    ;;
	deb)
	    EXTRA_PKGS="$EXTRA_PKGS devscripts debhelper"
	    ;;
	esac
done

apt install \
  autoconf \
  automake \
  make \
  pkg-config \
  libc6-dev \
  libncurses-dev \
  libcap2-dev \
  lua5.1 \
  liblua5.1-dev \
  $EXTRA_PKGS
