/* tfcntl2.c - test POSIX adsvisory record locks with multiple processes */

#if HAVE_CONFIG_H
#include "config.h"
#endif
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
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <grp.h>

#include "diod_log.h"

#include "test.h"

static const struct flock sf_rdlock = {
    .l_type = F_RDLCK,
    .l_whence = SEEK_SET,
    .l_start = 0,
    .l_len = 0
};
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

    msg ("1. Conflicting write locks cannot be held by two processes");
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
            msg ("child forked");
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2 open %s", argv[1]);
            msg ("fd2: open (child)");
            fl = sf_wrlock;
            if (fcntl (fd2, F_SETLK, &fl) < 0) {
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
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
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
            fl = sf_rdlock;
            if (fcntl (fd2, F_SETLK, &fl) < 0) {
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
    fl = sf_rdlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
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
            fl = sf_rdlock;
            if (fcntl (fd2, F_SETLK, &fl) < 0)
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

    msg ("4. Non-conflicting write locks CAN be held by two processes");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    fl = sf_wrlock;
    fl.l_len = 1;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: write-lock 1 byte failed, aborting");
    msg ("fd: write-locked 1 byte");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child forked");
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2 open %s", argv[1]);
            msg ("fd2: open (child)");
            fl = sf_wrlock;
            fl.l_start = 1;
            if (fcntl (fd2, F_SETLK, &fl) < 0)
                err_exit ("fd2: write-lock rest of file failed");
            msg ("fd2: write-locked rest of file");
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

    msg ("5. Non-conflicting read and write locks CAN be held by two processes");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open");
    fl = sf_wrlock;
    fl.l_len = 1;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: write-lock 1 byte failed, aborting");
    msg ("fd: write-locked 1 byte");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child forked");
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2 open %s", argv[1]);
            msg ("fd2: open (child)");
            fl = sf_rdlock;
            fl.l_start = 1;
            if (fcntl (fd2, F_SETLK, &fl) < 0)
                err_exit ("fd2: read-lock rest of file failed");
            msg ("fd2: read-locked rest of file");
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
