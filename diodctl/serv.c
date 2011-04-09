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

/* serv.c - manage child diod processes for diodctl */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _BSD_SOURCE         /* daemon */
#define _GNU_SOURCE         /* vasprintf */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <string.h>
#include <sys/resource.h>
#include <poll.h>
#include <assert.h>
#include <stdarg.h>
#include <netdb.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_trans.h"
#include "diod_upool.h"
#include "diod_sock.h"
#include "diod_conf.h"

#include "serv.h"

typedef struct {
    uid_t uid;              /* uid diod child is running as */
    char *jobid;            /* jobid of child, if any */
    pid_t pid;              /* pid of diod child */
    char *port;             /* port */
    pthread_t wait_thread;  /* thread to call waitpid () on child */
    struct pollfd *fds;     /* listen fds passed to child */
    int nfds;               /* count of above */
    int ac;
    char **av;
} Server;

typedef struct {
    List servers;           /* list of (Server *) */
    pthread_mutex_t lock;   /* protects servers list */
} Serverlist;

static Serverlist *serverlist = NULL;

#define BASE_PORT       1942 /* arbitrary non-privileged port */
#define MAX_PORT        (BASE_PORT + 1024)

/* Free server struct.  Suitable for cast to (ListDelF).
 */
static void
_free_server (Server *s)
{
    int i;

    if (s->port)
        free (s->port);
    if (s->pid != 0) {
        if (kill (s->pid, SIGTERM) < 0)
            err ("could not send SIGTERM to pid %d", s->pid);
    }
    if (s->fds != NULL)
        free (s->fds);
    if (s->av) {
        for (i = 0; i < s->ac; i++)
            if (s->av[i])
                free (s->av[i]);
        free (s->av);
    }
    for (i = 0; i < s->nfds; i++)
        (void) close (s->fds[i].fd);
    if (s->jobid)
        free (s->jobid);
    free (s);
}

/* Allocate server struct.
 */
static Server *
_alloc_server (uid_t uid, char *opts)
{
    Server *s = NULL;

    if (!(s = malloc (sizeof (Server)))) {
        np_uerror (ENOMEM);
        goto done;
    }
    memset (s, 0, sizeof (*s));
    if (!(s->av = malloc (sizeof (char *) * 1))) {
        np_uerror (ENOMEM);
        goto done;
    }
    s->av[0] = NULL;
    s->uid = uid;
    if (opts && !(s->jobid = strdup (opts))) {
        np_uerror (ENOMEM);
        goto done;
    }
done:
    if (np_rerror () && s != NULL)
        _free_server (s);
    return s;
}

/* Initialize the global list of servers.
 * Exit on error.
 */
void
diodctl_serv_init (void)
{
    int err;

    if (!(serverlist = malloc (sizeof (Serverlist))))
        msg_exit ("out of memory");
    if (!(serverlist->servers = list_create ((ListDelF)_free_server)))
        msg_exit ("out of memory");
    if ((err = pthread_mutex_init (&serverlist->lock, NULL)))
        msg_exit ("pthread_mutex_init: %s", strerror (err));
}

/* Return nonzero if servers match uid and jobid.
 * Suitable for cast to (ListFindF).
 */
static int
_smatch (Server *s1, Server *s2)
{
    if (s1->uid != s2->uid)
        return 0;
    if (s1->jobid && s2->jobid && strcmp (s1->jobid, s2->jobid) == 0)
        return 1;
    if (!s1->jobid && !s2->jobid)
        return 1;
    return 0;
}

/* Remove server from the list.
 */
static int
_remove_server (Server *s)
{
    ListIterator itr;
    int err;
    Server *key;

    if (!(itr = list_iterator_create (serverlist->servers))) {
        msg ("out of memory");
        goto done;
    }
    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        msg ("failed to lock serverlist");
        goto done;
    }
    if (!list_find (itr, (ListFindF)_smatch, s)) {
        /* can happen if startup failed, so keep silent */
        goto unlock_and_done;
    }
    key = list_remove (itr);
    assert (key == s);
    _free_server (s);
unlock_and_done:
    if ((err = pthread_mutex_unlock (&serverlist->lock))) {
        msg ("failed to unlock serverlist");
        goto done;
    }
done:
    if (itr)
        list_iterator_destroy (itr);
    return 0;
}

