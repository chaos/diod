/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include "src/libtap/tap.h"

#include "thread.h"
#include "state.h"

static int state;
static pthread_mutex_t state_lock;
static pthread_cond_t state_cond;

void test_state_init (int s)
{
    int e;

    if ((e = pthread_mutex_init (&state_lock, NULL)))
        BAIL_OUT ("pthread_mutex_init: %s", strerror (e));
    if ((e = pthread_cond_init (&state_cond, NULL)))
        BAIL_OUT ("pthread_cond_init: %s", strerror (e));
    state = s;
}

void test_state_change (int s)
{
    test_lock (&state_lock);
    state = s;
    test_condsig (&state_cond);
    test_unlock (&state_lock);
}

void test_state_wait (int s)
{
    test_lock (&state_lock);
    while ((state != s))
        test_condwait (&state_cond, &state_lock);
    test_unlock (&state_lock);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
