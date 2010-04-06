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
    pid_t pid;              /* pid of diod child */
    char *port;             /* port */
    pthread_t wait_thread;  /* thread to call waitpid () on child */
    struct pollfd *fds;     /* listen fds passed to child */
    int nfds;               /* count of above */
    int ac;
    char **av;
    char *tmp_exports;      /* temp file containing export list */
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

    if (s->tmp_exports && unlink (s->tmp_exports) < 0)
        err ("could not unlink tmp exports file: %s", s->tmp_exports);
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
    free (s);
}

/* Allocate server struct.
 */
static Server *
_alloc_server (void)
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
done:
    if (np_haserror () && s != NULL)
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

/* Return nonzero if s is server for uid.
 * Suitable for cast to (ListFindF).
 */
static int
_smatch (Server *s, uid_t *uid)
{
    return (s->uid == *uid);
}

/* Remove server from the list.
 */
static int
_remove_server (Server *s)
{
    ListIterator itr;
    int err;
    Server *s1;

    if (!(itr = list_iterator_create (serverlist->servers))) {
        msg ("out of memory");
        goto done;
    }
    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        msg ("failed to lock serverlist");
        goto done;
    }
    if (!list_find (itr, (ListFindF)_smatch, &s->uid)) {
        msg ("failed to delete server from list");
        goto unlock_and_done;
    }
    s1 = list_remove (itr);
    assert (s1 == s);
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
                msg ("diod (port %s user %d) exited with %d",
                      s->port, s->uid, WEXITSTATUS (status));
            } else if (WIFSIGNALED (status)) {
                msg ("diod (port %s user %d) killed by signal %d",
                      s->port, s->uid, WTERMSIG (status));
            } else if (WIFSTOPPED (status)) {
                msg ("diod (port %s user %d) stopped by signal %d",
                     s->port, s->uid, WSTOPSIG (status));
                continue;
            } else if (WIFCONTINUED (status)) {
                msg ("diod (port %s user %d) continued",
                     s->port, s->uid);
                continue;
            }
        }
    } while (pid < 0 && errno == EINTR);
    if (pid < 0)
        err ("wait for diod (port %s user %d)", s->port, s->uid);
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

    if (_append_arg (s, "diod-%s", s->uid != 0 ? "shared" : "private") < 0)
        goto done;
    if (_append_arg (s, "-f") < 0)
        goto done;
    if (_append_arg (s, "-x") < 0)
        goto done;
    if (s->uid != 0 && _append_arg (s, "-u%d", s->uid) < 0)
        goto done;
    if (_append_arg (s, "-F%d", s->nfds) < 0)
        goto done;
    if (_append_arg (s, "-d%d", diod_conf_get_debuglevel ()) < 0)
        goto done;
    if (_append_arg (s, "-w%d", diod_conf_get_nwthreads ()) < 0)
        goto done;
    if (!diod_conf_get_tcpwrappers () && _append_arg (s, "-a") < 0)
        goto done;
    if (!diod_conf_get_munge () && _append_arg (s, "-m") < 0)
        goto done;
    if (!(s->tmp_exports = diod_conf_write_exports ()))
        goto done;
    if (_append_arg (s, "-E%s", s->tmp_exports) < 0)
        goto done;
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

    for (i = 0; i < s->nfds; i++) {
        (void)close (i);
        if (dup2 (s->fds[i].fd, i) < 0)
            _exit (1);
        if (close (s->fds[i].fd) < 0)
            _exit (1);
    }

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
_new_server (Npuser *user)
{
    List l = diod_conf_get_diodctllisten ();
    int i, error;
    Server *s = NULL;

    if (!(s = _alloc_server ()))
        goto done;

    s->uid = user->uid;

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
            err ("fork error while starting diod server");
            break;
        case 0: /* child */
            _exec_server (s);
            /*NOTREACHED*/
        default: /* parent */
            break;
    }

    /* connect (with retries) until server responds */
    if (!diod_sock_tryconnect (l, s->port, 30, 100)) {
        msg ("could not connect to new diod server - maybe it didn't start?");
        goto done;
    }
    if ((error = pthread_create (&s->wait_thread, NULL, _wait_pid, s))) {
        np_uerror (error);
        errn (error, "could not start thread to wait on new diod server");
        goto done; 
    }
    if (!list_append (serverlist->servers, s)) {
        np_uerror (ENOMEM);
        msg ("out of memory while accounting for new diod server");
        goto done;
    }
done:
    for (i = 0; i < s->nfds; i++)
        close (s->fds[i].fd);
    if (np_haserror ()) {
        if (s) {
            _free_server (s);
            s = NULL;
        }
    }
    return s;
}

/* Get server port for a particular user.
 * Copy its description starting at 'offset' into 'data', max 'count' bytes.
 * This backs the read handler for 'server', hence the funny args.
 *
 * FIXME: improbable case: read is split into smaller chunks,
 * and server port changes in between reads.
 */
int
diodctl_serv_getname (Npuser *user, u64 offset, u32 count, u8* data)
{
    int cpylen = 0;
    Server *s;
    int err;

    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
    s = list_find_first (serverlist->servers, (ListFindF)_smatch, &user->uid);
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

/* Create a new server for uid if there isn't one already.
 * This backs the write handler for 'ctl'.
 */
int
diodctl_serv_create (Npuser *user)
{
    int err;
    int ret = 0;
    Server *s;

    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
    /* FIXME: what if found server is listening on wrong ip? */
    s = list_find_first (serverlist->servers, (ListFindF)_smatch, &user->uid);
    if (!s)
        s = _new_server (user);

    if ((err = pthread_mutex_unlock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
    ret = 1;
done:
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
