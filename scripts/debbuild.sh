#!/bin/sh
PACKAGE=diod
USER=$(git config --get user.name)
DEBFULLNAME=$USER
EMAIL=$(git config --get user.email)
DEBEMAIL=$EMAIL

SRCDIR=${1:-$(pwd)}

die() { echo "debbuild: $@" >&2; exit 1; }
log() { echo "debbuild: $@"; }

test -z "$USER" && die "User name not set in git-config"
test -z "$EMAIL" && die "User email not set in git-config"

log "Running make dist"
make dist >/dev/null || exit 1

log "Building package from latest dist tarball"
tarball=$(ls -tr *.tar.gz | tail -1)
version=$(echo $tarball | sed "s/${PACKAGE}-\(.*\)\.tar\.gz/\1/")

rm -rf debbuild
mkdir -p debbuild && cd debbuild || die "cannot create debbuild"

mv ../$tarball .

log "Unpacking $tarball"
tar xvfz $tarball >/dev/null || die "failed to unpack $tarball"

log "Creating debian directory and files"
cd ${PACKAGE}-${version} || die "cannot cd to ${PACKAGE}-${version}"
cp -a ${SRCDIR}/debian . || die "failed to copy debian dir"

export DEBEMAIL DEBFULLNAME
log "Creating debian/changelog"
dch --create --package=$PACKAGE --newversion $version build tree release \
    || die "failed to create debian/changelog"

log "Running debian-buildpackage -b"
dpkg-buildpackage -b || die "dpkg-buildpackage failed"
log "Check debbuild directory for results"
