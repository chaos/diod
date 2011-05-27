/*****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  This module written by Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-232438
 *  All Rights Reserved.
 *
 *  This file is part of Lustre Monitoring Tool, version 2.
 *  Authors: H. Wartens, P. Spencer, N. O'Neill, J. Long, J. Garlick
 *  For details, see http://code.google.com/p/lmt/.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/

/* sample.c - ADT for 2-point data values used in rate calculation */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "diod_log.h"
#include "sample.h"

struct sample_struct {
    double val[2];
    time_t time[2];
    int valid; /* count of valid samples [0,1,2] */
    int stale_secs;
};

sample_t
sample_create (int stale_secs)
{
    sample_t s = malloc (sizeof (*s));

    if (!s)
        msg_exit ("out of memory");
    memset (s, 0, sizeof (*s));
    s->stale_secs = stale_secs;

    return s;
}

void
sample_destroy (sample_t s)
{
    free (s);
}

sample_t
sample_copy (sample_t s1)
{
    sample_t s = malloc (sizeof (*s));

    if (!s)
        msg_exit ("out of memory");
    memcpy (s, s1, sizeof (*s));

    return s;
}

/* Invalidate both data points.
 */
void
sample_invalidate (sample_t s)
{
    s->valid = 0;
}

/* Update sample with val @ timestamp t.
 */
void
sample_update (sample_t s, double val, time_t t)
{
    if (s->valid == 0) {
        s->time[1] = t;
        s->val[1] = val;
        s->valid++;
    } else if (s->time[1] < t) {
        s->time[0] = s->time[1];
        s->val[0] = s->val[1];
        s->time[1] = t;
        s->val[1] = val;
        if (s->valid < 2)
            s->valid++;
    }
}

/* s1 += s2
 * Only has an effect if samples were collected at the same times.
 * (This is a somewhat contrived interface for ltop aggregation of ost data)
 */
void
sample_add (sample_t s1, sample_t s2)
{
    if (s1->valid != s2->valid)
        return;
    if (s1->valid > 0 && s1->time[1] != s2->time[1])
        return;
    if (s1->valid > 1 && s1->time[0] != s2->time[0])
        return;
    if (s1->valid > 0)
        s1->val[1] += s2->val[1];
    if (s1->valid > 1)
        s1->val[0] += s2->val[0];
}

#ifndef MAX
#define MAX(a,b)   ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b)   ((a) < (b) ? (a) : (b))
#endif

/* s1 = MAX (s1, s2)
 * Only has an effect if samples were collected at the same times.
 * (This is a somewhat contrived interface for ltop aggregation of ost data)
 */
void
sample_max (sample_t s1, sample_t s2)
{
    if (s1->valid != s2->valid)
        return;
    if (s1->valid > 0 && s1->time[1] != s2->time[1])
        return;
    if (s1->valid > 1 && s1->time[0] != s2->time[0])
        return;
    if (s1->valid > 0)
        s1->val[1] = MAX (s1->val[1], s2->val[1]);
    if (s1->valid > 1)
        s1->val[0] = MAX (s1->val[0], s2->val[0]);
}

/* s1 = MAX (s1, s2)
 * Only has an effect if samples were collected at the same times.
 * (This is a somewhat contrived interface for ltop aggregation of ost data)
 */
void
sample_min (sample_t s1, sample_t s2)
{
    if (s1->valid != s2->valid)
        return;
    if (s1->valid > 0 && s1->time[1] != s2->time[1])
        return;
    if (s1->valid > 1 && s1->time[0] != s2->time[0])
        return;
    if (s1->valid > 0)
        s1->val[1] = MIN (s1->val[1], s2->val[1]);
    if (s1->valid > 1)
        s1->val[0] = MIN (s1->val[0], s2->val[0]);
}

/* Return delta(val) / delta(time),
 * Returns 0 if expired, or < 2 valid data points.
 */
double
sample_rate (sample_t s, time_t tnow)
{
    double val = 0;

    if (s->valid == 2 && (tnow - s->time[1]) <= s->stale_secs)
        val = (s->val[1] - s->val[0]) / (s->time[1] - s->time[0]);
    if (val < 0)
        val = 0;
    return val;
}

/* Return newest data point.
 * Returns 0 if expired or < 1 valid data point.
 */
double
sample_val (sample_t s, time_t tnow)
{
    if (s->valid > 0 && (tnow - s->time[1]) <= s->stale_secs)
        return s->val[1];
    return 0;
}

/* Compare sample values for sorting.
 */
int
sample_val_cmp (sample_t s1, sample_t s2, time_t tnow)
{
    double v1 = sample_val (s1, tnow);
    double v2 = sample_val (s2, tnow);

    return (v1 == v2 ? 0 : v1 < v2 ? -1 : 1);
}

/* Compare rate values for sorting.
 */
int
sample_rate_cmp (sample_t s1, sample_t s2, time_t tnow)
{
    double v1 = sample_rate (s1, tnow);
    double v2 = sample_rate (s2, tnow);

    return (v1 == v2 ? 0 : v1 < v2 ? -1 : 1);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
