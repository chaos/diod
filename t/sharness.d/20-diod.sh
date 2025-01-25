PATH_DIOD=$SHARNESS_BUILD_DIRECTORY/src/cmd/diod
PATH_DIODMOUNT=$SHARNESS_BUILD_DIRECTORY/src/cmd/diodmount

##
# Set test prerequisites
#
# DIODMOUNT     diodmount build was not suppressed
# NOBODY        sudo -u nobody works
#
# CONFIG        diod has config file support
# GANESHA-KMOD  ganesha support was built
# AUTH          munge authentication support was built
# MULTIUSER     multiuser support was built
#
##
get_buildopts() {
	$PATH_DIOD -v | grep buildopts \
		| cut -d ' ' -f2 | tr a-z A-Z | sed -e 's/+/ /g'
}
for feature in $(get_buildopts); do
	test_set_prereq $feature
done
if test -x $PATH_DIODMOUNT; then
	test_set_prereq DIODMOUNT
fi
if test_have_prereq SUDO; then
	if $SUDO -u nobody true 2>/dev/null; then
		test_set_prereq NOBODY
	fi
fi

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
	DIOD_SOCKET=$sockdir/sock
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
	DIOD_SOCKET=$sockdir/sock
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
