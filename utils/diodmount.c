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

/* diodmount.c - diod file system mount helper
 * 
 * Usage: /sbin/mount.diod spec dir [-sfnv] [-o options]
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _GNU_SOURCE     /* asprintf, unshare */
#include <sched.h>
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
#include <mntent.h>
#include <sys/mount.h>
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

#define OPTIONS "fnvo:"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"fake-mount",      no_argument,         0, 'f'},
    {"no-mtab",         no_argument,         0, 'n'},
    {"verbose",         no_argument,         0, 'v'},
    {"options",         required_argument,   0, 'o'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

#define DIOD_DEFAULT_MSIZE 65512
static void _diod_mount (Opt o, int fd, char *spec, char *dir, int vopt,
                         int fopt, int nopt);
static void _diod_remount (Opt o, char *spec, char *dir, int vopt, int fopt);
static void _verify_mountpoint (char *path);
static void _parse_uname_access (Opt o);
static hostlist_t _parse_spec (char *spec, Opt o);
static void _mount (const char *source, const char *target,
                    unsigned long mountflags, const void *data);

static void
usage (void)
{
    fprintf (stderr,
"Usage: mount.diod [OPTIONS] host[,host,...]:aname [directory]\n"
"   -f,--fake-mount               do everything but the actual diod mount\n"
"   -n,--no-mtab                  do not update /etc/mtab\n"
"   -v,--verbose                  verbose mode\n"
"   -o,--options opt[,opt,...]    specify mount options\n" 
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *dir = NULL;
    char *port = NULL;
    char *spec;
    int c;
    int nopt = 0;
    int vopt = 0;
    int fopt = 0;
    int sfd = -1;
    hostlist_t hl = NULL;
    Opt o; 

    diod_log_init (argv[0]);

    o = opt_create ();

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'f':   /* --fake-mount */
                fopt = 1;
                break;
            case 'n':   /* --no-mtab */
                nopt = 1;
                break;
            case 'v':   /* --verbose */
                vopt++;
                break;
            case 'o':   /* --options OPT[,OPT]... */
                opt_add_cslist_override (o, optarg);
                break;
            default:
                usage ();
        }
    }
    if (optind != argc - 2)
        usage ();
    spec = argv[optind++];
    dir = argv[optind++];
    hl = _parse_spec (spec, o);

    if (geteuid () != 0)
        msg_exit ("you must be root");

    _verify_mountpoint (dir);

    /* Remount - only pass mount flags into the VFS for an existing mount.
     * Take care of it here and exit.
     */
    if (opt_find (o, "remount")) {
        if (opt_check_allowed_cslist (o, "ro,rw,aname,remount"))
            msg_exit ("-oremount can only be used with ro,rw (got %s)", opt_string (o));
        _diod_remount (o, spec, dir, vopt, fopt);
        goto done;
    }

    /* Ensure uname and access are set, and to diod-compatible values.
     * The uname user becomes the euid which will be used by munge auth.
     */
    _parse_uname_access (o);
    diod_become_user (opt_find (o, "uname"), 0, 0);

    /* We require -otrans=fd because auth occurs in user space, then live fd
     * is passed to the kernel via -orfdno,wfdno.
     */
    if (!opt_find (o, "trans"))
        opt_add (o, "trans=fd");
    else if (!opt_find (o, "trans=fd"))
        msg_exit ("only -otrans=fd transport is supported");

    /* Set msize if not already set.  Validate it later.
     */
    if (!opt_find (o, "msize"))
        opt_add (o, "msize=%d", DIOD_DEFAULT_MSIZE);

    /* Only .L version is supported.
     */
    if (!opt_find (o, "version"))
        opt_add (o, "version=9p2000.L");
    else if (!opt_find (o, "version=9p2000.L"))
        msg_exit ("only -oversion=9p2000.L is supported (little p, big L)");

    /* Set debug level.
     */
    if (!opt_find (o, "debug"))
        opt_add (o, "debug=0");

    /* Server is on an inherited file descriptor.
     * For testing, we start server on a socketpair duped to fd 0.
     */
    if (opt_find (o, "rfdno") || opt_find (o, "wfdno")) {
        int rfd, wfd;

        if (!opt_scan (o, "rfdno=%d", &rfd) || !opt_scan (o, "wfdno=%d", &wfd))
            msg_exit ("-orfdno,wfdno must be used together");
        if (rfd != wfd)    
            msg_exit ("-orfdno,wfdno must have same value");
        if (opt_find (o, "jobid"))
            msg_exit ("jobid cannot be used with -orfdno,wfdno");
        sfd = rfd;
        nopt = 1; /* force no mtab */

    /* Server port was specified.
     * Presume there is no need to contact diodctl for port.
     */
    } else if ((port = opt_find (o, "port"))) {
        hostlist_iterator_t hi;
        char *host;

        if (opt_find (o, "jobid"))
            msg_exit ("-ojobid cannot be used with -oport");
        if (!(hi = hostlist_iterator_create (hl)))
            msg_exit ("out of memory");
        while ((host = hostlist_next (hi)) && sfd < 0) {
            if (vopt)
                msg ("trying to connect to host %s port %s", host, port);
            sfd = diod_sock_connect (host, port, 1, 0);
        }
        hostlist_iterator_destroy (hi);
        if (sfd < 0)
            msg_exit ("could not contact diod server(s)");
        opt_delete (o, "port");
        opt_add (o, "rfdno=%d", sfd);
        opt_add (o, "wfdno=%d", sfd);

    /* Try diodctl server on each host until one responds.
     * Negotiate a port to connect to based on user and jobid.
     */
    } else {
        hostlist_iterator_t hi;
        char *jobid, *host; 

        jobid = opt_find (o, "jobid");
        if (!(hi = hostlist_iterator_create (hl)))
            msg_exit ("out of memory");
        while ((host = hostlist_next (hi)) && sfd < 0) {
            if (vopt)
                msg ("requesting diod port from %s", host);
            if (ctl_query (host, jobid, &port, NULL) == 0) {
                sfd = diod_sock_connect (host, port, 1, 0);
                free (port);
            }
        }
        hostlist_iterator_destroy (hi);
        if (sfd < 0)
            msg_exit ("failed to establish connection with server");
        opt_add (o, "rfdno=%d", sfd);
        opt_add (o, "wfdno=%d", sfd);
        if (jobid)
            opt_delete (o, "jobid");
    }

    /* Perform the mount here.
     * After sfd is passed to the kernel, we close it here.
     */
    _diod_mount (o, sfd, spec, dir, vopt, fopt, nopt);
    (void)close (sfd);

done:
    if (hl)
        hostlist_destroy (hl);
    opt_destroy (o);
    exit (0);
}

