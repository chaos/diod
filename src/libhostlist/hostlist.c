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
#include <sys/param.h>
#include <unistd.h>
#include <limits.h>

#include "errno_safe.h"

#include "hostlist.h"
#include "hostrange.h"
#include "hostname.h"
#include "util.h"

/* number of elements to allocate when extending the hostlist array */
#define HOSTLIST_CHUNK    16

/* max host range: anything larger will be assumed to be an error */
#define MAX_RANGE    1<<20    /* 1M Hosts */

/* Helper structure for hostlist iteration
 */
struct current {
    char *host;
    int index;
    int depth;
};

/* Hostlist - a dynamic array of hostrange objects
 */
struct hostlist {
    int size;               /* current number of elements available in array */
    int nranges;            /* current number of ranges stored in array */
    int nhosts;             /* current number of hosts stored in hostlist */
    struct hostrange **hr;  /* pointer to hostrange array */

    struct current current; /* iterator cursor */

    /* hostlist_nth() lookup cache: nth_count is the number of hosts in
     * ranges before range nth_index. Appending ranges keeps this valid,
     * other modifications must call nth_cache_reset().
     */
    int nth_index;
    int nth_count;
};

/* _range struct helper for parsing hostlist strings
 */
struct _range {
    unsigned long lo, hi;
    int width;
};


/*
 * Helper function for host list string parsing routines
 * Returns a pointer to the next token; additionally advance *str
 * to the next separator.
 *
 * next_tok was taken directly from pdsh courtesy of Jim Garlick.
 * (with modifications to support bracketed hostlists, i.e.:
 *  xxx[xx,xx,xx] is a single token)
 *
 * next_tok now handles multiple brackets within the same token,
 * e.g.  node[01-30]-[1-2,6].
 */
static char * next_tok (char *sep, char **str)
{
    char *tok;
    int level = 0;

    /* push str past any leading separators */
    while (**str != '\0' && strchr (sep, **str) != NULL)
        (*str)++;

    if (**str == '\0')
        return NULL;

    /* assign token ptr */
    tok = *str;

    while ( **str != '\0' &&
	        (level != 0 || strchr (sep, **str) == NULL) ) {
        if (**str == '[')
            level++;
        else if (**str == ']')
            level--;
        (*str)++;
    }

    /* nullify consecutive separators and push str beyond them */
    while (**str != '\0' && strchr (sep, **str) != NULL)
        *(*str)++ = '\0';

    return tok;
}


struct hostlist * hostlist_create (void)
{
    struct hostlist * new = calloc (1, sizeof (*new));
    if (!new)
        return NULL;

    new->hr = calloc (HOSTLIST_CHUNK, sizeof (struct hostrange *));
    if (!new->hr)
        goto fail;

    new->size = HOSTLIST_CHUNK;
    return new;

fail:
    free (new);
    return NULL;
}


/* Resize the internal array used to store the list of hostrange objects.
 *
 * returns 1 for a successful resize,
 *         0 if call to _realloc fails
 *
 * It is assumed that the caller has the hostlist hl locked
 */
static int hostlist_resize (struct hostlist *hl, size_t newsize)
{
    struct hostrange **hr;

    assert (hl != NULL);
    if (!(hr = realloc (hl->hr, newsize * sizeof (*hr))))
        return 0;
    for (size_t i = hl->size; i < newsize; i++)
        hr[i] = NULL;
    hl->hr = hr;
    hl->size = newsize;
    return 1;
}

/* Resize hostlist by one HOSTLIST_CHUNK
 * Assumes that hostlist hl is locked by caller
 */
static int hostlist_expand (struct hostlist *hl)
{
    if (!hostlist_resize (hl, hl->size + HOSTLIST_CHUNK))
        return 0;
    else
        return 1;
}

/* Push a hostrange object onto hostlist hl
 * Returns the number of hosts successfully pushed onto hl
 * or -1 if there was an error allocating memory
 */
