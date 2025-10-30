#!/bin/sh

test_description='A few easy tests for diod in "runas" mode

Note: this test runs even with
  --disable-auth --disable-config --disable-multiuser
'

. `dirname $0`/sharness.sh

diodload=$SHARNESS_BUILD_DIRECTORY/src/cmd/diodload

test_expect_success 'create export dir' '
	mkdir -p net/1 &&
	for file in net/a net/1/b net/1/c; do \
	    echo TESTING >$file; \
	done &&
	exportdir=$(pwd)/net
'

test_expect_success 'start diod in runasuser mode' '
	diod_start \
	    --config-file=/dev/null \
	    --no-auth \
	    --export=ctl \
	    --export=$exportdir \
	    --runas=$(id -u)
'

test_expect_success STAT 'permissions on unix domain socket are ok' '
	cat >stat.exp <<-EOT &&
	drwxr-xr-x
	srw-rw-rw-
	EOT
	$PATH_STAT -c "%A" $(dirname $DIOD_SOCKET) >stat.out &&
	$PATH_STAT -c "%A" $DIOD_SOCKET >>stat.out &&
	test_cmp stat.exp stat.out
'

for ctlfile in version exports connections date files tpools usercache; do
	test_expect_success "cat ctl:$ctlfile" \
	    "$PATH_DIODCLI --aname=ctl read $ctlfile"
done

test_expect_success 'list ctl:/ directory' '
	$PATH_DIODCLI --aname=ctl ls /
'

test_expect_success 'copy ctl:/zero to ctl:null' '
	$diodload --server=$DIOD_SOCKET --runtime=1
'

# N.B. root/nobody attach to ctl is allowed in runas mode because ctl is
# implemented directly in libnpfs, bypassing diod_ops.c, where op_attach()
# gates access to all other exports.
test_expect_success SUDO 'the root user can access ctl:/version' '
	$SUDO -E $PATH_DIODCLI --aname=ctl read version
'

test_expect_success NOBODY 'the nobody user can access ctl:/version' '
	$SUDO -E -u nobody $PATH_DIODCLI --aname=ctl read version
'

test_expect_success 'ls net:/ shows test files' '
	cat >net-ls.exp <<-EOT &&
	1
	a
	EOT
	$PATH_DIODCLI --aname=$exportdir ls / \
	    | sort >net-ls.out &&
	test_cmp net-ls.exp net-ls.out
'
test_expect_success 'ls net:/1 shows test files' '
	cat >net-ls1.exp <<-EOT &&
	b
	c
	EOT
	$PATH_DIODCLI --server=$DIOD_SOCKET --aname=$exportdir ls /1 \
	    | sort >net-ls1.out &&
	test_cmp net-ls1.exp net-ls1.out
'
test_expect_success 'cat net:/1/c produced test file content' '
	echo TESTING >cat-c.exp &&
	$PATH_DIODCLI --aname=$exportdir read /1/c \
	    | sort >cat-c.out &&
	test_cmp cat-c.exp cat-c.out
'

test_expect_success SUDO 'cat net:/1/c fails as root' '
	test_must_fail $SUDO -E \
	    $PATH_DIODCLI --aname=$exportdir read /1/c 2>rootcat.err &&
	grep "Operation not permitted" rootcat.err
'

test_expect_success NOBODY 'cat net:/1/c fails as nobody' '
	test_must_fail $SUDO -E -u nobody \
	    $PATH_DIODCLI --aname=$exportdir read /1/c \
	        2>nobodycat.err &&
	grep "Operation not permitted" nobodycat.err
'

test_expect_success 'stop diod' '
	diod_term $DIOD_SOCKET
'

test_done

# vi: set ft=sh
