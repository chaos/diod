#!/bin/sh

test_description='Test diod startup with a lua config file

The other tests all run diod with --config-file=/dev/null so that they
work when diod is built with --disable-config.  Exercise a populated
config file here instead, so that config file support has end to end
coverage in exactly one place.
'

. `dirname $0`/sharness.sh

if ! test_have_prereq CONFIG; then
	skip_all='diod was configured with --disable-config'
	test_done
fi

exportdir=$SHARNESS_TRASH_DIRECTORY/export
rodir=$SHARNESS_TRASH_DIRECTORY/ro

test_expect_success 'create export directories' '
	mkdir -p $exportdir $rodir &&
	echo TESTING >$exportdir/file &&
	echo TESTING >$rodir/file
'

test_expect_success 'create config file' '
	cat >diod.conf <<-EOT
	nwthreads = 4
	auth_required = 0
	exportopts = "sharefd"
	exports = {
	    "ctl",
	    "$exportdir",
	    { path="$rodir", opts="ro" },
	}
	EOT
'

test_expect_success 'start diod with config file' '
	diod_start --config-file=$SHARNESS_TRASH_DIRECTORY/diod.conf \
	    --runas-uid=$(id -u)
'

test_expect_success 'auth_required from the config file took effect' '
	test_grep "No authentication is required" diod.log
'

test_expect_success 'ctl export from config file is accessible' '
	$PATH_DIODCLI --aname=ctl read exports >exports.out &&
	test_grep "$exportdir" exports.out &&
	test_grep "$rodir" exports.out
'

# exportopts establishes a default that a per-export opts attribute
# overrides, for both the string and table forms of an exports entry.
test_expect_success 'exportopts applies to a string form export' '
	test_grep "$exportdir sharefd" exports.out
'

test_expect_success 'per-export opts overrides exportopts' '
	test_grep "$rodir ro" exports.out
'

test_expect_success 'export from config file can be read' '
	echo TESTING >cat.exp &&
	$PATH_DIODCLI --aname=$exportdir read /file >cat.out &&
	test_cmp cat.exp cat.out
'

test_expect_success 'export from config file can be written' '
	echo HELLO | $PATH_DIODCLI --aname=$exportdir write /file2 &&
	echo HELLO >write.exp &&
	test_cmp write.exp $exportdir/file2
'

test_expect_success 'read-only export can be read' '
	echo TESTING >rocat.exp &&
	$PATH_DIODCLI --aname=$rodir read /file >rocat.out &&
	test_cmp rocat.exp rocat.out
'

test_expect_success 'ro export option from config file is enforced' '
	echo HELLO >rowrite.in &&
	test_must_fail $PATH_DIODCLI --aname=$rodir write /file2 \
	    <rowrite.in 2>rowrite.err &&
	test_grep "Read-only file system" rowrite.err
'

test_expect_success 'a path not in the config file cannot be attached' '
	test_must_fail $PATH_DIODCLI --aname=/tmp read /file 2>noexp.err &&
	test_grep "Operation not permitted" noexp.err
'

test_expect_success 'stop diod' '
	diod_term $DIOD_SOCKET
'

test_expect_success 'diod fails on a config file with a syntax error' '
	cat >bad.conf <<-EOT &&
	exports = { "$exportdir" }
	this is not lua
	EOT
	test_must_fail $PATH_DIOD --foreground \
	    --config-file=$SHARNESS_TRASH_DIRECTORY/bad.conf \
	    --listen=$SHARNESS_TRASH_DIRECTORY/nosuchsock 2>bad.err &&
	test_grep "bad.conf" bad.err
'

test_expect_success 'diod fails on a config file with an unknown export option' '
	cat >badopt.conf <<-EOT &&
	exports = { { path="$exportdir", opts="xxx" } }
	EOT
	test_must_fail $PATH_DIOD --foreground \
	    --config-file=$SHARNESS_TRASH_DIRECTORY/badopt.conf \
	    --listen=$SHARNESS_TRASH_DIRECTORY/nosuchsock 2>badopt.err &&
	test_grep "unknown export option" badopt.err
'

test_expect_success 'restart diod with a command line export' '
	diod_start --config-file=$SHARNESS_TRASH_DIRECTORY/diod.conf \
	    --no-auth \
	    --export=ctl --export=$exportdir \
	    --runas-uid=$(id -u)
'

test_expect_success 'command line export overrides config file exports' '
	test_must_fail $PATH_DIODCLI --aname=$rodir read /file 2>override.err &&
	test_grep "Operation not permitted" override.err
'

test_expect_success 'stop diod again' '
	diod_term $DIOD_SOCKET
'

test_done

# vi: set ft=sh
