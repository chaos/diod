/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* list.c - exercise list package (valgrind me) */

/* Internal memory allocation in list.c hangs on to some memory that's not
 * freed.  This may not really be worth the hassle, so it's disabled and
 * now this simple test runs valgrind clean.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "src/libtap/tap.h"
#include "list.h"

#define ITERATIONS  1024

static int objcount = 0;

void lsd_fatal_error (char *file, int line, char *mesg)
{
    BAIL_OUT ("fatal error: %s: %s::%d", mesg, file, line);
}

void *lsd_nomem_error (char *file, int line, char *mesg)
{
    diag ("out of memory: %s: %s::%d", mesg, file, line);
    return NULL;
}

void myfree (void *x)
{
    free (x);
    objcount--;
}

char *myalloc (char *s)
{
    char *cpy;

    if (!(cpy = strdup ("xyz")))
        BAIL_OUT ("out of memory");
    objcount++;

    return cpy;
}

int
main (int argc, char *argv[])
{
    List l;
    char *p;
    int i;

    plan (NO_PLAN);

    l = list_create ((ListDelF)myfree);
    ok (l != NULL, "list_create with destructor works");

    int errors = 0;
    for (i = 0; i < ITERATIONS; i++) {
        if (!(p = myalloc ("xyz")))
            BAIL_OUT ("out of memory");
        if (!list_append (l, p)) {
            errors++;
        }
    }
    ok (errors == 0, "list_append worked %dX", ITERATIONS);
    ok (objcount == i, "%d objects were allocated", i);
    list_destroy (l);
    ok (objcount == 0, "list_destroy deallocated them");

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
