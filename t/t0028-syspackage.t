#!/bin/sh

test_description='Test the installed diod package running under systemd

These tests use the installed programs and the diod.service started by
systemd, not the build tree.  They run only if /etc/diod.conf is
identical to t/etc/syspackage.conf, which CI installs along with the
package.  They only write inside the exported test directories.
'

. `dirname $0`/sharness.sh

if ! test_have_prereq SYSPACKAGE; then
	skip_all='/etc/diod.conf is not the test config, or diod is not running'
	test_done
fi
if ! test_have_prereq SUDO; then
	skip_all='passwordless sudo is required'
	test_done
fi

sock=/run/diod-test/sock
rwdir=/var/lib/diod-test/rw
rodir=/var/lib/diod-test/ro
diodcli=/usr/sbin/diodcli
mountdiod=/usr/sbin/mount.diod
umountcmd="$SUDO umount -l"
myuid=$(id -u)
mypid=$(systemctl show -p MainPID --value diod)

test_set_xattr_prereq

#
# The unit
#

# If the package is not built from this tree, the rest of the test checks
# the wrong thing, so fail rather than skip.  This compares the version
# and build options; it cannot tell uncommitted changes apart.
test_expect_success 'installed diod matches the build tree' '
	$PATH_DIOD -v >tree.out &&
	/usr/sbin/diod -v >installed.out &&
	test_cmp tree.out installed.out
'

test_expect_success 'diod.service is running the installed diod' '
	test "$mypid" -gt 0 &&
	test "$($SUDO readlink /proc/$mypid/exe)" = /usr/sbin/diod
'
test_expect_success 'diod runs as root in multiuser mode with MUNGE' '
	test "$(stat -c %u /proc/$mypid)" -eq 0 &&
	$SUDO journalctl -u diod -b -o cat >journal.out &&
	test_grep "Anyone can attach" journal.out &&
	test_grep "MUNGE authentication is required" journal.out
'

# The capability bounding set is applied even in an LXC container, but
# the other sandbox settings may be overridden there with a drop-in.
# Check what took effect and skip the checks that depend on it.
if test "$(systemctl show -p ProtectKernelTunables --value diod)" = yes &&
   test "$(systemctl show -p ProtectControlGroups --value diod)" = yes &&
   test "$(systemctl show -p ProtectKernelModules --value diod)" = yes &&
   test "$(systemctl show -p NoNewPrivileges --value diod)" = yes; then
	test_set_prereq SANDBOX
else
	say "sandbox settings are overridden here; skipping sandbox checks"
fi

# Usage: readonly_for_diod PATH
# Succeed if PATH is read-only in diod's mount namespace.  Ask the kernel
# rather than parse mount options: systemd makes a path read-only by
# mounting a read-only copy over it, so the path then has two mounts.
# On failure, show every mount at PATH in diod's namespace and the systemd
# version, since a path can have several mounts stacked on it.
readonly_for_diod() {
	$SUDO nsenter -t $mypid -m -- sh -c "test -d $1 && ! test -w $1" &&
	return 0
	echo "$1 is writable for diod.  $(systemctl --version | head -1)" >&2
	echo "mounts at $1 in diod's namespace:" >&2
	$SUDO cat /proc/$mypid/mountinfo | while read -r id pid dev root mnt opts rest; do
		test "$mnt" = "$1" && echo "  root=$root opts=$opts $rest"
	done >&2
	return 1
}

test_expect_success SANDBOX 'diod has no_new_privs set' '
	grep "^NoNewPrivs:[[:space:]]*1" /proc/$mypid/status
'
test_expect_success 'diod keeps the capabilities multiuser needs' '
	bnd=$(awk "/^CapBnd/ {print \$2}" /proc/$mypid/status) &&
	capsh --decode=$bnd >caps.out &&
	for cap in chown dac_override fowner fsetid setuid setgid mknod; do
		grep -q "cap_$cap\b" caps.out || return 1
	done
'
test_expect_success 'diod lacks capabilities the unit drops' '
	for cap in sys_module sys_rawio sys_ptrace sys_time net_admin; do
		! grep -q "cap_$cap\b" caps.out || return 1
	done
'
test_expect_success SANDBOX '/proc/sys is read-only for diod' '
	readonly_for_diod /proc/sys
'
# ProtectKernelTunables is documented to make /sys read-only too, but
# systemd 259 leaves /sys itself writable: its read-only entry for /sys is
# merged into the API file system bind of /sys, which is skipped when /sys
# is already mounted, and a skipped mount is never remounted read-only.
# So /sys itself is not checked.
test_expect_success SANDBOX '/sys/fs/cgroup is read-only for diod' '
	readonly_for_diod /sys/fs/cgroup
'
test_expect_success SANDBOX '/usr/lib/modules is hidden from diod' '
	test -d /usr/lib/modules &&
	test -z "$($SUDO nsenter -t $mypid -m -- ls -A /usr/lib/modules)"
'

#
# Serving files
#

test_expect_success 'diodcli can attach and read ctl' '
	$diodcli -s $sock -a ctl read exports >exports.out &&
	test_grep "$rwdir" exports.out &&
	test_grep "$rodir" exports.out
'
test_expect_success 'mount.diod with access=client works' '
	mkdir mnt &&
	$SUDO $mountdiod -n -ouname=root,access=client $sock:$rwdir mnt
'
test_expect_success 'user creates a file as itself' '
	echo hello >mnt/userfile &&
	test $(stat -c %u $rwdir/userfile) -eq $myuid
'
test_expect_success 'root creates a file as root' '
	echo hello | $SUDO tee mnt/rootfile >/dev/null &&
	test $(stat -c %u $rwdir/rootfile) -eq 0
'
test_expect_success 'root can chown a file to the user' '
	$SUDO chown $myuid mnt/rootfile &&
	test $(stat -c %u $rwdir/rootfile) -eq $myuid
'
test_expect_success 'root can make a device node' '
	$SUDO mknod mnt/null c 1 3 &&
	test -c $rwdir/null
'
test_expect_success 'root can make a setuid file' '
	$SUDO sh -c "echo x >mnt/suid && chmod 4755 mnt/suid" &&
	test -u $rwdir/suid
'
test_expect_success 'user cannot read a root-only file' '
	$SUDO sh -c "echo secret >mnt/secret && chmod 600 mnt/secret" &&
	test_must_fail cat mnt/secret &&
	$SUDO cat mnt/secret
'
test_expect_success XATTR 'user can set an xattr' '
	setfattr -n user.test -v yes mnt/userfile &&
	getfattr -n user.test $rwdir/userfile
'
test_expect_success 'clean up the read-write export' '
	$SUDO rm -f mnt/userfile mnt/rootfile mnt/null mnt/suid mnt/secret &&
	$umountcmd mnt
'
# Write as root, so that ordinary permissions do not refuse the write
# before the read-only export check does.
test_expect_success 'read-only export refuses writes, even by root' '
	$SUDO $mountdiod -n -ouname=root,access=client $sock:$rodir mnt &&
	test_must_fail $SUDO sh -c "echo x >mnt/new" 2>ro.err &&
	test_grep "Read-only file system" ro.err &&
	! test -e $rodir/new &&
	$umountcmd mnt
'

test_done

# vi: set ft=sh
