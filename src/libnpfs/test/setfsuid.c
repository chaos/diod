/*************************************************************\
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

/* check that threads can independently setfsuid
 *
 * N.B. error handling for setfsuid/setfsgid is broken in the kernel.
 * Two errors can be detected:
 * - return -1 with errno set (EINVAL)
 * - return of value != previous fsuid/fsgid
 * However, they can silently fail, e.g. if process doesn't have CAP_SETUID.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <grp.h>
#include <sys/fsuid.h>

#include "src/libtap/tap.h"

#define TEST_UID 100
#define TEST_GID 100

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

static state_t         state = S0;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  state_cond = PTHREAD_COND_INITIALIZER;

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

static void
check_fsid (char *s, uid_t uid, gid_t gid)
{
    int fd;
    char path[] = "/tmp/testfsuid.XXXXXX";
    struct stat sb;

    fd = mkstemp (path);
    if (fd < 0)
        BAIL_OUT ("mkstemp: %s", strerror (errno));
    if (fstat (fd, &sb) < 0)
        BAIL_OUT ("fstat: %s", strerror (errno));
    if (unlink (path) < 0)
        BAIL_OUT ("unlink %s: %s", path, strerror (errno));
    ok (sb.st_uid == uid  && sb.st_gid == gid,
        "%s: temp file created with %d:%d", s, uid, gid);
}

static void
change_fsid (char *s, uid_t fromuid, gid_t fromgid, uid_t uid, gid_t gid)
{
    int u;
    int g;

    diag ("%s: changing to %d:%d", s, uid, gid);
    if (fromuid != uid) {
        u = setfsuid (uid);
        if (u == -1)
            diag ("%s: setfsuid: %s", s, strerror (errno));
        else if (u != fromuid)
            diag ("%s: setfsuid returned %d (wanted %d)", s, u, fromuid);
    }
    if (fromgid != gid) {
        g = setfsgid (gid);
        if (g == -1)
            diag ("%s: setfsgid: %s", s, strerror (errno));
        if (g != fromgid)
            diag ("%s: setfsgid returned %d (wanted %d)", s, g, fromgid);
    }
}

static void
change_state (state_t s)
{
    test_lock (&state_lock);
    state = s;
    test_condsig (&state_cond);
    test_unlock (&state_lock);
}

static void
wait_state (state_t s)
{
    test_lock (&state_lock);
    while ((state != s))
        test_condwait (&state_cond, &state_lock);
    test_unlock (&state_lock);
}

static void *proc1 (void *a)
{
    check_fsid ("task1", 0, 0);
    change_state (S1);
    wait_state (S2);
    check_fsid ("task1", 0, 0);
    change_fsid ("task1", 0, 0, TEST_UID, TEST_GID);
    check_fsid ("task1", TEST_UID, TEST_GID);
    change_state (S3);
    wait_state (S4);
    check_fsid ("task1", TEST_UID, TEST_GID);
    return NULL;
}

static void *proc2 (void *a)
{
    wait_state (S1);
    check_fsid ("task2", 0, 0);
    change_fsid ("task2", 0, 0, TEST_UID, TEST_GID);
    check_fsid ("task2", TEST_UID, TEST_GID);
    change_state (S2);
    wait_state (S3);
    change_fsid ("task2", TEST_UID, TEST_GID, 0, 0);
    check_fsid ("task2", 0, 0);
    change_state (S4);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t t1, t2;

    if (geteuid () != 0 || getenv ("FAKEROOTKEY") != NULL)
        plan (SKIP_ALL, "this test must run as root");
    plan (NO_PLAN);

    check_fsid ("task0", 0, 0);

    test_thread_create (&t1, proc1, NULL);
    test_thread_create (&t2, proc2, NULL);

    test_thread_join (t2, NULL);
    test_thread_join (t1, NULL);

    check_fsid ("task0", 0, 0);

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
