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
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
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

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "list.h"
#include "hostlist.h"
#include "diod_log.h"
#include "diod_sock.h"
#include "diod_auth.h"
#include "opt.h"

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

#define DIOD_DEFAULT_MSIZE 65536
static uid_t _uname2uid (char *uname);
static void _diod_mount (Opt o, int rfd, int wfd, char *spec, char *dir,
                         int vopt, int fopt, int nopt);
static void _diod_remount (Opt o, char *spec, char *dir, int vopt, int fopt);
static void _verify_mountpoint (char *path);
static void _parse_uname_access (Opt o);
static char *_parse_spec (char *spec, Opt o);
static void _mount (const char *source, const char *target,
                    unsigned long mountflags, const void *data);

static void
usage (void)
{
    fprintf (stderr,
"Usage: mount.diod [OPTIONS] host[:aname] [directory]\n"
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
    char *spec, *host;
    char *nspec = NULL;
    int c, i;
    int nopt = 0;
    int vopt = 0;
    int fopt = 0;
    int rfd = -1, wfd = -1;
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
                opt_addf (o, "%s", optarg);
                break;
            default:
                usage ();
        }
    }
    if (optind != argc - 2)
        usage ();
    spec = argv[optind++];
    dir = argv[optind++];
    host = _parse_spec (spec, o);

    if (geteuid () != 0)
        msg_exit ("you must be root");

    _verify_mountpoint (dir);

    /* Remount - only pass mount flags into the VFS for an existing mount.
     * Take care of it here and exit.
     */
    if (opt_find (o, "remount")) {
        if (opt_check_allowed_csv (o, "ro,rw,aname,remount"))
            msg_exit ("-oremount can only be used with ro,rw");
        _diod_remount (o, spec, dir, vopt, fopt);
        goto done;
    }

    /* Ensure uname and access are set, and to diod-compatible values.
     * The uname user becomes the euid which will be used by munge auth.
     */
    _parse_uname_access (o);
     if (seteuid (_uname2uid (opt_find (o, "uname"))) < 0)
        err_exit ("seteuid");

    /* We require -otrans=fd because auth occurs in user space, then live fd
     * is passed to the kernel via -orfdno,wfdno.
     */
    if (!opt_find (o, "trans"))
        opt_addf (o, "trans=%s", "fd");
    else if (!opt_find (o, "trans=fd"))
        msg_exit ("only -otrans=fd transport is supported");

    /* Set msize if not already set.  Validate it later.
     */
    if (!opt_find (o, "msize"))
        opt_addf (o, "msize=%d", DIOD_DEFAULT_MSIZE);

    /* Only .L version is supported.
     */
    if (!opt_find (o, "version"))
        opt_addf (o, "version=%s", "9p2000.L");
    else if (!opt_find (o, "version=9p2000.L"))
        msg_exit ("only -oversion=9p2000.L is supported (little p, big L)");

    /* Set debug level.
     */
    if (!opt_find (o, "debug"))
        opt_addf (o, "debug=%d", 0x1); /* send errors to dmesg */

    /* Set rwdepth (number of concurrent reads with buffer > msize).
     * N.B. this option is not upstream yet but unknown options are ignored.
     */
    if (!opt_find (o, "rwdepth"))
        opt_addf (o, "rwdepth=%d", 1);

    /* Server is on an inherited file descriptor.
     * For testing, we start server on a socketpair duped to fd 0.
     */
    if (opt_find (o, "rfdno") || opt_find (o, "wfdno")) {
        if (!opt_scanf (o, "rfdno=%d", &rfd) || !opt_scanf (o, "wfdno=%d",&wfd))
            msg_exit ("-orfdno,wfdno must be used together");
        nopt = 1; /* force no mtab */

    /* Connect to server on IANA port (or user-specified) and host.
     */
    } else {
        char *port = opt_find (o, "port");
        hostlist_iterator_t hi;
        hostlist_t hl; 
        char *h;

        if (!port)
            port = "564";
        if (!(hl = hostlist_create (host)))
            msg_exit ("error parsing host string: %s", host);
        if (!(hi = hostlist_iterator_create (hl)))
            msg_exit ("out of memory");
        while ((h = hostlist_next (hi))) {
            if (vopt)
                msg ("trying to connect to %s:%s", h, port);
            if ((rfd = diod_sock_connect (h, port, DIOD_SOCK_QUIET)) >= 0)
                break;
        }
        if (h) { /* create new 'spec' string identifying successful host */
            char *p = strchr (spec , ':');
            int len = strlen (h) + (p ? strlen (p) : 0) + 1;

            if (!(nspec = malloc (len)))
                msg_exit ("out of memory");
            snprintf (nspec, len, "%s%s", h, p ? p : "");
        }
        hostlist_destroy (hl);
        if (rfd < 0)
            msg_exit ("could not connect to server(s), giving up");
        wfd = rfd;
        
        opt_delete (o, "port");
        opt_addf (o, "rfdno=%d", rfd);
        opt_addf (o, "wfdno=%d", wfd);
    }

    NP_ASSERT (opt_find (o, "trans=fd"));
    NP_ASSERT (opt_scanf (o, "msize=%d", &i));
    NP_ASSERT (opt_find (o, "version=9p2000.L"));
    NP_ASSERT (opt_scanf (o, "debug=%d", &i) || opt_scanf (o, "debug=%x", &i));
    NP_ASSERT (opt_scanf (o, "wfdno=%d", &i) && opt_scanf (o, "rfdno=%d", &i));
    NP_ASSERT ((opt_find (o, "access=user") && opt_find(o, "uname=root"))
         || (opt_scanf (o, "access=%d", &i) && opt_find(o, "uname")));

    NP_ASSERT (!opt_find (o, "port"));

    _diod_mount (o, rfd, wfd, nspec ? nspec : spec, dir, vopt, fopt, nopt);

