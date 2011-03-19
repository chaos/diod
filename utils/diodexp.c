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

/* diodexp.c - list exports */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdint.h>
#include <netdb.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <libgen.h>
#include <assert.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "list.h"
#include "hostlist.h"
#include "diod_log.h"
#include "diod_upool.h"
#include "diod_sock.h"
#include "diod_auth.h"
#include "opt.h"
#include "ctl.h"

#define OPTIONS "a:"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"auto-indirect",   required_argument,      0, 'a'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void _list_exports (hostlist_t hl);
static void _list_exports_auto_indirect (hostlist_t hl, char *key);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodexp [OPTIONS] host[,host,...]\n"
"   -a,--auto-indirect KEY      lookup KEY for automounter program map\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    hostlist_t hl;
    char *aopt = NULL;
    int c;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'a':   /* --auto-indirect KEY */
                aopt = optarg;
                break;
            default:
                usage ();
        }
    }
    if (optind != argc - 1)
        usage ();
    if (!(hl = hostlist_create (argv[optind++])))
        msg_exit ("error parsing hostlist");
    if (hostlist_count (hl) == 0)
        usage ();
    if (aopt)
        _list_exports_auto_indirect (hl, aopt);
    else
        _list_exports (hl);

    hostlist_destroy (hl);
    exit (0);
}

static char *
_hstr (hostlist_t hl)
{
    int len = 0;
    char *s = NULL;

    do {
        len += 64;
        if (!(s = (s ? realloc (s, len) : malloc (len))))
            msg_exit ("out of memory");
    } while (hostlist_deranged_string(hl, len, s) < 0);

    return s;
}

static int
_match_exp_base (char *path, char *key)
{
    char *p = strrchr (path, '/');

    return (strcmp (key, p ? p + 1 : path) == 0);
}

static int
_match_exp_hy (char *path, char *key)
{
    char *cpy, *p;
    int ret = 0;

    if (!(cpy = strdup (path)))
        msg_exit ("out of memory");
    p = cpy;
    while (*p == '/')
        p++;
    while (*p && *key) {
        if ((*p != *key) && !(*key == '-' && *p == '/'))
            break;
        p++;
        key++;
    }
    if (*p == '\0' && *key == '\0')
        ret = 1;
    free (cpy);

    return ret;
}

static void
_list_exports_auto_indirect (hostlist_t hl, char *key)
{
    hostlist_iterator_t hi;
    List exports = NULL;
    char *host, *path;
    char *hstr = _hstr (hl);
    int hostok = 0;

    if (!(hi = hostlist_iterator_create (hl)))
        msg_exit ("out of memory");
    
    while ((host = hostlist_next (hi))) {
        if (ctl_query (host, NULL, NULL, &exports) == 0) {
            path = list_find_first (exports, (ListFindF)_match_exp_base, key);
            if (!path)
                path = list_find_first (exports, (ListFindF)_match_exp_hy, key);
            if (path)
                printf ("-fstype=diod %s:%s\n", hstr, path);
            hostok = 1;
            break;
        }
    }
    hostlist_iterator_destroy (hi);
    free (hstr);
    if (!hostok)
        msg_exit ("could not contact a diodctl server");
}


static void
_list_exports (hostlist_t hl)
{
    hostlist_iterator_t hi;
    List exports = NULL;
    ListIterator li;
    char *host, *path;
    char *hstr = _hstr (hl);
    int hostok = 0;

    if (!(hi = hostlist_iterator_create (hl)))
        msg_exit ("out of memory");
    while ((host = hostlist_next (hi))) {
        if (ctl_query (host, NULL, NULL, &exports) == 0) {
            if (!(li = list_iterator_create (exports)))
                msg_exit ("out of memory");
            while ((path = list_next (li)))
                printf ("%s:%s\n", hstr, path);
            list_iterator_destroy (li);
            hostok = 1;
            break;
        }
    }
    hostlist_iterator_destroy (hi);
    free (hstr);
    if (!hostok)
        msg_exit ("could not contact a diodctl server");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