static int hostlist_append_range (struct hostlist *hl, struct hostrange * hr)
{
    struct hostrange * tail;
    int retval;

    assert (hr != NULL);

    tail = (hl->nranges > 0) ? hl->hr[hl->nranges-1] : hl->hr[0];

    if (hl->size == hl->nranges && !hostlist_expand (hl))
        goto error;

    if (hl->nranges > 0
        && hostrange_prefix_cmp (tail, hr) == 0
        && hostrange_adjacent (tail, hr)
        && hostrange_width_combine (tail, hr)) {
        tail->hi = hr->hi;
    } else {
        if (!(hl->hr[hl->nranges] = hostrange_copy (hr)))
            goto error;
        hl->nranges++;
    }

    retval = hostrange_count (hr);
    hl->nhosts += retval;

    return retval;
  error:
    return -1;
}


/* Same as hostlist_append_range() above, but prefix, lo, hi, and width
 * are passed as args
 */
static int hostlist_append_hr (struct hostlist *hl,
                               char *prefix,
                               unsigned long lo,
                               unsigned long hi,
                               int width)
{
    int retval;
    struct hostrange * hr = hostrange_create (prefix, lo, hi, width);
    if (!hr)
        return -1;
    retval = hostlist_append_range (hl, hr);
    hostrange_destroy (hr);
    return retval;
}

/* Insert a range object hr into position n of the hostlist hl
 */
static int hostlist_insert_range (struct hostlist *hl,
                                  struct hostrange * hr,
                                  int n)
{
    struct hostrange *copy;

    assert (hl != NULL);
    assert (hr != NULL);

    if (n > hl->nranges)
        return 0;

    if (hl->size == hl->nranges && !hostlist_expand (hl))
        return 0;

    if (!(copy = hostrange_copy (hr)))
        return 0;

    /* push remaining hostrange entries up and copy hr into slot "n" */
    memmove (&hl->hr[n + 1],
             &hl->hr[n],
             (hl->nranges - n) * sizeof (hl->hr[0]));
    hl->hr[n] = copy;
    hl->nranges++;

    /* adjust current if necessary
     */
    if (hl->current.index >= n)
        hl->current.index++;

    return 1;
}


static void hostlist_shift_current (struct hostlist *hl,
                                    int index,
                                    int depth,
                                    int n)
{
    struct current *c = &hl->current;

    /*  Case 1: a single host was deleted in hl->hr[index] at depth.
     *   To leave c->depth pointing to the next host, shift c->depth
     *   back only if the deletion depth < c->depth.
     */
    if (n == 0 && c->index == index && depth < c->depth) {
        c->depth--;
        return;
    }
    /*  Case 2: current points to the hostrange which was deleted.
     *  Simply reset depth to -1 so that hostlist_next() starts at the
     *   next hostrange in the array.
     */
    if (index == c->index) {
        c->depth = -1;
        return;
    }
    /*  Case 3: hostrange elements were deleted at a position < c->index:
     *  In this case, adjust current index back by the number of deleted
     *   elements. (ensure index is at least -1 however.)
     */
    if (index < c->index) {
        /*  Move index up by the number of deleted ranges */
        if ((c->index -= n) < 0)
            c->index = -1;
    }
    /* O/w current iterator should not be affected.
     */
}



/* Delete the range at position n in the range array
 * Assumes the hostlist lock is already held.
 */
static void hostlist_delete_range (struct hostlist *hl, int n)
{
    int i;
    struct hostrange * old;

    assert (hl != NULL);
    assert (n < hl->nranges && n >= 0);

    old = hl->hr[n];
    for (i = n; i < hl->nranges - 1; i++)
        hl->hr[i] = hl->hr[i + 1];
    hl->nranges--;
    hl->hr[hl->nranges] = NULL;

    hostlist_shift_current (hl, n, 0, 1);

    /* XXX caller responsible for adjusting nhosts */
    /*hl->nhosts -= hostrange_count(old) */

    hostrange_destroy (old);
}


/* Convert str to an unsigned long. Unlike strtoul(3), fail with EINVAL
 * for leading whitespace or sign, or trailing characters, and with
 * ERANGE on overflow.
 */
