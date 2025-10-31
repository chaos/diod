#!/bin/sh

test_description='Test Linux kernel 9p client with diod in "runas" mode

Note: this test runs even with
  --disable-auth --disable-config --disable-multiuser
'

. `dirname $0`/sharness.sh

if ! test_have_prereq SUDO; then
        skip_all='passwordless sudo is required'
        test_done
fi
if ! test_have_prereq V9FS_CLIENT; then
        skip_all='linux 9p kernel client is required'
        test_done
fi
if PATH_TCLSH=$(which tclsh); then
	test_set_prereq TCLSH
fi

exportdir=$SHARNESS_TRASH_DIRECTORY/exp
test_under_diod unixsocket \
    --config-file=/dev/null \
    --debug=0x1 \
    --runas=$(id -u) \
    --no-auth \
    --export=$exportdir

# gnome probes for .Trash, autorun.inf, etc asynchronously on new mounts,
# causing umount to fail with EBUSY if still in progress.  Therefore --lazy.
umountcmd="$SUDO umount --lazy"
mountcmd="$SUDO mount -n -t 9p"
mountopts="trans=unix,uname=$(id -un)"

test_flock=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_flock
test_flock_single=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_flock_single
test_atomic_create=$SHARNESS_BUILD_DIRECTORY/src/cmd/test_atomic_create

# usage: create_file name [block_count]
create_file() {
	local name=$1
	local count=${2:-16}
	dd if=/dev/zero of=$name count=$count status=noxfer
}

# usage: stat_min path
stat_min() {
	$PATH_STAT -c "mode=%f owner=%u:%g size=%s blocks=%b blocksize=%B links=%h device=%t:%T mtime=%y ctime=%z atime=%x" $1
}

# usage: statfs_min path
statfs_min() {
	$PATH_STAT -f -c "blocks=% bsize=%S files=%c namelen=%l" $1
}

# Usage: check_chmod src-path dst-path
check_chmod () {
	for i in 0 1 2 3 4 5 6 7; do
	    chmod $i$i$i $1
	    test "$($PATH_STAT -c %a $1)" = "$($PATH_STAT -c %a $2)" || return 1
	done
	return 0;
}

# Usage: check_stat src-path dst-path
check_stat () {
	test "$(stat_min $1)" = "$(stat_min $2)"
}
# Usage: check_statfs src-path dst-path
check_statfs () {
	test "$(statfs_min $1)" = "$(statfs_min $2)"
}

test_expect_success 'create export/mount directories' '
	mkdir -p exp mnt mnt2
'
test_expect_success 'wait for server socket' '
	waitsock $DIOD_SOCKET 30
'
test_expect_success 'mount filesystem with access=<uid> on mnt' '
	$mountcmd -oaname=$exportdir,$mountopts,access=$(id -u) \
	    $DIOD_SOCKET mnt
'
test_expect_success STAT 'file system type is v9fs' '
	echo v9fs >type.exp &&
	stat -f -c "%T" mnt >type.out &&
	test_cmp type.exp type.out
'
test_expect_success STAT 'client/server mount point stats match' '
	check_stat exp mnt
'
test_expect_success STAT 'client/server fs stats match' '
	check_statfs exp mnt
'
test_expect_success STAT 'create a directory' '
	mkdir mnt/dir &&
	check_stat exp/dir mnt/dir
'
test_expect_success STAT 'create a file' '
	create_file mnt/dir/a 1024 &&
	cmp exp/dir/a mnt/dir/a &&
	check_stat exp/dir/a mnt/dir/a
'
test_expect_success STAT 'create a hard link' '
	ln mnt/dir/a mnt/dir/a_link &&
	check_stat exp/dir/a_link mnt/dir/a_link &&
	cmp exp/dir/a mnt/dir/a_link
'
test_expect_success STAT 'create a symbolic link' '
	ln -s a mnt/dir/a_slink &&
	check_stat exp/dir/a_slink mnt/dir/a_slink &&
	cmp exp/dir/a mnt/dir/a_slink
