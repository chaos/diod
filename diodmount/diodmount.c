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
#include <sys/wait.h>
#include <netdb.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <mntent.h>
#include <ctype.h>
#if HAVE_MUNGE
#define GPL_LICENSED 1
#include <munge.h>
#endif
#include <pwd.h>
#include <libgen.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "npfs.h"
#include "list.h"
#include "diod_log.h"
#include "diod_upool.h"
#include "opt.h"

typedef struct {
    List exports;
    char *port;
} query_t;

#define OPTIONS "au:no:O:TvdfDj:"
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
    {"direct",          no_argument,         0, 'd'},
    {"fake-mount",      no_argument,         0, 'f'},
    {"create-directories", no_argument,      0, 'D'},
#if HAVE_MUNGE
    {"jobid",           required_argument ,  0, 'j'},
#endif
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static query_t *_diodctl_query (char *ip, char *opts, int vopt, int getparent,
                                char *payload);
static void  _free_query       (query_t *r);
static void  _parse_device     (char *device, char **anamep, char **ipp);
static void  _diod_mount       (char *ip, char *dir, char *aname, char *port,
                                char *opts, int vopt, int fopt);
static void  _umount           (const char *target);
static int   _update_mtab      (char *dev, char *dir, char *opt);
static void _update_mtab_entries (char *host, char *root, List dirs, char *opt);
static void _verify_mountpoint (char *path, int Dopt);
static void _verify_mountpoints (List dirs, char *root, int Dopt);
static char *_name2ip          (char *name);

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
"   -d,--direct                   mount diod directly (requires -o port=N)\n"
"   -f,--fake-mount               do everything but the actual diod mount(s)\n"
"   -D,--create-directories       create mount directories as needed\n"
#if HAVE_MUNGE
"   -j,--jobid STR                set job id string\n"
#endif
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *dir = NULL, *ip;
    char *aname = NULL;
    char *device;
    int c;
    int aopt = 0;
    int nopt = 0;
    int vopt = 0;
    int dopt = 0;
    int fopt = 0;
    int Dopt = 0;
    char *uopt = NULL;
    char *oopt = NULL;
    char *Oopt = NULL;
    char *jopt = NULL;

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
            case 'd':   /* --direct */
                dopt = 1;
                break;
            case 'f':   /* --fake-mount */
                fopt = 1;
                break;
            case 'T':   /* --test-opt */
                opt_test ();
                exit (0);
#if HAVE_MUNGE
            case 'j':   /* --jobid STR */
                jopt = optarg;
                break;
#endif
            default:
                usage ();
        }
    }
    if (aopt && dopt)
        msg_exit ("--all and --direct are incompatible options");
    if (aopt) {/* Usage: diodmount [options] -a hostname [dir] */
        if (optind != argc - 1 && optind != argc - 2)
            usage ();
        device = argv[optind++];
        if (optind != argc)
            dir = argv[optind++];
        ip = _name2ip (device);
    } else {          /* Usage: diodmount [options] hostname:/aname directory */
        if (optind != argc - 2)
            usage ();
        device = argv[optind++];
        dir = argv[optind++];
        _parse_device (device, &aname, &ip);
    }

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
        query_t *ctl = _diodctl_query (ip, Oopt, vopt, aopt, jopt);
        ListIterator itr;
        char *el;

        if (aopt)
            _verify_mountpoints (ctl->exports, dir, Dopt);
        if (!(itr = list_iterator_create (ctl->exports)))
            msg_exit ("out of memory");
        while ((el = list_next (itr))) {
            char path[PATH_MAX];

            snprintf (path, sizeof (path), "%s%s", dir ? dir : "", el);
            _diod_mount (ip, path, el, ctl->port, oopt, vopt, fopt);
        }
        list_iterator_destroy (itr);

        if (!nopt)
            _update_mtab_entries (device, dir, ctl->exports, MNTOPT_DEFAULTS);
        _free_query (ctl);

    /* Mount one file system, obtaining port number from diodctl.
     * If -d option, skip diodctl and hope the user specified -o port=N.
     * If -D option, create mount point as needed.
     */
    } else {
        query_t *ctl;

        _verify_mountpoint (dir, Dopt);

        if (!dopt)
            ctl = _diodctl_query (ip, Oopt, vopt, 1, jopt);
        _diod_mount (ip, dir, aname, dopt ? NULL : ctl->port, oopt, vopt, fopt);
        if (!dopt)
            _free_query (ctl);

        if (!nopt) {
            if (!_update_mtab (device, dir, MNTOPT_DEFAULTS)) {
                _umount (dir);
                exit (1);
            }
        }
    }

    free (ip);
    if (aname)
        free (aname);
    exit (0);
}

