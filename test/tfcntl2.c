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

#if 0
static const struct flock sf_rdlock = {
    .l_type = F_RDLCK,
    .l_whence = SEEK_SET,
    .l_start = 0,
    .l_len = 0
};
#endif
static const struct flock sf_wrlock = {
    .l_type = F_WRLCK,
    .l_whence = SEEK_SET,
    .l_start = 0,
    .l_len = 0
};
static const struct flock sf_unlock = {
    .l_type = F_UNLCK,
    .l_whence = SEEK_SET,
    .l_start = 0,
    .l_len = 0
};

int
main (int argc, char *argv[])
{
    int fd = -1;
    int fd2 = -1;
    pid_t pid;
    int status;
    struct flock fl;

    diod_log_init (argv[0]);

    if (argc != 2) {
        msg ("Usage: tfcntl2 file");
        exit (1);
    }

    /* Try to take a write-lock on the same file from two fd's in the
     * same process.  The second one should succeed.
     */
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLKW, &fl) < 0)
        err_exit ("fd: blocking write-lock failed, aborting");
    msg ("fd: write-locked");
    if ((fd2 = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd2: open");
    fl = sf_wrlock;
    if (fcntl (fd2, F_SETLK, &fl) < 0)
        err_exit ("fd2: write-lock failed");
    msg ("fd2: write-locked");
    if (close (fd2) < 0)
        err_exit ("close fd2");
    msg ("fd2: close");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: close");
   
    /* Try to take a write-lock on the same file from two fd's in
     * different processes.  The second one should fail.
     */ 
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: write-lock failed, aborting");
    msg ("fd: write-locked");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2 open %s", argv[1]);
            msg ("fd2(child1): open");
            fl = sf_wrlock;
            if (fcntl (fd2, F_SETLK, &fl) < 0) {
                err ("fd2(child1): write-lock failed");
                exit (0);
            }
            msg_exit ("fd2(child1): write-locked");
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("waitpid");
            if (!WIFEXITED (status))
                msg_exit ("child1 terminated without exit");
            if (WEXITSTATUS (status) != 0)
                msg_exit ("child1 exited with %d, aborting",
                          WEXITSTATUS (status));
            msg ("child1 exited normally");
            break;
    }

    /* Drop the lock in one process and try to take it in the other.
     * Obviously this should work.
     */
    fl = sf_unlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: unlock failed");
    msg ("fd: unlocked");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("open %s", argv[1]);
            msg ("fd2(child2): open");
            fl = sf_wrlock;
            if (fcntl (fd2, F_SETLK, &fl) < 0)
                err_exit ("fd2(child2): write-lock failed (UNEXPECTED)");
            msg ("fd2(child2): write-locked");
            exit (0);
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("waitpid");
            if (!WIFEXITED (status))
                msg_exit ("child2 terminated without exit");
            if (WEXITSTATUS (status) != 0)
                msg_exit ("child2 exited with %d", WEXITSTATUS (status));
            msg ("child2 exited normally");
            break;
    }

    /* After implicit unlock in the child (due to exit == clunk), lock
     * again in the parent.  
     */
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLKW, &fl) < 0)
        err_exit ("fd: write-lock failed, aborting");
    msg ("fd: write-locked");

    msg ("success!");
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
