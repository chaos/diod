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

#include <errno.h>

#include "src/libtap/tap.h"
#include "src/libhostlist/hostname.h"

struct hostname_test {
    char *input;
    char *prefix;
    unsigned long num;
    char *suffix;
    int suffix_valid;
    int suffix_width;
};

struct hostname_test hostname_tests[] = {
    { "foo",      "foo",     0,   NULL,    0, 0 },
    { "foo0",     "foo",     0,   "0",     1, 1 },
    { "foo001",   "foo",     1,   "001",   1, 3 },
    { "foo01bar", "foo01bar",0,   NULL,    0, 0 },
    { " ",        NULL,      0,   NULL,    0, 0 },
    { "bar[1-5]", NULL,      0,   NULL,    0, 0 },
    { "bar,",     NULL,      0,   NULL,    0, 0 },
    { NULL,       NULL,     -1,   NULL,    0, 0 },
};

int main (int argc, char *argv[])
{
    struct hostname_test *t;

    plan (NO_PLAN);

    ok (hostname_create_with_suffix ("testname", 4) == NULL,
        "hostname_create_with_suffix() with invalid index returns EINVAL");

    t = hostname_tests;
    while (t && t->input != NULL) {
        struct hostlist_hostname *hn;
        errno = 0;
        hn = hostname_create (t->input);
        if (t->prefix == NULL) {
            /* Check expected failure */
            ok (hn == NULL && errno == EINVAL,
                "hostname_create (%s) fails with EINVAL", t->input);
            t++;
            continue;
        }
        if (!hn)
            BAIL_OUT ("hostname_create (%s) failed!", t->input);
        is (hn->prefix, t->prefix,
            "input=%s: prefix=%s", t->input, hn->prefix);
        ok (hn->num == t->num,
            "input=%s: hn->num = %lu", t->input, hn->num);
        if (t->suffix_valid) {
            ok (hn->suffix != NULL,
                "input=%s: hostname got valid suffix: %s",
                t->input, hn->suffix);
            ok (hostname_suffix_is_valid (hn),
                "input=%s: hostname_suffix_is_valid returns true",
                t->input);
            ok (hostname_suffix_width (hn) == t->suffix_width,
                "input=%s: hostname_suffix_width = %d",
                t->input, hostname_suffix_width (hn));
            is (hn->suffix, t->suffix,
                "input=%s: suffixes match", t->input);
        }
        else {
            ok (hostname_suffix_is_valid (hn) == 0,
                "input=%s: hostname_suffix_is_valid returns false",
                t->input);
            ok (hostname_suffix_width (hn) == 0,
                "input=%s: hostname_suffix_width returns 0",
                t->input);
        }
        hostname_destroy (hn);
        t++;
    }

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
