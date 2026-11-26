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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>

#include "util.h"
#include "hostrange.h"


/* allocate a new hostrange object
 */
static struct hostrange * hostrange_new (const char *prefix)
{
    struct hostrange *new;

    if (prefix == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (!(new = calloc (1, sizeof (*new)))
        || (!(new->prefix = strdup (prefix)))) {
        hostrange_destroy (new);
        return NULL;
    }
    new->len_prefix = strlen (prefix);
    return new;
}

/* Create a struct hostrange * containing a single host without a valid suffix
 * hr->prefix will represent the entire hostname.
 */
struct hostrange * hostrange_create_single (const char *prefix)
{
    struct hostrange * new = hostrange_new (prefix);
    if (!new)
        return NULL;
    new->singlehost = 1;
    return new;
}

/* Create a hostrange object with a prefix, hi, lo, and format width
 */
struct hostrange * hostrange_create (const char *prefix,
                                     unsigned long lo,
                                     unsigned long hi,
                                     int width)
{
    struct hostrange * new;

    if (lo > hi || width < 0) {
        errno = EINVAL;
        return NULL;
    }

    if (!(new = hostrange_new (prefix)))
        return NULL;

    new->lo = lo;
    new->hi = hi;
    new->width = width;
    return new;
}


/* Return the number of hosts stored in the hostrange object
 */
unsigned long hostrange_count (struct hostrange * hr)
{
    if (!hr) {
        errno = EINVAL;
        return 0;
    }
    if (hr->singlehost)
        return 1;
    else
        return hr->hi - hr->lo + 1;
}

/* Copy a hostrange object
 */
struct hostrange * hostrange_copy (struct hostrange * hr)
{
    if (hr == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (hr->singlehost)
        return hostrange_create_single (hr->prefix);
    else
        return hostrange_create (hr->prefix, hr->lo, hr->hi, hr->width);
}


/* free memory allocated by the hostrange object
 */
void hostrange_destroy (struct hostrange * hr)
{
    if (hr) {
        int saved_errno = errno;
        if (hr->prefix)
            free (hr->prefix);
        free (hr);
        errno = saved_errno;
    }
}

/* hostrange_delete_host() deletes a specific host from the range.
 * If the range is split into two, the greater range is returned in
 * 'newp', and `hi' of the lesser range is adjusted accordingly. If the
 * highest or lowest host is deleted from a range, 'newp' is set to NULL
 * and the hostrange hr is adjusted properly.
 * Returns 0 on success, -1 on failure with errno set.
 */
int hostrange_delete_host (struct hostrange * hr,
                           unsigned long n,
                           struct hostrange **newp)
{
    struct hostrange * new = NULL;

    if (!hr || !newp) {
        errno = EINVAL;
        return -1;
    }

    assert (n >= hr->lo && n <= hr->hi);

