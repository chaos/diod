#!/bin/sh
echo "Running autoreconf --force --verbose --install"
autoreconf --force --verbose --install || exit
echo "Now run ./configure."
