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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <inttypes.h>
#include <pthread.h>

#include "9p.h"
#include "npfs.h"

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
    NP_ASSERT (o->magic == OPT_MAGIC);
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
opt_csv (Opt o)
{
    ListIterator itr;
    char *item, *s;
    int strsize = 1;
    int n;

    NP_ASSERT (o->magic == OPT_MAGIC);
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

int
opt_addf (Opt o, const char *fmt, ...)
{
    va_list ap;
    char *csv, *item, *cpy;
    char *saveptr = NULL;
    int error;

    NP_ASSERT (o->magic == OPT_MAGIC);
    va_start (ap, fmt);
    error = vasprintf (&csv, fmt, ap);
    va_end (ap);
    if (error < 0)
        msg_exit ("out of memory");

    item = strtok_r (csv, ",", &saveptr);
    while (item) {
        if (!(cpy = strdup (item)))
            msg_exit ("out of memory");
        (void)list_delete_all (o->list, (ListFindF)_match_key, cpy);   
        if (!list_append (o->list, cpy))
            msg_exit ("out of memory");
        item = strtok_r (NULL, ",", &saveptr);
    }
    free (csv);
    return 1;
}

/* return option value or empty string (not null)
 */
static char *
_optstr (char *s)
{
    char *p = strrchr (s, '=');

    return p ? p + 1 : s + strlen (s);
}

char *
opt_find (Opt o, char *key)
{
    char *s;

    NP_ASSERT (o->magic == OPT_MAGIC);

    if (strchr (key, '='))
        s = list_find_first (o->list, (ListFindF)_match_keyval, key);
    else
        s = list_find_first (o->list, (ListFindF)_match_key, key);

    return s ? _optstr (s) : NULL;
}

/* Returns number of deletions.
 */
int
opt_delete (Opt o, char *key)
{
    NP_ASSERT (o->magic == OPT_MAGIC);

    return list_delete_all (o->list, (ListFindF)_match_key, key);   
}

int
opt_vscanf (Opt o, const char *fmt, va_list ap)
{
    ListIterator itr;
    char *item;
    int ret = 0;

    NP_ASSERT (o->magic == OPT_MAGIC);

    if (!(itr = list_iterator_create (o->list)))
        msg_exit ("out of memory");
    while ((item = list_next (itr))) {
        va_list vacpy;

        va_copy (vacpy, ap);
        ret = vsscanf (item, fmt, vacpy);
        va_end (vacpy);

        if (ret > 0)
            break;
    }
    list_iterator_destroy (itr);
    return ret;
}

int
opt_scanf (Opt o, const char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = opt_vscanf (o, fmt, ap);
    va_end(ap);

    return ret;
}


int
opt_check_allowed_csv (Opt o, const char *csv)
{
    Opt allow;
    ListIterator itr;
    char *item, *cpy, *p;
    int ret = 0;

    NP_ASSERT (o->magic == OPT_MAGIC);

    allow = opt_create ();
    opt_addf (allow, "%s", csv);

    if (!(itr = list_iterator_create (o->list)))
        msg_exit ("out of memory");
    while ((item = list_next (itr))) {
        if (!(cpy = strdup (item)))
            msg_exit ("out of memory");
        if ((p = strchr (cpy, '=')))
            *p = '\0';
        if (!opt_find (allow, cpy)) {
            ret = 1;
            free (cpy);
            break;
        }
        free (cpy);
    }
    list_iterator_destroy (itr);

    opt_destroy (allow);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
