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

/* ctl.c - manipulate diodctl pseudo-files system */

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

/* Mount diodctl file running on [host] on [dir].
 * Default mount options can be overridden by a comma separated list [opts].
 * If [vopt], say what we're doing before we do it.
 * Exit on error.
 */
static void
_diodctl_mount (char *host, char *dir, char *opts, int vopt, char *opt_debug)
{
    Opt o = opt_create ();
    char *options, *dev;
    char *u = auth_mkuser ();
    int fd;

    opt_add (o, "uname=%s", u);
    if ((fd = diod_sock_connect (host, "10005", 1, 0)) < 0)
        err_exit ("connect failed");
    opt_add (o, "rfdno=%d", fd);
    opt_add (o, "wfdno=%d", fd);
    opt_add (o, "trans=fd");
    opt_add (o, "aname=/diodctl");
    opt_add (o, "version=9p2000.L");
    opt_add (o, "access=%d", geteuid ());
    if (opt_debug)
        opt_add (o, opt_debug);
    if (opts)
        opt_add_cslist_override (o, opts);
    free (u);
    options = opt_string (o);
    opt_destroy (o);

    if (!(dev = malloc (strlen (host) + strlen ("/diodctl") + 2)))
        msg_exit ("out of memory");
    sprintf (dev, "%s:%s", host, "/diodctl");
    if (vopt)
        msg ("mount %s %s -o%s", dev, dir, options);
    util_mount (dev, dir, options);
    free (options);
    free (dev);
    close (fd);
}

static void
_cat_file (FILE *f)
{
    char buf[PATH_MAX];

    while (fgets (buf, sizeof (buf), f))
        printf ("%s%s", buf, buf[strlen (buf) - 1] == '\n' ? "" : "\n");
    if (ferror (f))
        errn_exit (ferror (f), "read error");
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
void
free_query (query_t *r)
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
_query_parent (int fd, pid_t pid, query_t *r, int getport)
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
            msg ("child exited with rc=%d", WEXITSTATUS (status));
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
query_t *
ctl_query (char *host, char *opts, int vopt, int getport, char *jobid,
                char *opt_debug)
{
    char tmppath[] = "/tmp/diodmount.XXXXXX";
    char path[PATH_MAX];
    query_t *res = _alloc_query ();
    int pfd[2];
    pid_t pid;
    char *tmpdir;
    FILE *f;


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
            util_unshare ();
            _diodctl_mount (host, tmpdir, opts, vopt, opt_debug);
            if (getport) {
                snprintf (path, sizeof (path), "%s/ctl", tmpdir);
                if (!(f = fopen (path, "r+")))
                    err_exit ("error opening %s", path);
                if (fprintf (f, "%s", jobid ? jobid : "nojob") < 0)
                    err_exit ("error writing to %s", path);
                fflush (f);
                rewind (f); 
                _cat_file (f);
                fclose (f);
            }
            snprintf (path, sizeof (path), "%s/exports", tmpdir);
            if (!(f = fopen (path, "r")))
                err_exit ("error opening %s", path);
            _cat_file (f);
            fclose (f);
            fflush (stdout);
            /* ummount is implicit when we exit */
            exit (0); 
        default:    /* parent */
            close (pfd[1]);
            if (!_query_parent (pfd[0], pid, res, getport)) {
                free_query (res);
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
