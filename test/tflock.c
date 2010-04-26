/* tflock.c - test BSD adsvisory file locks */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <grp.h>

#include "diod_log.h"

#include "test.h"


int
main (int argc, char *argv[])
{
    int fd = -1;
    int fd2 = -1;

    diod_log_init (argv[0]);

    if (argc != 2) {
        fprintf (stderr, "Usage: tflock file");
        exit (1);
    }

    /* Try to write-lock a file twice on different fd's in the same process.
     * The second one should fail.
     */
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    if (flock (fd, LOCK_EX) < 0)
        err_exit ("fd: write-lock failed, aborting");
    msg ("fd: write-locked");

    if ((fd2 = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd2: open");
    if (flock (fd2, LOCK_EX | LOCK_NB) < 0)
        err ("fd2: second write-lock failed");
    else
        msg_exit ("fd2: write-locked twice, aborting");

    /* Downgrade the original write-lock to a read-lock and try again
     * with a read-lock on the second fd.  This time it should succeed.
     */
    if (flock (fd, LOCK_SH | LOCK_NB) < 0)
        err_exit ("fd: read-lock (downgrade) failed, aborting");
    msg ("fd: read-locked (downgrade)");
    if (flock (fd2, LOCK_SH | LOCK_NB) < 0)
        err_exit ("fd2: read-lock failed, aborting");
    msg ("fd2: read-locked");

    /* Try to upgrade the second fd to a write-lock.  With the
     * first fd still holding the read-lock.  This should fail.
     */

    if (flock (fd2, LOCK_EX | LOCK_NB) < 0)
        err ("fd2: write-lock failed");
    else
        msg_exit ("fd2: write-locked with a read lock held, aborting");

    /* Unlock the original read-lock and try again.  This time it should
     * succeed.
     */
    if (flock (fd, LOCK_UN) < 0)
        err_exit ("fd: unlock failed, aborting");
    msg ("fd: unlocked");
    if (flock (fd2, LOCK_EX | LOCK_NB) < 0)
        err_exit ("fd2: write-lock failed, aborting");
    msg ("fd2: write-locked");
    if (flock (fd2, LOCK_UN) < 0)
        err_exit ("fd2: unlock failed, aborting");
    msg ("fd2: unlocked");

    msg ("success!");
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
