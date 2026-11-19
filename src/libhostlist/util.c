/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* !HAVE_CONFIG_H */

#include "util.h"

static int zero_padded (unsigned long num, int width)
{
    int n = 1;
    while (num /= 10L)
        n++;
    return width > n ? width - n : 0;
}

int width_equiv(unsigned long n, int *wn, unsigned long m, int *wm)
{
    int npad, nmpad, mpad, mnpad;

    if (wn == wm)
        return 1;

    npad = zero_padded(n, *wn);
    nmpad = zero_padded(n, *wm);
    mpad = zero_padded(m, *wm);
    mnpad = zero_padded(m, *wn);

    if (npad != nmpad && mpad != mnpad)
        return 0;

    if (npad != nmpad) {
        if (mpad == mnpad) {
            *wm = *wn;
            return 1;
        } else
            return 0;
    } else {        /* mpad != mnpad */
        if (npad == nmpad) {
            *wn = *wm;
            return 1;
        } else
            return 0;
    }

    /* not reached */
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
