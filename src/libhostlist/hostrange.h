/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_FLUX_HOSTRANGE_H
#define HAVE_FLUX_HOSTRANGE_H

#include "hostname.h"

/* A single prefix with `hi' and `lo' numeric suffix values
 */
struct hostrange {
    char *prefix;        /* alphanumeric prefix: */
    unsigned long len_prefix; /* length of the prefix */

    /* beginning (lo) and end (hi) of suffix range */
    unsigned long lo, hi;

    /* width of numeric output format
     * (pad with zeros up to this width) */
    int width;

    /* If singlehost is 1, `lo' and `hi' are invalid */
    unsigned singlehost:1;
};

struct hostrange * hostrange_create_single (const char *);

struct hostrange * hostrange_create (const char *s,
                                     unsigned long lo,
                                     unsigned long hi,
                                     int width);

unsigned long hostrange_count (struct hostrange *);

struct hostrange * hostrange_copy (struct hostrange *);

void hostrange_destroy (struct hostrange *);

struct hostrange * hostrange_delete_host (struct hostrange *, unsigned long);

int hostrange_cmp (struct hostrange *, struct hostrange *);

int hostrange_prefix_cmp (struct hostrange *, struct hostrange *);
int hostrange_within_range (struct hostrange *, struct hostrange *);
int hostrange_width_combine (struct hostrange *, struct hostrange *);
int hostrange_empty (struct hostrange *);

int hostrange_join (struct hostrange *, struct hostrange *);

struct hostrange * hostrange_intersect (struct hostrange *,
                                        struct hostrange *);

int hostrange_hn_within (struct hostrange *, struct stack_hostname *);

size_t hostrange_numstr(struct hostrange *, size_t, char *);

/*  Return the string representation of the nth string in 'hr'.
 */
char * hostrange_host_tostring (struct hostrange *hr, int n);

#endif /* !HAVE_FLUX_HOSTRANGE_H */
