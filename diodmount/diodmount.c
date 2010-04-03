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
#define _GNU_SOURCE     /* asprintf */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <mntent.h>
#if HAVE_LIBMUNGE
#define GPL_LICENSED 1
#include <munge.h>
#endif
#include <pwd.h>

#include "npfs.h"

#include "diod_log.h"
#include "diod_upool.h"

#include "opt.h"

#define OPTIONS "u:nx:o:O:Tvd"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"mount-user",      required_argument,   0, 'u'},
    {"no-mtab",         no_argument,         0, 'n'},
    {"list-exports",    required_argument,   0, 'x'},
    {"diod-option",     required_argument,   0, 'o'},
    {"diodctl-option",  required_argument,   0, 'O'},
    {"test-opt",        no_argument,         0, 'T'},
    {"verbose",         no_argument,         0, 'v'},
    {"direct",          no_argument,         0, 'd'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void  _parse_device     (char *device, char **anamep, char **ipp);
static void  _diod_mount       (char *ip, char *dir, char *aname, char *port,
                                char *opts, int vopt);
static void  _diodctl_mount    (char *ip, char *dir, char *opts, int vopt);
static char *_diodctl_getport  (char *dir);
static int   _diodctl_listexp  (char *dir);
static void  _umount           (const char *target);
static int   _update_mtab      (char *dev, char *dir, char *opt);
static char *_name2ip          (char *name);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodmount -x,--list-exports HOST\n"
"   or: diodmount [OPTIONS] device directory\n"
"   -u,--mount-user USER          set up the mount so only USER can use it\n"
"   -n,--no-mtab                  do not update /etc/mtab\n"
"   -o,--diod-options OPT[,...]   additional mount options for diod\n"
"   -O,--diodctl-option OPT[,...] additional mount options for diodctl\n"
"   -v,--verbose                  be verbose about what is going on\n"
"   -d,--direct                   mount diod directly (requires -o port=N)\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *device, *dir, *aname, *ip;
    int c;
    struct stat sb;
    int nopt = 0;
    int vopt = 0;
    int dopt = 0;
    char *uopt = NULL;
    char *xopt = NULL;
    char *oopt = NULL;
    char *Oopt = NULL;

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
            case 'x':   /* --list-exports HOST */
                xopt = optarg;
                break;
            case 'o':   /* --diod-option OPT[,OPT]... */
                oopt = optarg;
                break;
            case 'O':   /* --diodctl-option OPT[,OPT]... */
                Oopt = optarg;
                break;
            case 'T':   /* --test-opt [hidden test option] */
                opt_test ();
                exit (0);
            case 'v':   /* --verbose */
                vopt = 1;
                break;
            case 'd':   /* --direct */
                dopt = 1;
                break;
            default:
                usage ();
        }
    }

    /* If just listing exports, take care of it here and exit.
     */
    if (xopt) {
        char tmpl[] = "/tmp/diodmount.XXXXXX";
        char *ip = _name2ip (xopt);
        int ret;

        if (!(dir = mkdtemp (tmpl)))
            err_exit ("failed to create temporary directory for mount");
        _diodctl_mount (ip, dir, Oopt, vopt);
        free (ip);
        unlink (dir);
        ret = _diodctl_listexp (dir);
        _umount (dir);
        exit (ret);
    }

    if (optind != argc - 2)
        usage ();
    device = argv[optind++];
    dir = argv[optind++];

    if (stat (dir, &sb) < 0)
        err_exit (dir);
    if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s: not a directory", dir);

    if (geteuid () != 0)
        msg_exit ("effective uid is not root");

    /* If not mounting as root, seteuid to the user whose munge
     * credentials will be used for the initial attach.
     * We restore root privileges later.
     */
    if (uopt)
        diod_become_user (uopt, 0, 0);

    _parse_device (device, &aname, &ip);

    if (dopt)
        _diod_mount (ip, dir, aname, NULL, oopt, vopt);
    else if (!strcmp (aname, "/diodctl")) {
        _diodctl_mount (ip, dir, Oopt, vopt);
    } else {
        char *port;

        _diodctl_mount (ip, dir, Oopt, vopt);
        port = _diodctl_getport (dir);
        if (!port)
            exit (1); 
        _umount (dir);
        _diod_mount (ip, dir, aname, port, oopt, vopt);
        free (port);
    }

    free (aname);
    free (ip);

    if (!nopt) {
        if (!_update_mtab (device, dir, MNTOPT_DEFAULTS)) {
            _umount (dir);
            exit (1);
        }
    }

    exit (0);
}

