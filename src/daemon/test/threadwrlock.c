/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* demonstrate that fcntl locking is only effective at process granularity */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <grp.h>

#include "src/libtap/tap.h"

#define TEST_UID 100
#define TEST_GID 100

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

static state_t         state = S0;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_cond = PTHREAD_COND_INITIALIZER;
static char path[] = "/tmp/test.fcntl.XXXXXX";
static int fd = -1;

static void test_lock (pthread_mutex_t *l)
{
    int n = pthread_mutex_lock (l);
    if (n)
        BAIL_OUT ("pthread_mutex_lock: %s", strerror (n));
}
static void test_unlock (pthread_mutex_t *l)
{
    int n = pthread_mutex_unlock (l);
    if (n)
        BAIL_OUT ("pthread_mutex_unlock: %s", strerror (n));
}
static void test_condsig (pthread_cond_t *c)
{
    int n = pthread_cond_signal (c);
    if (n)
        BAIL_OUT ("pthread_cond_signal: %s", strerror (n));
}
static void test_condwait (pthread_cond_t *c, pthread_mutex_t *l)
{
    int n = pthread_cond_wait (c, l);
    if (n)
        BAIL_OUT ("pthread_cond_wait: %s", strerror (n));
}
static void test_thread_create (pthread_t *t, void *(f)(void *), void *a)
{
    int n = pthread_create (t, NULL, f, a);
    if (n)
        BAIL_OUT ("pthread_create: %s", strerror (n));
}
static void test_thread_join (pthread_t t, void **a)
{
    int n = pthread_join (t, a);
    if (n)
        BAIL_OUT ("pthread_join: %s", strerror (n));
}

static void change_state (state_t s)
{
    test_lock (&state_lock);
    state = s;
    test_condsig (&state_cond);
    test_unlock (&state_lock);
}

static void wait_state (state_t s)
{
    test_lock (&state_lock);
    while (state != s)
        test_condwait (&state_cond, &state_lock);
    test_unlock (&state_lock);
}

static void mkfile (void)
{
    char buf[1024];

    memset (buf, 0, sizeof (buf));
    if ((fd = mkstemp (path)) < 0)
        BAIL_OUT ("mkstemp: %s", strerror (errno));
    if (write (fd, buf, sizeof (buf)) < 0)
        BAIL_OUT ("write: %s", strerror (errno));
    if (close (fd) < 0)
        BAIL_OUT ("close: %s", strerror (errno));
    if ((fd = open (path, O_RDWR)) < 0)
        BAIL_OUT ("open %s: %s", path, strerror (errno));
}

static void *proc1 (void *a)
{
    struct flock f;

    diag ("proc1: locking bytes 0-63 (locked in same process)");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 64;
    ok (fcntl (fd, F_SETLK, &f) == 0,
        "proc1: fcntl F_WRLCK 0-63 works");
    change_state (S1);

    wait_state (S2);
    change_state (S3);
    wait_state (S4);
    return NULL;
}

static void *proc2 (void *a)
{
    struct flock f;

    wait_state (S1);
    diag ("proc2: locking bytes 0-63 (locked in same process)");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 64;
    ok (fcntl (fd, F_SETLK, &f) == 0,
        "proc2: fcntl F_WRLCK 0-63 works");
    change_state (S2);

    wait_state (S3);
    change_state (S4);
    return NULL;
}

int
main (int arg, char *argv[])
{
    pthread_t t1, t2;
    struct flock f;
    pid_t pid;
    int status;
    int rc;

    plan (NO_PLAN);

    mkfile ();

    /* same task/thread contending for write lock */
    diag ("proc0: locking bytes 0-63");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 0;
    f.l_len = 64;
    rc = fcntl (fd, F_SETLK, &f);
    ok (rc == 0, "proc0 fcntl F_WRLCK 0-63 works");

    diag ("proc0: locking bytes 32-63 (locked in same process+thread)");
    f.l_type = F_WRLCK;
    f.l_whence = SEEK_SET;
    f.l_start = 32;
    f.l_len = 64;
    rc = fcntl (fd, F_SETLK, &f);
    ok (rc == 0, "proc0: F_WRLCK 32-63 works");

    /* two threads contending for write lock */
    test_thread_create (&t1, proc1, NULL);
    test_thread_create (&t2, proc2, NULL);

    test_thread_join (t2, NULL);
    test_thread_join (t1, NULL);

    fflush (stderr);

    /* two processes contending for write lock - inherited fd */
    diag ("child0: tries to lock bytes 32-63 (locked in different process)");
    switch (pid = fork ()) {
        case -1:
            BAIL_OUT ("fork: %s", strerror (errno));
        case 0: /* child */
            f.l_type = F_WRLCK;
            f.l_whence = SEEK_SET;
            f.l_start = 32;
            f.l_len = 64;
            if (fcntl (fd, F_SETLK, &f) < 0) {
                diag ("fcntl F_WRLCK 32-63: %s", strerror (errno));
                exit (1);
            }
            exit (0);
            break;
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                BAIL_OUT ("waitpid: %s", strerror (errno));
            ok (WIFEXITED (status) && WEXITSTATUS (status) == 1,
                "child0 failed to lock bytes 32-63 as expected");
            break;
    }

    /* two processes contending for write lock - seperate fd's */
    diag ("child1: locking bytes 32-63 (locked in different process)");
    switch (pid = fork ()) {
        case -1:
            BAIL_OUT ("fork: %s", strerror (errno));
        case 0: /* child */
            if (close (fd) < 0
                || (fd = open (path, O_RDWR)) < 0) {
                diag ("close/open %s (child): %s", path, strerror (errno));
                exit (2);
            }
            f.l_type = F_WRLCK;
            f.l_whence = SEEK_SET;
            f.l_start = 32;
            f.l_len = 64;
            int exit_code = fcntl (fd, F_SETLK, &f) < 0 ? 1 : 0;
            if (exit_code == 1)
                diag ("fcntl F_WRLCK 32-63: %s", strerror (errno));
            (void)close (fd);
            exit (exit_code);
            break;
        default: /* parent */
            if (waitpid (pid, &status, 0) < 0)
                BAIL_OUT ("waitpid: %s", strerror (errno));
            ok (WIFEXITED (status) && WEXITSTATUS (status) == 1,
                "child1 failed to lock bytes 32-63 as expected");
            break;
    }

    if (close (fd) < 0)
        BAIL_OUT ("close: %s", strerror (errno));
    if (unlink (path) < 0)
        BAIL_OUT ("unlink %s: %s", path, strerror (errno));
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */