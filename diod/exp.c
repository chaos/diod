/*****************************************************************************
 *  Copyright (C) 2010-14 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see http://code.google.com/p/diod.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also: http://www.gnu.org/licenses
 *****************************************************************************/

/* exp.c - functions for manipulating exports */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <stdarg.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"
#include "hostlist.h"

#include "diod_conf.h"
#include "diod_log.h"
#include "exp.h"

static int
_match_export_users (Export *x, Npuser *user)
{
    if (!x->users)
        return 1;
    /* FIXME */
    return 0; /* no match */
}


/* FIXME: client_id could be hostname or IP.
 * We probably want both to work for an exports match.
 */
static int
_match_export_hosts (Export *x, Npconn *conn)
{
    char *client_id = np_conn_get_client_id (conn);
    hostlist_t hl = NULL;
    int res = 0; /* no match */

    /* privport is required */
    if (x->oflags & XFLAGS_PRIVPORT && !(conn->flags & CONN_FLAGS_PRIVPORT)) {
        np_uerror (EPERM);
        goto done;
    }

    /* no client_id restrictions */
    if (!x->hosts) {
        res = 1;
        goto done;
    }
    if (!(hl = hostlist_create (x->hosts))) {
        np_uerror (ENOMEM);
        goto done;
    }
    /* client_id found in exports */
    if (hostlist_find (hl, client_id) != -1) {
        res = 1;
        goto done;
    }
done:
    if (hl)
        hostlist_destroy (hl);
    return res;
}

/* N.B. in diod_conf_validate_exports () we already have ensured
 * that export begins with / and contains no /.. elements.
 */
static int
_match_export_path (Export *x, char *path)
{
    int xlen = strlen (x->path);
    int plen = strlen (path);

    /* an export of / matches all but "ctl" */
    if (strcmp (x->path, "/") == 0 && strcmp (path, "ctl") != 0)
        return 1;
    /* drop trailing slashes from export */
    while (xlen > 0 && x->path[xlen - 1] == '/')
        xlen--;
    /* export is identical to path */
    if (plen == xlen && strncmp (x->path, path, plen) == 0)
        return 1;
    /* export is parent of path */
    if (plen > xlen && path[xlen] == '/')
        return 1;

    return 0; /* no match */
}

static int
_match_mounts (char *path, int *xfp)
{
    List exports  = diod_conf_get_mounts ();
    ListIterator itr = NULL;
    Export *x;
    int res = 0; /* DENIED */

    if (!exports) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (!(itr = list_iterator_create (exports))) {
        np_uerror (ENOMEM);
        goto done;
    }
    while (res == 0 && (x = list_next (itr))) {
        if (_match_export_path (x, path)) {
            *xfp = x->oflags;
            res = 1;
        }
    }
    if (res == 0)
        np_uerror (EPERM);
done:
    if (itr)
        list_iterator_destroy (itr);
    if (exports)
        list_destroy (exports);
    return res;
}

/* Called from attach to determine if aname is valid for user/conn.
 * (Now via fcall.c::np_attach, not through diod_attach)
 */
int
diod_match_exports (char *path, Npconn *conn, Npuser *user, int *xfp)
{
    List exports = diod_conf_get_exports ();
    ListIterator itr = NULL;
    Export *x;
    int res = 0; /* DENIED */

    NP_ASSERT (exports != NULL);
    if (strstr (path, "/..") != NULL) {
        np_uerror (EPERM);
        goto done;
    }
    if (!(itr = list_iterator_create (exports))) {
        np_uerror (ENOMEM);
        goto done;
    }
    while (res == 0 && (x = list_next (itr))) {
        if (!_match_export_path (x, path))
            continue;
        if ((x->oflags & XFLAGS_SUPPRESS))
            goto done;
        if (!_match_export_hosts (x, conn))
            goto done;
        if (!_match_export_users (x, user))
            goto done;
        if (xfp)
            *xfp = x->oflags;
        res = 1;
    }
    if (res == 0 && diod_conf_get_exportall ())
        res = _match_mounts (path, xfp);
    if (res == 0 && np_rerror () == 0)
        np_uerror (EPERM);
done:
    if (itr)
        list_iterator_destroy (itr);
    return res;
}

/* Retrieve export flags for the given aname.
 * Don't set np_uerror() here, just return 1 on match, 0 otherwise.
 */
int diod_fetch_xflags (Npstr *aname, int *xfp)
{
    List exports = diod_conf_get_exports ();
    ListIterator itr = NULL;
    Export *x;
    char *path = NULL;
    int res = 0;

    if (!(path = np_strdup (aname)))
        goto done;
    NP_ASSERT (exports != NULL);
    if (strstr (path, "/..") != NULL)
        goto done;
    if (!(itr = list_iterator_create (exports)))
        goto done;
    while ((x = list_next (itr))) {
        if (_match_export_path (x, path)) {
            if (xfp)
                *xfp = x->oflags;
            res = 1;
            break;
        }
    }
done:
    if (itr)
        list_iterator_destroy (itr);
    if (path)
        free (path);
    return res;
}

/**
 ** ctl/exports handling
 **/

static int
_strmatch (char *s1, char *s2)
{
    return (!strcmp (s1, s2) ? 1 : 0);
}


static char *
_get_mounts (char **sp, int *lp, List seen)
{
    List exports = diod_conf_get_mounts ();
    ListIterator itr = NULL;
    Export *x;
    char *ret = NULL;

    if (!exports) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (!(itr = list_iterator_create (exports))) {
        np_uerror (ENOMEM);
        goto done;
    }
    while ((x = list_next (itr))) {
        if (list_find_first (seen, (ListFindF)_strmatch, x->path))
            continue;
        if (!list_append (seen, x->path)) {
            np_uerror (ENOMEM);
            goto done;
        }
        if (aspf (sp, lp, "%s %s %s %s\n",
                  x->path,
                  x->opts ? x->opts : "-",
                  x->users ? x->users : "-",
                  x->hosts ? x->hosts : "-") < 0) { 
            np_uerror (ENOMEM);
            goto done;
        }
    }
    ret = *sp;
done:
    if (itr)
        list_iterator_destroy (itr);
    if (exports)
        list_destroy (exports);
    return ret;
}

char *
diod_get_exports (char *name, void *a)
{
    List exports = diod_conf_get_exports ();
    List seen = NULL;
    ListIterator itr = NULL;
    Export *x;
    int len = 0;
    char *s = NULL;
    char *ret = NULL;

    NP_ASSERT (exports != NULL);

    if (!(seen = list_create (NULL))) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (!(itr = list_iterator_create (exports))) {
        np_uerror (ENOMEM);
        goto done;
    }
    while ((x = list_next (itr))) {
        if (list_find_first (seen, (ListFindF)_strmatch, x->path))
            continue;
        if (!list_append (seen, x->path)) {
            np_uerror (ENOMEM);
            goto done;
        }
        if (!(x->oflags & XFLAGS_SUPPRESS)) {
            if (aspf (&s, &len, "%s %s %s %s\n",
                      x->path,
                      x->opts ? x->opts : "-",
                      x->users ? x->users : "-",
                      x->hosts ? x->hosts : "-") < 0) { 
                np_uerror (ENOMEM);
                goto done;
            }
        }
    }
    if (diod_conf_get_exportall ())
        if (!_get_mounts (&s, &len, seen))
            goto done;
    ret = s;
done:
    if (itr)
        list_iterator_destroy (itr);
    if (seen)
        list_destroy (seen);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
