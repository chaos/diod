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
#include <sys/capability.h>
#include <sys/fsuid.h>
#include <string.h>

#include "src/libtest/thread.h"
#include "src/libtest/state.h"
#include "src/libtap/tap.h"

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

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

static void *proc1 (void *a)
{
    /* 1) task 1, expect clr */
    check_capability ("task1", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task1", "CHOWN", CAP_CHOWN, CAP_CLEAR);
    test_state_change (S1);
    test_state_wait (S2);
    /* 4) task 1, still clr */
    check_capability ("task1", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task1", "CHOWN", CAP_CHOWN, CAP_CLEAR);
    diag ("task1: clr cap");
    set_capability ("task1", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    test_state_change (S3);
    test_state_wait (S4);
    return NULL;
}

static void *proc2 (void *a)
{
    /* 2) task 2, expect clr */
    test_state_wait (S1);
    check_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_CLEAR);
    check_capability ("task2", "CHOWN", CAP_CHOWN, CAP_CLEAR);
    diag ("task2: set cap");
    set_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    set_capability ("task2", "CHOWN", CAP_CHOWN, CAP_SET);
    /* 3) task 2, expect set */
    check_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task2", "CHOWN", CAP_CHOWN, CAP_SET);
    test_state_change (S2);
    test_state_wait (S3);
    /* 5) task 2, expect set */
    check_capability ("task2", "DAC_OVERRIDE", CAP_DAC_OVERRIDE, CAP_SET);
    check_capability ("task2", "CHOWN", CAP_CHOWN, CAP_SET);
    test_state_change (S4);
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

int main(int argc, char *argv[])
{
    if (geteuid () != 0 || getenv ("FAKEROOTKEY") != NULL)
        plan (SKIP_ALL, "this test must run as root");
    plan (NO_PLAN);

    test_state_init (S0);
    proc0 (); // spawns proc1 and proc2

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
