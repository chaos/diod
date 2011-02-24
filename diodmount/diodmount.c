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

/* diodmount.c - mount a diod file system */

/* Usage: diodmount host:path dir
 *     All users can access dir.
 *     The server is shared by all mounts of this type for all users.
 *
 * Usage: diodmount -u USER host:path dir
 *     Only USER can access dir.
 *     The server is shared by all mounts by USER of this type.
 */

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

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "9p.h"
#include "npfs.h"
#include "list.h"
#include "diod_log.h"
#include "diod_upool.h"
#include "diod_sock.h"
#include "opt.h"
#include "util.h"
#include "ctl.h"
#include "auth.h"

#define OPTIONS "au:no:O:Tvp:fDj:d:"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"all",             no_argument,         0, 'a'},
    {"mount-user",      required_argument,   0, 'u'},
    {"no-mtab",         no_argument,         0, 'n'},
    {"diod-options",    required_argument,   0, 'o'},
    {"diodctl-options", required_argument,   0, 'O'},
    {"test-opt",        no_argument,         0, 'T'},
    {"verbose",         no_argument,         0, 'v'},
    {"diod-port",       required_argument,   0, 'p'},
    {"fake-mount",      no_argument,         0, 'f'},
    {"create-directories", no_argument,      0, 'D'},
    {"jobid",           required_argument ,  0, 'j'},
    {"v9fs-debug",      required_argument ,  0, 'd'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void  _diod_mount       (char *host, char *dir, char *aname, char *port,
                                char *opts, int vopt, int fopt,
                                char *opt_debug);
static void _update_mtab_entries (char *host, char *root, List dirs);
static void _verify_mountpoint (char *path, int Dopt);
static void _verify_mountpoints (List dirs, char *root, int Dopt);
static char *_parse_debug      (char *s);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodmount [OPTIONS] device [directory]\n"
"   -a,--all                      mount all exported file systems\n"
"   -u,--mount-user USER          set up the mount so only USER can use it\n"
"   -n,--no-mtab                  do not update /etc/mtab\n"
"   -o,--diod-options OPT[,...]   additional mount options for diod\n"
"   -O,--diodctl-option OPT[,...] additional mount options for diodctl\n"
"   -v,--verbose                  be verbose about what is going on\n"
"   -p,--diod-port PORT           mount diod directly on specified port num\n"
"   -f,--fake-mount               do everything but the actual diod mount(s)\n"
"   -D,--create-directories       create mount directories as needed\n"
"   -d,--v9fs-debug flag[,flag...] set v9fs debugging flags [error,9p,vfs,\n"
"                                 conv,mux,trans,slabs,fcall,fid,pkt,fsc]\n"
"   -j,--jobid STR                set job id string\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *dir = NULL, *host = NULL;
    char *aname = NULL;
    char *device;
    int c;
    int aopt = 0;
    int nopt = 0;
    int vopt = 0;
    int fopt = 0;
    int Dopt = 0;
    char *popt = NULL;
    char *uopt = NULL;
    char *oopt = NULL;
    char *Oopt = NULL;
    char *jopt = NULL;
    char *dopt = NULL;
    char *opt_debug = NULL;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'a':   /* --all */
                aopt = 1;
                break;
            case 'D':   /* --create-directories */
                Dopt = 1;
                break;
            case 'u':   /* --mount-user USER */
                uopt = optarg;
                break;
            case 'n':   /* --no-mtab */
                nopt = 1;
                break;
            case 'o':   /* --diod-options OPT[,OPT]... */
                oopt = optarg;
                break;
            case 'O':   /* --diodctl-options OPT[,OPT]... */
                Oopt = optarg;
                break;
            case 'v':   /* --verbose */
                vopt = 1;
                break;
            case 'p':   /* --diod-port */
                popt = optarg;
                break;
            case 'f':   /* --fake-mount */
                fopt = 1;
                break;
            case 'T':   /* --test-opt */
                opt_test ();
                exit (0);
            case 'd':   /* --v9fs-debug flag[,flag...] */
                dopt = optarg;
                break;
            case 'j':   /* --jobid STR */
                jopt = optarg;
                break;
            default:
                usage ();
        }
    }
    if (aopt && popt)
        msg_exit ("--all and --diod-port are incompatible options");
    if (aopt) {/* Usage: diodmount [options] -a hostname [dir] */
        if (optind != argc - 1 && optind != argc - 2)
            usage ();
        device = argv[optind++];
        if (optind != argc)
            dir = argv[optind++];
        if (!(host = strdup (device)))
            msg_exit ("out of memory");
    } else {          /* Usage: diodmount [options] hostname:/aname directory */
        if (optind != argc - 2)
            usage ();
        device = argv[optind++];
        dir = argv[optind++];
        util_parse_device (device, &aname, &host);
    }
    opt_debug = _parse_debug (dopt);

    /* Must start out with effective root id.
     * We drop euid = root but preserve ruid = root for mount, etc.
     */
    if (geteuid () != 0)
        msg_exit ("you must be root to run diodmount");
    if (uopt)
        diod_become_user (uopt, 0, 0);

    /* Mount everything, obtaining port number and exports from diodctl.
     * If directory is specified, use as the root for mount points, else /.
     * If -D option, create mount points as needed.
     */
    if (aopt) {
        query_t *ctl = ctl_query (host, Oopt, vopt, aopt, jopt, opt_debug);
        ListIterator itr;
        char *el;

        if (aopt)
            _verify_mountpoints (ctl->exports, dir, Dopt);
        if (!(itr = list_iterator_create (ctl->exports)))
            msg_exit ("out of memory");
        while ((el = list_next (itr))) {
            char path[PATH_MAX];

            snprintf (path, sizeof (path), "%s%s", dir ? dir : "", el);
            _diod_mount (host, path, el, ctl->port, oopt, vopt, fopt, opt_debug);
        }
        list_iterator_destroy (itr);

        if (!nopt)
            _update_mtab_entries (device, dir, ctl->exports);
        free_query (ctl);

    /* Mount one file system, obtaining port number from command line.
     * If -D option, create mount point as needed.
     */
    } else if (popt) {

        _verify_mountpoint (dir, Dopt);
        _diod_mount (host, dir, aname, popt, oopt, vopt, fopt, opt_debug);

        if (!nopt) {
            if (!util_update_mtab (device, dir)) {
                util_umount (dir);
                exit (1);
            }
        }

    /* Mount one file system, obtaining port number from diodctl.
     * If -D option, create mount point as needed.
     */
    } else {
        query_t *ctl;

        _verify_mountpoint (dir, Dopt);
        ctl = ctl_query (host, Oopt, vopt, 1, jopt, opt_debug);
        _diod_mount (host, dir, aname, ctl->port, oopt, vopt, fopt, opt_debug);
        free_query (ctl);

        if (!nopt) {
            if (!util_update_mtab (device, dir)) {
                util_umount (dir);
                exit (1);
            }
        }
    }

    free (host);
    if (aname)
        free (aname);
    exit (0);
}

