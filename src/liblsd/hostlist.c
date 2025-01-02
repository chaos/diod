/*****************************************************************************\
 *  $Id: hostlist.c 9378 2009-12-16 05:34:32Z grondo $
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STRING_H
#    include <string.h>
#  endif
#  if HAVE_PTHREAD_H
#    include <pthread.h>
#  endif
#else                /* !HAVE_CONFIG_H */
#  include <string.h>
#  include <pthread.h>
#endif                /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <sys/param.h>
#include <unistd.h>

#include "hostlist.h"

/*
 * lsd_fatal_error : fatal error macro
 */
#ifdef WITH_LSD_FATAL_ERROR_FUNC
#  undef lsd_fatal_error
   extern void lsd_fatal_error(char *file, int line, char *mesg);
#else /* !WITH_LSD_FATAL_ERROR_FUNC */
#  ifndef lsd_fatal_error
#    define lsd_fatal_error(file, line, mesg)                                \
       do {                                                                  \
           fprintf(stderr, "ERROR: [%s:%d] %s: %s\n",                        \
           file, line, mesg, strerror(errno));                               \
       } while (0)
#  endif /* !lsd_fatal_error */
#endif /* !WITH_LSD_FATAL_ERROR_FUNC */

/*
 * lsd_nomem_error
 */
#ifdef WITH_LSD_NOMEM_ERROR_FUNC
#  undef lsd_nomem_error
   extern void * lsd_nomem_error(char *file, int line, char *mesg);
#else /* !WITH_LSD_NOMEM_ERROR_FUNC */
#  ifndef lsd_nomem_error
#    define lsd_nomem_error(file, line, mesg) (NULL)
#  endif /* !lsd_nomem_error */
#endif /* !WITH_LSD_NOMEM_ERROR_FUNC */

/*
 * OOM helper function
 *  Automatically call lsd_nomem_error with appropriate args
 *  and set errno to ENOMEM
 */
#define out_of_memory(mesg)                                                  \
    do {                                                                     \
        errno = ENOMEM;                                                      \
        return(lsd_nomem_error(__FILE__, __LINE__, mesg));                   \
    } while (0)

/* 
 * Some constants and tunables:
 */

/* number of elements to allocate when extending the hostlist array */
#define HOSTLIST_CHUNK    16

/* max host range: anything larger will be assumed to be an error */
#define MAX_RANGE    16384    /* 16K Hosts */

/* max host suffix value */
#define MAX_HOST_SUFFIX 1<<25

/* max number of ranges that will be processed between brackets */
#define MAX_RANGES    10240    /* 10K Ranges */

/* size of internal hostname buffer (+ some slop), hostnames will probably
 * be truncated if longer than MAXHOSTNAMELEN */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN    64
#endif

/* max size of internal hostrange buffer */
#define MAXHOSTRANGELEN 1024

/* ----[ Internal Data Structures ]---- */

/* hostname type: A convenience structure used in parsing single hostnames */
struct hostname_components {
    char *hostname;         /* cache of initialized hostname        */
    char *prefix;           /* hostname prefix                      */
    unsigned long num;      /* numeric suffix                       */

    /* string representation of numeric suffix
     * points into `hostname'                                       */
    char *suffix;
};

typedef struct hostname_components *hostname_t;

/* hostrange type: A single prefix with `hi' and `lo' numeric suffix values */
struct hostrange_components {
    char *prefix;        /* alphanumeric prefix: */

    /* beginning (lo) and end (hi) of suffix range */
    unsigned long lo, hi;

    /* width of numeric output format
     * (pad with zeros up to this width) */
    int width;

    /* If singlehost is 1, `lo' and `hi' are invalid */
    unsigned singlehost:1;
};

typedef struct hostrange_components *hostrange_t;

/* The hostlist type: An array based list of hostrange_t's */
struct hostlist {
#ifndef NDEBUG
#define HOSTLIST_MAGIC    57005
    int magic;
#endif
#if    WITH_PTHREADS
    pthread_mutex_t mutex;
#endif                /* WITH_PTHREADS */

    /* current number of elements available in array */
    int size;

    /* current number of ranges stored in array */
    int nranges;

    /* current number of hosts stored in hostlist */
    int nhosts;

    /* pointer to hostrange array */
    hostrange_t *hr;

    /* list of iterators */
    struct hostlist_iterator *ilist;

};


/* a hostset is a wrapper around a hostlist */
struct hostset {
    hostlist_t hl;
};

struct hostlist_iterator {
#ifndef NDEBUG
    int magic;
#endif
    /* hostlist we are traversing */
    hostlist_t hl;

    /* current index of iterator in hl->hr[] */
    int idx;

    /* current hostrange object in list hl, i.e. hl->hr[idx] */
    hostrange_t hr;

    /* current depth we've traversed into range hr */
    int depth;

    /* next ptr for lists of iterators */
    struct hostlist_iterator *next;
};


/* ---- ---- */

/* ------[ static function prototypes ]------ */

static void _error(char *file, int line, char *mesg, ...);
static char * _next_tok(char *, char **);
static int    _zero_padded(unsigned long, int);
static int    _width_equiv(unsigned long, int *, unsigned long, int *);

static int           host_prefix_end(const char *);
static hostname_t    hostname_create(const char *);
static void          hostname_destroy(hostname_t);
static int           hostname_suffix_is_valid(hostname_t);
static int           hostname_suffix_width(hostname_t);

static hostrange_t   hostrange_new(void);
static hostrange_t   hostrange_create_single(const char *);
static hostrange_t   hostrange_create(char *, unsigned long, unsigned long, int);
static unsigned long hostrange_count(hostrange_t);
static hostrange_t   hostrange_copy(hostrange_t);
static void          hostrange_destroy(hostrange_t);
static hostrange_t   hostrange_delete_host(hostrange_t, unsigned long);
static int           hostrange_cmp(hostrange_t, hostrange_t);
static int           hostrange_prefix_cmp(hostrange_t, hostrange_t);
static int           hostrange_within_range(hostrange_t, hostrange_t);
static int           hostrange_width_combine(hostrange_t, hostrange_t);
static int           hostrange_empty(hostrange_t);
static char *        hostrange_pop(hostrange_t);
static char *        hostrange_shift(hostrange_t);
static int           hostrange_join(hostrange_t, hostrange_t);
static hostrange_t   hostrange_intersect(hostrange_t, hostrange_t);
static int           hostrange_hn_within(hostrange_t, hostname_t);
static size_t        hostrange_to_string(hostrange_t hr, size_t, char *, char *);
static size_t        hostrange_numstr(hostrange_t, size_t, char *);

static hostlist_t  hostlist_new(void);
static hostlist_t _hostlist_create_bracketed(const char *, char *, char *);
static int         hostlist_resize(hostlist_t, size_t);
static int         hostlist_expand(hostlist_t);
static int         hostlist_push_range(hostlist_t, hostrange_t);
static int         hostlist_push_hr(hostlist_t, char *, unsigned long,
                                    unsigned long, int);
static int         hostlist_insert_range(hostlist_t, hostrange_t, int);
static void        hostlist_delete_range(hostlist_t, int n);
static void        hostlist_coalesce(hostlist_t hl);
static void        hostlist_collapse(hostlist_t hl);
static hostlist_t _hostlist_create(const char *, char *, char *);
static void        hostlist_shift_iterators(hostlist_t, int, int, int);
static int        _attempt_range_join(hostlist_t, int);
static int        _is_bracket_needed(hostlist_t, int);

static hostlist_iterator_t hostlist_iterator_new(void);
static void               _iterator_advance(hostlist_iterator_t);
static void               _iterator_advance_range(hostlist_iterator_t);

static int hostset_find_host(hostset_t, const char *);

/* ------[ macros ]------ */