static int parse_ulong (const char *str, unsigned long *valp)
{
    char *endptr;
    unsigned long val;

    if (!isdigit ((unsigned char) *str)) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    val = strtoul (str, &endptr, 10);
    if (errno != 0)
        return -1;
    if (*endptr != '\0') {
        errno = EINVAL;
        return -1;
    }
    *valp = val;
    return 0;
}

/* Grab a single range from str
 * returns 0 if str contained a valid number or range,
 *        -1 if conversion of str to a range failed.
 */
static int parse_next_range (char *str, struct _range *range)
{
    char *p;

    if ((p = strchr (str, '-')))
        *p++ = '\0';

    if (parse_ulong (str, &range->lo) < 0)
        return -1;
    if (p) {
        if (parse_ulong (p, &range->hi) < 0)
            return -1;
    }
    else
        range->hi = range->lo;

    if (range->lo > range->hi) {
        errno = EINVAL;
        return -1;
    }

    if (range->hi - range->lo >= MAX_RANGE) {
        errno = ERANGE;
        return -1;
    }

    range->width = strlen (str);
    return 0;
}

static int hostlist_append_host (struct hostlist *hl, const char *str)
{
    int rc = -1;
    struct hostrange * hr;
    struct hostlist_hostname * hn;

    assert (hl != NULL);

    if (str == NULL || *str == '\0')
        return 0;

    if (!(hn = hostname_create (str)))
        return -1;

    if (hostname_suffix_is_valid (hn)) {
        hr = hostrange_create (hn->prefix,
                               hn->num,
                               hn->num,
                               hostname_suffix_width (hn));
    } else
        hr = hostrange_create_single (str);

    if (hr && hostlist_append_range (hl, hr) >= 0)
        rc = 0;

    hostrange_destroy (hr);
    hostname_destroy (hn);

    return rc;
}

/*
 * Convert 'str' containing comma separated digits and ranges into an array
 *  of struct _range types. The array is grown as needed, and its address
 *  and size are returned in 'rangesp' and 'sizep'.
 *
 * Return number of ranges created, or -1 on error.
 */
static int parse_range_list (char *str, struct _range **rangesp, int *sizep)
{
    char *p;
    int count = 0;

    while (str) {
        if (count == *sizep) {
            int size = *sizep ? *sizep * 2 : 16;
            struct _range *new;
            if (!(new = realloc (*rangesp, size * sizeof (*new))))
                return -1;
            *rangesp = new;
            *sizep = size;
        }
        if ((p = strchr (str, ',')))
            *p++ = '\0';
        if (parse_next_range (str, &(*rangesp)[count++]) < 0)
            return -1;
        str = p;
    }
    return count;
}

static int append_range_list (struct hostlist *hl,
                              char *pfx,
                              struct _range *rng,
                              int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (hostlist_append_hr (hl, pfx, rng->lo, rng->hi, rng->width) < 0)
            return -1;
        rng++;
    }
    return 0;
}

static int append_range_list_with_suffix (struct hostlist *hl,
                                          char *pfx,
                                          char *sfx,
                                          struct _range *rng,
                                          int n)
{
    for (int i = 0; i < n; i++) {
        unsigned long j = rng->lo;
        while (1) {
            char *host;
            struct hostrange *hr;
            int rc;

            if (asprintf (&host, "%s%0*lu%s", pfx, rng->width, j, sfx) < 0)
                return -1;
            hr = hostrange_create_single (host);
            ERRNO_SAFE_WRAP (free, host);
            if (!hr)
                return -1;
            rc = hostlist_append_range (hl, hr);
            hostrange_destroy (hr);
            if (rc < 0)
                return -1;
            /* N.B. rng->hi may be ULONG_MAX, so do not increment past it */
            if (j++ == rng->hi)
                break;
        }
        rng++;
    }
    return 0;
}

/*
 * Create a hostlist from a string with brackets '[' ']'
 */
