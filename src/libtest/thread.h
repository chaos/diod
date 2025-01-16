/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBTEST_THREAD_H
#define LIBTEST_THREAD_H

#include <pthread.h>

/* wrappers for pthreads operations that call BAIL_OUT internally on failure */
void test_lock (pthread_mutex_t *l);
void test_unlock (pthread_mutex_t *l);
void test_condsig (pthread_cond_t *c);
void test_condwait (pthread_cond_t *c, pthread_mutex_t *l);
void test_thread_create (pthread_t *t, void *(f)(void *), void *a);
void test_thread_join (pthread_t t, void **a);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
