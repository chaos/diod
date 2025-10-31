/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

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

#include "src/libdiod/diod_log.h"

static inline void
_lock (pthread_mutex_t *l)
{
    int n = pthread_mutex_lock (l);
    if (n)
        errn_exit (n, "_lock");
}
static inline void
_unlock (pthread_mutex_t *l)
{
    int n = pthread_mutex_unlock (l);
    if (n)
        errn_exit (n, "_unlock");
}
static inline void
_condsig (pthread_cond_t *c)
{
    int n = pthread_cond_signal (c);
    if (n)
        errn_exit (n, "_condsig");
}
static inline void
_condwait (pthread_cond_t *c, pthread_mutex_t *l)
{
    int n = pthread_cond_wait (c, l);
    if (n)
        errn_exit (n, "_condwait");
}
static inline void
_create (pthread_t *t, void *(f)(void *), void *a)
{
    int n = pthread_create (t, NULL, f, a);
    if (n)
        errn_exit (n,"_create");
}
static inline void
_join (pthread_t t, void **a)
{
    int n = pthread_join (t, a);
    if (n)
        errn_exit (n,"_join");
}
static inline int
_mkstemp (char *p)
{
    int fd = mkstemp (p);
    if (fd < 0)
        err_exit ("_mkstemp");
    return fd;
}
static inline void
_fstat (int fd, struct stat *sb)
{
    if (fstat (fd, sb) < 0)
        err_exit ("_fstat");
}
static inline void
_unlink (char *p)
{
    if (unlink (p) < 0)
        err_exit ("_unlink");
}
static inline void
_fchown (int fd, uid_t u, gid_t g)
{
    if (fchown (fd, u, g) < 0)
        err_exit ("_fchown");
}
static inline void
_fchmod (int fd, mode_t m)
{
    if (fchmod (fd, m) < 0)
        err_exit ("_fchmod");
}
static inline void
_setgroups (size_t s, gid_t *g)
{
    if (setgroups (s, g) < 0)
        err_exit ("_setgroups");
}
static inline int
_getgroups (size_t s, gid_t *g)
{
    int n = getgroups (s, g);
    if (n < 0)
        err_exit ("_getgroups");
    return n;
}
static inline void
_setreuid (uid_t r, uid_t u)
{
    if (setreuid (r, u) < 0)
        err_exit ("_setreuid");
}
static inline void
_setregid (gid_t r, gid_t g)
{
    if (setregid (r, g) < 0)
        err_exit ("_setregid");
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
