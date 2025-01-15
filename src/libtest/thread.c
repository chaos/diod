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
#include <pthread.h>
#include <string.h>

#include "thread.h"
#include "src/libtap/tap.h"

void test_lock (pthread_mutex_t *l)
{
    int n = pthread_mutex_lock (l);
    if (n)
        BAIL_OUT ("pthread_mutex_lock: %s", strerror (n));
}

void test_unlock (pthread_mutex_t *l)
{
    int n = pthread_mutex_unlock (l);
    if (n)
        BAIL_OUT ("pthread_mutex_unlock: %s", strerror (n));
}

void test_condsig (pthread_cond_t *c)
{
    int n = pthread_cond_signal (c);
    if (n)
        BAIL_OUT ("pthread_cond_signal: %s", strerror (n));
}

void test_condwait (pthread_cond_t *c, pthread_mutex_t *l)
{
    int n = pthread_cond_wait (c, l);
    if (n)
        BAIL_OUT ("pthread_cond_wait: %s", strerror (n));
}

void test_thread_create (pthread_t *t, void *(f)(void *), void *a)
{
    int n = pthread_create (t, NULL, f, a);
    if (n)
        BAIL_OUT ("pthread_create: %s", strerror (n));
}

void test_thread_join (pthread_t t, void **a)
{
    int n = pthread_join (t, a);
    if (n)
        BAIL_OUT ("pthread_join: %s", strerror (n));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
