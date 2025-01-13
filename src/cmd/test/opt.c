/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* topt.c - test didomount/opt.c */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#include "src/libtap/tap.h"
#include "opt.h"

int
main (int argc, char *argv[])
{
    Opt o;

    char *s;
    int i;

    plan (NO_PLAN);

    o = opt_create ();
    ok (o != NULL, "opt_create works");

    ok (opt_addf (o, "mickey=%d", 42) == 1, "opt_addf mickey=42 works");
    ok (opt_addf (o, "goofey=%s", "yes") == 1, "opt_addf goofey=yes works");
    ok (opt_addf (o, "donald") == 1, "opt_addf donald works");
    ok (opt_addf (o, "foo,bar,baz") == 1, "opt_addf foo,bar,baz works");
    ok (opt_addf (o, "lastone") == 1, "opt_addf lastone works");

    s = opt_csv (o);
    ok (s != NULL
        && !strcmp (s, "mickey=42,goofey=yes,donald,foo,bar,baz,lastone"),
        "opt_csv returned expected result");
    free (s);

    ok (opt_find (o, "mickey") != NULL, "opt_find mickey works");
    ok (opt_find (o, "bar") != NULL, "opt_find bar works");
    ok (opt_find (o, "barn") == NULL, "opt_find barn fails as expected");

    i = -1;
    ok (opt_scanf (o, "mickey=%d", &i) && i == 42,
       "opt_scanf mickey value of 42");

    ok (opt_addf (o, "mickey=string,foo=%d,bar=%d,baz", 12, 15) == 1,
        "opt_addf mickey=string,foo=12,bar=15,baz works");
    s = opt_csv (o);
    ok (s != NULL
        && !strcmp (s,
                    "goofey=yes,donald,lastone,"
                    "mickey=string,foo=12,bar=15,baz"),
        "opt_csv returned expected result");
    free (s);

    opt_destroy (o);

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