/* Create a directory, recursively creating parents as needed.
 * Return success (0) if the directory already exists, or creation was
 * successful, (-1) on failure.
 */
static int
_mkdir_p (char *path, mode_t mode)
{
    struct stat sb;
    char *cpy;
    int res = 0;

    if (stat (path, &sb) == 0) {
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }
    if (!(cpy = strdup (path))) {
        errno = ENOMEM;
        return -1;
    }
    res = _mkdir_p (dirname (cpy), mode);
    free (cpy);
    if (res == 0)
        res = mkdir (path, mode);

    return res;
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
        if (_mkdir_p (path, 0755) < 0)
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

/* Add an entry for [dev] mounted on [dir] to /etc/mtab.
 * Return success (1) or failure (0).
 */
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

/* Run a list of directories through _update_mtab ().
 * Mount point path is assumed to be same as that in the "device" (HOST:/dir).
 * If root is non-null, prepend it to the mount point path.
 */
static void
_update_mtab_entries (char *host, char *root, List dirs, char *opt)
{
    char dev[PATH_MAX + NI_MAXSERV + 1];
    char path[PATH_MAX];
    ListIterator itr;
    char *el;

    if (!(itr = list_iterator_create (dirs)))
        msg_exit ("out of memory");
    while ((el = list_next (itr))) {
        snprintf (path, sizeof(path), "%s%s", root ? root : "", el);
        snprintf(dev, sizeof (dev), "%s:%s", host, el);
        (void)_update_mtab (dev, path, opt);
    }
    list_iterator_destroy (itr);
}

/* Mount 9p file system [source] on [target] with options [data].
 * Swap effective (user) and real (root) uid's for the duration of mount call.
 * Exit on error.
 */
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

/* Mount file system [target].
 * Swap effective (user) and real (root) uid's for the duration of umount call.
 * Exit on error.
 */
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

/* Unshare file system name space.
 * Swap effective (user) and real (root) uid's for the duration of unshare call.
 * Exit on error.
 */
static void
_unshare (void)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (unshare (CLONE_NEWNS) < 0)
        err_exit ("failed to unshare name space");
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

#if HAVE_MUNGE
/* Create a munge credential for the effective uid and return it as a string
 * that must be freed by the caller.
 */
static char *
_create_mungecred (char *payload)
{
    char *mungecred;
    int paylen = payload ? strlen(payload) + 1 : 0;
    munge_ctx_t ctx;
    munge_err_t err;

    if (!(ctx = munge_ctx_create ()))
        msg_exit ("out of memory");
    err = munge_encode (&mungecred, ctx, payload, paylen);
    if (err != EMUNGE_SUCCESS)
        msg_exit ("munge_encode: %s", munge_strerror (err));
    munge_ctx_destroy (ctx);
    return mungecred;
}
#else
static char *
_create_user (void)
{
    struct passwd *pwd;
    char *u;

    if (!(pwd = getpwuid (geteuid ())))
        msg_exit ("could not look up uid %d", geteuid ());
    if (!(u = strdup (pwd->pw_name)))
        msg_exit ("out of memory");
    return u;
}
#endif

/* Mount diod file system specified by [ip], [port], [aname] on [dir].
 * Default mount options can be overridden by a comma separated list [opts].
 * If [vopt], say what we're doing before we do it.
 * If [fopt], don't actually perform the mount.
 * Exit on error.
 */
static void
_diod_mount (char *ip, char *dir, char *aname, char *port, char *opts,
             int vopt, int fopt)
{
    Opt o = opt_create ();
    char *options, *cred;
#if HAVE_MUNGE
    cred = _create_mungecred (NULL);
#else
    cred = _create_user ();
#endif
    opt_add (o, "uname=%s", cred);
    if (port)
        opt_add (o, "port=%s", port);
    opt_add (o, "aname=%s", aname);
    opt_add (o, "msize=65560");
#if HAVE_DOTL
    opt_add (o, "version=9p2000.L");
#else
    opt_add (o, "version=9p2000.u");
#endif
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
    if (!fopt)
        _mount (ip, dir, options);
    free (options);
}

/* Mount diodctl file running on [ip] on [dir].
 * Default mount options can be overridden by a comma separated list [opts].
 * If [vopt], say what we're doing before we do it.
 * Exit on error.
 */
static void
#if HAVE_MUNGE
_diodctl_mount (char *ip, char *dir, char *opts, int vopt, char *payload)
#else
_diodctl_mount (char *ip, char *dir, char *opts, int vopt)
#endif
{
    Opt o = opt_create ();
    char *options, *cred;
#if HAVE_MUNGE
    cred = _create_mungecred (payload);
#else
    cred = _create_user ();
#endif
    opt_add (o, "uname=%s", cred);
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

/* Read file [path] to stdout.
 * Exit on error.
 */
static void
_cat_file (char *path)
{
    char buf[PATH_MAX];
    FILE *f;

    if (!(f = fopen (path, "r")))
        err_exit ("error opening %s", path);
    while (fgets (buf, sizeof (buf), f))
        printf ("%s%s", buf, buf[strlen (buf) - 1] == '\n' ? "" : "\n");
    if (ferror (f))
        errn_exit (ferror (f), "error reading %s", path);
    fclose (f);
}

/* Put string [s] to file.
 * Exit on error.
 */
static void
_puts_file (char *path, char *s)
{
    FILE *f;

    if (!(f = fopen (path, "w")))
        err_exit ("error opening %s", path);
    if (fprintf (f, "%s", s) < 0)
        err_exit ("error writing to %s", path);
    if (fclose (f) != 0)
        err_exit ("error writing to %s", path);
}

/* Allocate and initialize a query_t struct.
 * Exit on error.
 */
static query_t *
_alloc_query (void)
{
    query_t *r;
    
    if (!(r = malloc (sizeof (*r))))
        err_exit ("out of memory");
    if (!(r->exports = list_create ((ListDelF)free)))
        err_exit ("out of memory");
    r->port = NULL;
    return r;
}

/* Free a query_t struct.
 * This function always succeedes.
 */
static void
_free_query (query_t *r)
{
    if (r->port)
        free (r->port);
    list_destroy (r->exports);
    free (r);
}

/* Overwrite any trailing white space in string [s] with nulls.
 */
static void
_delete_trailing_whitespace (char *s)
{
    int n = strlen (s) - 1;

    while (n >= 0) {
        if (!isspace (s[n]))
            break;
        s[n--] = '\0';
    }
}

/* Helper for _diodctl_query ().  This is the parent leg of the fork,
 * responsible for reading data from pipe [fd], assigning it to query_t [r],
 * reaping the child [pid], and returning success (1) or failure (0).
 * N.B. This function cannot exit directly on error because
 * the temporary mount point has to be cleaned up.
 */
static int
_diodctl_query_parent (int fd, pid_t pid, query_t *r, int getport)
{
    int status, res = 0;
    FILE *f;
    char buf[PATH_MAX], *cpy;

    if (!(f = fdopen (fd, "r"))) {
        err ("error fdopening pipe");
        goto done;
    }
    while (fgets (buf, sizeof (buf), f)) {
        _delete_trailing_whitespace (buf);
        if (!(cpy = strdup (buf))) {
            msg ("out of memory");
            goto done;
        }
        if (getport && r->port == NULL)
            r->port = cpy;
        else if (!list_append (r->exports, cpy)) {
            msg ("out of memory");
            goto done;
        }
    }
    if (ferror (f)) {
        errn (ferror (f), "error reading pipe");
        goto done;
    }
    fclose (f);
    if (waitpid (pid, &status, 0) < 0) {
        err ("wait");
        goto done;
    }
    if (WIFEXITED (status)) {
        if (WEXITSTATUS (status) != 0) {
            err ("child exited with %d", WEXITSTATUS (status));
            goto done;
        }
    } else if (WIFSIGNALED (status)) {
        err ("child killed by signal %d", WTERMSIG (status));
        goto done;
    } else if (WIFSTOPPED (status)) {
        err ("child stopped by signal %d", WSTOPSIG (status));
        goto done;
    } else if (WIFCONTINUED (status)) {
        err ("child continued");
        goto done;
    }
    res = 1;
done:
    return res;
}

/* Interact with diodctl server to determine port number of diod server
 * and a list of exports, returned in a query_t struct that caller must free.
 * If getport is false, skip port query that triggers server creation.
 * Exit on error.
 * N.B. mount is always cleaned up because it's in a private namespace.
 */
static query_t *
_diodctl_query (char *ip, char *opts, int vopt, int getport, char *payload)
{
    char tmppath[] = "/tmp/diodmount.XXXXXX";
    char path[PATH_MAX];
    query_t *res = _alloc_query ();
    int pfd[2];
    pid_t pid;
    char *tmpdir;

    if (pipe (pfd) < 0)
        err_exit ("pipe");
    if (!(tmpdir = mkdtemp (tmppath)))
        err_exit ("failed to create temporary diodctl mount point");
    switch ((pid = fork())) {
        case -1:    /* error */
            err_exit ("fork");
        case 0:     /* child */
            close (pfd[0]);
            close (STDOUT_FILENO);
            if (dup2 (pfd[1], STDOUT_FILENO) < 0)
                err_exit ("failed to dup stdout");
            _unshare ();
#if HAVE_MUNGE
            _diodctl_mount (ip, tmpdir, opts, vopt, payload);
#else
            _diodctl_mount (ip, tmpdir, opts, vopt);
#endif
            if (getport) {
                snprintf (path, sizeof (path), "%s/ctl", tmpdir);
                _puts_file (path, "new");
                snprintf (path, sizeof (path), "%s/server", tmpdir);
                _cat_file (path);
            }
            snprintf (path, sizeof (path), "%s/exports", tmpdir);
            _cat_file (path);
            fflush (stdout);
            _umount (tmpdir);
            exit (0); 
        default:    /* parent */
            close (pfd[1]);
            if (!_diodctl_query_parent (pfd[0], pid, res, getport)) {
                _free_query (res);
                res = NULL;
            }
            break; 
    }
    if (rmdir (tmpdir) < 0)
        err_exit ("failed to remove temporary diodctl mount point");
    if (res == NULL)
        exit (1);
    return res;
}

/* Obtain the IP address for [name] and return it as a string the caller
 * must free.  Exit on error.
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
        msg_exit ("out of memory");
    return ip;
}

/* Given [device] in host:aname format, parse out the host (converting
 * to ip address for in-kernel v9fs which can't handle hostnames) and
 * storing in [ipp], and the aname, storing in [anamep].
 * Caller must free the resulting strings.
 * Exit on error.
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