static char *
_parse_debug (char *s)
{
    char *optstr;
    Opt o;
    int f = 0;

    if (!(optstr = malloc(64)))
        msg_exit ("out of memory");
    if (s) {
        o = opt_create();

        opt_add_cslist (o, s);
        if (opt_find (o, "error"))
            f |= P9_DEBUG_ERROR;
        if (opt_find (o, "9p"))
            f |= P9_DEBUG_9P;
        if (opt_find (o, "vfs"))
            f |= P9_DEBUG_VFS;
        if (opt_find (o, "conv"))
            f |= P9_DEBUG_CONV;
        if (opt_find (o, "mux"))
            f |= P9_DEBUG_MUX;
        if (opt_find (o, "trans"))
            f |= P9_DEBUG_TRANS;
        if (opt_find (o, "slabs"))
            f |= P9_DEBUG_SLABS;
        if (opt_find (o, "fcall"))
            f |= P9_DEBUG_FCALL;
        if (opt_find (o, "fid"))
            f |= P9_DEBUG_FID;
        if (opt_find (o, "pkt"))
            f |= P9_DEBUG_PKT;
        if (opt_find (o, "fsc"))
            f |= P9_DEBUG_FSC;
        opt_destroy (o);
    }
    snprintf (optstr, 64, "debug=0x%x", f);

    return optstr; 
}

