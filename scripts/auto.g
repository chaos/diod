#!/bin/bash

# Example of indirect map for /g.  Add to auto.master like this:
#   /g /etc/auto.g
# and install this file as /etc/auto.g, mode 755.
# Set DIOD_SERVERS to list of diod servers in /etc/sysconfig/auto.g

key="$1"

DIOD_MAP=/g
DIOD_SYSCONF=/etc/sysconfig/auto.g
DIOD_DIODEXP=/usr/sbin/diodexp
DIOD_SERVERS=""

if [ -r $DIOD_SYSCONF ]; then
    . $DIOD_SYSCONF
fi
if [ -z "$DIOD_SERVERS" ]; then
    echo "auto.diod: DIOD_SERVERS is not set" >&2
    exit 1
fi
if ! [ -x $DIOD_DIODEXP ]; then
    echo "auto.diod: could not execute $DIOD_DIODEXP" >&2
    exit 1
fi
if [ -n "$key" ]; then
    $DIOD_DIODEXP -i $DIOD_MAP -k $key $DIOD_SERVERS
else
    $DIOD_DIODEXP -i $DIOD_MAP -K $DIOD_SERVERS
fi