/* Thread to wait on child process and report status.
 * FIXME: when diodctl dies with SIGSEGV, this thread hangs
 */
static void *
_wait_pid (void *arg)
{
    Server *s = arg;
    pid_t pid;
    int status;

    do {
        pid = waitpid (s->pid, &status, 0);
        if (pid == s->pid) {
            if (WIFEXITED (status)) {
                msg ("%s exited with %d", s->av[0], WEXITSTATUS (status));
            } else if (WIFSIGNALED (status)) {
                msg ("%s killed by signal %d", s->av[0], WTERMSIG (status));
            } else if (WIFSTOPPED (status)) {
                msg ("%s stopped by signal %d", s->av[0], WSTOPSIG (status));
                continue;
            } else if (WIFCONTINUED (status)) {
                msg ("%s continued", s->av[0]);
                continue;
            }
        }
    } while (pid < 0 && errno == EINTR);
    if (pid < 0)
        err ("wait for %s", s->av[0]);
    else
        s->pid = 0; /* prevent _free_server () from sending signal */
    _remove_server (s);
    return NULL;
}

/* Append an argument to s->ac/s->av, expanding as needed.
 * Initial empty, NULL terminated s->ac/s->av is allocated in _alloc_server ().
 * s->av and any non-NULL args are freed in _free_server ().
 */
static int
_append_arg (Server *s, const char *fmt, ...)
{
    va_list ap;
    char *arg;
    int error;
    int ret = 0;

    va_start (ap, fmt);     
    error = vasprintf (&arg, fmt, ap);
    va_end (ap);
    if (error < 0) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (!(s->av = realloc (s->av, sizeof (char *) * (s->ac + 2)))) {
        np_uerror (ENOMEM);
        goto done;
    }
    s->av[s->ac++] = arg;
    s->av[s->ac] = NULL;
    ret = 1;
done:
    return ret;
}

/* Prep for exec of child diod server.
 */
static int
_build_server_args (Server *s)
{
    int ret = 0;
    int r;

    if (s->uid == 0)
        r = _append_arg (s, "diod-shared");
    else if (s->jobid)
        r = _append_arg (s, "diod-jobid-%s", s->jobid);
    else
        r = _append_arg (s, "diod-uid-%lu", (unsigned long)s->uid);
    if (r < 0)
            goto done;
    if (s->uid != 0 && _append_arg (s, "-u%d", s->uid) < 0)
        goto done;
    if (_append_arg (s, "-F%d", s->nfds) < 0)
        goto done;
    if (diod_conf_opt_debuglevel ()) {
        if (_append_arg (s, "-d%d", diod_conf_get_debuglevel ()) < 0)
            goto done;
    }
    if (diod_conf_opt_auth_required ()) {
        if (!diod_conf_get_auth_required () && _append_arg (s, "-n") < 0)
            goto done;
    }
    if (diod_conf_opt_configpath ()) {
	    if (_append_arg (s, "-c%s", diod_conf_get_configpath ()) < 0)
            goto done;
    }
    if (diod_conf_opt_logdest () || diod_conf_opt_foreground ()) {
        char *logdest = diod_log_get_dest ();

        if (logdest) {
            r = _append_arg (s, "-L%s", logdest);
            free (logdest);
        }
        if (!logdest || r < 0)
            goto done;
    }
    if (diod_conf_opt_exportall ())
        if (_append_arg (s, "-E") < 0)
            goto done;
    if (diod_conf_opt_exports ()) {
        List l = diod_conf_get_exports ();
        ListIterator itr = list_iterator_create (l);
        Export *x;

        if (!itr) {
            np_uerror (ENOMEM);
            goto done; 
        }
        while ((x = list_next (itr))) {
            if (_append_arg (s, "-e%s", x->path) < 0)
                goto done;
        }
        list_iterator_destroy (itr);
    }
    ret = 1;
done:
    return ret;
}

/* N.B. Do as little work as possible in the child since mutexes are in an
 * undefined state after the fork, malloc might have one, etc..
 */
