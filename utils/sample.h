/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

typedef struct sample_struct *sample_t;

sample_t sample_create (int stale_secs);
void sample_destroy (sample_t s);
sample_t sample_copy (sample_t s1);

void sample_invalidate (sample_t s);
void sample_update (sample_t s, double val, time_t t);

void sample_add (sample_t s1, sample_t s2);
void sample_max (sample_t s1, sample_t s2);
void sample_min (sample_t s1, sample_t s2);

double sample_rate (sample_t s, time_t tnow);
double sample_val (sample_t s, time_t tnow);

int sample_val_cmp (sample_t s1, sample_t s2, time_t tnow);
int sample_rate_cmp (sample_t s1, sample_t s2, time_t tnow);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
