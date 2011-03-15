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
#include "npclient.h"
#include "list.h"
#include "hostlist.h"
#include "diod_log.h"
#include "diod_upool.h"
#include "diod_sock.h"
#include "diod_auth.h"
#include "opt.h"
#include "util.h"
#include "ctl.h"

#define OPTIONS "u:nj:fo:vp:d:S"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"mount-user",      required_argument,   0, 'u'},
    {"no-mtab",         no_argument,         0, 'n'},
    {"jobid",           required_argument ,  0, 'j'},
    {"fake-mount",      no_argument,         0, 'f'},
    {"verbose",         no_argument,         0, 'v'},
    {"diod-port",       required_argument,   0, 'p'},
    {"v9fs-debug",      required_argument ,  0, 'd'},
    {"v9fs-options",    required_argument,   0, 'o'},
    {"stdin",           no_argument,         0, 'S'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

#define DIOD_MSIZE 65512

static void  _diod_mount (int fd, char *dev, char *dir, char *aname,
                          char *opts, int vopt, int fopt, char *dopt);
static void _verify_mountpoint (char *path);
static hostlist_t _parse_device (char *device, char **anamep);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodmount [OPTIONS] host[,host,...]:aname [directory]\n"
"   -u,--mount-user USER          set up the mount so only USER can use it\n"
"   -n,--no-mtab                  do not update /etc/mtab\n"
"   -j,--jobid STR                set job id string\n"
"   -f,--fake-mount               do everything but the actual diod mount(s)\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *dir = NULL;
    char *aname = NULL;
    char *device;
    int c;
    int nopt = 0;
    int vopt = 0;
    int fopt = 0;
    int Sopt = 0;
    char *popt = NULL;
    char *uopt = NULL;
    char *oopt = NULL;
    char *jopt = NULL;
    char *dopt = NULL;
    int sfd = -1;
    hostlist_t hl = NULL;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'u':   /* --mount-user USER */
                uopt = optarg;
                break;
            case 'n':   /* --no-mtab */
                nopt = 1;
                break;
            case 'o':   /* --v9fs-options OPT[,OPT]... */
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
            case 'd':   /* --v9fs-debug flag[,flag...] */
                dopt = optarg;
                break;
            case 'j':   /* --jobid STR */
                jopt = optarg;
                break;
            case 'S':   /* --stdin */
                Sopt = 1;
                break;
            default:
                usage ();
        }
    }
    if (optind != argc - 2)
        usage ();
    device = argv[optind++];
    dir = argv[optind++];
    hl = _parse_device (device, &aname);

    _verify_mountpoint (dir);

    /* Must start out with effective root id.
     * We drop euid = root but preserve ruid = root for mount, etc.
     */
    if (geteuid () != 0)
        msg_exit ("you must be root to run diodmount");
    if (uopt)
        diod_become_user (uopt, 0, 0);

    /* Server is on stdin, not much to do.
     */
    if (Sopt) {
        sfd = 0;
        nopt = 1;

    /* Port specified, try each host until one responds on that port.
     */
    } else if (popt) {
        hostlist_iterator_t hi;
        char *host;

        if (!(hi = hostlist_iterator_create (hl)))
            msg_exit ("out of memory");
        while ((host = hostlist_next (hi)) && sfd < 0) {
            if (vopt)
                msg ("trying to connect to host %s port %s", host, popt);
            sfd = diod_sock_connect (host, popt, 1, 0);
        }
        hostlist_iterator_destroy (hi);
        if (sfd < 0)
            msg_exit ("could not contact diod server(s)");

    /* Try each diodctl server until one responds.
     * Negotiate a port to conect on based on effective uid and jopt.
     */
    } else {
        hostlist_iterator_t hi;
        char *host, *port;

        if (!(hi = hostlist_iterator_create (hl)))
            msg_exit ("out of memory");
        while ((host = hostlist_next (hi)) && sfd < 0) {
            if (vopt)
                msg ("trying to connect to host %s", host);
            if (ctl_query (host, jopt, &port, NULL) == 0) {
                sfd = diod_sock_connect (host, port, 1, 0);
                free (port);
            }
        }
        hostlist_iterator_destroy (hi);
        if (sfd < 0)
            msg_exit ("could not contact diodctl/diod server(s)");
    }

    _diod_mount (sfd, device, dir, aname, oopt, vopt, fopt, dopt);
    (void)close (sfd);
    if (!nopt) {
        if (!util_update_mtab (device, dir)) {
            util_umount (dir);
            exit (1);
        }
    }

    if (aname)
        free (aname);
    if (hl)
        hostlist_destroy (hl);
    exit (0);
}

/* Given [device] in host[,host,...]:aname format,
 * parse out the hostlist and aname.
 * Caller must free the results.
 * Exit on error.
 */ 
static hostlist_t
_parse_device (char *device, char **anamep)
{
    char *host, *p, *aname;
    hostlist_t hl;

    if (!(host = strdup (device)))
        msg_exit ("out of memory");
    if (!(p = strchr (host, ':')))
        msg_exit ("device is not in host[,host,...]:aname format");
    *p++ = '\0';
    if (!(aname = strdup (p)))
        msg_exit ("out of memory");
    if (!(hl = hostlist_create (host)))
        msg_exit ("failed to parse hostlist");
    free (host);
    *anamep = aname;

    return hl;
}

static char *
_parse_v9fs_debug (char *s)
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

/* Verify that directory exists, exiting on failure.
 */
static void
_verify_mountpoint (char *path)
{
    struct stat sb;

    if (stat (path, &sb) < 0)
        err_exit ("stat %s", path);
    if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s: not a directory", path);
}

/* Mount diod file system already connected to [fd], attaching [aname] to [dir].
 * Default mount options can be overridden by a comma separated list [opts].
 * If [vopt], say what we're doing before we do it.
 * If [fopt], don't actually perform the mount.
 * Authenticate connection in user space, then pass fd to kernel.
 * Exit on error.
 */
static void
_diod_mount (int fd, char *dev, char *dir, char *aname, char *opts,
             int vopt, int fopt, char *dopt)
{
    Opt o = opt_create ();
    char *options;
    uid_t uid = geteuid ();;
    Npcfsys *fs;

    opt_add (o, "rfdno=%d", fd);
    opt_add (o, "wfdno=%d", fd);
    opt_add (o, "trans=fd");
    opt_add (o, "aname=%s", aname);
    opt_add (o, "msize=%d", DIOD_MSIZE);
    opt_add (o, "version=9p2000.L");
    if (uid == 0) {
        opt_add (o, "uname=root");
        opt_add (o, "access=user");
    } else {
        struct passwd *pw = getpwuid (uid);

        if (!pw)
            err_exit ("could not look up name for uid=%d", uid);
        opt_add (o, "access=%d", uid);
        opt_add (o, "uname=%s", pw->pw_name);
    }
    opt_add (o, _parse_v9fs_debug (dopt)); /* debug=0 if !dopt */
    if (opts)
        opt_add_cslist_override (o, opts);
    options = opt_string (o);
    opt_destroy (o);

    if (vopt)
        msg ("mount -t 9p %s %s -o%s", dev, dir, options);
    if (!(fs = npc_mount (fd, DIOD_MSIZE, aname, diod_auth_client_handshake)))
        err_exit ("npc_mount");
    npc_umount2 (fs);
    if (!fopt)
        util_mount (dev, dir, options);
    npc_finish (fs);
    free (options);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
