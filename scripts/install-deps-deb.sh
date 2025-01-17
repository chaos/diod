#!/bin/sh

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
  valgrind \

# to build test DEBs you need devscripts and debhelper too
