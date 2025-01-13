/*************************************************************\
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

/* check that threads can independently set capabilities */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/fsuid.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#if HAVE_LIBCAP
#include <sys/capability.h>
#endif
#include <grp.h>

#include "src/libdiod/diod_log.h"
#include "src/libtap/tap.h"

#if HAVE_LIBCAP
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

static void check_capability (const char *who,
                              const char *capname,
                              cap_value_t capflag,
                              cap_flag_value_t expect)
{
    cap_t cap;
    cap_flag_value_t val;

    if (!(cap = cap_get_proc ()))
        BAIL_OUT ("%s: cap_get_proc failed", who);
    if (cap_get_flag (cap, capflag, CAP_EFFECTIVE, &val) < 0)
        BAIL_OUT ("%s: cap_get_flag %s failed", who, capname);
    if (cap_free (cap) < 0)
        BAIL_OUT ("%s: cap_free failed", capname);
    ok (val == expect,
        "%s %s is %s", who, capname, expect == CAP_SET ? "set" : "clear");
}

static void set_capability (char *who,
                            const char *capname,
                            cap_value_t capflag,
                            cap_flag_value_t val)
{
    cap_t cap;

    if (!(cap = cap_get_proc ()))
        BAIL_OUT ("%s: cap_get_proc failed", who);
    if (cap_set_flag (cap, CAP_EFFECTIVE, 1, &capflag, val) < 0)
        BAIL_OUT ("%s: cap_set_flag %s failed", who, capname);
    if (cap_set_proc (cap) < 0)
        BAIL_OUT ("%s: cap_set_proc failed", who);
    if (cap_free (cap) < 0)
        BAIL_OUT ("%s: cap_free failed", who);
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
    /* 1) task 1, expect clr */
    check_capability ("task1", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task1", "CHOWN", CAP_CHOWN, CAP_CLEAR);
    change_state (S1);
    wait_state (S2);
    /* 4) task 1, still clr */
    check_capability ("task1", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task1", "CHOWN", CAP_CHOWN, CAP_CLEAR);
    diag ("task1: clr cap");
    set_capability ("task1", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    change_state (S3);
    wait_state (S4);
    return NULL;
}

static void *proc2 (void *a)
{
    /* 2) task 2, expect clr */
    wait_state (S1);
    check_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task2", "CHOWN", CAP_CHOWN, CAP_CLEAR);
    diag ("task2: set cap");
    set_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    set_capability ("task2", "CHOWN", CAP_CHOWN, CAP_SET);
    /* 3) task 2, expect set */
    check_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task2", "CHOWN", CAP_CHOWN, CAP_SET);
    change_state (S2);
    wait_state (S3);
    /* 5) task 2, expect set */
    check_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task2", "CHOWN", CAP_CHOWN, CAP_SET);
    change_state (S4);
    return NULL;
}

// main
static void proc0 (void)
{
    pthread_t t1, t2;

    /* root, expect set */
    diag ("task0: initial state");
    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_SET);

    /* non-root, expect clr */
    diag ("task0: setfsuid 1");
    setfsuid (1);
    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_CLEAR);

    /* root, expect set */
    diag ("task0: setfsuid 0");
    setfsuid (0);
    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_SET);

    /* non-root, expect clr */
    diag ("task0: setfsuid 1");
    setfsuid (1);
    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_CLEAR);

    /* root with cap explicitly set, expect set */
    diag ("task0: set cap");
    set_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    set_capability ("task0", "CHOWN", CAP_CHOWN, CAP_SET);
    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_SET);

    /* non-root with cap explicitly set (as root) expect set */
    diag ("task0: setfsuid 2");
    setfsuid (2);
    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_SET);

    /* non-root with cap explicitly clr (as non-root) expect clr */
    diag ("task0: clr cap");
    set_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    set_capability ("task0", "CHOWN", CAP_CHOWN, CAP_CLEAR);
    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_CLEAR);

    test_thread_create (&t1, proc1, NULL);
    test_thread_create (&t2, proc2, NULL);

    test_thread_join (t2, NULL);
    test_thread_join (t1, NULL);

    check_capability ("task0", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task0", "CHOWN", CAP_CHOWN, CAP_CLEAR);
}

#endif

int main(int argc, char *argv[])
{
#if HAVE_LIBCAP
    if (geteuid () != 0 || getenv ("FAKEROOTKEY") != NULL)
        plan (SKIP_ALL, "this test must run as root");
    plan (NO_PLAN);
    diod_log_init (argv[0]);
    proc0 (); // spawns proc1 and proc2
#else
    plan (SKIP_ALL, "libcap2-dev is not installed");
#endif
    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