static hostlist_t
_parse_spec (char *spec, Opt o)
{
    char *host, *aname;
    hostlist_t hl;

    if (!(host = strdup (spec)))
        msg_exit ("out of memory");
    if ((aname = strchr (host, ':')))
        *aname++ = '\0';
    if (strlen (host) == 0)
        msg_exit ("no host specified");
    if (!aname || strlen (aname) == 0)
        aname = opt_find (o, "aname");
    else if (!opt_add (o, "aname=%s", aname))
        msg_exit ("you cannot have both -oaname and spec=host:aname");
    if (!aname || strlen (aname) == 0)
        msg_exit ("no aname specified");
    if (!(hl = hostlist_create (host)))
        msg_exit ("failed to parse hostlist");
    free (host);

    return hl;
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

/* Private or public mount?  Only allow two modes:
 *    private:        -ouname=USER,access=UID (uname could be root)
 *    public (dflt):  -ouname=root,access=user
 */
static void
_parse_uname_access (Opt o)
{
    char *uname = opt_find (o, "uname");
    int uname_uid = -1;
    char *access = opt_find (o, "access");
    int access_uid = -1;
    char *access_name = NULL;
    struct passwd *pw;
    
    if (uname) {
        if (!(pw = getpwnam (uname)))
            msg_exit ("could not look up uname='%s'", uname);
        uname_uid = pw->pw_uid;
    }
    if (access && opt_scan (o, "access=%d", &access_uid)) {
        if (!(pw = getpwuid (access_uid)))
            msg_exit ("could not look up access='%d'", access_uid);
        if (!(access_name = strdup (pw->pw_name)))
            msg_exit ("out of memory");
    }

    if (!uname && !access) {
        opt_add (o, "uname=root");        
        opt_add (o, "access=user");

    } else if (uname && !access) {
        if (uname_uid == 0)
            opt_add (o, "access=user");
        else
            opt_add (o, "access=%d", uname_uid);

    } else if (!uname && access) {
        if (strcmp (access, "user") == 0)
            opt_add (o, "uname=root");
        else if (access_name) /* access=<uid> */
            opt_add (o, "uname=%s", access_name);
        else
            msg_exit ("unsupported -oaccess=%s", access);
    } else { /* if (uname && access) */
        if (strcmp (access, "user") == 0) {
            if (uname_uid != 0)
                msg_exit ("-oaccess=user can only be used with -ouname=root");
        } else if (access_name) { /* access=<uid> */
            if (uname_uid != access_uid)
                msg_exit ("-oaccess=<uid> requires matching -ouname=<name>");
        } else
            msg_exit ("unsupported -oaccess=%s", access);
    } 
    if (access_name)
        free (access_name);
}

typedef struct {
    char *opt;
    unsigned long flag;
} map_t;

static map_t setopt[] = {
    { "dirsync",        MS_DIRSYNC},
    { "noatime",        MS_NOATIME},
    { "nodev",          MS_NODEV},
    { "nodiratime",     MS_NODIRATIME},
    { "noexec",         MS_NOEXEC},
    { "nosuid",         MS_NOSUID},
    { "ro",             MS_RDONLY },
    { "relatime",       MS_RELATIME},
    { "remount",        MS_REMOUNT},
    { "silent",         MS_SILENT},
    { "strictatime",    MS_STRICTATIME},
    { "sync",           MS_SYNCHRONOUS},
};

static map_t clropt[] = {
    { "atime",          MS_NOATIME},
    { "dev",            MS_NODEV},
    { "diratime",       MS_NODIRATIME},
    { "exec",           MS_NOEXEC},
    { "suid",           MS_NOSUID},
    { "rw",             MS_RDONLY },
    { "norelatime",     MS_RELATIME},
    { "nostrictatime",  MS_STRICTATIME},
    { "nosync",         MS_SYNCHRONOUS},
};

static void
_getflags (Opt o, unsigned long *flags)
{
    int i;

    for (i = 0; i < sizeof (setopt) / sizeof (map_t); i++) {
        if (opt_find (o, setopt[i].opt)) {
            *flags |= setopt[i].flag;            
            opt_delete (o, setopt[i].opt);
        }
    }
    for (i = 0; i < sizeof (clropt) / sizeof (map_t); i++) {
        if (opt_find (o, clropt[i].opt)) {
            *flags &= ~clropt[i].flag;            
            opt_delete (o, clropt[i].opt);
        }
    }
}

static void
_diod_remount (Opt o, char *spec, char *dir, int vopt, int fopt)
{
    char *options = opt_string (o);
    unsigned long mountflags = 0;

    _getflags (o, &mountflags);
    if (vopt)
        msg ("mount %s %s -o%s", spec, dir, options);
    if (!fopt)
        _mount (spec, dir, mountflags, NULL);
}

static int
_update_mtab (char *options, char *spec, char *dir)
{
    uid_t saved_euid = geteuid ();
    FILE *f;
    int ret = 0;
    struct mntent mnt;

    mnt.mnt_fsname = spec;
    mnt.mnt_dir = dir;
    mnt.mnt_type = "diod";
    mnt.mnt_opts = options;
    mnt.mnt_freq = 0;
    mnt.mnt_passno = 0;

    if (seteuid (0) < 0) {
        err ("failed to set effective uid to root");
        goto done;
    }
    if (!(f = setmntent (_PATH_MOUNTED, "a"))) {
        err (_PATH_MOUNTED);
        goto done;
    }
    if (addmntent (f, &mnt) != 0) {
        msg ("failed to add entry to %s", _PATH_MOUNTED);
        endmntent (f);
        goto done;
    }
    endmntent (f);
    if (seteuid (saved_euid) < 0) {
        err ("failed to restore effective uid to %d", saved_euid);
        goto done;
    }
    ret = 1;
done:
    return ret;
}

static void
_diod_mount (Opt o, int fd, char *spec, char *dir, int vopt, int fopt, int nopt)
{
    char *options, *options9p, *aname;
    int msize;
    Npcfsys *fs;
    unsigned long mountflags = 0;

    options = opt_string (o);
    _getflags (o, &mountflags);
    options9p = opt_string (o); /* after mountflags removed from opt list */

    if (!(aname = opt_find (o, "aname")))
        msg_exit ("aname is not set"); /* can't happen */
    if (!opt_scan (o, "msize=%d", &msize) || msize < P9_IOHDRSZ)
        msg_exit ("msize must be set to integer >= %d", P9_IOHDRSZ);

    if (vopt)
        msg ("pre-authenticating connection to server");
    if (!(fs = npc_mount (fd, msize, aname, diod_auth_client_handshake)))
        err_exit ("npc_mount");
    npc_umount2 (fs);
    if (vopt)
        msg ("mount -t 9p %s %s -o%s", spec, dir, options);
    if (!fopt)
        _mount (spec, dir, mountflags, options9p);
    npc_finish (fs);
    if (!nopt) {
        if (!_update_mtab (options, spec, dir))
            msg_exit ("failed to update /etc/mtab");
    }
    free (options);
    free (options9p);
}


/* Mount 9p file system [source] on [target] with options [data].
 * Swap effective (user) and real (root) uid's for the duration of mount call.
 * Exit on error.
 */
static void
_mount (const char *source, const char *target, unsigned long mountflags,
        const void *data)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (mount (source, target, "9p", mountflags, data))
        err_exit ("mount");
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