    /* Empty a range of one host with lo > hi without wrapping lo or hi
     */
    if (hr->lo == hr->hi) {
        if (hr->lo == 0)
            hr->lo = 1;
        else
            hr->hi--;
    }
    else if (n == hr->lo)
        hr->lo++;
    else if (n == hr->hi)
        hr->hi--;
    else {
        if (!(new = hostrange_copy (hr)))
            return -1;
        hr->hi = n - 1;
        new->lo = n + 1;
    }
    *newp = new;
    return 0;
}

/* compare the prefixes of two hostrange objects.
 * returns:
 *    < 0   if h1 prefix is less than h2 OR h1 == NULL.
 *
 *      0   if h1's prefix and h2's prefix match,
 *          UNLESS, either h1 or h2 (NOT both) do not have a valid suffix.
 *
 *    > 0   if h1's prefix is greater than h2's OR h2 == NULL.
 */
int hostrange_prefix_cmp (struct hostrange * h1, struct hostrange * h2)
{
    int retval;
    if (h1 == NULL)
        return -1;
    if (h2 == NULL)
        return 1;

    int min_len = h1->len_prefix < h2->len_prefix ? h1->len_prefix : h2->len_prefix;
    retval = memcmp (h1->prefix, h2->prefix, min_len);

    if (retval == 0) {
        if (h1->len_prefix < h2->len_prefix)
            return -1;
        if (h1->len_prefix > h2->len_prefix)
            return 1;
        retval = h2->singlehost - h1->singlehost;
    }

    return retval;
}


/* hostrange_cmp() is used to sort hostrange objects. It will
 * sort based on the following (in order):
 *  o result of strcmp on prefixes
 *  o if widths are compatible, then:
 *       sort based on lowest suffix in range
 *    else
 *       sort based on width                     */
int hostrange_cmp (struct hostrange * h1, struct hostrange * h2)
{
    int retval;

    assert (h1 != NULL);
    assert (h2 != NULL);

    if ((retval = hostrange_prefix_cmp (h1, h2)) == 0) {
        if (hostrange_width_combine (h1, h2))
            retval = (h1->lo > h2->lo) - (h1->lo < h2->lo);
        else
            retval = h1->width - h2->width;
    }

    /*  Only return -1, 0, or 1 */
    if (retval != 0)
        retval = retval < 0 ? -1 : 1;

    return retval;
}

/* returns true if h1 and h2 would be included in the same bracketed hostlist.
 * h1 and h2 will be in the same bracketed list iff:
 *
 *  1. h1 and h2 have same prefix
 *  2. neither h1 nor h2 are singlet hosts (i.e. invalid suffix)
 *
 *  (XXX: Should incompatible widths be placed in the same bracketed list?
 *        There's no good reason not to, except maybe aesthetics)
 */
int hostrange_within_range (struct hostrange * h1, struct hostrange * h2)
{
    if (hostrange_prefix_cmp (h1, h2) == 0)
        return h1->singlehost || h2->singlehost ? 0 : 1;
    else
        return 0;
}

/* compare two hostrange objects to determine if they are width
 * compatible,  returns:
 *  1 if widths can safely be combined
 *  0 if widths cannot be safely combined
 */
int hostrange_width_combine (struct hostrange * h0, struct hostrange * h1)
{
    assert (h0 != NULL);
    assert (h1 != NULL);

    return width_equiv (h0->lo, &h0->width, h1->lo, &h1->width);
}


/* Return true if hostrange hr contains no hosts, i.e. hi < lo
 */
int hostrange_empty (struct hostrange * hr)
{
    assert (hr != NULL);
    return hr->hi < hr->lo;
}

/* Return true if h2 starts immediately after the end of h1.
 */
bool hostrange_adjacent (struct hostrange *h1, struct hostrange *h2)
{
    return h2->lo > 0 && h1->hi == h2->lo - 1;
}

/* join two hostrange objects.
 *
 * returns:
 *
 * -1 if ranges do not overlap (including incompatible zero padding)
 *  0 if ranges join perfectly
 * >0 number of hosts that were duplicated in  h1 and h2
 *
 * h2 will be coalesced into h1 if rc >= 0
 *
 * it is assumed that h1->lo <= h2->lo, i.e. hr1 <= hr2
 *
 */
int hostrange_join (struct hostrange * h1, struct hostrange * h2)
{
    int duplicated = -1;

    assert (h1 != NULL);
    assert (h2 != NULL);
    assert (hostrange_cmp (h1, h2) <= 0);

    if (hostrange_prefix_cmp (h1, h2) == 0 &&
        hostrange_width_combine (h1, h2)) {

        if (h1->singlehost && h2->singlehost) {    /* matching singlets  */
            duplicated = 1;
        } else if (hostrange_adjacent (h1, h2)) {    /* perfect join       */
            h1->hi = h2->hi;
            duplicated = 0;
        } else if (h1->hi >= h2->lo) {    /* some duplication   */
            if (h1->hi < h2->hi) {
                duplicated = h1->hi - h2->lo + 1;
                h1->hi = h2->hi;
            } else
                duplicated = hostrange_count (h2);
        }
    }

    return duplicated;
}

/* hostrange_intersect_ids() returns true if hostrange objects h1 and h2
 * intersect, and stores the common ids in 'lo' and 'hi'.
 *
 * It is assumed that h1 <= h2 (i.e. h1->lo <= h2->lo)
 */
bool hostrange_intersect_ids (struct hostrange * h1,
                              struct hostrange * h2,
                              unsigned long *lo,
                              unsigned long *hi)
{
    assert (h1 != NULL);
    assert (h2 != NULL);

    if (h1->singlehost || h2->singlehost)
        return false;

    assert (hostrange_cmp (h1, h2) <= 0);

    if ((hostrange_prefix_cmp (h1, h2) == 0)
        && (h1->hi > h2->lo)
        && (hostrange_width_combine (h1, h2))) {
        *lo = h2->lo;
        *hi = h2->hi < h1->hi ? h2->hi : h1->hi;
        return true;
    }
    return false;
}

/* return offset of hn if it is in the hostlist or
 *        -1 if not.
 */
int hostrange_hn_within (struct hostrange * hr, struct stack_hostname * hn)
{
    if (hr->singlehost) {
        /*
         *  If the current hostrange [hr] is a `singlehost' (no valid
         *   numeric suffix (lo and hi)), then the hostrange [hr]
         *   stores just one host with name == hr->prefix.
         *
         *  Thus the full hostname in [hn] must match hr->prefix, in
         *   which case we return true. Otherwise, there is no
         *   possibility that [hn] matches [hr].
         */
        if (hr->len_prefix != hn->len)
            return -1;
        if (memcmp (hn->hostname, hr->prefix, hr->len_prefix) == 0)
            return 0;
        else
            return -1;
    }

    /*
     *  Now we know [hr] is not a "singlehost", so hostname
     *   better have a valid numeric suffix, or there is no
     *   way we can match
     */
    if (!hn || !hn->suffix)
        return -1;

    /*
     *  If hostrange and hostname prefixes don't match to at least
     *   the length of the hostname object (which will have the min
     *   possible prefix length), then there is no way the hostname
     *   falls within the range [hr].
     */
    if (hr->len_prefix < hn->len_prefix)
        return -1;
    if (memcmp (hr->prefix, hn->hostname, hn->len_prefix) != 0)
        return -1;

    /*
     *  Now we know hostrange and hostname prefixes match up to the
     *   length of the hostname prefix.  If the hostrange and hostname
     *   prefix lengths do not match (specifically if the hostname prefix
     *   length is less than the hostrange prefix length) and the
     *   hostrange prefix contains trailing digits, then it might be
     *   the case that the hostrange was created by forcing the prefix
     *   to contain digits a la f00[1-2]. So we try adjusting the
     *   hostname with the longer prefix and calling this function
     *   again with the new hostname. (Yes, this is ugly, sorry)
     */
    if ((hn->len_prefix < hr->len_prefix)
         && (hn->width > 1)
         && (isdigit (hr->prefix [hr->len_prefix - 1]))
         && (hr->prefix [hn->len_prefix] == hn->suffix[0]) ) {
        int rc;
        /*
         *  Create new hostname object with its prefix offset by one
         */
        struct stack_hostname shn;
        struct stack_hostname * h = hostname_stack_copy_one_less_digit (&shn, hn);
        /*
         *  Recursive call :-o
         */
        rc = hostrange_hn_within (hr, h);
        return rc;
    }


    /*
     *  Finally, check whether [hn], with a valid numeric suffix,
     *   falls within the range of [hr] if [hn] and [hr] prefix are
     *   identical.
     */
    if ((hr->len_prefix == hn->len_prefix)
        && (hn->num <= hr->hi)
        && (hn->num >= hr->lo)) {
        int width = hn->width;
        if (!width_equiv (hr->lo, &hr->width, hn->num, &width))
            return -1;
        return (hn->num - hr->lo);
    }

    return -1;
}

/* Place the string representation of the numeric part of hostrange into buf
 * writing at most n chars including NUL termination.
 */
size_t hostrange_numstr (struct hostrange * hr, size_t n, char *buf)
{
    int len = 0;

    assert (hr != NULL && buf != NULL);

    if (hr->singlehost || n == 0)
        return 0;

    len = snprintf (buf, n, "%0*lu", hr->width, hr->lo);

    if ((len >= 0) && (len < n) && (hr->lo < hr->hi)) {
        int len2 = snprintf (buf+len, n-len, "-%0*lu", hr->width, hr->hi);
        if (len2 < 0)
            len = -1;
        else
            len += len2;
    }

    return len;
}

char * hostrange_host_tostring (struct hostrange * hr, int depth)
{
    char *host;
    int rc;

    if (!hr || depth < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (hr->singlehost)
        return strdup (hr->prefix);

    if (depth > hr->hi - hr->lo) {
        errno = ERANGE;
        return NULL;
    }
    rc = asprintf (&host, "%s%0*lu", hr->prefix, hr->width, hr->lo + depth);
    if (rc < 0)
        return NULL;
    return host;
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
