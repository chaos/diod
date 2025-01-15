/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef _LIBTEST_STATE_H
#define _LIBTEST_STATE_H

#include <pthread.h>

void test_state_init (int s);
void test_state_change (int s);
void test_state_wait (int s);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
