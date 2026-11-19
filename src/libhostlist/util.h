/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_FLUX_HOSTLIST_UTIL_H
#define HAVE_FLUX_HOSTLIST_UTIL_H

/* Test whether two format `width' parameters are "equivalent"
 * The width arguments "wn" and "wm" for integers "n" and "m"
 * are equivalent if:
 *
 *  o  wn == wm  OR
 *
 *  o  applying the same format width (either wn or wm) to both of
 *     'n' and 'm' will not change the zero padding of *either* 'm' nor 'n'.
 *
 *  If this function returns 1 (or true), the appropriate width value
 *  (either 'wm' or 'wn') will have been adjusted such that both format
 *  widths are equivalent.
 */
int width_equiv (unsigned long n, int *wn,
                 unsigned long m, int *wm);

#endif /* !HAVE_FLUX_HOSTLIST_UTIL_H */
