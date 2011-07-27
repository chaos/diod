/* tflock.c - test BSD advisory file locks with multiple processes */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdarg.h>
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
    pid_t pid;
    int status;

    diod_log_init (argv[0]);

    if (argc != 2) {
        msg ("Usage: tflock file");
        exit (1);
    }

    msg ("1. Conflicting write locks cannot be held by two processes");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    if (flock (fd, LOCK_EX | LOCK_NB) < 0)
        err_exit ("fd: write-lock failed, aborting");
    msg ("fd: write-locked");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child forked");
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2 open %s", argv[1]);
            msg ("fd2: open (child)");
            if (flock (fd2, LOCK_EX | LOCK_NB) < 0) {
                err ("fd2: write-lock failed");
                exit (0);
            }
            msg_exit ("fd2: write-locked");
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("waitpid");
            if (!WIFEXITED (status))
                msg_exit ("child terminated without exit");
            if (WEXITSTATUS (status) != 0)
                msg_exit ("child exited with %d, aborting",
                          WEXITSTATUS (status));
            msg ("child exited normally");
            break;
    }
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("2. Conflicting read and write locks cannot be held by two processes");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    if (flock (fd, LOCK_EX | LOCK_NB) < 0)
        err_exit ("fd: write-lock failed, aborting");
    msg ("fd: write-locked");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child forked");
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2 open %s", argv[1]);
            msg ("fd2: open (child)");
            if (flock (fd2, LOCK_SH | LOCK_NB) < 0) {
                err ("fd2: read-lock failed");
                exit (0);
            }
            msg_exit ("fd2: read-locked");
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("waitpid");
            if (!WIFEXITED (status))
                msg_exit ("child terminated without exit");
            if (WEXITSTATUS (status) != 0)
                msg_exit ("child exited with %d, aborting",
                          WEXITSTATUS (status));
            msg ("child exited normally");
            break;
    }
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("3. Conflicting read locks CAN be held by two processes");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    if (flock (fd, LOCK_SH | LOCK_NB) < 0)
        err_exit ("fd: read-lock failed, aborting");
    msg ("fd: read-locked");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child forked");
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2 open %s", argv[1]);
            msg ("fd2: open (child)");
            if (flock (fd, LOCK_SH | LOCK_NB) < 0)
                err_exit ("fd2: read-lock failed");
            msg ("fd2: read-locked");
            exit (0);
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                err_exit ("waitpid");
            if (!WIFEXITED (status))
                msg_exit ("child terminated without exit");
            if (WEXITSTATUS (status) != 0)
                msg_exit ("child exited with %d, aborting",
                          WEXITSTATUS (status));
            msg ("child exited normally");
            break;
    }
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
