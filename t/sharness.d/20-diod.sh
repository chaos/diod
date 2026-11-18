PATH_DIOD=$SHARNESS_BUILD_DIRECTORY/src/cmd/diod
PATH_MOUNT_DIOD=$SHARNESS_BUILD_DIRECTORY/src/cmd/mount.diod
PATH_DIODRUN=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_diodrun
PATH_DIODCLI=$SHARNESS_BUILD_DIRECTORY/src/cmd/diodcli

##
# Set test prerequisites
#
# DIODMOUNT     mount.diod build was not suppressed
# NOBODY        sudo -u nobody works and nobody can execute diodcli
#               (e.g. the build tree is not under a 0700 home directory)
#
# CONFIG        diod has config file support
# GANESHA-KMOD  ganesha support was built
# AUTH          munge authentication support was built
# MULTIUSER     multiuser support was built
#
# V9FS_CLIENT   9p linux filesystem is available
# STAT          stat(1) is available
# FLOCK		flock(1) is available
# VSOCK		kernel vsock loopback transport works
# MOUNT_NOSTAT  mount(8) does not stat a mount after making it, so it
#               can mount a 9p file system that excludes root (#181)
#
##
get_buildopts() {
	$PATH_DIOD -v | grep buildopts \
		| cut -d ' ' -f2 | tr a-z A-Z | sed -e 's/+/ /g'
}
for feature in $(get_buildopts); do
	test_set_prereq $feature
done
if test -x $PATH_MOUNT_DIOD; then
	test_set_prereq DIODMOUNT
fi
if test_have_prereq SUDO; then
	if $SUDO -u nobody test -x $PATH_DIODCLI 2>/dev/null; then
		test_set_prereq NOBODY
	fi
fi
if test_have_prereq SUDO; then
	errmsg=$($SUDO mount -t 9p check / 2>&1)
	case "$errmsg" in
		*'unknown filesystem type'*) ;;
		*) test_set_prereq V9FS_CLIENT ;;
	esac
fi
if which stat >/dev/null 2>&1; then
	test_set_prereq STAT
fi
if which flock >/dev/null 2>&1; then
	test_set_prereq FLOCK
fi
test_set_mount_nostat_prereq

# Set VSOCK if the kernel has a working vsock loopback transport.
# Check with a standalone helper rather than diod itself, and rather than
# assuming from the presence of /dev/vsock, which does not imply loopback.
if $SHARNESS_BUILD_DIRECTORY/src/cmd/test_vsockcheck 2>/dev/null; then
	test_set_prereq VSOCK
fi

# Report the mount(8) and kernel 9p details that a v9fs test failure may
# turn on, once per run.  mount(8) does not say which of these is
# responsible when a mount fails.
report_9p_environment() {
	# test_report_kernel_config() reports once per run and says so in
	# its return status, which gates the rest.
	test_report_kernel_config '9P*' 'NET_9P*' || return 0

	say "mount: $(mount --version 2>/dev/null | head -1)"
	if grep -qw 9p /proc/filesystems 2>/dev/null; then
		say "9p: registered in /proc/filesystems"
	else
		say "9p: not registered in /proc/filesystems"
	fi
}
report_9p_environment

# Usage: waitsock sockpath [retries]
waitsock() {
	local sock=$1
	local retry=${2:-300}
	while ! test -S $sock; do
	    test $retry -gt 0 || return 1
	    sleep 0.2
	    retry=$(($retry-1))
	done
}

# Usage: diod_start [diod options ...]
# This sets $DIOD_SOCKET in the caller's environment
diod_start() {
	local sockdir=$(mktemp -d --tmpdir diodtest.XXXXXXXXXX) || return 1
	$DIOD_RUN_WRAPPER $PATH_DIOD \
		--logdest=diod.log --debug=1 --listen=$sockdir/sock "$@" &
	echo $! >$sockdir/pid
	waitsock $sockdir/sock || return 1
	chmod go=u-w $sockdir || return 1
	DIOD_SOCKET=$sockdir/sock; export DIOD_SOCKET
}

diod_start_asroot() {
	local sockdir=$(mktemp -d --tmpdir diodtest.XXXXXXXXXX) || return 1
	$SUDO -u root $PATH_DIOD \
		--logdest=diod.log --debug=1 --listen=$sockdir/sock "$@" &
	echo $! >$sockdir/pid
	waitsock $sockdir/sock || return 1
	chmod go=u-w $sockdir || return 1
	# the background sudo seems to mess up the tty (if any) - fix here
	reset 2>/dev/null
	DIOD_SOCKET=$sockdir/sock; export DIOD_SOCKET
}

# Usage: diod_term $DIOD_SOCKET
# If diod fails, its exit code is returned
diod_term() {
	local sockdir=$(dirname $1) || return 1
	test -S $sockdir/sock || return 1
	pid=$(cat $sockdir/pid) || return 1
	kill -15 $pid || return 1
	wait $pid
	exit_rc=$?
	rm -f $sockdir/sock $sockdir/pid
	rmdir $sockdir
	return $exit_rc
}

diod_term_asroot() {
	local sockdir=$(dirname $1) || return 1
	test -S $sockdir/sock || return 1
	pid=$(cat $sockdir/pid) || return 1
	$SUDO kill -15 $pid || return 1
	wait $pid
	exit_rc=$?
	rm -f $sockdir/sock $sockdir/pid
	rmdir $sockdir
	return $exit_rc
}

# Re-exec the test script "under" a test instance of diod.
# Usage: test_under_diod method ARGS ...
test_under_diod() {
	local method=$1; shift
	local diodrun_opts
	local diodsudo
	case "$method" in
	socketpair)
		diodrun_opts="--socketpair"
		;;
	unixsocket)
		;;
	unixsocketroot)
		#  MALLOC_CHECK_ is exported below, but sudo does not
		#  preserve the environment, so pass it explicitly.
		diodsudo="$SUDO MALLOC_CHECK_=3"
		;;
	*)
		error "unknown test method $method"
		;;
	esac
	log_file="$SHARNESS_TEST_NAME.diod.log"
	if test -n "$TEST_UNDER_DIOD_ACTIVE"; then
		# Keep the server log if the test failed so that CI can
		# capture the 9P protocol trace.  $test_failure is escaped
		# so it is evaluated by test_done(), not here.  Use if/fi
		# rather than && so that a failed test does not alter the
		# exit status cleanup() preserves.
		log_path="${SHARNESS_TEST_DIRECTORY:-..}/$log_file"
		test "$debug" = "t" \
		    || cleanup "if test \$test_failure -eq 0; then rm -f '$log_path'; fi"
		return
	fi
	if test "$verbose" = "t"; then
		flags="${flags} --verbose"
	fi
	if test "$debug" = "t"; then
		flags="${flags} --debug"
	fi
	if test "$chain_lint" = "t"; then
		flags="${flags} --chain-lint"
	fi

	if test -n "$SHARNESS_TEST_DIRECTORY"; then
		cd $SHARNESS_TEST_DIRECTORY
	fi

	ulimit -c unlimited

	export MALLOC_CHECK_=3
	export TEST_UNDER_DIOD_ACTIVE=t

	exec $PATH_DIODRUN $diodrun_opts \
	    "$diodsudo $PATH_DIOD -L $log_file $*" \
	    "sh $0 ${flags}"
}