done:
    opt_destroy (o);
    exit (0);
}

static uid_t
_uname2uid (char *uname)
{
    struct passwd *pw;

    if (!(pw = getpwnam (uname)))
        msg_exit ("could not look up uname='%s'", uname);
    return pw->pw_uid;
}

static char *
_parse_spec (char *spec, Opt o)
{
    char *host, *aname;

    if (!(host = strdup (spec)))
        msg_exit ("out of memory");
    if ((aname = strchr (host, ':')))
        *aname++ = '\0';
    if (strlen (host) == 0)
        msg_exit ("no host specified");
    if (!aname || strlen (aname) == 0)
        ; /* aname = opt_find (o, "aname"); */
    else if (!opt_addf (o, "aname=%s", aname))
        msg_exit ("you cannot have both -oaname and spec=host:aname");

    return host;
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
    if (access && opt_scanf (o, "access=%d", &access_uid)) {
        if (!(pw = getpwuid (access_uid)))
            msg_exit ("could not look up access='%d'", access_uid);
        if (!(access_name = strdup (pw->pw_name)))
            msg_exit ("out of memory");
    }

    if (!uname && !access) {
        opt_addf (o, "uname=%s", "root");        
        opt_addf (o, "access=%s", "user");

    } else if (uname && !access) {
        if (uname_uid == 0)
            opt_addf (o, "access=%s", "user");
        else
            opt_addf (o, "access=%d", uname_uid);

    } else if (!uname && access) {
        if (strcmp (access, "user") == 0)
            opt_addf (o, "uname=%s", "root");
        else if (access_name) /* access=<uid> */
            opt_addf (o, "uname=%s", access_name);
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
    { "noatime",        MS_NOATIME},
    { "nodev",          MS_NODEV},
    { "nodiratime",     MS_NODIRATIME},
    { "noexec",         MS_NOEXEC},
    { "nosuid",         MS_NOSUID},
    { "ro",             MS_RDONLY },
    { "remount",        MS_REMOUNT},
    { "sync",           MS_SYNCHRONOUS},
};

static map_t clropt[] = {
    { "atime",          MS_NOATIME},
    { "dev",            MS_NODEV},
    { "diratime",       MS_NODIRATIME},
    { "exec",           MS_NOEXEC},
    { "suid",           MS_NOSUID},
    { "rw",             MS_RDONLY },
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
    char *options = opt_csv (o);
    unsigned long mountflags = 0;

    _getflags (o, &mountflags);
    if (vopt)
        msg ("mount %s %s -o%s", spec, dir, options);
    if (!fopt)
        _mount (spec, dir, mountflags, NULL);

    free (options);
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
_diod_mount (Opt o, int rfd, int wfd, char *spec, char *dir, int vopt,
             int fopt, int nopt)
{
    char *options, *options9p, *aname, *uname;
    uid_t uid;
    int msize;
    Npcfsys *fs;
    Npcfid *afid, *root;
    unsigned long mountflags = 0;

    options = opt_csv (o);
    _getflags (o, &mountflags);
    options9p = opt_csv (o); /* after mountflags removed from opt list */

    if (!(uname = opt_find (o, "uname")))
        msg_exit ("uname is not set"); /* can't happen */
    uid = _uname2uid (uname);
    aname = opt_find (o, "aname"); /* can be null */
    if (!opt_scanf (o, "msize=%d", &msize) || msize < P9_IOHDRSZ)
        msg_exit ("msize must be set to integer >= %d", P9_IOHDRSZ);

    if (vopt)
        msg ("pre-authenticating connection to server");
    if (!(fs = npc_start (rfd, wfd, msize, 0)))
        errn_exit (np_rerror (), "version");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "auth");
    if (!(root = npc_attach (fs, afid, aname, uid))) {
        errn (np_rerror (), "attach");
        if (afid)
            (void)npc_clunk (afid);
        exit (1);
    }
    if (afid && npc_clunk (afid) < 0)
        errn_exit (np_rerror (), "clunk afid");
    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "clunk root");
    if (vopt)
        msg ("mount -t 9p %s %s -o%s", spec, dir, options);
    if (!fopt) {
        /* kernel wants non-blocking */
        if (fcntl (rfd, F_SETFL, O_NONBLOCK) < 0)
            err_exit ("setting O_NONBLOCK flag on rfd=%d", rfd);
        if (fcntl (wfd, F_SETFL, O_NONBLOCK) < 0)
            err_exit ("setting O_NONBLOCK flag on wfd=%d", wfd);
        _mount (spec, dir, mountflags, options9p);
    }
    npc_finish (fs); /* closes fd */

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
    if (mount (source, target, "9p", mountflags, data) < 0)
        err_exit ("mount");
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