static struct hostlist * hostlist_create_bracketed (const char *hostlist,
                                                    char *sep)
{
    struct hostlist * new = hostlist_create ();
    struct _range *ranges = NULL;
    int size = 0;
    int nr;
    int rc;
    char *p, *tok, *str, *orig;

    if (!new)
        return NULL;

    if (hostlist == NULL)
        return new;

    if (!(orig = str = strdup (hostlist))) {
        hostlist_destroy (new);
        return NULL;
    }

    while ((tok = next_tok (sep, &str)) != NULL) {
        if ((p = strchr (tok, '[')) != NULL) {
            char *q, *prefix = tok;
            *p++ = '\0';

            if ((q = strchr (p, ']'))) {
                /* Only one bracketed idlist is allowed per expression */
                if (strchr (prefix, ']') || strpbrk (q + 1, "[]"))
                    goto error_unmatched;
                *q = '\0';
                nr = parse_range_list (p, &ranges, &size);
                if (nr < 0)
                    goto error;

                if (*(++q) != '\0')
                    rc = append_range_list_with_suffix (new,
                                                        prefix,
                                                        q,
                                                        ranges,
                                                        nr);
                else
                    rc = append_range_list (new,
                                            prefix,
                                            ranges,
                                            nr);
                if (rc < 0)
                    goto error;

            } else                    /* Error: brackets must be balanced */
                goto error_unmatched;

        } else if (strchr (tok, ']')) /* Error: brackets must be balanced */
            goto error_unmatched;
        else if (hostlist_append_host (new, tok) < 0) /* single host */
            goto error;
    }

    free (orig);
    free (ranges);
    return new;

  error_unmatched:
    errno = EINVAL;
  error:
    hostlist_destroy (new);
    ERRNO_SAFE_WRAP (free, orig);
    ERRNO_SAFE_WRAP (free, ranges);
    return NULL;
}

struct hostlist * hostlist_decode (const char *str)
{
    if (!str) {
        errno = EINVAL;
        return NULL;
    }
    return hostlist_create_bracketed (str, "\t, ");
}

struct hostlist * hostlist_copy (const struct hostlist *hl)
{
    int i;
    struct hostlist * new;

    if (hl == NULL)
        return NULL;

    if (!(new = hostlist_create ()))
        return NULL;

    if (hl->nranges > new->size && !hostlist_resize (new, hl->nranges))
        goto error;

    for (i = 0; i < hl->nranges; i++) {
        if (!(new->hr[i] = hostrange_copy (hl->hr[i])))
            goto error;
        new->nranges++;
    }
    new->nhosts = hl->nhosts;
    return new;
error:
    hostlist_destroy (new);
    return NULL;
}


void hostlist_destroy (struct hostlist *hl)
{
    if (hl) {
        int saved_errno = errno;
        for (int i = 0; i < hl->nranges; i++)
            hostrange_destroy (hl->hr[i]);
        free (hl->hr);
        free (hl->current.host);
        free (hl);
        errno = saved_errno;
    }
}