static int
_update_mtab (char *dev, char *dir, char *opt)
{
    uid_t saved_euid = geteuid ();
    FILE *f;
    int ret = 0;
    struct mntent mnt;

    mnt.mnt_fsname = dev;
    mnt.mnt_dir = dir;
    mnt.mnt_type = "diod";
    mnt.mnt_opts = opt;
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
_mount (const char *source, const char *target, const void *data)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (mount (source, target, "9p", 0, data))
        err_exit ("mount");
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

static void
_umount (const char *target)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (umount (target) < 0)
        err_exit ("umount %s", target);
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

static char *
_create_mungecred (char *payload)
{
    char *mungecred;
#if HAVE_LIBMUNGE
    int paylen = payload ? strlen(payload) : 0;
    munge_ctx_t ctx;
    munge_err_t err;

    if (!(ctx = munge_ctx_create ()))
        msg_exit ("out of memory");
    err = munge_encode (&mungecred, ctx, payload, paylen);
    if (err != EMUNGE_SUCCESS)
        msg_exit ("munge_encode: %s", munge_strerror (err));
    munge_ctx_destroy (ctx);
#else
    struct passwd *pwd;

    if (!(pwd = getpwuid (geteuid ())))
        msg_exit ("could not look up uid %d", geteuid ());
    if (!(mungecred = strdup (pwd->pw_name)))
        msg_exit ("out of memory");
#endif
    return mungecred;
}

static void
_diod_mount (char *ip, char *dir, char *aname, char *port, char *opts, int vopt)
{
    Opt o = opt_create ();
    char *options, *cred;

    opt_add (o, "uname=%s", (cred = _create_mungecred (NULL)));
    if (port)
        opt_add (o, "port=%s", port);
    opt_add (o, "aname=%s", aname);
    opt_add (o, "msize=65560");
    opt_add (o, "version=9p2000.L");
    opt_add (o, "debug=0x1"); /* errors */
    if (geteuid () != 0)
        opt_add (o, "access=%d", geteuid ());
    if (opts)
        opt_add_cslist_override (o, opts);
    free (cred);
    options = opt_string (o);
    opt_destroy (o);

    if (vopt)
        msg ("mount %s %s -o%s", ip, dir, options);
    _mount (ip, dir, options);
    free (options);
}

static void
_diodctl_mount (char *ip, char *dir, char *opts, int vopt)
{
    Opt o = opt_create ();
    char *options, *cred;

    opt_add (o, "uname=%s", (cred = _create_mungecred (NULL)));
    opt_add (o, "port=10005");
    opt_add (o, "aname=/diodctl");
    opt_add (o, "debug=0x1"); /* errors */
    if (geteuid () != 0)
        opt_add (o, "access=%d", geteuid ());
    if (opts)
        opt_add_cslist_override (o, opts);
    free (cred);
    options = opt_string (o);
    opt_destroy (o);

    if (vopt)
        msg ("mount %s %s -o%s", ip, dir, options);
    _mount (ip, dir, options);
    free (options);
}

static int
_diodctl_listexp (char *dir)
{
    char buf[PATH_MAX];
    char *exports = NULL;
    FILE *f;
    int ret = 0; 
   
    /* read /exports to stdout */
    if (asprintf (&exports, "%s/exports", dir) < 0) {
        msg ("out of memory");
        goto done;
    }
    if (!(f = fopen (exports, "r"))) {
        err ("error opening %s", exports);
        goto done;
    }
    while (fgets (buf, sizeof (buf), f))
        printf ("%s", buf);
    if (ferror (f)) {
        errn (ferror (f), "error reading %s", exports);
        fclose (f);
        goto done;
    }
    fclose (f);
    ret = 1;
done:
    if (exports)
        free (exports);
    return ret;
}

static char *
_diodctl_getport (char *dir)
{
    char *ctl = NULL, *server = NULL;
    FILE *f;
    int port, n;
    char *ret = NULL;
   
    /* poke /ctl to trigger creation of new server (if needed) */ 
    if (asprintf (&ctl, "%s/ctl", dir) < 0) {
        msg ("out of memory");
        goto done;
    }
    if (!(f = fopen (ctl, "w"))) {
        err ("error opening %s", ctl);
        goto done;
    }
    if (fprintf (f, "new") < 0) {
        if (errno == EPERM)
            err ("requesting private server");
        else
            err ("error writing to %s", ctl);
        fclose (f);
        goto done;
    }
    if (fclose (f) != 0) {
        if (errno == EPERM)
            err ("requesting private server");
        else
            err ("error writing to %s", ctl);
        goto done;
    }

    /* read port from /server */
    if (asprintf (&server, "%s/server", dir) < 0) {
        msg ("out of memory");
        goto done;
    }
    if (!(f = fopen (server, "r"))) {
        err ("error opening %s", server);
        goto done;
    }
    if ((n = fscanf (f, "%d", &port)) != 1) {
        if (n < 0)
            err ("error reading from %s", server);
        else
            msg ("failed to read port number from %s", server);
        fclose (f);
        goto done;
    }
    fclose (f);
    if (!(ret = malloc (NI_MAXSERV))) {
        msg ("out of memory");
        goto done;
    }
    snprintf (ret, NI_MAXSERV, "%d", port); 
done:
    if (ctl)
        free (ctl);
    if (server)
        free (server);
    return ret;
}

/* Obtain the IP address for 'name' and write it into 'ip' which is of
 * size 'len'.
 */
static char *
_name2ip (char *name)
{
    char buf[NI_MAXHOST], *ip;
    struct addrinfo hints, *res;
    int error;

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((error = getaddrinfo (name, NULL, &hints, &res)))
        err_exit ("getaddrinfo: %s: %s", name, gai_strerror(error));
    if (!res)
        err_exit ("%s has no address info", name);

    /* FIXME: we take the first entry in the res array.
     * Should we loop through them and take the first one that works?
     */
    if (getnameinfo (res->ai_addr, res->ai_addrlen, buf, sizeof (buf),
                     NULL, 0, NI_NUMERICHOST) < 0)
        err_exit ("%s has no address", name);
    freeaddrinfo (res);
    if (!(ip = strdup (buf)))
        err_exit ("out of memory");
    return ip;
}

/* Given a "device" in host:aname format, parse out the host (converting
 * to ip address for in-kernel v9fs which can't handle hostnames),
 * and the aname.  Exit on error.
 */
static void
_parse_device (char *device, char **anamep, char **ipp)
{
    char *host, *ip, *p, *aname;

    if (!(host = strdup (device)))
        msg_exit ("out of memory");
    if (!(p = strchr (host, ':')))
        msg_exit ("device is not in host:directory format");
    *p++ = '\0';
    if (!(aname = strdup (p)))
        msg_exit ("out of memory");
    ip = _name2ip (host);
    free (host);
    
    *ipp = ip;
    *anamep = aname;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
