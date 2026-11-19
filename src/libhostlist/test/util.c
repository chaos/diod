/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "src/libtap/tap.h"
#include "src/libhostlist/util.h"

struct width_test {
    const char *descr;

    unsigned long n;
    int wn;

    unsigned long m;
    int wm;

    int result;
};

struct width_test width_tests[] = {
    { "single digit widths are equivalent",
        0,     1,     1,     1,     1 },
    { "single digit basic test",
        0,     1,     100,   3,     1 },
    { "multi digit basic test",
        0,     3,     100,   1,     1 },
    { "003 and 10 are not equivalent",
        0,     3,     10,    1,     0 },
    { 0 },
};

int main (int argc, char *argv[])
{
    struct width_test *t;

    plan (NO_PLAN);

    t = width_tests;
    while (t && t->descr != NULL) {
        int result = width_equiv (t->n, &t->wn, t->m, &t->wm);
        ok (result == t->result,
            "%s", t->descr);
        if (result == 1)
            ok (t->wn == t->wm,
                "%s: wn = wm",
                t->descr);
        t++;
    }

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
