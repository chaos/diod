/*****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see <http://code.google.com/p/diod/>.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/

/* opt.c - handle comma-separated key[=val] lists */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _GNU_SOURCE     /* asprintf */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#include "diod_log.h"

#include "opt.h"
#include "list.h"

#define OPT_MAGIC 0x54545344

struct opt_struct {
    int     magic;
    List    list;
};

void
opt_destroy (Opt o)
{
    assert (o->magic == OPT_MAGIC);
    if (o->list)
        list_destroy (o->list);
    free (o);
}

Opt
opt_create (void)
{
    Opt o = malloc (sizeof (*o));

    if (!o)
        msg_exit ("out of memory");
    o->magic = OPT_MAGIC;
    o->list = list_create ((ListDelF)free);
    if (!o->list)
        msg_exit ("out of memory");

    return o;
}

char *
opt_string (Opt o)
{
    ListIterator itr;
    char *item, *s;
    int strsize = 1;
    int n;

    assert (o->magic == OPT_MAGIC);
    if (!(itr = list_iterator_create (o->list)))
        msg_exit ("out of memory");
    while ((item = list_next (itr)))
        strsize += strlen (item) + 1;
    list_iterator_reset (itr);
    if (!(s = malloc (strsize + list_count (o->list) - 1)))
        msg_exit ("out of memory");
    n = 0;
    while ((item = list_next (itr))) {
        snprintf (s + n, strsize - n, "%s%s", n > 0 ? "," : "", item);
        n = strlen (s);
    }
    list_iterator_destroy (itr);

    return s;
}

static int
_match_keyval (char *item, char *key)
{
    return (strcasecmp(item, key) == 0);
}


static int
_match_key (char *item, char *key)
{
    char *p = strchr (item, '=');
    int n = p ? p - item : strlen (item);
    char *q = strchr (key, '=');
    int m = q ? q - key : strlen (key);

    return (m == n && strncasecmp(item, key, n) == 0);
}

void
opt_add (Opt o, const char *fmt, ...)
{
    va_list ap;
    char *item;
    int error;

    assert (o->magic == OPT_MAGIC);
    va_start (ap, fmt);
    error = vasprintf (&item, fmt, ap);
    va_end (ap);
    if (error < 0)
        msg_exit ("out of memory");
    if (list_find_first (o->list, (ListFindF)_match_key, item))
        msg_exit ("%s: option is already set", item);
    if (!list_append (o->list, item))
        msg_exit ("out of memory");
}

void
opt_add_override (Opt o, const char *fmt, ...)
{
    va_list ap;
    char *item;
    int error;

    assert (o->magic == OPT_MAGIC);
    va_start (ap, fmt);
    error = vasprintf (&item, fmt, ap);
    va_end (ap);
    if (error < 0) 
        msg_exit ("out of memory");
    list_delete_all (o->list, (ListFindF)_match_key, item);
    if (!list_append (o->list, item))
        msg_exit ("out of memory");
}

void
opt_add_cslist (Opt o, char *s)
{
    char *cpy = strdup (s);
    char *item;

    if (!cpy)
        msg_exit ("out of memory");

    item = strtok (cpy, ",");
    while (item) {
        opt_add (o, item);    
        item = strtok (NULL, ",");
    }
    free (cpy);
}

void
opt_add_cslist_override (Opt o, char *s)
{
    char *cpy = strdup (s);
    char *item;

    if (!cpy)
        msg_exit ("out of memory");

    item = strtok (cpy, ",");
    while (item) {
        opt_add_override (o, item);    
        item = strtok (NULL, ",");
    }
    free (cpy);
}

char *
opt_find (Opt o, char *key)
{
    assert (o->magic == OPT_MAGIC);

    return list_find_first (o->list, (ListFindF)_match_key, key);
}

char *
opt_find_withval (Opt o, char *keyval)
{
    assert (o->magic == OPT_MAGIC);

    return list_find_first (o->list, (ListFindF)_match_keyval, keyval);
}


void
opt_test (void)
{
    Opt o = opt_create ();
    char *s;

    opt_add (o, "mickey=%d", 42);
    opt_add (o, "goofey=%s", "yes");
    opt_add (o, "donald");
    opt_add_cslist (o, "foo,bar,baz");
    opt_add (o, "lastone");

    s = opt_string (o);
    msg ("opt string='%s'", s);
    free (s);

    assert (opt_find (o, "mickey"));
    assert (opt_find (o, "bar"));
    assert (!opt_find (o, "barn"));

    opt_add_cslist_override (o, "mickey=string,foo=12,bar=15,baz");
    s = opt_string (o);
    msg ("opt string='%s'", s);
    free (s);

    opt_destroy (o);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
