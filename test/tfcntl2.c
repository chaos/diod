/* tfcntl2.c - test POSIX adsvisory record locks */

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
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

/* Flock emulated with fcntl locks.
 */
static int
_flock (int fd, int cmd)
{
    struct flock l;
    int op = (cmd & LOCK_NB) ? F_SETLK : F_SETLKW;

    if (cmd & LOCK_SH)
        l.l_type = F_RDLCK;
    else if (cmd & LOCK_EX)
        l.l_type = F_WRLCK;
    else if (cmd & LOCK_UN)
        l.l_type = F_UNLCK;
    l.l_whence = SEEK_SET;
    l.l_start = 0;
    l.l_len = 0;

    return fcntl (fd, op, &l);
}

int
main (int argc, char *argv[])
{
    int fd = -1;
    int fd2 = -1;
    pid_t pid;
    int status;

    diod_log_init (argv[0]);

    if (argc != 2) {
        msg ("Usage: tfcntl2 file");
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

    if (_flock (fd, LOCK_EX) < 0) {
        err ("fd: blocking exclusive request failed, aborting");
        goto done;
    } else
        msg ("fd: blocking exclusive request succeeded");

    /* different fd, same process - should succeed */
    if (_flock (fd2, LOCK_EX | LOCK_NB) < 0) {
        err ("fd2: exclusive request failed, this is incorrect behavior!");
    } else
        msg ("fd2: exclusive request succeeded");

    /* different process - should fail */
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            if (_flock (fd2, LOCK_EX | LOCK_NB) < 0) {
                err ("fd2(child): exclusive request failed");
                exit (0);
            }
            msg_exit ("fd2(child): exclusive request succeeded, aborting");
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("waitpid");
            if (!WIFEXITED (status))
                msg_exit ("child terminated without exit");
            if (WEXITSTATUS (status) != 0)
                goto done;
            msg ("child exited normally");
            break;
    }

    if (_flock (fd, LOCK_UN) < 0) {
        err ("fd: unlock failed, aborting");
        goto done;
    } else
        msg ("fd: unlock succeeded");

    /* different process - should succeed */
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            if (_flock (fd2, LOCK_EX | LOCK_NB) < 0)
                err_exit ("fd2(child): exclusive request failed, aborting");
            msg ("fd2(child): exclusive request succeeded");
            exit (0);
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("waitpid");
            if (!WIFEXITED (status))
                msg_exit ("child terminated without exit");
            if (WEXITSTATUS (status) != 0)
                goto done;
            msg ("child exited normally");
            break;
    }

    if (_flock (fd2, LOCK_EX | LOCK_NB) < 0) {
        err ("fd2: exclusive request failed, aborting");
        goto done;
    } else
        msg ("fd2: exclusive request succeeded");

    if (_flock (fd2, LOCK_UN) < 0) {
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
