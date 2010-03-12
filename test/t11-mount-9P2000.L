#!/bin/bash -e

doit ()
{
	echo starting diod
	$PATH_DIOD -c $PATH_DIODCONF -f -l "localhost:2006" -x -m \
			-e $PATH_EXPDIR &
	sleep 1
	echo mounting
	$PATH_DIODMOUNT -o uname=root,port=2006,version=9p2000.L \
			-n localhost:$PATH_EXPDIR $PATH_MNTDIR
	echo running statfs comparison
	./tstatfs $PATH_EXPDIR $PATH_MNTDIR || :  # should succeed

	echo trying rename
	touch $PATH_MNTDIR/a
	./trename $PATH_MNTDIR/a $PATH_MNTDIR/b

	echo trying rename to new directory
	mkdir -p $PATH_MNTDIR/dir
	./trename $PATH_MNTDIR/b $PATH_MNTDIR/dir/c
	rm -f $PATH_MNTDIR/dir/c
	rmdir $PATH_MNTDIR/dir

	echo unmounting
	umount $PATH_MNTDIR
	echo waiting for diod to terminate
	wait
}

TEST=$(basename $0 | cut -d- -f1)
test $(id -u) == 0 || exit 77 #skip if not root


doit >$TEST.out 2>&1

diff $TEST.exp $TEST.out >$TEST.diff