int hostlist_append (struct hostlist *hl, const char *hosts)
{
    struct hostlist * new;
    int retval;
    if (hl == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (hosts == NULL)
        return 0;
    new = hostlist_decode (hosts);
    if (!new)
        return -1;
    retval = hostlist_append_list (hl, new);
    hostlist_destroy (new);
    return retval;
}

int hostlist_append_list (struct hostlist * h1, struct hostlist * h2)
{
    int i, n = 0;

    if (h1 == NULL || h2 == NULL) {
        errno = EINVAL;
        return -1;
    }

    /*  Appending to h1 modifies h2 if they are the same list,
     *   so append from a copy.
     */
    if (h1 == h2) {
        struct hostlist *copy;
        if (!(copy = hostlist_copy (h2)))
            return -1;
        n = hostlist_append_list (h1, copy);
        hostlist_destroy (copy);
        return n;
    }

    for (i = 0; i < h2->nranges; i++) {
        int count = hostlist_append_range (h1, h2->hr[i]);
        if (count < 0)
            return -1;
        n += count;
    }
    return n;
}

static inline int nth_args_valid (struct hostlist *hl, int n)
{
    if (hl == NULL || n < 0) {
        errno = EINVAL;
        return -1;
    }
    if (n >= hl->nhosts) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

static void nth_cache_reset (struct hostlist *hl)
{
    hl->nth_index = 0;
    hl->nth_count = 0;
}

static void set_current (struct current *cur, int index, int depth)
{
    if (cur) {
        free (cur->host);
        cur->host = NULL;
        cur->index = index;
        cur->depth = depth;
    }
}

const char * hostlist_nth (struct hostlist *hl, int n)
{
    int   i, count;

    if (nth_args_valid (hl, n) < 0)
        return NULL;

    /* Start from the last range found if possible, so that access in
     * order is not quadratic.
     */
    if (n < hl->nth_count)
        nth_cache_reset (hl);
    count = hl->nth_count;
    for (i = hl->nth_index; i < hl->nranges; i++) {
        int num_in_range = hostrange_count (hl->hr[i]);

        if (n <= (num_in_range - 1 + count)) {
            int d = n - count;
            hl->nth_index = i;
            hl->nth_count = count;
            set_current (&hl->current, i, d);
            return hostlist_current (hl);
        } else
            count += num_in_range;
    }
    return NULL;
}

int hostlist_count (struct hostlist *hl)
{
    return hl ? hl->nhosts : 0;
}

static int hostlist_find_host (struct hostlist *hl,
                               struct stack_hostname *hn,
                               struct current *cur)
{
    int i, count, ret = -1;
    for (i = 0, count = 0; i < hl->nranges; i++) {
        int offset = hostrange_hn_within (hl->hr[i], hn);
        if (offset >= 0) {
            ret = count + offset;
            set_current (cur, i, offset);
            break;
        }
        else
            count += hostrange_count (hl->hr[i]);
    }

    if (ret < 0)
        errno = ENOENT;
    return ret;

}

int hostlist_find (struct hostlist *hl, const char *hostname)
{
    if (!hl || !hostname) {
        errno = EINVAL;
        return -1;
    }
    struct stack_hostname hn_storage;

    struct stack_hostname *hn = hostname_stack_create (&hn_storage, hostname);
    if (!hn)
        return -1;
    return hostlist_find_host (hl, hn, &hl->current);
}

int hostlist_find_hostname (struct hostlist *hl, struct hostlist_hostname *hn)
{
    struct stack_hostname hn_storage;
    struct stack_hostname *shn;

    if (!hl || !hn) {
        errno = EINVAL;
        return -1;
    }

    shn = hostname_stack_create_from_hostname (&hn_storage, hn);
    if (!shn)
        return -1;
    return hostlist_find_host (hl, shn, &hl->current);
}

struct hostlist_hostname *hostlist_hostname_create (const char *hn)
{
    return hostname_create (hn);
}

void hostlist_hostname_destroy (struct hostlist_hostname *hn)
{
    hostname_destroy (hn);
}

/*  Remove host at cursor 'cur'. If the current real cursor hl->current
 *   is affected, adjust it accordingly.
 */
static int hostlist_remove_at (struct hostlist *hl, struct current *cur)
{
    struct hostrange *hr;
    struct hostrange *new;

    if (cur->index < 0
        || cur->index >= hl->nranges
        || cur->depth < 0
        || cur->depth >= hostrange_count (hl->hr[cur->index]))
        return 0;

    nth_cache_reset (hl);

    hr = hl->hr[cur->index];

    /*  If we're removing the current host, invalidate cursor hostname
     */
    if (hl->current.index == cur->index
        && hl->current.depth == cur->depth) {
        free (hl->current.host);
        hl->current.host = NULL;
    }

    if (hostrange_delete_host (hr, hr->lo + cur->depth, &new) < 0)
        return -1;
    if (new) {
        if (!hostlist_insert_range (hl, new, cur->index + 1)) {
            /* undo the split of hr */
            hr->hi = new->hi;
            hostrange_destroy (new);
            return -1;
        }
        hostrange_destroy (new);

        /*  If the split hostrange affects the cursor, adjust it now
         */
        if (hl->current.index == cur->index
            && hl->current.depth >= cur->depth) {
            /*
             *  Current cursor was at or ahead of split. Advance to new
             *   hostrange at the correct "depth".
             */
            hl->current.index++;
            hl->current.depth = hl->current.depth - cur->depth - 1;
        }
    }
    else if (hostrange_empty (hr)) {
        /*  Hostrange is now empty, remove it. Cursor will be adjusted
         *   accordingly in hostlist_delete_range ()
         */
        hostlist_delete_range (hl, cur->index);
    }
    else if (hl->current.index == cur->index
            && hl->current.depth >= cur->depth) {
        /*
         *  Current range affected, but range was not split. The host
         *   was removed from the start or end of the range, so hosts
         *   at or after the cursor shift down by one.
         */
        hl->current.depth--;
    }

    hl->nhosts--;
    return 1;
}

/* implementation needs improvement
 */
static int hostlist_delete_host (struct hostlist *hl, const char *hostname)
{
    struct current cur = { 0 };
    struct stack_hostname hn_storage;

    struct stack_hostname *hn = hostname_stack_create (&hn_storage, hostname);
    if (!hn)
        return -1;
    int n = hostlist_find_host (hl, hn, &cur);
    if (n < 0)
        return errno == ENOENT ? 0 : -1;
    return hostlist_remove_at (hl, &cur);
}

/* XXX: Note: efficiency improvements needed */
int hostlist_delete (struct hostlist *hl, const char *hosts)
{
    int n = 0;
    const char *host = NULL;
    struct hostlist *hltmp;

    if (!hl || !hosts) {
        errno = EINVAL;
        return -1;
    }

    if (!(hltmp = hostlist_decode (hosts)))
        return -1;

    host = hostlist_first (hltmp);
    while (host) {
        int rc = hostlist_delete_host (hl, host);
        if (rc < 0) {
            hostlist_destroy (hltmp);
            return -1;
        }
        n += rc;
        host = hostlist_next (hltmp);
    }
    hostlist_destroy (hltmp);

    return n;
}

/* search through hostlist for ranges that can be collapsed
 * does =not= delete any hosts
 */
static void hostlist_collapse (struct hostlist *hl)
{
    int i, n;

    if (hl->nranges <= 1)
        return;

    for (i = 1, n = 0; i < hl->nranges; i++) {
        struct hostrange * hprev = hl->hr[n];
        struct hostrange * hnext = hl->hr[i];

        if (hostrange_prefix_cmp (hprev, hnext) == 0 &&
            hostrange_adjacent (hprev, hnext) &&
            hostrange_width_combine (hprev, hnext)) {
            hprev->hi = hnext->hi;
            hostrange_destroy (hnext);
        }
        else
            hl->hr[++n] = hnext;
    }
    for (i = n + 1; i < hl->nranges; i++)
        hl->hr[i] = NULL;
    hl->nranges = n + 1;
}

/* Insert the duplicated ids lo to hi of range 'hr' at position 'n' of hl
 * in sorted order, i.e. lo+1 to hi-1 twice each, preceded by lo and
 * followed by hi. Hosts are inserted as single host ranges and are
 * joined later by hostlist_collapse(). The hostlist is unchanged on
 * failure.
 */
static int hostlist_insert_dups (struct hostlist *hl,
                                 int n,
                                 struct hostrange *hr,
                                 unsigned long lo,
                                 unsigned long hi)
{
    unsigned long count = 2 * (hi - lo);
    struct hostrange **ranges;
    unsigned long k;

    if (count == 0)
        return 0;
    if (count > INT_MAX - hl->size) {
        errno = ERANGE;
        return -1;
    }
    if (hl->nranges + count > hl->size
        && !hostlist_resize (hl, hl->nranges + count + HOSTLIST_CHUNK))
        return -1;
    if (!(ranges = calloc (count, sizeof (*ranges))))
        return -1;
    for (k = 0; k < count; k++) {
        unsigned long id = lo + (k + 1) / 2;
        if (!(ranges[k] = hostrange_create (hr->prefix, id, id, hr->width)))
            goto error;
    }
    memmove (&hl->hr[n + count],
             &hl->hr[n],
             (hl->nranges - n) * sizeof (hl->hr[0]));
    memcpy (&hl->hr[n], ranges, count * sizeof (ranges[0]));
    hl->nranges += count;
    free (ranges);
    return 0;
error:
    for (k = 0; k < count; k++)
        hostrange_destroy (ranges[k]);
    ERRNO_SAFE_WRAP (free, ranges);
    return -1;
}

/* search through hostlist (hl) for intersecting ranges
 * split up duplicates and coalesce ranges where possible
 */
static void hostlist_coalesce (struct hostlist *hl)
{
    int i;

    /*  Scan backwards through hostranges, and determine if the
     *   current and previous ranges intersect. If so, then
     *   collect and split contiguous ranges into new ranges.
     */
    for (i = hl->nranges - 1; i > 0; i--) {
        struct hostrange * hprev = hl->hr[i - 1];
        struct hostrange * hnext = hl->hr[i];
        unsigned long lo, hi;

        /* If ranges intersect, then the common (duplicated) hosts
         *  are lo to hi.
         */
        if (hostrange_intersect_ids (hprev, hnext, &lo, &hi)) {
            /*  Insert duplicated hosts between hprev and hnext first,
             *   so that the list is unchanged on failure.
             */
            if (hostlist_insert_dups (hl, i, hprev, lo, hi) < 0)
                break;

            /*  Upper bound of duplicated range is below hprev -
             *   hnext will now hold duplicates, set next->hi to prev->hi
             */
            if (hi < hprev->hi)
                hnext->hi = hprev->hi;

            /*
             *  The duplicated range was inserted piecemeal above,
             *   e.g. [5-7,6-8] -> [5-6,6-7,7-8]
             *  Therefore adjust the end of hprev to lo (the first
             *   duplicated host), and the start of hnext to hi (the
             *   last duplicated host).
             */
            hprev->hi = lo;
            hnext->lo = hi;

            i = hl->nranges;
        }
    }

    hostlist_collapse (hl);
}


/* hostrange compare with void * arguments to allow use with
 * libc qsort()
 */
int _cmp (const void *h1, const void *h2)
{
    return hostrange_cmp ( *(struct hostrange **) h1,
                           *(struct hostrange **) h2);
}


void hostlist_sort (struct hostlist *hl)
{
    if (hl == NULL)
        return;
    if (hl->nranges <= 1)
        return;
    nth_cache_reset (hl);
    qsort (hl->hr, hl->nranges, sizeof (struct hostrange *), _cmp);
    hostlist_coalesce (hl);

    /* Sorting invalidates the cursor, so reset it to the start */
    set_current (&hl->current, 0, -1);
}

void hostlist_uniq (struct hostlist *hl)
{
    int i, n;

    if (hl == NULL)
        return;

    if (hl->nranges <= 1)
        return;

    nth_cache_reset (hl);
    qsort (hl->hr, hl->nranges, sizeof (struct hostrange *), &_cmp);

    /*  Join each range into the last kept range if possible, otherwise
     *   keep it. This compacts the array in a single pass.
     */
    for (i = 1, n = 0; i < hl->nranges; i++) {
        int ndup = hostrange_join (hl->hr[n], hl->hr[i]);
        if (ndup >= 0) {
            hostrange_destroy (hl->hr[i]);
            hl->nhosts -= ndup;
        }
        else
            hl->hr[++n] = hl->hr[i];
    }
    for (i = n + 1; i < hl->nranges; i++)
        hl->hr[i] = NULL;
    hl->nranges = n + 1;

    /* Sorting invalidates the cursor, so reset it to the start */
    set_current (&hl->current, 0, -1);
}

/* return true if a bracket is needed for the range at i in hostlist hl */
static int is_bracket_needed (struct hostlist *hl, int i)
{
    struct hostrange * h1 = hl->hr[i];
    struct hostrange * h2 = i < hl->nranges - 1 ? hl->hr[i + 1] : NULL;
    return hostrange_count (h1) > 1 || hostrange_within_range (h1, h2);
}

/* write the next bracketed hostlist, i.e. prefix[n-m,k,...]
 * into buf, writing at most n chars including the terminating '\0'
 *
 * leaves start pointing to one past last range object in bracketed list,
 * and returns the number of bytes written into buf.
 *
 * Assumes hostlist is locked.
 */
static int get_bracketed_list (struct hostlist *hl,
                              int *start,
                              FILE *fp)
{
    struct hostrange * *hr = hl->hr;
    int i = *start;
    int bracket_needed = is_bracket_needed (hl, i);

    if (fprintf (fp, "%s", hr[i]->prefix) < 0
        || (bracket_needed && fputc ('[', fp) == EOF))
        return -1;

    while (1) {
        if (!hr[i]->singlehost) {
            if (fprintf (fp, "%0*lu", hr[i]->width, hr[i]->lo) < 0)
                return -1;
            if (hr[i]->lo < hr[i]->hi)
                if (fprintf (fp, "-%0*lu", hr[i]->width, hr[i]->hi) < 0)
                    return -1;
        }
        if (++i >= hl->nranges
            || !hostrange_within_range (hr[i], hr[i-1]))
            break;
        /* Only need comma inside brackets */
        if (bracket_needed && fputc (',', fp) == EOF)
            return -1;
    }

    if (bracket_needed && fputc (']', fp) == EOF)
        return -1;

    *start = i;
    return 0;
}

static int hostlist_ranged_string (struct hostlist *hl, FILE *fp)
{
    int i = 0;
    while (i < hl->nranges) {
        if (get_bracketed_list (hl, &i, fp) < 0)
            return -1;
        if (i < hl->nranges && fputc (',', fp) == EOF)
            return -1;
    }
    return 0;
}

char * hostlist_encode (struct hostlist *hl)
{
    int saved_errno;
    char *result = NULL;
    size_t len;
    FILE *fp;

    if (hl == NULL) {
        errno = EINVAL;
        return NULL;
    }
    if (!(fp = open_memstream (&result, &len))
        || hostlist_ranged_string (hl, fp) < 0)
        goto fail;
    if (fclose (fp) < 0) {
        fp = NULL;
        goto fail;
    }
    return result;
fail:
    saved_errno = errno;
    if (fp)
        fclose (fp);
    free (result);
    errno = saved_errno;
    return NULL;
}

static struct hostrange * hr_current (struct hostlist *hl)
{
    assert (hl != NULL);
    assert (hl->current.index < hl->nranges);
    return hl->hr[hl->current.index];
}

const char *hostlist_current (struct hostlist *hl)
{
    int depth;

    if (hl == NULL) {
        errno = EINVAL;
        return NULL;
    }

    depth = hl->current.depth;
    if (depth < 0 || hl->current.index >= hl->nranges)
        return NULL;
    if (!hl->current.host)
        hl->current.host = hostrange_host_tostring (hr_current (hl), depth);
    return hl->current.host;
}

const char *hostlist_first (struct hostlist *hl)
{
    if (hl == NULL) {
        errno = EINVAL;
        return NULL;
    }
    errno = 0;
    if (hl->nranges == 0)
        return NULL;

    set_current (&hl->current, 0, 0);

    return hostlist_current (hl);
}

const char *hostlist_last (struct hostlist *hl)
{
    struct hostrange *last;

    if (hl == NULL) {
        errno = EINVAL;
        return NULL;
    }
    errno = 0;
    if (hl->nranges == 0)
        return NULL;

    last = hl->hr[hl->nranges - 1];
    set_current (&hl->current, hl->nranges - 1, hostrange_count (last) - 1);

    return hostlist_current (hl);
}

const char *hostlist_next (struct hostlist *hl)
{
    int n;

    if (hl == NULL) {
        errno = EINVAL;
        return NULL;
    }
    errno = 0;
    free (hl->current.host);
    hl->current.host = NULL;

    /*  Already at end of list */
    if (hl->current.index > hl->nranges - 1)
        return NULL;

    n = hostrange_count (hr_current (hl));

    /* Advance current within hostrange object,
     * Move to next object if necessary
     */
    if (++(hl->current.depth) > n - 1) {
        if (++(hl->current.index) >= hl->nranges)
            return NULL;
        hl->current.depth = 0;
    }
    return hostlist_current (hl);
}

int hostlist_remove_current (struct hostlist *hl)
{
    if (hl == NULL) {
        errno = EINVAL;
        return -1;
    }
    return hostlist_remove_at (hl, &hl->current);
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
