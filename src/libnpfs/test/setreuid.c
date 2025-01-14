/*************************************************************\
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

/* check that threads CANNOT independently setreuid */

/* This was an assumption made in the old npfs code, no longer true
 * post-linuxthreads.  It's here as a demonstration of that fact,
 * and in case anything changes in this area we might want to flag it.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <grp.h>

#include "src/libtap/tap.h"

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
    while ((state != s))
        test_condwait (&state_cond, &state_lock);
    test_unlock (&state_lock);
}

static void *proc1 (void *a)
{
    ok (geteuid () == 0, "task1: geteuid returned 0");
    change_state (S1);

    wait_state (S2);
    ok (geteuid () == 100, "task1: geteuid returned 100");
    ok (setreuid (0, 0) == 0, "task1: setreuid 0 0 worked");
    ok (setreuid (-1, 101) == 0, "task1: setreuid -1 %d works", 101);
    ok (geteuid () == 101, "task1: geteuid returned 101");
    change_state (S3);

    wait_state (S4);
    ok (geteuid () == 102, "task1: geteuid returned 102");
    return NULL;
}

static void *proc2 (void *a)
{
    wait_state (S1);
    ok (geteuid () == 0, "task2: geteuid returned 0");
    ok (setreuid (0, 0) == 0, "setreuid 0 0 worked");
    ok (setreuid (-1, 100) == 0, "setreuid -1 %d worked", 100);
    ok (geteuid () == 100, "task2: geteuid returned 100");
    change_state (S2);

    wait_state (S3);
    ok (geteuid () == 101, "task2: geteuid returned 101");
    ok (setreuid (0, 0) == 0, "setreuid 0 0 worked");
    ok (setreuid (-1, 102) == 0, "setreuid -1 %d worked", 102);
    ok (geteuid () == 102, "task2: geteuid returned 102");
    change_state (S4);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (geteuid () != 0 || getenv ("FAKEROOTKEY") != NULL)
        plan (SKIP_ALL, "this test must run as root");

    plan (NO_PLAN);

    pthread_t t1, t2;

    ok (geteuid () == 0, "task0: geteuid returned 0");

    test_thread_create (&t1, proc1, NULL);
    test_thread_create (&t2, proc2, NULL);

    test_thread_join (t2, NULL);
    test_thread_join (t1, NULL);

    ok (geteuid () == 102, "task0: geteuid returned 102");

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
