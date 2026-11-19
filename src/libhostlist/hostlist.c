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

#include "errno_safe.h"

#include "hostlist.h"
#include "hostrange.h"
#include "hostname.h"
#include "util.h"

/* number of elements to allocate when extending the hostlist array */
#define HOSTLIST_CHUNK    16

/* max host range: anything larger will be assumed to be an error */
#define MAX_RANGE    1<<20    /* 1M Hosts */

/* max host suffix value */
#define MAX_HOST_SUFFIX 1<<25

/* max number of ranges that will be processed between brackets */
#define MAX_RANGES    10240    /* 10K Ranges */

/* max size of internal hostrange buffer */
#define MAXHOSTRANGELEN 1024

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
    int i;
    size_t oldsize;
    assert (hl != NULL);
    oldsize = hl->size;
    hl->size = newsize;
    hl->hr = realloc ((void *) hl->hr, hl->size*sizeof (struct hostrange *));
    if (!(hl->hr))
        return 0;

    for (i = oldsize; i < newsize; i++)
        hl->hr[i] = NULL;

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
        && tail->hi == hr->lo - 1
        && hostrange_width_combine (tail, hr)) {
        tail->hi = hr->hi;
    } else {
        if (!(hl->hr[hl->nranges++] = hostrange_copy (hr)))
            goto error;
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
    int i;
    struct hostrange * tmp;

    assert (hl != NULL);
    assert (hr != NULL);

    if (n > hl->nranges)
        return 0;

    if (hl->size == hl->nranges && !hostlist_expand (hl))
        return 0;

    /* copy new hostrange into slot "n" in array */
    tmp = hl->hr[n];
    hl->hr[n] = hostrange_copy (hr);

    /* push remaining hostrange entries up */
    for (i = n + 1; i < hl->nranges + 1; i++) {
        struct hostrange * last = hl->hr[i];
        hl->hr[i] = tmp;
        tmp = last;
    }
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


/* Grab a single range from str
 * returns 0 if str contained a valid number or range,
 *        -1 if conversion of str to a range failed.
 */
static int parse_next_range (const char *str, struct _range *range)
{
    int saved_errno;
    char *p, *q;
    char *orig = strdup (str);
    if (!orig)
        return -1;

    if ((p = strchr (orig, '-'))) {
        *p++ = '\0';
        if (*p == '-') {  /* don't allow negative numbers */
            errno = EINVAL;
            goto error;
        }
    }
    range->lo = strtoul (orig, &q, 10);
    if (q == orig)  {
        errno = EINVAL;
        goto error;
    }

    range->hi = (p && *p) ? strtoul (p, &q, 10) : range->lo;

    errno = EINVAL;
    if (q == p || *q != '\0')
        goto error;

    if (range->lo > range->hi)
        goto error;

    if (range->hi - range->lo + 1 > MAX_RANGE ) {
        errno = ERANGE;
        goto error;
    }

    range->width = strlen (orig);
    free (orig);
    return 0;

  error:
    saved_errno = errno;
    free (orig);
    errno = saved_errno;
    return -1;
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
 *  of struct _range types (max 'len' elements).
 *
 * Return number of ranges created, or -1 on error.
 */
static int parse_range_list (char *str, struct _range *ranges, int len)
{
    char *p;
    int count = 0;

    while (str) {
        if (count == len)
            return -1;
        if ((p = strchr (str, ',')))
            *p++ = '\0';
        if (parse_next_range (str, &ranges[count++]) < 0)
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
    int i;
    unsigned long j;

    /* compute max buffer size for this set of hosts */
    int size = strlen (pfx) + strlen (sfx) + 20 + rng->width;


    for (i = 0; i < n; i++) {
        for (j = rng->lo; j <= rng->hi; j++) {
            char host[size];
            struct hostrange * hr;
            snprintf (host,
                      sizeof (host),
                      "%s%0*lu%s",
                      pfx,
                      rng->width,
                      j,
                      sfx);
            if (!(hr = hostrange_create_single (host)))
                return -1;
            hostlist_append_range (hl, hr);
            /*
             * hr is copied in hostlist_append_range. Need to free here.
             */
            hostrange_destroy (hr);
        }
        rng++;
    }
    return 0;
}

/*
 * Create a hostlist from a string with brackets '[' ']'
 */
static struct hostlist * hostlist_create_bracketed (const char *hostlist,
                                                    char *sep,
                                                    char *r_op)
{
    struct hostlist * new = hostlist_create ();
    struct {
        struct _range ranges[MAX_RANGES];
        char cur_tok[1024];
    } *ctx;
    int nr;
    int rc;
    char *p, *tok, *str, *orig;

    if (!new)
        return NULL;

    if (hostlist == NULL)
        return new;

    if (!(orig = str = strdup (hostlist))
        || !(ctx = calloc (1, sizeof (*ctx)))) {
        hostlist_destroy (new);
        ERRNO_SAFE_WRAP (free, orig);
        return NULL;
    }

    while ((tok = next_tok (sep, &str)) != NULL) {
        strncpy (ctx->cur_tok, tok, 1024 - 1);

        if ((p = strchr (tok, '[')) != NULL) {
            char *q, *prefix = tok;
            *p++ = '\0';

            if ((q = strchr (p, ']'))) {
                *q = '\0';
                nr = parse_range_list (p, ctx->ranges, MAX_RANGES);
                if (nr < 0)
                    goto error;

                if (*(++q) != '\0')
                    rc = append_range_list_with_suffix (new,
                                                        prefix,
                                                        q,
                                                        ctx->ranges,
                                                        nr);
                else
                    rc = append_range_list (new,
                                            prefix,
                                            ctx->ranges,
                                            nr);
                if (rc < 0)
                    goto error;

            } else                    /* Error: brackets must be balanced */
                goto error_unmatched;

        } else if (strchr (tok, ']')) /* Error: brackets must be balanced */
            goto error_unmatched;
        else                          /* Ok: No brackets found, single host */
            hostlist_append_host (new, ctx->cur_tok);
    }

    free (orig);
    free (ctx);
    return new;

  error_unmatched:
    errno = EINVAL;
  error:
    hostlist_destroy (new);
    ERRNO_SAFE_WRAP (free, orig);
    ERRNO_SAFE_WRAP (free, ctx);
    return NULL;
}

struct hostlist * hostlist_decode (const char *str)
{
    if (!str) {
        errno = EINVAL;
        return NULL;
    }
    return hostlist_create_bracketed (str, "\t, ", "-");
}

struct hostlist * hostlist_copy (const struct hostlist *hl)
{
    int i;
    struct hostlist * new;

    if (hl == NULL)
        return NULL;

    if (!(new = hostlist_create ()))
        goto done;

    new->nranges = hl->nranges;
    new->nhosts = hl->nhosts;
    if (new->nranges > new->size)
        hostlist_resize (new, new->nranges);

    for (i = 0; i < hl->nranges; i++)
        new->hr[i] = hostrange_copy (hl->hr[i]);

  done:
    return new;
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
    retval = new->nhosts;
    hostlist_append_list (hl, new);
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

    for (i = 0; i < h2->nranges; i++)
        n += hostlist_append_range (h1, h2->hr[i]);

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

    count = 0;
    for (i = 0; i < hl->nranges; i++) {
        int num_in_range = hostrange_count (hl->hr[i]);

        if (n <= (num_in_range - 1 + count)) {
            int d = n - count;
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
    if (!hn)
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

    if (cur->index > hl->nhosts - 1)
        return 0;

    hr = hl->hr[cur->index];

    /*  If we're removing the current host, invalidate cursor hostname
     */
    if (hl->current.index == cur->index
        && hl->current.depth == cur->depth) {
        free (hl->current.host);
        hl->current.host = NULL;
    }

    new = hostrange_delete_host (hr, hr->lo + cur->depth);
    if (new) {
        hostlist_insert_range (hl, new, cur->index + 1);
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
         *  Current range affected, but range was not split. Adjust
         *   current cursor appropriately
         */
        hl->current.depth = hl->current.depth - cur->depth - 1;
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
        n += hostlist_delete_host (hl, host);
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
    int i;

    for (i = hl->nranges - 1; i > 0; i--) {
        struct hostrange * hprev = hl->hr[i - 1];
        struct hostrange * hnext = hl->hr[i];

        if (hostrange_prefix_cmp (hprev, hnext) == 0 &&
            hprev->hi == hnext->lo - 1 &&
            hostrange_width_combine (hprev, hnext)) {
            hprev->hi = hnext->hi;
            hostlist_delete_range (hl, i);
        }
    }
}

/* search through hostlist (hl) for intersecting ranges
 * split up duplicates and coalesce ranges where possible
 */
static void hostlist_coalesce (struct hostlist *hl)
{
    int i, j;
    struct hostrange * new;

    /*  Scan backwards through hostranges, and determine if the
     *   current and previous ranges intersect. If so, then
     *   collect and split contiguous ranges into new ranges.
     */
    for (i = hl->nranges - 1; i > 0; i--) {
        struct hostrange * hprev = hl->hr[i - 1];
        struct hostrange * hnext = hl->hr[i];

        /* If ranges intersect, then the common (duplicated) hosts
         *  are returned in 'new'.
         */
        new = hostrange_intersect (hprev, hnext);
        if (new) {
            j = i;

            /*  Upper bound of duplicated range is below hprev -
             *   hnext will now hold duplicates, set next->hi to prev->hi
             */
            if (new->hi < hprev->hi)
                hnext->hi = hprev->hi;

            /*
             *  The duplicated range will inserted piecemeal below,
             *   e.g. [5-7,6-8] -> [5-6,6-7,7-8]
             *  Therefore adjust the end of hprev to new->lo (the
             *   first duplicated host), and the start of hnext to
             *   new->hi (the last duplicated host). The rest of the
             *   duplicates will be inserted below.
             */
            hprev->hi = new->lo;
            hnext->lo = new->hi;

            /*  N.B.: It should not be possible that hprev is now
             *   empty, so the below should be deadcode
             *
             *if (hostrange_empty (hprev))
             *   hostlist_delete_range (hl, i);
             */

            /*  Now insert each duplicated number between hprev and
             *   hnext:
             */
            while (new->lo <= new->hi) {
                struct hostrange * hr = hostrange_create (new->prefix,
                                                          new->lo,
                                                          new->lo,
                                                          new->width);

                if (new->lo > hprev->hi)
                    hostlist_insert_range (hl, hr, j++);

                if (new->lo < hnext->lo)
                    hostlist_insert_range (hl, hr, j++);

                hostrange_destroy (hr);

                new->lo++;
            }
            i = hl->nranges;
            hostrange_destroy (new);
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
    qsort (hl->hr, hl->nranges, sizeof (struct hostrange *), _cmp);
    hostlist_coalesce (hl);
}

/* attempt to join ranges at loc and loc-1 in a hostlist  */
/* delete duplicates, return the number of hosts deleted  */
/* assumes that the hostlist hl has been locked by caller */
/* returns -1 if no range join occurred */
static int attempt_range_join (struct hostlist *hl, int loc)
{
    int ndup;
    assert (hl != NULL);
    assert (loc > 0);
    assert (loc < hl->nranges);
    ndup = hostrange_join (hl->hr[loc - 1], hl->hr[loc]);
    if (ndup >= 0) {
        hostlist_delete_range (hl, loc);
        hl->nhosts -= ndup;
    }
    return ndup;
}

void hostlist_uniq (struct hostlist *hl)
{
    int i = 1;
    if (hl == NULL)
        return;

    if (hl->nranges <= 1)
        return;

    qsort (hl->hr, hl->nranges, sizeof (struct hostrange *), &_cmp);

    while (i < hl->nranges) {
        if (attempt_range_join (hl, i) < 0) /* No range join occurred */
            i++;
    }
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
    assert (hl->current.index <= hl->nhosts);
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
    if (depth < 0 || hl->current.index > hl->nhosts - 1)
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
