/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_FLUX_HOSTLIST_HOSTNAME_H
#define HAVE_FLUX_HOSTLIST_HOSTNAME_H

/*
 * Convenience structure representing a hostname as a prefix,
 *  optional numeric part, and suffix.
 */
struct hostlist_hostname {
    char *hostname;         /* cache of initialized hostname        */
    char *prefix;           /* hostname prefix                      */
    int len; /* length minus invalid characters */
    int len_prefix; /* length of the prefix */
    int width;   /* length of the suffix */
    unsigned long num;      /* numeric suffix                       */

    /* string representation of numeric suffix
     * points into `hostname'                                       */
    char *suffix;
};

struct stack_hostname {
    const char *hostname;         /* cache of initialized hostname        */
    int len; /* length minus invalid characters */
    int len_prefix; /* length of the prefix */
    int width;   /* length of the suffix */
    unsigned long num;      /* numeric suffix                       */

    /* string representation of numeric suffix
     * points into `hostname'                                       */
    const char *suffix;
};

struct hostlist_hostname * hostname_create (const char *s);
struct hostlist_hostname * hostname_create_with_suffix (const char *s, int i);
struct stack_hostname *hostname_stack_create (struct stack_hostname *hn,
                                              const char *hostname);
struct stack_hostname *hostname_stack_create_from_hostname (
    struct stack_hostname *hn,
    struct hostlist_hostname *hn_src);
struct stack_hostname *hostname_stack_create_with_suffix (
    struct stack_hostname *hn,
    const char *hostname,
    int len,
    int idx);
struct stack_hostname *hostname_stack_copy_one_less_digit (
    struct stack_hostname *dst,
    struct stack_hostname *src);
void hostname_destroy (struct hostlist_hostname *hn);

int hostname_suffix_is_valid (struct hostlist_hostname *hn);
int hostname_suffix_width (struct hostlist_hostname *hn);

#endif /* !HAVE_FLUX_HOSTLIST_HOSTNAME_H */
