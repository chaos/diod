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

#define OPTIONS "i:ak:K"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"auto-indirect",   required_argument,   0, 'i'},
    {"auto-key",        required_argument,   0, 'k'},
    {"auto-listkeys",   no_argument,         0, 'K'},
    {"auto-direct",     no_argument,         0, 'a'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static int _list_exports (hostlist_t hl);
static int _list_exports_auto_direct (hostlist_t hl, char *key, int listkey);
static int _list_exports_auto_indirect (hostlist_t hl, char *map, char *key,
                                        int listkey);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodexp [OPTIONS] host[,host,...]\n"
"   -a,--auto-direct            list in automounter direct map format\n"
"   -i,--auto-indirect MAP      list in automounter indirect map format\n"
"   -k,--auto-key KEY           list automounter map entry for KEY\n"
"   -K,--auto-listkeys          list automounter keys\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    hostlist_t hl;
    char *iopt = NULL;
    int aopt = 0;
    int Kopt = 0;
    char *kopt = NULL;
    int c;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'i':   /* --auto-indirect */
                iopt = optarg;
                break;
            case 'a':   /* --auto-direct */
                aopt = 1;
                break;
            case 'k':   /* --auto-key */
                kopt = optarg;
                break;
            case 'K':   /* --auto-listkeys */
                Kopt = 1;
                break;
            default:
                usage ();
        }
    }
    if (optind != argc - 1)
        usage ();
    if ((kopt || Kopt) && !iopt && !aopt)
        msg_exit ("-k or -K should be used only with -i or -a");
    if (!(hl = hostlist_create (argv[optind++])))
        msg_exit ("error parsing hostlist");
    if (hostlist_count (hl) == 0)
        usage ();
    if (aopt)
        (void)_list_exports_auto_direct (hl, kopt, Kopt);
    else if (iopt)
        (void)_list_exports_auto_indirect (hl, iopt, kopt, Kopt);
    else
        (void)_list_exports (hl);

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

static char *
_dir1name (char *path)
{
    char *s, *p;

    if (!(s = strdup (path)))
        msg_exit ("out of memory");
    p = s;
    if (*p == '/')
        p++;
    if ((p = strchr (p, '/')))
        *p = '\0';
    return s;
}

/* List available exports in automounter direct map format:
 *    local-path [options] host[,host,...]:remote-path
 * Quit after the first server responds (assumes all servers are equal).
 */
static int
_list_exports_auto_indirect (hostlist_t hl, char *map, char *key, int listkey)
{
    hostlist_iterator_t hi;
    List exports = NULL;
    ListIterator li;
    char *host, *path;
    char *hstr = _hstr (hl);
    int n = 0;

    if (!(hi = hostlist_iterator_create (hl)))
        msg_exit ("out of memory");
    
    while ((host = hostlist_next (hi))) {
        if (ctl_query (host, NULL, NULL, &exports) == 0) {
            if (!(li = list_iterator_create (exports)))
                msg_exit ("out of memory");
            while ((path = list_next (li))) {
                char *p = _dir1name (path);

                if (strcmp (p, map) == 0 && strlen (path) > strlen (p)) {
                    if (key) {
                        if (strcmp (key, path + strlen (p) + 1) == 0)
                            printf ("-fstype=diod %s:%s\n", hstr, path);
                    } else if (listkey) {
                        printf ("%s\n", path + strlen (p) + 1);
                    } else {
                        printf ("%s -fstype=diod %s:%s\n",
                                path + strlen (p) + 1, hstr, path);
                    }
                }
                free (p);
                n++;
            } 
            list_iterator_destroy (li);
            break;
        }
    }
    hostlist_iterator_destroy (hi);
    free (hstr);
    return n;
}


/* List available exports in automounter direct map format:
 *    local-path [options] host[,host,...]:remote-path
 * Quit after the first server responds (assumes all servers are equal).
 */
static int
_list_exports_auto_direct (hostlist_t hl, char *key, int listkey)
{
    hostlist_iterator_t hi;
    List exports = NULL;
    ListIterator li;
    char *host, *path;
    char *hstr = _hstr (hl);
    int n = 0;

    if (!(hi = hostlist_iterator_create (hl)))
        msg_exit ("out of memory");
    
    while ((host = hostlist_next (hi))) {
        if (ctl_query (host, NULL, NULL, &exports) == 0) {
            if (!(li = list_iterator_create (exports)))
                msg_exit ("out of memory");
            while ((path = list_next (li))) {
                if (key) {
                    if (strcmp (key, path) == 0)
                        printf ("-fstype=diod %s:%s\n", hstr, path);
                } else if (listkey) {
                    printf ("%s\n", path);
                } else {
                    printf ("%s -fstype=diod %s:%s\n", path, hstr, path);
                }
                n++;
            } 
            list_iterator_destroy (li);
            break;
        }
    }
    hostlist_iterator_destroy (hi);
    free (hstr);
    return n;
}


/* List available exports in a simple format:
 *    host[,host,...]:/aname
 * Quit after the first server responds (assumes all servers are equal).
 */
static int
_list_exports (hostlist_t hl)
{
    hostlist_iterator_t hi;
    List exports = NULL;
    ListIterator li;
    char *host, *path;
    char *hstr = _hstr (hl);
    int n = 0;

    if (!(hi = hostlist_iterator_create (hl)))
        msg_exit ("out of memory");
    while ((host = hostlist_next (hi))) {
        if (ctl_query (host, NULL, NULL, &exports) == 0) {
            if (!(li = list_iterator_create (exports)))
                msg_exit ("out of memory");
            while ((path = list_next (li))) {
                printf ("%s:%s\n", hstr, path);
                n++;
            } 
            list_iterator_destroy (li);
            break;
        }
    }
    hostlist_iterator_destroy (hi);
    free (hstr);
    return n;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
