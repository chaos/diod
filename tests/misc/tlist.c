/* tlist.c - exercise list package (valgrind me) */

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
#include <assert.h>

#include "list.h"

#define ITERATIONS  1024

static int objcount = 0;

void oom (void)
{
    fprintf (stderr, "out of memory\n");
    exit (1);
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
        oom ();
    objcount++;

    return cpy;
}

int
main (int argc, char *argv[])
{
    List l;
    char *p;
    int i;

    if (!(l = list_create ((ListDelF)myfree)))
        oom ();

    for (i = 0; i < ITERATIONS; i++) {
        if (!(p = myalloc ("xyz")))
            oom ();
        if (!list_append (l, p))
            oom ();
    }

    assert (objcount == i);
    list_destroy (l);
    assert (objcount == 0);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

