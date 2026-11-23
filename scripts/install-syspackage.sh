#!/bin/sh
#
#  Install the diod package built by "make deb" and start it under systemd
#  with the test config, so that "make check" runs t/t0028-syspackage.t
#  against it.
#
#  This replaces /etc/diod.conf, so run it only on a throwaway machine
#  such as a CI runner.  Run from the top of a configured build tree,
#  after "make deb".
#
#  Steps:
#    1. install the package
#    2. check that diod.service neither starts nor fails without a config
#    3. install the test config and start diod
#
#  On failure, dump the diod journal.
#

set -e

die () {
    echo "install-syspackage: $@" >&2
    sudo journalctl -u diod --no-pager >&2 || true
    exit 1
}
log () {
    echo "install-syspackage: $@"
}

test -f t/etc/syspackage.conf || die "run from the top of the build tree"
sudo -n true || die "passwordless sudo is required"
test -e /etc/diod.conf && die "/etc/diod.conf exists; run on a clean machine"

#  1. install the package
set -- debbuild/diod_*.deb
test $# -eq 1 -a -f "$1" || die "expected one package in debbuild, found: $*"
log "installing $1"
sudo dpkg -i "$1" || die "dpkg -i failed"

#  2. without a config, the service is skipped, not failed
state=$(systemctl show diod -p ActiveState --value)
result=$(systemctl show diod -p Result --value)
log "without a config: ActiveState=$state Result=$result"
test "$state" = inactive || die "diod.service is $state without a config"
test "$result" = success || die "diod.service result is $result without a config"

#  3. install the test config and start diod
sudo mkdir -p /var/lib/diod-test/rw /var/lib/diod-test/ro /run/diod-test
sudo chmod 1777 /var/lib/diod-test/rw
sudo install -m 644 t/etc/syspackage.conf /etc/diod.conf
sudo systemctl start diod || die "systemctl start diod failed"
systemctl is-active -q diod || die "diod.service is not active"
log "diod.service is running with the test config"