#ifdef WITH_PTHREADS
#  define mutex_init(mutex)                                                  \
     do {                                                                    \
        int e = pthread_mutex_init(mutex, NULL);                             \
        if (e) {                                                             \
            errno = e;                                                       \
            lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex init:");     \
            abort();                                                         \
        }                                                                    \
     } while (0)

#  define mutex_lock(mutex)                                                  \
     do {                                                                    \
        int e = pthread_mutex_lock(mutex);                                   \
        if (e) {                                                             \
           errno = e;                                                        \
           lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex lock:");      \
           abort();                                                          \
        }                                                                    \
     } while (0)

#  define mutex_unlock(mutex)                                                \
     do {                                                                    \
        int e = pthread_mutex_unlock(mutex);                                 \
        if (e) {                                                             \
            errno = e;                                                       \
            lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex unlock:");   \
            abort();                                                         \
        }                                                                    \
     } while (0)

#  define mutex_destroy(mutex)                                               \
     do {                                                                    \
        int e = pthread_mutex_destroy(mutex);                                \
        if (e) {                                                             \
            errno = e;                                                       \
            lsd_fatal_error(__FILE__, __LINE__, "hostlist mutex destroy:");  \
            abort();                                                         \
        }                                                                    \
     } while (0)

#else                /* !WITH_PTHREADS */

#  define mutex_init(mutex)
#  define mutex_lock(mutex)
#  define mutex_unlock(mutex)
#  define mutex_destroy(mutex)

#endif                /* WITH_PTHREADS */

#define LOCK_HOSTLIST(_hl)                                                   \
      do {                                                                   \
          assert(_hl != NULL);                                               \
          mutex_lock(&(_hl)->mutex);                                         \
          assert((_hl)->magic == HOSTLIST_MAGIC);                            \
      } while (0)

#define UNLOCK_HOSTLIST(_hl)                                                 \
      do {                                                                   \
          mutex_unlock(&(_hl)->mutex);                                       \
      } while (0)                       

#define seterrno_ret(_errno, _rc)                                            \
      do {                                                                   \
          errno = _errno;                                                    \
          return _rc;                                                        \
      } while (0)

/* ------[ Function Definitions ]------ */

/* ----[ general utility functions ]---- */


/*
 *  Varargs capable error reporting via lsd_fatal_error()
 */
static void _error(char *file, int line, char *msg, ...)
{
    va_list ap;
    char    buf[1024];
    int     len = 0;
    va_start(ap, msg);

    len = vsnprintf(buf, 1024, msg, ap);
    if ((len < 0) || (len > 1024)) 
        buf[1023] = '\0';

    lsd_fatal_error(file, line, buf);

    va_end(ap);
    return;
}

static int _advance_past_brackets (char *tok, char **str)
{
    /* if _single_ opening bracket exists b/w tok and str, push str
     * past first closing bracket to next seperator */
    if (   memchr(tok, '[', *str - tok) != NULL
        && memchr(tok, ']', *str - tok) == NULL ) {
        char *q = strchr(*str, ']');
        if (q && memchr(*str, '[', q - *str) == NULL) {
            *str = q + 1;
            return (1);
        }
    }

    return 0;
}

/* 
 * Helper function for host list string parsing routines 
 * Returns a pointer to the next token; additionally advance *str
 * to the next separator.
 *
 * next_tok was taken directly from pdsh courtesy of Jim Garlick.
 * (with modifications to support bracketed hostlists, i.e.:
 *  xxx[xx,xx,xx] is a single token)
 *
 */
static char * _next_tok(char *sep, char **str)
{
    char *tok;

    /* push str past any leading separators */
    while (**str != '\0' && strchr(sep, **str) != NULL)
        (*str)++;

    if (**str == '\0')
        return NULL;

    /* assign token ptr */
    tok = *str;

    /*
     * Advance str past any separators, but if a separator occurs between
     *  brackets, e.g. foo[0-3,5], then advance str past closing brackets and
     *  try again.
     */
    do {
        /* push str past token and leave pointing to first separator */
        while (**str != '\0' && strchr(sep, **str) == NULL)
            (*str)++;
    } while (_advance_past_brackets (tok, str));

   /* nullify consecutive separators and push str beyond them */
    while (**str != '\0' && strchr(sep, **str) != NULL)
        *(*str)++ = '\0';

    return tok;
}


/* return the number of zeros needed to pad "num" to "width"
 */
static int _zero_padded(unsigned long num, int width)
{
    int n = 1;
    while (num /= 10L)
        n++;
    return width > n ? width - n : 0;
}

/* test whether two format `width' parameters are "equivalent"
 * The width arguments "wn" and "wm" for integers "n" and "m" 
 * are equivalent if:
 *  
 *  o  wn == wm  OR
 *
 *  o  applying the same format width (either wn or wm) to both of  
 *     'n' and 'm' will not change the zero padding of *either* 'm' nor 'n'.
 *
 *  If this function returns 1 (or true), the appropriate width value
 *  (either 'wm' or 'wn') will have been adjusted such that both format
 *  widths are equivalent.
 */
static int _width_equiv(unsigned long n, int *wn, unsigned long m, int *wm)
{
    int npad, nmpad, mpad, mnpad;

    if (wn == wm)
        return 1;

    npad = _zero_padded(n, *wn);
    nmpad = _zero_padded(n, *wm);
    mpad = _zero_padded(m, *wm);
    mnpad = _zero_padded(m, *wn);

    if (npad != nmpad && mpad != mnpad)
        return 0;

    if (npad != nmpad) {
        if (mpad == mnpad) {
            *wm = *wn;
            return 1;
        } else
            return 0;
    } else {        /* mpad != mnpad */
        if (npad == nmpad) {
            *wn = *wm;
            return 1;
        } else
            return 0;
    }

    /* not reached */
}


/* ----[ hostname_t functions ]---- */

/* 
 * return the location of the last char in the hostname prefix
 */
static int host_prefix_end(const char *hostname)
{
    int idx = strlen(hostname) - 1;

    while (idx >= 0 && isdigit((char) hostname[idx])) 
        idx--;
    return idx;
}

static hostname_t hostname_create_with_suffix (const char *hostname, int idx)
{
    hostname_t hn = NULL;
    char *p;

    assert(hostname != NULL);

    if (!(hn = (hostname_t) malloc(sizeof(*hn))))
        out_of_memory("hostname create");

    if (!(hn->hostname = strdup(hostname))) {
        free(hn);
        out_of_memory("hostname create");
    }

    hn->num = 0;
    hn->prefix = NULL;
    hn->suffix = NULL;

    if (idx == strlen(hostname) - 1) {
        if ((hn->prefix = strdup(hostname)) == NULL) {
            hostname_destroy(hn);
            out_of_memory("hostname prefix create");
        }
        return hn;
    }

    hn->suffix = hn->hostname + idx + 1;
    hn->num = strtoul(hn->suffix, &p, 10);

    if ((*p == '\0') && (hn->num <= MAX_HOST_SUFFIX)) {
        if (!(hn->prefix = malloc((idx + 2) * sizeof(char)))) {
            hostname_destroy(hn);
            out_of_memory("hostname prefix create");
        }
        memcpy(hn->prefix, hostname, idx + 1);
        hn->prefix[idx + 1] = '\0';
    } else {
        if (!(hn->prefix = strdup(hostname))) {
            hostname_destroy(hn);
            out_of_memory("hostname prefix create");
        }
        hn->suffix = NULL;
    }

    return hn;

}


/* 
 * create a hostname_t object from a string hostname
 */
static hostname_t hostname_create(const char *hostname)
{
    int idx = host_prefix_end (hostname);
    return hostname_create_with_suffix (hostname, idx);
}

/* free a hostname object
 */
static void hostname_destroy(hostname_t hn)
{
    if (hn == NULL)
        return;
    hn->suffix = NULL;
    if (hn->hostname)
        free(hn->hostname);
    if (hn->prefix)
        free(hn->prefix);
    free(hn);
}

/* return true if the hostname has a valid numeric suffix 
 */
static int hostname_suffix_is_valid(hostname_t hn)
{
    return hn->suffix != NULL;
}

/* return the width (in characters) of the numeric part of the hostname
 */
static int hostname_suffix_width(hostname_t hn)
{
    assert(hn->suffix != NULL);
    return (int) strlen(hn->suffix);
}


/* ----[ hostrange_t functions ]---- */

/* allocate a new hostrange object 
 */
static hostrange_t hostrange_new(void)
{
    hostrange_t new = (hostrange_t) malloc(sizeof(*new));
    if (!new) 
        out_of_memory("hostrange create");
    return new;
}

/* Create a hostrange_t containing a single host without a valid suffix
 * hr->prefix will represent the entire hostname.
 */
static hostrange_t hostrange_create_single(const char *prefix)
{
    hostrange_t new;

    assert(prefix != NULL);

    if ((new = hostrange_new()) == NULL)
        goto error1;

    if ((new->prefix = strdup(prefix)) == NULL)
        goto error2;

    new->singlehost = 1;
    new->lo = 0L;
    new->hi = 0L;
    new->width = 0;

    return new;

  error2:
    free(new);
  error1:
    out_of_memory("hostrange create single");
}


/* Create a hostrange object with a prefix, hi, lo, and format width
 */
static hostrange_t
hostrange_create(char *prefix, unsigned long lo, unsigned long hi, int width)
{
    hostrange_t new;

    assert(prefix != NULL);

    if ((new = hostrange_new()) == NULL)
        goto error1;

    if ((new->prefix = strdup(prefix)) == NULL)
        goto error2;

    new->lo = lo;
    new->hi = hi;
    new->width = width;

    new->singlehost = 0;

    return new;

  error2:
    free(new);
  error1:
    out_of_memory("hostrange create");
}


/* Return the number of hosts stored in the hostrange object
 */
static unsigned long hostrange_count(hostrange_t hr)
{
    assert(hr != NULL);
    if (hr->singlehost)
        return 1;
    else
        return hr->hi - hr->lo + 1;
}

/* Copy a hostrange object
 */
static hostrange_t hostrange_copy(hostrange_t hr)
{
    assert(hr != NULL);

    if (hr->singlehost)
        return hostrange_create_single(hr->prefix);
    else
        return hostrange_create(hr->prefix, hr->lo, hr->hi,
                    hr->width);
}


/* free memory allocated by the hostrange object
 */
static void hostrange_destroy(hostrange_t hr)
{
    if (hr == NULL)
        return;
    if (hr->prefix)
        free(hr->prefix);
    free(hr);
}

/* hostrange_delete_host() deletes a specific host from the range.
 * If the range is split into two, the greater range is returned,
 * and `hi' of the lesser range is adjusted accordingly. If the
 * highest or lowest host is deleted from a range, NULL is returned
 * and the hostrange hr is adjusted properly.
 */
static hostrange_t hostrange_delete_host(hostrange_t hr, unsigned long n)
{
    hostrange_t new = NULL;

    assert(hr != NULL);
    assert(n >= hr->lo && n <= hr->hi);

    if (n == hr->lo)
        hr->lo++;
    else if (n == hr->hi)
        hr->hi--;
    else {
        if (!(new = hostrange_copy(hr)))
            out_of_memory("hostrange copy");
        hr->hi = n - 1;
        new->lo = n + 1;
    }

    return new;
}

/* hostrange_cmp() is used to sort hostrange objects. It will
 * sort based on the following (in order):
 *  o result of strcmp on prefixes
 *  o if widths are compatible, then: 
 *       sort based on lowest suffix in range
 *    else
 *       sort based on width                     */
static int hostrange_cmp(hostrange_t h1, hostrange_t h2)
{
    int retval;

    assert(h1 != NULL);
    assert(h2 != NULL);

    if ((retval = hostrange_prefix_cmp(h1, h2)) == 0)
        retval = hostrange_width_combine(h1, h2) ?
            h1->lo - h2->lo : h1->width - h2->width;

    return retval;
}


/* compare the prefixes of two hostrange objects. 
 * returns:
 *    < 0   if h1 prefix is less than h2 OR h1 == NULL.
 *
 *      0   if h1's prefix and h2's prefix match, 
 *          UNLESS, either h1 or h2 (NOT both) do not have a valid suffix.
 *
 *    > 0   if h1's prefix is greater than h2's OR h2 == NULL. */
static int hostrange_prefix_cmp(hostrange_t h1, hostrange_t h2)
{
    int retval;
    if (h1 == NULL)
        return 1;
    if (h2 == NULL)
        return -1;

    retval = strcmp(h1->prefix, h2->prefix);
    return retval == 0 ? h2->singlehost - h1->singlehost : retval;
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
static int hostrange_within_range(hostrange_t h1, hostrange_t h2)
{
    if (hostrange_prefix_cmp(h1, h2) == 0)
        return h1->singlehost || h2->singlehost ? 0 : 1;
    else
        return 0;
}


/* compare two hostrange objects to determine if they are width 
 * compatible,  returns:
 *  1 if widths can safely be combined
 *  0 if widths cannot be safely combined
 */
static int hostrange_width_combine(hostrange_t h0, hostrange_t h1)
{
    assert(h0 != NULL);
    assert(h1 != NULL);

    return _width_equiv(h0->lo, &h0->width, h1->lo, &h1->width);
}


/* Return true if hostrange hr contains no hosts, i.e. hi < lo
 */
static int hostrange_empty(hostrange_t hr)
{
    assert(hr != NULL);
    return ((hr->hi < hr->lo) || (hr->hi == (unsigned long) -1));
}

/* return the string representation of the last host in hostrange hr
 * and remove that host from the range (i.e. decrement hi if possible)
 *
 * Returns NULL if malloc fails OR there are no more hosts left
 */
static char *hostrange_pop(hostrange_t hr)
{
    size_t size = 0;
    char *host = NULL;

    assert(hr != NULL);

    if (hr->singlehost) {
        hr->lo++;    /* effectively set count == 0 */
        host = strdup(hr->prefix);
    } else if (hostrange_count(hr) > 0) {
        size = strlen(hr->prefix) + hr->width + 16;    
        if (!(host = (char *) malloc(size * sizeof(char))))
            out_of_memory("hostrange pop");
        snprintf(host, size, "%s%0*lu", hr->prefix, 
             hr->width, hr->hi--);
    }

    return host;
}

/* Same as hostrange_pop(), but remove host from start of range */
static char *hostrange_shift(hostrange_t hr)
{
    size_t size = 0;
    char *host = NULL;

    assert(hr != NULL);

    if (hr->singlehost) {
        hr->lo++;
        if (!(host = strdup(hr->prefix)))
            out_of_memory("hostrange shift");
    } else if (hostrange_count(hr) > 0) {
        size = strlen(hr->prefix) + hr->width + 16;
        if (!(host = (char *) malloc(size * sizeof(char))))
            out_of_memory("hostrange shift");
        snprintf(host, size, "%s%0*lu", hr->prefix,
             hr->width, hr->lo++);
    }

    return host;
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
static int hostrange_join(hostrange_t h1, hostrange_t h2)
{
    int duplicated = -1;

    assert(h1 != NULL);
    assert(h2 != NULL);
    assert(hostrange_cmp(h1, h2) <= 0);

    if (hostrange_prefix_cmp(h1, h2) == 0 &&
        hostrange_width_combine(h1, h2)) {

        if (h1->singlehost && h2->singlehost) {    /* matching singlets  */
            duplicated = 1;
        } else if (h1->hi == h2->lo - 1) {    /* perfect join       */
            h1->hi = h2->hi;
            duplicated = 0;
        } else if (h1->hi >= h2->lo) {    /* some duplication   */
            if (h1->hi < h2->hi) {
                duplicated = h1->hi - h2->lo + 1;
                h1->hi = h2->hi;
            } else
                duplicated = hostrange_count(h2);
        }
    }

    return duplicated;
}

/* hostrange intersect returns the intersection (common hosts)
 * of hostrange objects h1 and h2. If there is no intersection,
 * NULL is returned.
 *
 * It is assumed that h1 <= h2 (i.e. h1->lo <= h2->lo)
 */
static hostrange_t hostrange_intersect(hostrange_t h1, hostrange_t h2)
{
    hostrange_t new = NULL;

    assert(h1 != NULL);
    assert(h2 != NULL);

    if (h1->singlehost || h2->singlehost)
        return NULL;

    assert(hostrange_cmp(h1, h2) <= 0);

    if ((hostrange_prefix_cmp(h1, h2) == 0)
        && (h1->hi > h2->lo) 
        && (hostrange_width_combine(h1, h2))) {

        if (!(new = hostrange_copy(h1)))
            return NULL;
        new->lo = h2->lo;
        new->hi = h2->hi < h1->hi ? h2->hi : h1->hi;
    }

    return new;
}

/* return offset of hn if it is in the hostlist or
 *        -1 if not.
 */
static int hostrange_hn_within(hostrange_t hr, hostname_t hn)
{
    int len_hr;
    int len_hn;
    int width;

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
        if (strcmp (hn->hostname, hr->prefix) == 0)
            return 0;
        else
            return -1;
    }

    /*
     *  Now we know [hr] is not a "singlehost", so hostname
     *   better have a valid numeric suffix, or there is no
     *   way we can match
     */
    if (!hostname_suffix_is_valid (hn))
        return -1;

    len_hn = strlen (hn->prefix);

    /*
     *  If hostrange and hostname prefixes don't match to at least
     *   the length of the hostname object (which will have the min
     *   possible prefix length), then there is no way the hostname
     *   falls within the range [hr].
     */
    if (strncmp (hr->prefix, hn->prefix, len_hn) != 0)
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
    len_hr = strlen (hr->prefix);
    width = hostname_suffix_width (hn);

    if ((len_hn < len_hr)
         && (width > 1)
         && (isdigit (hr->prefix [len_hr - 1]))
         && (hr->prefix [len_hn] == hn->suffix[0]) ) {
        int rc;
        /*
         *  Create new hostname object with its prefix offset by one
         */
        hostname_t h = hostname_create_with_suffix (hn->hostname, len_hn);
        /*
         *  Recursive call :-o
         */
        rc = hostrange_hn_within (hr, h);
        hostname_destroy (h);
        return rc;
    }


    /*
     *  Finally, check whether [hn], with a valid numeric suffix,
     *   falls within the range of [hr].
     */
    if (hn->num <= hr->hi && hn->num >= hr->lo) {
        int width = hostname_suffix_width (hn);
        if (!_width_equiv(hr->lo, &hr->width, hn->num, &width))
            return -1;
        return (hn->num - hr->lo);
    }

    return -1;
}


/* copy a string representation of the hostrange hr into buffer buf,
 * writing at most n chars including NUL termination
 */
static size_t
hostrange_to_string(hostrange_t hr, size_t n, char *buf, char *separator)
{
    unsigned long i;
    int truncated = 0;
    int len = 0;
    char sep = separator == NULL ? ',' : separator[0];

    if (n == 0)
        return 0;

    if (hr->singlehost)
        return snprintf(buf, n, "%s", hr->prefix);

    for (i = hr->lo; i <= hr->hi; i++) {
        size_t m = (n - len) <= n ? n - len : 0; /* check for < 0 */
        int ret = snprintf(buf + len, m, "%s%0*lu",
                   hr->prefix, hr->width, i);
        if (ret < 0 || ret >= m) {
            len = n;
            truncated = 1;
            break;
        }
        len+=ret;
        buf[len++] = sep;
    }

    if (truncated) {
        buf[n-1] = '\0';
        return -1;
    } else {
        /* back up over final separator */
        buf[--len] = '\0';
        return len;
    }
}

/* Place the string representation of the numeric part of hostrange into buf
 * writing at most n chars including NUL termination.
 */
static size_t hostrange_numstr(hostrange_t hr, size_t n, char *buf)
{
    int len = 0;

    assert(buf != NULL);

    if (hr->singlehost || n == 0)
        return 0;

    len = snprintf(buf, n, "%0*lu", hr->width, hr->lo);

    if ((len >= 0) && (len < n) && (hr->lo < hr->hi)) {
        int len2 = snprintf(buf+len, n-len, "-%0*lu", hr->width, hr->hi);
        if (len2 < 0) 
            len = -1;
        else
            len += len2;
    }

    return len;
}


/* ----[ hostlist functions ]---- */

/* Create a new hostlist object. 
 * Returns an empty hostlist, or NULL if memory allocation fails.
 */
static hostlist_t hostlist_new(void)
{
    int i;
    hostlist_t new = (hostlist_t) malloc(sizeof(*new));
    if (!new)
        goto fail1;

    assert(new->magic = HOSTLIST_MAGIC);
    mutex_init(&new->mutex);

    new->hr = (hostrange_t *) malloc(HOSTLIST_CHUNK * sizeof(hostrange_t));
    if (!new->hr)
        goto fail2;

    /* set entries in hostrange array to NULL */
    for (i = 0; i < HOSTLIST_CHUNK; i++)
        new->hr[i] = NULL;

    new->size = HOSTLIST_CHUNK;
    new->nranges = 0;
    new->nhosts = 0;
    new->ilist = NULL;
    return new;

  fail2:
    free(new);
  fail1:
    out_of_memory("hostlist_create");
}


/* Resize the internal array used to store the list of hostrange objects.
 *
 * returns 1 for a successful resize,
 *         0 if call to _realloc fails    
 *
 * It is assumed that the caller has the hostlist hl locked 
 */
static int hostlist_resize(hostlist_t hl, size_t newsize)
{
    int i;
    size_t oldsize;
    assert(hl != NULL);
    assert(hl->magic == HOSTLIST_MAGIC);
    oldsize = hl->size;
    hl->size = newsize;
    hl->hr = realloc((void *) hl->hr, hl->size*sizeof(hostrange_t));
    if (!(hl->hr)) 
        return 0;

    for (i = oldsize; i < newsize; i++)
        hl->hr[i] = NULL;

    return 1;
}

/* Resize hostlist by one HOSTLIST_CHUNK
 * Assumes that hostlist hl is locked by caller
 */
static int hostlist_expand(hostlist_t hl)
{
    if (!hostlist_resize(hl, hl->size + HOSTLIST_CHUNK))
        return 0;
    else
        return 1;
}

/* Push a hostrange object onto hostlist hl
 * Returns the number of hosts successfully pushed onto hl
 * or -1 if there was an error allocating memory
 */
static int hostlist_push_range(hostlist_t hl, hostrange_t hr)
{
    hostrange_t tail;
    int retval;

    assert(hr != NULL);
    LOCK_HOSTLIST(hl);

    tail = (hl->nranges > 0) ? hl->hr[hl->nranges-1] : hl->hr[0];

    if (hl->size == hl->nranges && !hostlist_expand(hl))
        goto error;

    if (hl->nranges > 0
        && hostrange_prefix_cmp(tail, hr) == 0
        && tail->hi == hr->lo - 1
        && hostrange_width_combine(tail, hr)) {
        tail->hi = hr->hi;
    } else {
        if ((hl->hr[hl->nranges++] = hostrange_copy(hr)) == NULL)
            goto error;
    }

    retval = hl->nhosts += hostrange_count(hr);

    UNLOCK_HOSTLIST(hl);

    return retval;

  error:
    UNLOCK_HOSTLIST(hl);
    return -1;
}



/* Same as hostlist_push_range() above, but prefix, lo, hi, and width
 * are passed as args 
 */
static int
hostlist_push_hr(hostlist_t hl, char *prefix, unsigned long lo,
         unsigned long hi, int width)
{
    hostrange_t hr = hostrange_create(prefix, lo, hi, width);
    int retval = hostlist_push_range(hl, hr);
    hostrange_destroy(hr);
    return retval;
}

/* Insert a range object hr into position n of the hostlist hl
 * Assumes that hl->mutex is already held by calling process
 */
static int hostlist_insert_range(hostlist_t hl, hostrange_t hr, int n)
{
    int i;
    hostrange_t tmp;
    hostlist_iterator_t hli;

    assert(hl != NULL);
    assert(hl->magic == HOSTLIST_MAGIC);
    assert(hr != NULL);

    if (n > hl->nranges)
        return 0;

    if (hl->size == hl->nranges && !hostlist_expand(hl))
        return 0;

    /* copy new hostrange into slot "n" in array */
    tmp = hl->hr[n];
    hl->hr[n] = hostrange_copy(hr);

    /* push remaining hostrange entries up */
    for (i = n + 1; i < hl->nranges + 1; i++) {
        hostrange_t last = hl->hr[i];
        hl->hr[i] = tmp;
        tmp = last;
    }
    hl->nranges++;

    /* adjust hostlist iterators if needed */
    for (hli = hl->ilist; hli; hli = hli->next) {
        if (hli->idx >= n)
            hli->hr = hli->hl->hr[++hli->idx];
    }

    return 1;
}

/* Delete the range at position n in the range array
 * Assumes the hostlist lock is already held.
 */
static void hostlist_delete_range(hostlist_t hl, int n)
{
    int i;
    hostrange_t old;

    assert(hl != NULL);
    assert(hl->magic == HOSTLIST_MAGIC);
    assert(n < hl->nranges && n >= 0);

    old = hl->hr[n];
    for (i = n; i < hl->nranges - 1; i++)
        hl->hr[i] = hl->hr[i + 1];
    hl->nranges--;
    hl->hr[hl->nranges] = NULL;
    hostlist_shift_iterators(hl, n, 0, 1);

    /* XXX caller responsible for adjusting nhosts */
    /* hl->nhosts -= hostrange_count(old) */

    hostrange_destroy(old);
}

#if WANT_RECKLESS_HOSTRANGE_EXPANSION

/* The reckless hostrange expansion function.
 * See comment in hostlist.h:hostlist_create() for more info on
 * the different choices for hostlist notation.
 */
hostlist_t _hostlist_create(const char *hostlist, char *sep, char *r_op)
{
    char *str, *orig;
    char *tok, *cur;
    int high, low, fmt = 0;
    char prefix[256] = "";
    int pos = 0;
    int error = 0;
    char range_op = r_op[0];/* XXX support > 1 char range ops in future? */

    hostlist_t new = hostlist_new();

    orig = str = strdup(hostlist);

    /* return an empty list if an empty string was passed in */
    if (str == NULL || strlen(str) == 0)
        goto done;

    /* Use hostlist_create_bracketed if we see "[" */
    if (strchr(str, '[') != NULL)
        return _hostlist_create_bracketed(hostlist, sep, r_op);

    while ((tok = _next_tok(sep, &str)) != NULL) {

        /* save the current string for error messages */
        cur = tok;

        high = low = 0;

        /* find end of alpha part 
         *   do this by finding last occurence of range_op in str */
        pos = strlen(tok) - 1;
        if (strstr(tok, r_op) != '\0') {
            while (pos >= 0 && (char) tok[pos] != range_op) 
                pos--;
        }

        /* now back up past any digits */
        while (pos >= 0 && isdigit((char) tok[--pos])) {;}

        /* Check for valid x-y range (x must be a digit) 
         *   Reset pos if the range is not valid         */
        if (!isdigit((char) tok[++pos]))
            pos = strlen(tok) - 1;

        /* create prefix string 
         * if prefix will be zero length, but prefix already exists
         * use the previous prefix and fmt
         */
        if ((pos > 0) || (prefix[0] == '\0')) {
            memcpy(prefix, tok, (size_t) pos * sizeof(char));
            prefix[pos] = '\0';

            /* push pointer past prefix */
            tok += pos;

            /* count number of digits for ouput fmt */
            for (fmt = 0; isdigit(tok[fmt]); ++fmt) {;}

            if (fmt == 0)
                error = 1;

        } else
            tok += pos;

        /* get lower bound */
        low = strtoul(tok, (char **) &tok, 10);

        if (*tok == range_op) {    /* now get range upper bound */
            /* push pointer past range op */
            ++tok;

            /* find length of alpha part */
            for (pos = 0; tok[pos] && !isdigit(tok[pos]); ++pos) {;}

            /* alpha part must match prefix or error
             * this could mean we've got something like "rtr1-a2"
             * so just record an error
             */
            if (pos > 0) {
                if (pos != strlen(prefix) ||
                    strncmp(prefix, tok, pos) != 0)
                    error = 1;
            }

            if (*tok != '\0')
                tok += pos;

            /* make sure we have digits to the end */
            for (pos = 0; tok[pos] && isdigit((char) tok[pos]); ++pos) {;}

            if (pos > 0) {    /* we have digits to process */
                high = strtoul(tok, (char **) &tok, 10);
            } else {    /* bad boy, no digits */
                error = 1;
            }

            if ((low > high) || (high - low > MAX_RANGE))
                error = 1;

        } else {    /* single value */
            high = 0;    /* special case, ugh. */
        }

        /* error if: 
         * 1. we are not at end of string
         * 2. upper bound equals lower bound
         */
        if (*tok != '\0' || high == low)
            error = 1;

        if (error) {    /* assume this is not a range on any error */
            hostlist_push_host(new, cur);
        } else {
            if (high < low)
                high = low;
            hostlist_push_hr(new, prefix, low, high, fmt);
        }

        error = 0;
    }

  done:
    free(orig);

    return new;
}

#else                /* !WANT_RECKLESS_HOSTRANGE_EXPANSION */

hostlist_t _hostlist_create(const char *hostlist, char *sep, char *r_op) 
{
    return _hostlist_create_bracketed(hostlist, sep, r_op);
}

#endif                /* WANT_RECKLESS_HOSTRANGE_EXPANSION */

struct _range {
    unsigned long lo, hi;
    int width;
};

/* Grab a single range from str 
 * returns 1 if str contained a valid number or range,
 *         0 if conversion of str to a range failed.
 */
static int _parse_single_range(const char *str, struct _range *range)
{
    char *p, *q;
    char *orig = strdup(str);
    if (!orig) 
        seterrno_ret(ENOMEM, 0);

    if ((p = strchr(str, '-'))) {
        *p++ = '\0';
        if (*p == '-')     /* do NOT allow negative numbers */
            goto error;
    }
    range->lo = strtoul(str, &q, 10);
    if (q == str) 
        goto error;

    range->hi = (p && *p) ? strtoul(p, &q, 10) : range->lo;

    if (q == p || *q != '\0') 
        goto error;

    if (range->lo > range->hi) 
        goto error;

    if (range->hi - range->lo + 1 > MAX_RANGE ) {
        _error(__FILE__, __LINE__, "Too many hosts in range `%s'", orig);
        free(orig);
        seterrno_ret(ERANGE, 0);
    }

    free(orig);
    range->width = strlen(str);
    return 1;

  error:
    _error(__FILE__, __LINE__, "Invalid range: `%s'", orig);
    free(orig);
    seterrno_ret(EINVAL, 0);
}


/*
 * Convert 'str' containing comma separated digits and ranges into an array
 *  of struct _range types (max 'len' elements).  
 *
 * Return number of ranges created, or -1 on error.
 */
static int _parse_range_list(char *str, struct _range *ranges, int len)
{
    char *p;
    int count = 0;

    while (str) {
        if (count == len)
            return -1;
        if ((p = strchr(str, ',')))
            *p++ = '\0';
        if (!_parse_single_range(str, &ranges[count++])) 
            return -1;  
        str = p;
    }
    return count;
}

static void
_push_range_list(hostlist_t hl, char *pfx, struct _range *rng,
             int n)
{
    int i;
    for (i = 0; i < n; i++) {
        hostlist_push_hr(hl, pfx, rng->lo, rng->hi, rng->width);
        rng++;
    }
}

static void
_push_range_list_with_suffix(hostlist_t hl, char *pfx, char *sfx, 
                             struct _range *rng, int n)
{
    int i;
    unsigned long j;
    for (i = 0; i < n; i++) {
        for (j = rng->lo; j <= rng->hi; j++) {
            char host[4096];
            hostrange_t hr;
            snprintf (host, 4096, "%s%0*lu%s", pfx, rng->width, j, sfx);
            hr = hostrange_create_single (host);
            hostlist_push_range (hl, hr);
            /*
             * hr is copied in hostlist_push_range. Need to free here.
             */
            hostrange_destroy (hr);
        }
        rng++;
    }
}

/*
 * Create a hostlist from a string with brackets '[' ']' to aid 
 * detection of ranges and compressed lists
 */
static hostlist_t 
_hostlist_create_bracketed(const char *hostlist, char *sep, char *r_op)
{
    hostlist_t new = hostlist_new();
    struct _range ranges[MAX_RANGES];
    int nr, err;
    char *p, *tok, *str, *orig;
    char cur_tok[1024];

    if (hostlist == NULL)
        return new;

    if (!(orig = str = strdup(hostlist))) {
        hostlist_destroy(new);
        return NULL;
    }

    while ((tok = _next_tok(sep, &str)) != NULL) {
        strncpy(cur_tok, tok, sizeof(cur_tok) - 1);
        cur_tok[sizeof(cur_tok) - 1] = '\0'; /* make sure it is NUL-terminated */

        if ((p = strchr(tok, '[')) != NULL) {
            char *q, *prefix = tok;
            *p++ = '\0';

            if ((q = strchr(p, ']'))) {
                *q = '\0';
                nr = _parse_range_list(p, ranges, MAX_RANGES);
                if (nr < 0) 
                    goto error;

                if (*(++q) != '\0')
                    _push_range_list_with_suffix (new, prefix, q, ranges, nr);
                else
                    _push_range_list(new, prefix, ranges, nr);

                
            } else
                hostlist_push_host(new, cur_tok);

        } else
            hostlist_push_host(new, cur_tok);
    }

    free(orig);
    return new;

  error:
    err = errno;
    hostlist_destroy(new);
    free(orig);
    seterrno_ret(err, NULL);
}



hostlist_t hostlist_create(const char *str)
{
    return _hostlist_create(str, "\t, ", "-");
}


hostlist_t hostlist_copy(const hostlist_t hl)
{
    int i;
    hostlist_t new;

    if (hl == NULL)
        return NULL;

    LOCK_HOSTLIST(hl);
    if (!(new = hostlist_new()))
        goto done;

    new->nranges = hl->nranges;
    new->nhosts = hl->nhosts;
    if (new->nranges > new->size)
        hostlist_resize(new, new->nranges);

    for (i = 0; i < hl->nranges; i++)
        new->hr[i] = hostrange_copy(hl->hr[i]);

  done:
    UNLOCK_HOSTLIST(hl);
    return new;
}


void hostlist_destroy(hostlist_t hl)
{
    int i;
    if (hl == NULL)
        return;
    LOCK_HOSTLIST(hl);
    while (hl->ilist) {
        mutex_unlock(&hl->mutex);
        hostlist_iterator_destroy(hl->ilist);
        mutex_lock(&hl->mutex);
    }
    for (i = 0; i < hl->nranges; i++)
        hostrange_destroy(hl->hr[i]);
    free(hl->hr);
    assert(hl->magic = 0x1);
    UNLOCK_HOSTLIST(hl);
    mutex_destroy(&hl->mutex);
    free(hl);
}


int hostlist_push(hostlist_t hl, const char *hosts)
{
    hostlist_t new;
    int retval;
    if (hosts == NULL)
        return 0;
    new = hostlist_create(hosts);
    if (!new)
        return 0;
    mutex_lock(&new->mutex);
    retval = new->nhosts;
    mutex_unlock(&new->mutex);
    hostlist_push_list(hl, new);
    hostlist_destroy(new);
    return retval;
}

int hostlist_push_host(hostlist_t hl, const char *str)
{
    hostrange_t hr;
    hostname_t hn;

    if (str == NULL)
        return 0;

    hn = hostname_create(str);

    if (hostname_suffix_is_valid(hn)) {
        hr = hostrange_create(hn->prefix, hn->num, hn->num,
                      hostname_suffix_width(hn));
    } else
        hr = hostrange_create_single(str);

    hostlist_push_range(hl, hr);

    hostrange_destroy(hr);
    hostname_destroy(hn);

    return 1;
}

int hostlist_push_list(hostlist_t h1, hostlist_t h2)
{
    int i, n = 0;

    if (h2 == NULL)
        return 0;

    LOCK_HOSTLIST(h2);

    for (i = 0; i < h2->nranges; i++)
        n += hostlist_push_range(h1, h2->hr[i]);

    UNLOCK_HOSTLIST(h2);

    return n;
}


char *hostlist_pop(hostlist_t hl)
{
    char *host = NULL;

    LOCK_HOSTLIST(hl);
    if (hl->nhosts > 0) {
        hostrange_t hr = hl->hr[hl->nranges - 1];
        host = hostrange_pop(hr);
        hl->nhosts--;
        if (hostrange_empty(hr)) {
            hostrange_destroy(hl->hr[--hl->nranges]);
            hl->hr[hl->nranges] = NULL;
        }
    }
    UNLOCK_HOSTLIST(hl);
    return host;
}

/* find all iterators affected by a shift (or deletion) at 
 * hl->hr[idx], depth, with the deletion of n ranges */
static void
hostlist_shift_iterators(hostlist_t hl, int idx, int depth, int n)
{
    hostlist_iterator_t i;
    for (i = hl->ilist; i; i = i->next) {
        if (n == 0) {
            if (i->idx == idx && i->depth >= depth)
                i->depth = i->depth > -1 ? i->depth - 1 : -1;
        } else {
            if (i->idx >= idx) {
                if ((i->idx -= n) >= 0)
                    i->hr = i->hl->hr[i->idx];
                else
                    hostlist_iterator_reset(i);
            }
        }
    }
}

char *hostlist_shift(hostlist_t hl)
{
    char *host = NULL;

    LOCK_HOSTLIST(hl);

    if (hl->nhosts > 0) {
        hostrange_t hr = hl->hr[0];

        host = hostrange_shift(hr);
        hl->nhosts--;

        if (hostrange_empty(hr)) {
            hostlist_delete_range(hl, 0);
            /* hl->nranges--; */
        } else
            hostlist_shift_iterators(hl, 0, 0, 0);
    }

    UNLOCK_HOSTLIST(hl);

    return host;
}


char *hostlist_pop_range(hostlist_t hl)
{
    int i;
    char buf[MAXHOSTRANGELEN + 1];
    hostlist_t hltmp;
    hostrange_t tail;

    LOCK_HOSTLIST(hl);
    if (hl->nranges < 1 || !(hltmp = hostlist_new())) {
        UNLOCK_HOSTLIST(hl);
        return NULL;
    }

    i = hl->nranges - 2;
    tail = hl->hr[hl->nranges - 1];
    while (i >= 0 && hostrange_within_range(tail, hl->hr[i]))
        i--;

    for (i++; i < hl->nranges; i++) {
        hostlist_push_range(hltmp, hl->hr[i]);
        hostrange_destroy(hl->hr[i]);
        hl->hr[i] = NULL;
    }
    hl->nhosts -= hltmp->nhosts;
    hl->nranges -= hltmp->nranges;

    UNLOCK_HOSTLIST(hl);
    hostlist_ranged_string(hltmp, MAXHOSTRANGELEN, buf);
    hostlist_destroy(hltmp);
    return strdup(buf);
}


char *hostlist_shift_range(hostlist_t hl)
{
    int i;
    char buf[1024];
    hostlist_t hltmp = hostlist_new();
    if (!hltmp)
        return NULL;

    LOCK_HOSTLIST(hl);

    if (hl->nranges == 0) {
        hostlist_destroy(hltmp);
        UNLOCK_HOSTLIST(hl);
        return NULL;
    }

    i = 0;
    do {
        hostlist_push_range(hltmp, hl->hr[i]);
        hostrange_destroy(hl->hr[i]);
    } while ( (++i < hl->nranges) 
            && hostrange_within_range(hltmp->hr[0], hl->hr[i]) );

    hostlist_shift_iterators(hl, i, 0, hltmp->nranges);

    /* shift rest of ranges back in hl */
    for (; i < hl->nranges; i++) {
        hl->hr[i - hltmp->nranges] = hl->hr[i];
        hl->hr[i] = NULL;
    }
    hl->nhosts -= hltmp->nhosts;
    hl->nranges -= hltmp->nranges;

    UNLOCK_HOSTLIST(hl);

    hostlist_ranged_string(hltmp, 1024, buf);
    hostlist_destroy(hltmp);

    return strdup(buf);
}

/* XXX: Note: efficiency improvements needed */
int hostlist_delete(hostlist_t hl, const char *hosts)
{
    int n = 0;
    char *hostname = NULL;
    hostlist_t hltmp;

    if (!(hltmp = hostlist_create(hosts)))
        seterrno_ret(EINVAL, 0);

    while ((hostname = hostlist_pop(hltmp)) != NULL) {
        n += hostlist_delete_host(hl, hostname);
        free(hostname);
    }
    hostlist_destroy(hltmp);

    return n;
}


/* XXX watch out! poor implementation follows! (fix it at some point) */
int hostlist_delete_host(hostlist_t hl, const char *hostname)
{
    int n = hostlist_find(hl, hostname);
    if (n >= 0)
        hostlist_delete_nth(hl, n);
    return n >= 0 ? 1 : 0;
}


static char *
_hostrange_string(hostrange_t hr, int depth)
{
    char buf[MAXHOSTNAMELEN + 16];
    int  len = snprintf(buf, MAXHOSTNAMELEN + 15, "%s", hr->prefix);

    if (!hr->singlehost)
        snprintf(buf+len, MAXHOSTNAMELEN+15 - len, "%0*lu", 
                 hr->width, hr->lo + depth);
    return strdup(buf);
}

char * hostlist_nth(hostlist_t hl, int n)
{
    char *host = NULL;
    int   i, count;

    LOCK_HOSTLIST(hl);
    count = 0;
    for (i = 0; i < hl->nranges; i++) {
        int num_in_range = hostrange_count(hl->hr[i]);

        if (n <= (num_in_range - 1 + count)) {
            host = _hostrange_string(hl->hr[i], n - count);
            break;
        } else
            count += num_in_range;
    }

    UNLOCK_HOSTLIST(hl);

    return host;
}


int hostlist_delete_nth(hostlist_t hl, int n)
{
    int i, count;

    LOCK_HOSTLIST(hl);
    assert(n >= 0 && n <= hl->nhosts);

    count = 0;

    for (i = 0; i < hl->nranges; i++) {
        int num_in_range = hostrange_count(hl->hr[i]);
        hostrange_t hr = hl->hr[i];

        if (n <= (num_in_range - 1 + count)) {
            unsigned long num = hr->lo + n - count;
            hostrange_t new;

            if (hr->singlehost) { /* this wasn't a range */
                hostlist_delete_range(hl, i);
            } else if ((new = hostrange_delete_host(hr, num))) {
                hostlist_insert_range(hl, new, i + 1);
                hostrange_destroy(new);
            } else if (hostrange_empty(hr))
                hostlist_delete_range(hl, i);

            goto done;
        } else
            count += num_in_range;

    }

  done:
    hl->nhosts--;
    UNLOCK_HOSTLIST(hl);
    return 1;
}

int hostlist_count(hostlist_t hl)
{
    int retval;
    LOCK_HOSTLIST(hl);
    retval = hl->nhosts;
    UNLOCK_HOSTLIST(hl);
    return retval;
}

int hostlist_find(hostlist_t hl, const char *hostname)
{
    int i, count, ret = -1;
    hostname_t hn;

    if (!hostname)
        return -1;

    hn = hostname_create(hostname);

    LOCK_HOSTLIST(hl);

    for (i = 0, count = 0; i < hl->nranges; i++) {
        int offset = hostrange_hn_within(hl->hr[i], hn);
        if (offset >= 0) {
            ret = count + offset;
            break;
        }
        else
            count += hostrange_count(hl->hr[i]);
    }

    UNLOCK_HOSTLIST(hl);
    hostname_destroy(hn);
    return ret;
}

/* hostrange compare with void * arguments to allow use with 
 * libc qsort()
 */
int _cmp(const void *hr1, const void *hr2)
{
    hostrange_t *h1 = (hostrange_t *) hr1;
    hostrange_t *h2 = (hostrange_t *) hr2;
    return hostrange_cmp((hostrange_t) * h1, (hostrange_t) * h2);
}


void hostlist_sort(hostlist_t hl)
{
    hostlist_iterator_t i;
    LOCK_HOSTLIST(hl);

    if (hl->nranges <= 1) {
        UNLOCK_HOSTLIST(hl);
        return;
    }

    qsort(hl->hr, hl->nranges, sizeof(hostrange_t), &_cmp);

    /* reset all iterators */
    for (i = hl->ilist; i; i = i->next)
        hostlist_iterator_reset(i);

    UNLOCK_HOSTLIST(hl);

    hostlist_coalesce(hl);

}


/* search through hostlist for ranges that can be collapsed 
 * does =not= delete any hosts
 */
static void hostlist_collapse(hostlist_t hl)
{
    int i;

    LOCK_HOSTLIST(hl);
    for (i = hl->nranges - 1; i > 0; i--) {
        hostrange_t hprev = hl->hr[i - 1];
        hostrange_t hnext = hl->hr[i];

        if (hostrange_prefix_cmp(hprev, hnext) == 0 &&
            hprev->hi == hnext->lo - 1 &&
            hostrange_width_combine(hprev, hnext)) {
            hprev->hi = hnext->hi;
            hostlist_delete_range(hl, i);
        }
    }
    UNLOCK_HOSTLIST(hl);
}

/* search through hostlist (hl) for intersecting ranges 
 * split up duplicates and coalesce ranges where possible
 */
static void hostlist_coalesce(hostlist_t hl)
{
    int i, j;
    hostrange_t new;

    LOCK_HOSTLIST(hl);

    for (i = hl->nranges - 1; i > 0; i--) {

        new = hostrange_intersect(hl->hr[i - 1], hl->hr[i]);

        if (new) {
            hostrange_t hprev = hl->hr[i - 1];
            hostrange_t hnext = hl->hr[i];
            j = i;

            if (new->hi < hprev->hi)
                hnext->hi = hprev->hi;

            hprev->hi = new->lo;
            hnext->lo = new->hi;

            if (hostrange_empty(hprev))
                hostlist_delete_range(hl, i);

            while (new->lo <= new->hi) {
                hostrange_t hr = hostrange_create( new->prefix,
                                                   new->lo, new->lo,
                                                   new->width );

                if (new->lo > hprev->hi)
                    hostlist_insert_range(hl, hr, j++);

                if (new->lo < hnext->lo)
                    hostlist_insert_range(hl, hr, j++);

                hostrange_destroy(hr);

                new->lo++;
            }
            i = hl->nranges;
            hostrange_destroy(new);
        }
    }
    UNLOCK_HOSTLIST(hl);

    hostlist_collapse(hl);

}

/* attempt to join ranges at loc and loc-1 in a hostlist  */
/* delete duplicates, return the number of hosts deleted  */
/* assumes that the hostlist hl has been locked by caller */
/* returns -1 if no range join occurred */
static int _attempt_range_join(hostlist_t hl, int loc)
{
    int ndup;
    assert(hl != NULL);
    assert(hl->magic == HOSTLIST_MAGIC);
    assert(loc > 0);
    assert(loc < hl->nranges);
    ndup = hostrange_join(hl->hr[loc - 1], hl->hr[loc]);
    if (ndup >= 0) {
        hostlist_delete_range(hl, loc);
        hl->nhosts -= ndup;
    }
    return ndup;
}

void hostlist_uniq(hostlist_t hl)
{
    int i = 1;
    hostlist_iterator_t hli;
    LOCK_HOSTLIST(hl);
    if (hl->nranges <= 1) {
        UNLOCK_HOSTLIST(hl);
        return;
    }
    qsort(hl->hr, hl->nranges, sizeof(hostrange_t), &_cmp);

    while (i < hl->nranges) {
        if (_attempt_range_join(hl, i) < 0) /* No range join occurred */
            i++;
    }

    /* reset all iterators */
    for (hli = hl->ilist; hli; hli = hli->next)
        hostlist_iterator_reset(hli);

    UNLOCK_HOSTLIST(hl);
}


ssize_t hostlist_deranged_string(hostlist_t hl, size_t n, char *buf)
{
    int i;
    int len = 0;
    int truncated = 0;

    LOCK_HOSTLIST(hl);
    for (i = 0; i < hl->nranges; i++) {
        size_t m = (n - len) <= n ? n - len : 0;
        int ret = hostrange_to_string(hl->hr[i], m, buf + len, ",");
        if (ret < 0 || ret > m) {
            len = n;
            truncated = 1;
            break;
        }
        len+=ret;
        buf[len++] = ',';
    }
    UNLOCK_HOSTLIST(hl);

    buf[len > 0 ? --len : 0] = '\0';
    if (len == n)
        truncated = 1;

    return truncated ? -1 : len;
}

/* return true if a bracket is needed for the range at i in hostlist hl */
static int _is_bracket_needed(hostlist_t hl, int i)
{
    hostrange_t h1 = hl->hr[i];
    hostrange_t h2 = i < hl->nranges - 1 ? hl->hr[i + 1] : NULL;
    return hostrange_count(h1) > 1 || hostrange_within_range(h1, h2);
}

/* write the next bracketed hostlist, i.e. prefix[n-m,k,...]
 * into buf, writing at most n chars including the terminating '\0'
 *
 * leaves start pointing to one past last range object in bracketed list,
 * and returns the number of bytes written into buf.
 *
 * Assumes hostlist is locked.
 */
static int
_get_bracketed_list(hostlist_t hl, int *start, const size_t n, char *buf)
{
    hostrange_t *hr = hl->hr;
    int i = *start;
    int m, len = 0;
    int bracket_needed = _is_bracket_needed(hl, i);

    len = snprintf(buf, n, "%s", hr[i]->prefix);

    if ((len < 0) || (len > n))
        return n; /* truncated, buffer filled */

    if (bracket_needed && len < n && len >= 0)
        buf[len++] = '[';

    do {
        m = (n - len) <= n ? n - len : 0;
        len += hostrange_numstr(hr[i], m, buf + len);
        if (len >= n)
            break;
        if (bracket_needed) /* Only need commas inside brackets */
            buf[len++] = ',';
    } while (++i < hl->nranges && hostrange_within_range(hr[i], hr[i-1]));

    if (bracket_needed && len < n && len > 0) {

        /* Add trailing bracket (change trailing "," from above to "]" */
        buf[len - 1] = ']';

        /* NUL terminate for safety, but do not add terminator to len */
        buf[len]   = '\0';

    } else if (len >= n) {
        if (n > 0)
            buf[n-1] = '\0';

    } else {
        /* If len is > 0, NUL terminate (but do not add to len) */
        buf[len > 0 ? len : 0] = '\0';
    }

    *start = i;
    return len;
}

ssize_t hostlist_ranged_string(hostlist_t hl, size_t n, char *buf)
{
    int i = 0;
    int len = 0;
    int truncated = 0;

    LOCK_HOSTLIST(hl);
    while (i < hl->nranges && len < n) {
        len += _get_bracketed_list(hl, &i, n - len, buf + len);
        if ((len > 0) && (len < n) && (i < hl->nranges))
            buf[len++] = ',';
    }
    UNLOCK_HOSTLIST(hl);

    /* NUL terminate */
    if (len >= n) {
        truncated = 1;
        if (n > 0)
            buf[n-1] = '\0';
    } else
        buf[len > 0 ? len : 0] = '\0';

    return truncated ? -1 : len;
}

/* ----[ hostlist iterator functions ]---- */

static hostlist_iterator_t hostlist_iterator_new(void)
{
    hostlist_iterator_t i = (hostlist_iterator_t) malloc(sizeof(*i));
    if (!i) 
        return NULL;
    i->hl = NULL;
    i->hr = NULL;
    i->idx = 0;
    i->depth = -1;
    i->next = i;
    assert(i->magic = HOSTLIST_MAGIC);
    return i;
}

hostlist_iterator_t hostlist_iterator_create(hostlist_t hl)
{
    hostlist_iterator_t i;

    if (!(i = hostlist_iterator_new()))
        out_of_memory("hostlist_iterator_create");

    LOCK_HOSTLIST(hl);
    i->hl = hl;
    i->hr = hl->hr[0];
    i->next = hl->ilist;
    hl->ilist = i;
    UNLOCK_HOSTLIST(hl);
    return i;
}

hostlist_iterator_t hostset_iterator_create(hostset_t set)
{
    return hostlist_iterator_create(set->hl);
}

void hostlist_iterator_reset(hostlist_iterator_t i)
{
    assert(i != NULL);
    assert(i->magic == HOSTLIST_MAGIC);
    i->idx = 0;
    i->hr = i->hl->hr[0];
    i->depth = -1;
    return;
}

void hostlist_iterator_destroy(hostlist_iterator_t i)
{
    hostlist_iterator_t *pi;
    if (i == NULL)
        return;
    assert(i != NULL);
    assert(i->magic == HOSTLIST_MAGIC);
    LOCK_HOSTLIST(i->hl);
    for (pi = &i->hl->ilist; *pi; pi = &(*pi)->next) {
        assert((*pi)->magic == HOSTLIST_MAGIC);
        if (*pi == i) {
            *pi = (*pi)->next;
            break;
        }
    }
    UNLOCK_HOSTLIST(i->hl);
    assert(i->magic = 0x1);
    free(i);
}

static void _iterator_advance(hostlist_iterator_t i)
{
    assert(i != NULL);
    assert(i->magic == HOSTLIST_MAGIC);
    if (i->idx > i->hl->nranges - 1)
        return;
    if (++(i->depth) > (i->hr->hi - i->hr->lo)) {
        i->depth = 0;
        i->hr = i->hl->hr[++i->idx];
    }
}

/* advance iterator to end of current range (meaning within "[" "]")
 * i.e. advance iterator past all range objects that could be represented
 * in on bracketed hostlist.
 */
static void _iterator_advance_range(hostlist_iterator_t i)
{
    int nr, j;
    hostrange_t *hr;
    assert(i != NULL);
    assert(i->magic == HOSTLIST_MAGIC);

    nr = i->hl->nranges;
    hr = i->hl->hr;
    j = i->idx;
    if (++i->depth > 0) {
        while (++j < nr && hostrange_within_range(i->hr, hr[j])) {;}
        i->idx = j;
        i->hr = i->hl->hr[i->idx];
        i->depth = 0;
    }
}

char *hostlist_next(hostlist_iterator_t i)
{
    char *buf = NULL;
    char suffix[16];
    int len = 0;
    assert(i != NULL);
    assert(i->magic == HOSTLIST_MAGIC);
    LOCK_HOSTLIST(i->hl);
    _iterator_advance(i);

    if (i->idx > i->hl->nranges - 1) {
        UNLOCK_HOSTLIST(i->hl);
        return NULL;
    }

    suffix[0] = '\0';

    if (!i->hr->singlehost)
        snprintf (suffix, 15, "%0*lu", i->hr->width, i->hr->lo + i->depth);

    len = strlen (i->hr->prefix) + strlen (suffix) + 1;
    if (!(buf = malloc (len)))
        out_of_memory("hostlist_next");
    
    buf[0] = '\0';
    strcat (buf, i->hr->prefix);
    strcat (buf, suffix);

    UNLOCK_HOSTLIST(i->hl);
    return (buf);
}

char *hostlist_next_range(hostlist_iterator_t i)
{
    char buf[MAXHOSTRANGELEN + 1];
    int j;

    assert(i != NULL);
    assert(i->magic == HOSTLIST_MAGIC);
    LOCK_HOSTLIST(i->hl);

    _iterator_advance_range(i);

    if (i->idx > i->hl->nranges - 1) {
        UNLOCK_HOSTLIST(i->hl);
        return NULL;
    }

    j = i->idx;
    _get_bracketed_list(i->hl, &j, MAXHOSTRANGELEN, buf);

    UNLOCK_HOSTLIST(i->hl);

    return strdup(buf);
}

int hostlist_remove(hostlist_iterator_t i)
{
    hostrange_t new;
    assert(i != NULL);
    assert(i->magic == HOSTLIST_MAGIC);
    LOCK_HOSTLIST(i->hl);
    new = hostrange_delete_host(i->hr, i->hr->lo + i->depth);
    if (new) {
        hostlist_insert_range(i->hl, new, i->idx + 1);
        hostrange_destroy(new);
        i->hr = i->hl->hr[++i->idx];
        i->depth = -1;
    } else if (hostrange_empty(i->hr)) {
        hostlist_delete_range(i->hl, i->idx);
    } else
        i->depth--;

    i->hl->nhosts--;
    UNLOCK_HOSTLIST(i->hl);

    return 1;
}

/* ----[ hostset functions ]---- */

hostset_t hostset_create(const char *hostlist)
{
    hostset_t new;

    if (!(new = (hostset_t) malloc(sizeof(*new))))
        goto error1;

    if (!(new->hl = hostlist_create(hostlist)))
        goto error2;

    hostlist_uniq(new->hl);
    return new;

  error2:
    free(new);
  error1:
    return NULL;
}

hostset_t hostset_copy(const hostset_t set)
{
    hostset_t new;
    if (!(new = (hostset_t) malloc(sizeof(*new))))
        goto error1;

    if (!(new->hl = hostlist_copy(set->hl)))
        goto error2;

    return new;
  error2:
    free(new);
  error1:
    return NULL;
}

void hostset_destroy(hostset_t set)
{
    if (set == NULL)
        return;
    hostlist_destroy(set->hl);
    free(set);
}

/* inserts a single range object into a hostset 
 * Assumes that the set->hl lock is already held
 * Updates hl->nhosts 
 */
static int hostset_insert_range(hostset_t set, hostrange_t hr)
{
    int i = 0;
    int inserted = 0;
    int nhosts = 0;
    int ndups = 0;
    hostlist_t hl;

    hl = set->hl;

    if (hl->size == hl->nranges && !hostlist_expand(hl))
        return 0;

    nhosts = hostrange_count(hr);

    for (i = 0; i < hl->nranges; i++) {
        if (hostrange_cmp(hr, hl->hr[i]) <= 0) {

            if ((ndups = hostrange_join(hr, hl->hr[i])) >= 0) 
                hostlist_delete_range(hl, i);
            else if (ndups < 0)
                ndups = 0;

            hostlist_insert_range(hl, hr, i);

            /* now attempt to join hr[i] and hr[i-1] */
            if (i > 0) {
                int m;
                if ((m = _attempt_range_join(hl, i)) > 0)
                    ndups += m;
            }
            hl->nhosts += nhosts - ndups;
            inserted = 1;
            break;
        }
    }

    if (inserted == 0) {
        hl->hr[hl->nranges++] = hostrange_copy(hr);
        hl->nhosts += nhosts;
        if (hl->nranges > 1) {
            if ((ndups = _attempt_range_join(hl, hl->nranges - 1)) <= 0)
                ndups = 0;
        }
    } 

    /* 
     *  Return the number of unique hosts inserted 
     */
    return nhosts - ndups;
}

int hostset_insert(hostset_t set, const char *hosts)
{
    int i, n = 0;
    hostlist_t hl = hostlist_create(hosts);
    if (!hl)
        return 0;

    hostlist_uniq(hl);
    LOCK_HOSTLIST(set->hl);
    for (i = 0; i < hl->nranges; i++) 
        n += hostset_insert_range(set, hl->hr[i]);
    UNLOCK_HOSTLIST(set->hl);
    hostlist_destroy(hl);
    return n;
}


/* linear search through N ranges for hostname "host"
 * */
static int hostset_find_host(hostset_t set, const char *host)
{
    int i;
    int retval = 0;
    hostname_t hn;
    LOCK_HOSTLIST(set->hl);
    hn = hostname_create(host);
    for (i = 0; i < set->hl->nranges; i++) {
        if (hostrange_hn_within(set->hl->hr[i], hn) >= 0) {
            retval = 1;
            goto done;
        }
    }
  done:
    UNLOCK_HOSTLIST(set->hl);
    hostname_destroy(hn);
    return retval;
}

int hostset_within(hostset_t set, const char *hosts)
{
    int nhosts, nfound;
    hostlist_t hl;
    char *hostname;

    assert(set->hl->magic == HOSTLIST_MAGIC);

    if (!(hl = hostlist_create(hosts)))
        return (0);

    nhosts = hostlist_count(hl);
    nfound = 0;

    while ((hostname = hostlist_pop(hl)) != NULL) {
        nfound += hostset_find_host(set, hostname);
        free(hostname);
    }

    hostlist_destroy(hl);

    return (nhosts == nfound);
}

int hostset_delete(hostset_t set, const char *hosts)
{
    return hostlist_delete(set->hl, hosts);
}

int hostset_delete_host(hostset_t set, const char *hostname)
{
    return hostlist_delete_host(set->hl, hostname);
}

char *hostset_shift(hostset_t set)
{
    return hostlist_shift(set->hl);
}

char *hostset_pop(hostset_t set)
{
    return hostlist_pop(set->hl);
}

char *hostset_shift_range(hostset_t set)
{
    return hostlist_shift_range(set->hl);
}

char *hostset_pop_range(hostset_t set)
{
    return hostlist_pop_range(set->hl);
}

int hostset_count(hostset_t set)
{
    return hostlist_count(set->hl);
}

ssize_t hostset_ranged_string(hostset_t set, size_t n, char *buf)
{
    return hostlist_ranged_string(set->hl, n, buf);
}

ssize_t hostset_deranged_string(hostset_t set, size_t n, char *buf)
{
    return hostlist_deranged_string(set->hl, n, buf);
}

#if TEST_MAIN 

int hostlist_nranges(hostlist_t hl)
{
    return hl->nranges;
}

int hostset_nranges(hostset_t set)
{
    return set->hl->nranges;
}

/* test iterator functionality on the list of hosts represented
 * by list
 */
int iterator_test(char *list)
{
    int j;
    char buf[1024];
    hostlist_t hl = hostlist_create(list);
    hostset_t set = hostset_create(list);

    hostlist_iterator_t i = hostlist_iterator_create(hl);
    hostlist_iterator_t seti = hostset_iterator_create(set);
    hostlist_iterator_t i2 = hostlist_iterator_create(hl);
    char *host;


    hostlist_ranged_string(hl, 1024, buf);
    printf("iterator_test: hl = `%s' passed in `%s'\n", buf, list);
    host = hostlist_next(i);
    printf("first host in list hl = `%s'\n", host);
    free(host);

    /* forge ahead three hosts with i2 */
    for (j = 0; j < 4; j++) {
        host = hostlist_next(i2);
        free(host);
    }

    host = hostlist_shift(hl);
    printf("result of shift(hl)   = `%s'\n", host);
    free(host);
    host = hostlist_next(i);
    printf("next host in list hl  = `%s'\n", host);
    free(host);
    host = hostlist_next(i2);
    printf("next host for i2      = `%s'\n", host);
    free(host);

    hostlist_iterator_destroy(i);

    hostlist_destroy(hl);
    hostset_destroy(set);
    return 1;
}

int main(int ac, char **av)
{
    char buf[1024000];
    int i;
    char *str;

    hostlist_t hl1, hl2, hl3;
    hostset_t set, set1;
    hostlist_iterator_t iter, iter2;

    if (!(hl1 = hostlist_create(ac > 1 ? av[1] : NULL)))
        perror("hostlist_create");
    if (!(set = hostset_create(ac > 1 ? av[1] : NULL)))
        perror("hostlist_create");

    hl3 = hostlist_create("f[0-5]");
    hostlist_delete(hl3, "f[1-3]");
    hostlist_ranged_string(hl3, 102400, buf);
    printf("after delete = `%s'\n", buf);
    hostlist_destroy(hl3);

    for (i = 2; i < ac; i++) {
        hostlist_push(hl1, av[i]);
        hostset_insert(set, av[i]);
    }

    hostlist_ranged_string(hl1, 102400, buf);
    printf("ranged   = `%s'\n", buf);

    iterator_test(buf);

    hostlist_deranged_string(hl1, 10240, buf);
    printf("deranged = `%s'\n", buf);

    hostset_ranged_string(set, 1024, buf);
    printf("hostset  = `%s'\n", buf);

    hostlist_sort(hl1);
    hostlist_ranged_string(hl1, 1024, buf);
    printf("sorted   = `%s'\n", buf);

    hostlist_uniq(hl1);
    hostlist_ranged_string(hl1, 1024, buf);
    printf("uniqed   = `%s'\n", buf);

    hl2 = hostlist_copy(hl1);
    printf("pop_range: ");
    while ((str = hostlist_pop_range(hl2))) {
        printf("`%s' ", str);
        free(str);
    }
    hostlist_destroy(hl2);
    printf("\n");

    hl2 = hostlist_copy(hl1);
    printf("shift_range: ");
    while ((str = hostlist_shift_range(hl2))) {
        printf("`%s' ", str);
        free(str);
    }
    hostlist_destroy(hl2);
    printf("\n");

    iter = hostset_iterator_create(set);
    iter2 = hostset_iterator_create(set);
    hostlist_iterator_destroy(iter2);

    printf("next: ");
    while ((str = hostlist_next(iter))) {
        printf("`%s' ", str);
        free(str);
    }
    printf("\n");

    hostlist_iterator_reset(iter);
    printf("next_range: ");
    while ((str = hostlist_next_range(iter))) {
        printf("`%s' ", str);
        free(str);
    }
    printf("\n");

    printf("nranges = %d\n", hostset_nranges(set));

    hostset_ranged_string(set, 1024, buf);
    printf("set = %s\n", buf);

    hostset_destroy(set);
    hostlist_destroy(hl1);
    return 0;
}

#endif                /* TEST_MAIN */

/* 
 * vi: tabstop=4 shiftwidth=4 expandtab 
 */