/* Recursively create directory [path] if Dopt is true.
 * Verify that directory exists, exiting on failure.
 */
static void
_verify_mountpoint (char *path, int Dopt)
{
    struct stat sb;

    if (stat (path, &sb) < 0) {
        if (Dopt)
            err_exit ("stat %s", path);
        if (util_mkdir_p (path, 0755) < 0)
            err_exit ("mkdir %s", path);
        if (stat (path, &sb) < 0)
            err_exit ("stat %s", path);
    } 
    if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s: not a directory", path);
}

/* Run a list of directories through _verify_mountpoint ().
 * If root is non-null, prepend it to the path.
 */
static void
_verify_mountpoints (List dirs, char *root, int Dopt)
{
    ListIterator itr;
    char *el;
    char path[PATH_MAX];

    if (!(itr = list_iterator_create (dirs)))
        msg_exit ("out of memory");
    while ((el = list_next (itr))) {
        snprintf (path, sizeof(path), "%s%s", root ? root : "", el);
        _verify_mountpoint (path, Dopt);
    }
    list_iterator_destroy (itr);
}

/* Run a list of directories through util_update_mtab ().
 * Mount point path is assumed to be same as that in the "device" (HOST:/dir).
 * If root is non-null, prepend it to the mount point path.
 */
static void
_update_mtab_entries (char *host, char *root, List dirs)
{
    char dev[PATH_MAX + NI_MAXSERV + 1];
    char path[PATH_MAX];
    ListIterator itr;
    char *el;

    if (!(itr = list_iterator_create (dirs)))
        msg_exit ("out of memory");
    while ((el = list_next (itr))) {
        snprintf (path, sizeof(path), "%s%s", root ? root : "", el);
        snprintf (dev, sizeof (dev), "%s:%s", host, el);
        (void)util_update_mtab (dev, path);
    }
    list_iterator_destroy (itr);
}

/* Mount diod file system specified by [host], [port], [aname] on [dir].
 * Default mount options can be overridden by a comma separated list [opts].
 * If [vopt], say what we're doing before we do it.
 * If [fopt], don't actually perform the mount.
 * Exit on error.
 */
static void
_diod_mount (char *host, char *dir, char *aname, char *port, char *opts,
             int vopt, int fopt, char *opt_debug)
{
    Opt o = opt_create ();
    char *options, *cred, *dev;
    int fd;
    cred = auth_mkuser (NULL);
    opt_add (o, "uname=%s", cred);
    if ((fd = diod_sock_connect (host, port ? port : "564", 1, 0)) < 0)
        err_exit ("connect failed");
    opt_add (o, "rfdno=%d", fd);
    opt_add (o, "wfdno=%d", fd);
    opt_add (o, "trans=fd");
    opt_add (o, "aname=%s", aname);
    opt_add (o, "msize=65560");
    opt_add (o, "version=9p2000.L");
    if (geteuid () != 0)
        opt_add (o, "access=%d", geteuid ());
    else
        opt_add (o, "access=user");
    if (opt_debug)
        opt_add (o, opt_debug);
    if (opts)
        opt_add_cslist_override (o, opts);
    free (cred);
    options = opt_string (o);
    opt_destroy (o);

    if (!(dev = malloc (strlen (host) + strlen (aname) + 2)))
        msg_exit ("out of memory");
    sprintf (dev, "%s:%s", host, aname);
    if (vopt)
        msg ("mount %s %s -o%s", dev, dir, options);
    if (!fopt)
        util_mount (dev, dir, options);
    free (options);
    free (dev);
    close (fd);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