static void
_exec_server (Server *s)
{
    int i;

    /* skip stdio 0..2, 3..3+nfds duped listen fds, close the rest */
    
    for (i = 0; i < s->nfds; i++) {
        (void)close (i + 3);
        if (dup2 (s->fds[i].fd, i + 3) < 0)
            _exit (1);
        if (close (s->fds[i].fd) < 0)
            _exit (1);
    }
    /* FIXME: use _SC_OPEN_MAX? */
    for (i = 0; i < 256; i++)
        (void)close (i + 3 + s->nfds);

    execv (diod_conf_get_diodpath (), s->av);
    _exit (1);
}

/* Dynamically allocate a port for diod to listen on
 * in the range of BASE_PORT:MAX_PORT.
 * Assign port string to *portp (caller to free).
 * Expand *fdsp and *nfdsp to include the new file descriptor(s).
 * Return the number of file descriptors opened (can return 0).
 */
static int
_alloc_ports (List l, struct pollfd **fdsp, int *nfdsp, char **portp)
{
    int i = BASE_PORT;
    char *port;
    int ret = 0;
    int flags = DIOD_SOCK_SKIPLISTEN | DIOD_SOCK_QUIET_EADDRINUSE;

    if (!(port = malloc (NI_MAXSERV))) {
        msg ("out of memory");
        goto done;
    }
    for (i = BASE_PORT; i <= MAX_PORT; i++) {
        snprintf (port, NI_MAXSERV, "%d", i);
        ret = diod_sock_listen_hostport_list (l, fdsp, nfdsp, port, flags);
        if (ret > 0) {
            *portp = port;
            break;
        }
    }
done:
    if (ret == 0 && port)
        free (port);
    return ret;
}

/* Spawn a new diod daemon running as [uid].
 * We are holding serverlist->lock.
 */
static Server *
_new_server (Npuser *user, char *opts)
{
    List l = diod_conf_get_diodctllisten ();
    int i, error;
    Server *s = NULL;

    if (!(s = _alloc_server (user->uid, opts)))
        goto done;

    if (!_alloc_ports (l, &s->fds, &s->nfds, &s->port)) {
        msg ("failed to allocate diod port for user %d", user->uid);
        np_uerror (EIO);
        goto done;
    }
    if (!_build_server_args (s))
        goto done;

    switch (s->pid = fork ()) {
        case -1:
            np_uerror (errno);
            err ("fork error while starting %s", s->av[0]);
            break;
        case 0: /* child */
            _exec_server (s);
            /*NOTREACHED*/
        default: /* parent */
            break;
    }

    if ((error = pthread_create (&s->wait_thread, NULL, _wait_pid, s))) {
        np_uerror (error);
        errn (error, "could not start thread to wait on %s", s->av[0]);
        goto done; 
    }
    /* connect (with retries) until server responds */
    if (!diod_sock_tryconnect (l, s->port, 30, 100)) {
        msg ("could not connect to %s - maybe it didn't start?", s->av[0]);
        goto done;
    }
    if (!list_append (serverlist->servers, s)) {
        np_uerror (ENOMEM);
        msg ("out of memory while accounting for %s", s->av[0]);
        goto done;
    }
    msg ("started %s", s->av[0]);
done:
    for (i = 0; i < s->nfds; i++)
        close (s->fds[i].fd);
    if (np_rerror ()) {
        if (s) {
            _free_server (s);
            s = NULL;
        }
    }
    return s;
}

/* Get server port for a particular user.
 * Copy its description starting at 'offset' into 'data', max 'count' bytes.
 * This backs the read handler for 'ctl'.
 * A previous write to 'ctl would have deposited option string in 'opts'.
 *
 * FIXME: improbable case: read is split into smaller chunks,
 * and server port changes in between reads.
 */
int
diodctl_serv_readctl (Npuser *user, char *opts, u64 offset, u32 count, u8* data)
{
    int cpylen = 0;
    Server *s, *key;
    int err;

    if (!(key = _alloc_server (user->uid, opts)))
        goto done;
    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
    s = list_find_first (serverlist->servers, (ListFindF)_smatch, key);
    free (key);
    if (!s)
        s = _new_server (user, opts);
    if (s) {
        cpylen = strlen (s->port) - offset;
        if (cpylen > count)
            cpylen = count;
        if (cpylen < 0)
            cpylen = 0;
        memcpy (data, s->port + offset, cpylen);
    } else
        np_uerror (ESRCH);
    if ((err = pthread_mutex_unlock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
done:
    return cpylen;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
