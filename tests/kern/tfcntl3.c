/* tfcntl3.c - test POSIX advisory record locks within one process */

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
        msg ("Usage: tfcntl3 file");
        exit (1);
    }

    msg ("1. Upgrade read lock to write lock (one fd)");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDWR");
    fl = sf_rdlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK rdlock failed");
    msg ("fd: read-locked");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK wrlock failed");
    msg ("fd: write-locked");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("2. Upgrade read lock to write lock (two fds)");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("open %s", argv[1]);
    msg ("fd: open O_RDWR");
    fl = sf_rdlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK rdlock failed");
    msg ("fd: read-locked");
    if ((fd2 = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd2: open %s", argv[1]);
    msg ("fd2: open O_RDWR");
    fl = sf_wrlock;
    if (fcntl (fd2, F_SETLK, &fl) < 0)
        err_exit ("fd2: fcntl F_SETLK wrlock failed");
    msg ("fd2: write-locked");
    if (close (fd2) < 0)
        err_exit ("close fd2");
    msg ("fd2: closed");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");
   
    msg ("3. Upgrade byte range of write lock (one fd)");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDWR");
    fl = sf_wrlock;
    fl.l_len = 1;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK wrlock (1 byte) failed");
    msg ("fd: write-locked 1 byte");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK wrlock (entire file) failed");
    msg ("fd: write-locked entire file");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("4. Downgrade byte range of write lock (one fd)");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDWR");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK wrlock (entire file) failed");
    msg ("fd: write-locked entire file");
    fl = sf_wrlock;
    fl.l_len = 1;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK wrlock (one byte) failed");
    msg ("fd: write-locked 1 byte");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("5. Downgrade write lock to read lock (one fd)");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDWR");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK wrlock failed");
    msg ("fd: write-locked");
    fl = sf_rdlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK rdlock failed");
    msg ("fd: read-locked");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("6. Read lock with O_RDONLY should succeed");
    if ((fd = open (argv[1], O_RDONLY)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDONLY");
    fl = sf_rdlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: fcntl F_SETLK rdlock failed");
    msg ("fd: read-locked");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");
    
    msg ("7. Read lock with O_WRONLY should fail");
    if ((fd = open (argv[1], O_WRONLY)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_WRONLY");
    fl = sf_rdlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err ("fd: fcntl F_SETLK rdlock failed");
    else
        msg ("fd: read-locked");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("8. Write lock with O_RDONLY should fail");
    if ((fd = open (argv[1], O_RDONLY)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDONLY");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err ("fd: fcntl F_SETLK wrlock failed");
    else
        msg ("fd: write-locked");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("9. Write lock with O_WRONLY should succeed");
    if ((fd = open (argv[1], O_WRONLY)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_WRONLY");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err ("fd: fcntl F_SETLK wrlock failed");
    else
        msg ("fd: write-locked");
    if (close (fd) < 0)
        err_exit ("close fd");
    msg ("fd: closed");

    msg ("10. Write lock is not inherited across a fork");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDWR");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: write-lock failed, aborting");
    msg ("fd: write-locked");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child forked");
            fl = sf_rdlock;
            if (fcntl (fd, F_SETLK, &fl) < 0) {
                err ("fd: read-lock failed (child)");
                exit (0);
            }
            msg_exit ("fd: read-locked (child)");
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

    msg ("11. Write lock is dropped if another fd to same file is closed");
    if ((fd = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd: open %s", argv[1]);
    msg ("fd: open O_RDWR");
    fl = sf_wrlock;
    if (fcntl (fd, F_SETLK, &fl) < 0)
        err_exit ("fd: write-lock failed, aborting");
    msg ("fd: write-locked");
    if ((fd2 = open (argv[1], O_RDWR)) < 0)
        err_exit ("fd2: open %s", argv[1]);
    msg ("fd2: open O_RDWR");
    if (close (fd2) < 0)
        err_exit ("close fd2");
    msg ("fd2: closed");
    switch (pid = fork ()) {
        case -1:
            err_exit ("fork");
        case 0: /* child */
            msg ("child forked");
            if (close (fd) < 0)
                err_exit ("close fd");
            msg ("fd: closed (child)");
            if ((fd2 = open (argv[1], O_RDWR)) < 0)
                err_exit ("fd2: open %s", argv[1]);
            msg ("fd2: open O_RDWR (child)");
            fl = sf_rdlock;
            if (fcntl (fd2, F_SETLK, &fl) < 0) {
                err ("fd2: read-lock failed (child)");
                exit (1);
            }
            msg ("fd2: read-locked (child)");
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

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
