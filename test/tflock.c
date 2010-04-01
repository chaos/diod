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
        msg ("Usage: tflock file");
        exit (1);
    }
    if ((fd = open (argv[1], O_RDWR)) < 0) {
        err ("open %s", argv[1]);
        goto done;
    }
    if ((fd2 = open (argv[1], O_RDWR)) < 0) {
        err ("open %s", argv[1]);
        goto done;
    }

    /* N.B. blocking attempt may fail due to incomplete diod implementation */
    if (flock (fd, LOCK_EX) < 0) {
        err ("fd: blocking exclusive request failed");
        if (flock (fd, LOCK_EX | LOCK_NB) < 0) {
            err ("fd: nonblocking exclusive request failed");
            goto done;
        } else 
            msg ("fd: nonblocking exclusive request succeeded");
    } else
        msg ("fd: blocking exclusive request succeeded");

    if (flock (fd2, LOCK_EX | LOCK_NB) < 0)
        err ("fd2: exclusive request failed");
    else {
        msg ("fd2: exclusive request succeeded, aborting");
        goto done; 
    }

    if (flock (fd, LOCK_SH | LOCK_NB) < 0) {
        err ("fd: shared request (downgrade) failed, aborting");
        goto done; 
    } else
        msg ("fd: shared request (downgrade) succeeded");

    if (flock (fd2, LOCK_SH | LOCK_NB) < 0) {
        err ("fd2: shared request failed, aborting");
        goto done; 
    } else
        msg ("fd2: shared request succeeded");

    if (flock (fd2, LOCK_EX | LOCK_NB) < 0)
        err ("fd2: exclusive request failed");
    else {
        msg ("fd2: exclusive request succeeded, aborting");
        goto done; 
    }

    if (flock (fd, LOCK_UN) < 0) {
        err ("fd: unlock failed, aborting");
        goto done;
    } else
        msg ("fd: unlock succeeded");

    if (flock (fd2, LOCK_EX | LOCK_NB) < 0) {
        err ("fd2: exclusive request failed, aborting");
        goto done;
    } else
        msg ("fd2: exclusive request succeeded");

    if (flock (fd2, LOCK_UN) < 0) {
        err ("fd2: unlock failed, aborting");
        goto done;
    } else
        msg ("fd2: unlock succeeded");

done:
    msg ("cleaning up");
    if (fd >= 0 && close (fd) < 0)
        err_exit ("close");
    if (fd2 >= 0 && close (fd2) < 0)
        err_exit ("close");
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