'
test_expect_success 'readlink on link returns target' '
	echo a >readlink.exp &&
	readlink mnt/dir/a_slink >readlink.out &&
	test_cmp readlink.exp readlink.out
'
test_expect_success STAT 'chmod file/dir propagates client to server' '
	check_chmod mnt/dir/a exp/dir/a &&
	check_chmod mnt/dir exp/dir
'
test_expect_success STAT 'chmod file/dir propagates server to client' '
	check_chmod exp/dir/a mnt/dir/a &&
	check_chmod exp/dir mnt/dir
'
test_expect_success 'rename a file' '
	mv mnt/dir/a mnt/dir/a2
'
test_expect_success 'move a file to a new direcory' '
	mkdir -p mnt/dir2 &&
	mv mnt/dir/a2 mnt/dir2/a
'
test_expect_success 'copy a file' '
	cp mnt/dir2/a mnt/dir/a
'
test_expect_success 'create a large file, then fsync+fdatasync' '
	dd if=/dev/zero of=mnt/dir/c count=4096 conv=fsync status=noxfer &&
	cmp exp/dir/c mnt/dir/c
'
test_expect_success STAT 'append 3 bytes to a file' '
	orig_size=$($PATH_STAT -c "%s" mnt/dir/a) &&
	echo -n foo >>mnt/dir/a &&
	new_size=$($PATH_STAT -c "%s" mnt/dir/a) &&
	test $new_size -eq $(($orig_size+3)) &&
	cmp exp/dir/a mnt/dir/a
'
test_expect_success 'remove a file' '
	rm mnt/dir/c &&
	test ! -e exp/dir/c
'
test_expect_success 'create a file' '
	create_file mnt/dir/d 1024
'
test_expect_success FLOCK 'flock-write a file' '
	$PATH_FLOCK -x mnt/dir/d true
'
test_expect_success FLOCK 'flock-read a file' '
	$PATH_FLOCK -s mnt/dir/d true
'
test_expect_success 'run flock concurrency test' '
	$test_flock  mnt/dir/d
'
test_expect_success 'run flock single-process test' '
	$test_flock_single  mnt/dir/d
'
test_expect_success 'chown a file to current owner' '
	chown $(id -u) mnt/dir/d
'
test_expect_success 'chgrp a file to current owner' '
	chgrp $(id -g) mnt/dir/d
'
test_expect_success STAT 'create a new directory with mode' '
	mkdir -m 500 mnt/dir3 &&
	echo "500" >mkdir_mode.exp &&
	stat -c "%a" exp/dir3 >mkdir_mode.out &&
	test_cmp mkdir_mode.exp mkdir_mode.out
'
test_expect_success 'create and remove lots of directories' '
	mkdir -p mnt/{1,2,3,4}/{1,2,3,4}/{1,2,3,4}/{1,2,3,4} &&
	rm -rf mnt/{1,2,3,4}
'
test_expect_success 'mktemp works' '
	mktemp -p mnt
'
test_expect_success 'mktemp -d works' '
	mktemp -d -p mnt
'
test_expect_success STAT 'create a fifo' '
	mkfifo mnt/fifo &&
	test $($PATH_STAT -c %F exp/fifo) = fifo
'
test_expect_success 'df works' '
	df mnt
'
test_expect_success 'du works' '
	du mnt
'
test_expect_success 'tar works' '
	tar cvf foo.tar mnt
'
# Test for gcode issue 29: EIO during tcl.tk file delete --force
test_expect_success TCLSH 'tclsh file delete -force works' '
	mkdir -p mnt/tcldir/x/y &&
	(cd mnt/tcldir && $PATH_TCLSH <<-EOT
	file delete -force x
	EOT
	) &&
	! test -d mnt/tcldir/x
'
# Trigger a Tflush
test_expect_success 'start a streaming write then abort it' '
	dd if=/dev/zero of=mnt/stream bs=1024k count=500 status=noxfer &
	pid=$! &&
	sleep 0.5 &&
	kill -15 $pid &&
	test_expect_code 143 wait $pid
'
test_expect_success 'create 1000 files in a exp/bigdir' '
	mkdir -p exp/bigdir &&
	for i in $(seq 1 1000); do \
	    touch exp/bigdir/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.$i;
	done
