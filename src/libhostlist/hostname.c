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
#endif  /* !HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hostname.h"

#define INVALID_CHARS ",[]\t "

/*
 * return the location of the last char in the hostname prefix
 */
static int host_prefix_end (const char *hostname, int len)
{
    if (len < 0)
        return -1;
    int idx = len - 1;

    /* Rewind to the last non-digit in hostname
     */
    while (idx >= 0 && isdigit ((char) hostname[idx]))
        idx--;

    return idx;
}

/*  Return < 0 if hostname is not valid (e.g. contains invalid characters),
 *  O/w, return the length of the string.
 */
static int hostname_len (const char *hostname)
{
    int len = strlen (hostname);
    if (len != strcspn (hostname, INVALID_CHARS))
        return -1;
    return len;
}

struct stack_hostname *hostname_stack_create_from_hostname (
    struct stack_hostname *hn,
    struct hostlist_hostname *src)
{
    if (!hn || !src) {
        errno = EINVAL;
        return NULL;
    }
    hn->hostname = src->hostname;
    hn->suffix = src->suffix;
    hn->len = src->len;
    hn->len_prefix = src->len_prefix;
    hn->width = src->width;
    hn->num = src->num;
    return hn;
}

struct stack_hostname *hostname_stack_create_with_suffix (
    struct stack_hostname *hn,
    const char *hostname,
    int len,
    int idx)
{
    if (!hostname || !hn || len < 0) {
        errno = EINVAL;
        return NULL;
    }
    hn->hostname = hostname;
    hn->len = len;
    hn->len_prefix = idx + 1;
    hn->num = 0;
    hn->suffix = NULL;
    if (idx == hn->len - 1) {
        return hn;
    }
    hn->suffix = hn->hostname + hn->len_prefix;

    char *p = NULL;
    hn->num = strtoul (hn->suffix, &p, 10);
    if (p == hn->suffix && hn->num == 0) {
        errno = EINVAL;
        return NULL;
    }
    hn->width = hn->len - hn->len_prefix;
    return hn;
}

struct stack_hostname *hostname_stack_copy_one_less_digit (
    struct stack_hostname *dst,
    struct stack_hostname *src)
{

    if (!dst || !src) {
        errno = EINVAL;
        return NULL;
    }
    *dst = *src;
    dst->len_prefix = src->len_prefix + 1;
    if (src->len_prefix == dst->len - 1) {
        return dst;
    }
    dst->suffix = dst->hostname + dst->len_prefix;

    dst->width = dst->len - dst->len_prefix;
    if (dst->width < 0 || dst->width >= 10) {
        errno = EINVAL;
        return NULL;
    }

    // remove the most significant decimal digit without reparsing
    // lookup table because pow is slow
    static const int pow10[10] = {
        1,
        10,
        100,
        1000,
        10000,
        100000,
        1000000,
        10000000,
        100000000,
        1000000000};
    dst->num = src->num % pow10[dst->width];
    return dst;
}

struct hostlist_hostname *hostname_create_with_suffix (const char *hostname,
                                                       int idx)
{
    struct hostlist_hostname *hn = NULL;
    int len;
    char *p = NULL;

    if (!hostname || (len = hostname_len (hostname)) < 0) {
        errno = EINVAL;
        return NULL;
    }

    if (!(hn = calloc (1, sizeof (*hn))))
        return NULL;

    if (!(hn->hostname = strdup (hostname))) {
        hostname_destroy (hn);
        return NULL;
    }

    hn->num = 0;
    hn->len = len;
    hn->len_prefix = idx + 1;
    hn->width = hn->len - hn->len_prefix;
    hn->prefix = NULL;
    hn->suffix = NULL;

    if (idx == len - 1) {
        if ((hn->prefix = strdup (hostname)) == NULL) {
            hostname_destroy (hn);
            return NULL;
        }
        return hn;
    }

    hn->suffix = hn->hostname + idx + 1;
    errno = 0;
    hn->num = strtoul (hn->suffix, &p, 10);
    if (p == hn->suffix || errno != 0) {
        hostname_destroy (hn);
        errno = EINVAL;
        return NULL;
    }
    if (*p == '\0') {
        if (!(hn->prefix = malloc ((idx + 2) * sizeof (char)))) {
            hostname_destroy (hn);
            return NULL;
        }
        memcpy (hn->prefix, hostname, idx + 1);
        hn->prefix[idx + 1] = '\0';
    }
    return hn;
}

struct stack_hostname *hostname_stack_create (struct stack_hostname *hn,
                                              const char *hostname)
{
    int len = 0;
    if (!hostname || (len = hostname_len (hostname)) < 0) {
        errno = EINVAL;
        return NULL;
    }
    return hostname_stack_create_with_suffix (hn,
                                              hostname,
                                              len,
                                              host_prefix_end (hostname, len));
}

/*
 * create a struct hostlist_hostname * object from a string hostname
 */
struct hostlist_hostname *hostname_create (const char *hostname)
{
    int end;
    if (!hostname) {
        errno = EINVAL;
        return NULL;
    }
    end = host_prefix_end (hostname, hostname_len (hostname));
    return hostname_create_with_suffix (hostname, end);
}

/* free a hostname object
 */
void hostname_destroy (struct hostlist_hostname * hn)
{
    int saved_errno = errno;
    if (hn) {
        hn->suffix = NULL;
        free (hn->hostname);
        free (hn->prefix);
        free (hn);
    }
    errno = saved_errno;
}

/* return true if the hostname has a valid numeric suffix
 */
int hostname_suffix_is_valid (struct hostlist_hostname * hn)
{
    return (hn && hn->suffix != NULL);
}

/* return the width (in characters) of the numeric part of the hostname
 */
int hostname_suffix_width (struct hostlist_hostname * hn)
{
    if (!hn) {
        errno = EINVAL;
        return -1;
    }
    if (!hn->suffix)
        return 0;
    return strlen (hn->suffix);
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
