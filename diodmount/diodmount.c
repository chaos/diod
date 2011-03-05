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
#include <assert.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "9p.h"
#include "npfs.h"
#include "list.h"
#include "diod_log.h"
#include "diod_upool.h"
#include "diod_sock.h"
#include "diod_auth.h"
#include "opt.h"
#include "util.h"
#include "ctl.h"

#define OPTIONS "au:Dnj:fs:x:o:Tvp:d:"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"all",             no_argument,         0, 'a'},
    {"mount-user",      required_argument,   0, 'u'},
    {"create-directories", no_argument,      0, 'D'},
    {"no-mtab",         no_argument,         0, 'n'},
    {"jobid",           required_argument ,  0, 'j'},
    {"fake-mount",      no_argument,         0, 'f'},
    {"server",          required_argument,   0, 's'},
    {"exec",            required_argument,   0, 'x'},
    {"diod-options",    required_argument,   0, 'o'},
    {"test-opt",        no_argument,         0, 'T'},
    {"verbose",         no_argument,         0, 'v'},
    {"diod-port",       required_argument,   0, 'p'},
    {"v9fs-debug",      required_argument ,  0, 'd'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void  _diod_mount (int fd, char *dev, char *dir, char *aname,
                          char *opts, int vopt, int fopt, char *opt_debug);
static void _verify_mountpoint (char *path, int Dopt);
static char *_parse_debug (char *s);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodmount [OPTIONS] device [directory]\n"
"   -a,--all                      mount all exported file systems\n"
"   -u,--mount-user USER          set up the mount so only USER can use it\n"
"   -D,--create-directories       create mount directories as needed\n"
"   -n,--no-mtab                  do not update /etc/mtab\n"
"   -j,--jobid STR                set job id string\n"
"   -f,--fake-mount               do everything but the actual diod mount(s)\n"
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
    char *jopt = NULL;
    char *dopt = NULL;
    char *sopt = NULL;
    char *xopt = NULL;
    char *opt_debug = NULL;
    query_t *ctl = NULL;
    int rc = 0;
    pid_t spid;
    int sfd;

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
            case 's':   /* --server CMD */
                sopt = optarg;
                break;
            case 'x':   /* --exec CMD */
                xopt = optarg;
                break;
            default:
                usage ();
        }
    }
    if (aopt && popt)
        msg_exit ("--all and --diod-port are incompatible options");
    if (aopt && sopt)
        msg_exit ("--all and --server are incompatible options");
    if (popt && sopt)
        msg_exit ("--server and --diod-port are incompatible options");
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

    /* Fetch server export and port info from diodctl, if mode requires it.
     */
    if ((!popt && !sopt) || aopt)
        ctl = ctl_query (host, aopt, jopt);

    /* Move into a new namespace for ephemeral mounts (-s, -x).
     * Force -n so we don't leave behind mtab entries.
     * All mounts will be implicitly unmounted when we exit.
     */
    if (xopt || sopt) {
        util_unshare ();
        nopt = 1;
    }

    /* Mount everything.
     * If directory is specified, use as the root for mount points, else /.
     * If -D option, create mount points as needed.
     */
    if (aopt) {
        ListIterator itr;
        char *el;
        char path[PATH_MAX];
        char dev[PATH_MAX];

        if (!(itr = list_iterator_create (ctl->exports)))
            msg_exit ("out of memory");
        while ((el = list_next (itr))) {
            snprintf (path, sizeof (path), "%s%s", dir ? dir : "", el);
            _verify_mountpoint (path, Dopt);
        }
        list_iterator_reset(itr);
        while ((el = list_next (itr))) {
            if ((sfd = diod_sock_connect (host, ctl->port, 1, 0)) < 0)
                err_exit ("connect failed");
            snprintf (dev, sizeof(dev), "%s:%s", host, el);
            _diod_mount (sfd, dev, path, el, oopt, vopt, fopt, opt_debug);
            close (sfd);
            if (!nopt)
                (void)util_update_mtab (dev, path); /* FIXME: handle error */
        }
        list_iterator_destroy (itr);

    /* Mount one file system.
     * If -D option, create mount point as needed.
     */
    } else {
        _verify_mountpoint (dir, Dopt);
        if (sopt) {
            sfd = util_spopen (sopt, &spid, 0);
            if (vopt)
                msg ("server started");
        } else {
            sfd = diod_sock_connect (host, popt ? popt : ctl->port, 1, 0);
            if (sfd < 0)
                err_exit ("connect failed");
        }
        _diod_mount (sfd, device, dir, aname, oopt, vopt, fopt, opt_debug);
        close (sfd);
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
    if (ctl)
        free_query (ctl);

    /* Run -x command in ephemeral mount sandbox.
     */
    if (xopt)
        rc = util_runcmd (xopt);
    exit (rc);
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

/* Mount diod file system already connected to [fd], attaching [aname] to [dir].
 * Default mount options can be overridden by a comma separated list [opts].
 * If [vopt], say what we're doing before we do it.
 * If [fopt], don't actually perform the mount.
 * Exit on error.
 */
static void
_diod_mount (int fd, char *dev, char *dir, char *aname, char *opts,
             int vopt, int fopt, char *opt_debug)
{
    Opt o = opt_create ();
    char *options;
    //char *u = strdup ("root"); /* FIXME */

    //opt_add (o, "uname=%s", u);
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
    //free (u);
    options = opt_string (o);
    opt_destroy (o);

    if (vopt)
        msg ("mount %s %s -o%s", dev, dir, options);
    if (!fopt)
        util_mount (dev, dir, options);
    free (options);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
