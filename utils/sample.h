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