'
test_expect_success 'list long directory' '
	ls mnt/bigdir >bigdir.out &&
	test $(wc -l <bigdir.out) -eq 1000
'
test_expect_success 'root cannot read file' '
	test -f mnt/dir/a &&
	test_must_fail $SUDO cat mnt/dir/a >/dev/null 2>rootread.err &&
	grep "not permitted" rootread.err
'
test_expect_success STAT 'root cannot stat file' '
	test_must_fail $SUDO $PATH_STAT mnt/dir/a >/dev/null 2>rootstat.err &&
	grep "not permitted" rootstat.err
'
test_expect_success 'root cannot create file' '
	test_must_fail $SUDO touch mnt/dir/asroot 2>rootwrite.err &&
	grep "not permitted" rootwrite.err
'

# Usage: makefiles dir
makefiles () {
    local path=$1
    local seq=0
    local rc=0
    local u g o
    for u in 0 1 2 3 4 5 6 7; do
        for g in 0 1 2 3 4 5 6 7; do
            for o in 0 1 2 3 4 5 6 7; do
                install -m 0$u$g$o /dev/null $path/f.$seq || rc=1
                seq=$(($seq + 1))
            done
        done
    done
    return $rc
}
# Usage: checkmodes dir
checkmodes () {
    local path=$1
    local seq=0
    local rc=0
    local u g o
    for u in 0 1 2 3 4 5 6 7; do
        for g in 0 1 2 3 4 5 6 7; do
            for o in 0 1 2 3 4 5 6 7; do
                test "$($PATH_STAT -c "%a" $path/f.$seq)" -eq "$u$g$o" || rc=1
                seq=$(($seq + 1))
            done
        done
    done
    return $rc
}

test_expect_success 'atomically create files with all the modes' '
	mkdir mnt/ptest &&
	makefiles mnt/ptest
'
test_expect_success 'all the modes were set on the server' '
	checkmodes exp/ptest
'
test_expect_success 'creating a file with O_CREAT|OEXCL fails if file exists' '
	$test_atomic_create mnt/atomic_create
'
test_expect_success 'mount filesystem with access=any on mnt2' '
	$mountcmd \
	    -oaname=$exportdir,$mountopts,access=any \
	    $DIOD_SOCKET mnt2
'
test_expect_success STAT 'create file as root on mnt2' '
	$SUDO touch mnt2/dir/asroot &&
	test "$($PATH_STAT -c "%u:%g" exp/dir/asroot)" = "$(id -u):$(id -g)"
'
test_expect_success 'create file on server, appears on client' '
	touch exp/dir/z &&
	test -f mnt/dir/z
'
test_expect_success 'remove file on server, removed on client' '
	rm exp/dir/z &&
	! test -f mnt/dir/z
'
test_expect_success 'unmount mnt2' '
	$umountcmd mnt2
'
test_expect_success 'mount filesystem with access=user on mnt2' '
	$mountcmd \
	    -oaname=$exportdir,$mountopts,access=user \
	    $DIOD_SOCKET mnt2
'
test_expect_success STAT 'create a file as me' '
	create_file mnt2/dir/asme &&
	test "$($PATH_STAT -c "%u:%g" exp/dir/asme)" = "$(id -u):$(id -g)"
'
test_expect_success 'root cannot create file' '
	test_must_fail $SUDO touch mnt2/dir/asroot2 2>rootwrite2.err &&
	grep "not permitted" rootwrite2.err
'
test_expect_success 'unmount mnt2' '
	$umountcmd mnt2
'
test_expect_success 'mount filesystem with uname=root,access=any fails' '
	test_must_fail $mountcmd \
	    -oaname=$exportdir,uname=root,trans=unix,access=any \
	    $DIOD_SOCKET mnt2
'
test_expect_success 'clean up mnt2, if mount unexpectedly succeeded' '
	$umountcmd mnt2 2>/dev/null || :
'
test_expect_success 'unmount mnt' '
	$umountcmd mnt
'

# N.B. Server exits when conn count drops to zero.

test_done

# vi: set ft=sh
