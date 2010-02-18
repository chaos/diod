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
    char *name;             /* host:port */
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
static char *path_config = NULL;

#define BASE_PORT       1942 /* arbitrary non-privileged port */
#define MAX_PORT        (BASE_PORT + 1024)


/* Free server struct.  Suitable for cast to (ListDelF).
 */
static void
_free_server (Server *s)
{
    int i;

    if (s->name)
        free (s->name);
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
    s->av = malloc (sizeof (char) * 1);
    s->av[0] = NULL;
    s->ac = 0;
done:
    if (np_haserror () && s != NULL)
        _free_server (s);
    return s;
}

/* Initialize the global list of servers.
 * Exit on error.
 */
void
diodctl_serv_init (char *path)
{
    int err;

    if (!(serverlist = malloc (sizeof (Serverlist))))
        msg_exit ("out of memory");
    if (!(serverlist->servers = list_create ((ListDelF)_free_server)))
        msg_exit ("out of memory");
    if ((err = pthread_mutex_init (&serverlist->lock, NULL)))
        msg_exit ("pthread_mutex_init: %s", strerror (err));
    if (path)
        path_config = path;
}

/* Return nonzero if s is server for uid.
 * Suitable for cast to (ListFindF).
 */
static int
_match_server_byuid (Server *s, uid_t *uid)
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
    if (!list_find (itr, (ListFindF)_match_server_byuid, &s->uid)) {
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
                if (WEXITSTATUS (status) != 0)
                    msg ("%s exited with %d", s->name, WEXITSTATUS (status));
            } else if (WIFSIGNALED (status)) {
                msg ("%s killed by signal %d", s->name, WTERMSIG (status));
            } else if (WIFSTOPPED(status)) {
                msg ("%s stopped by signal %d", s->name, WSTOPSIG(status));
                continue;
            } else if (WIFCONTINUED(status)) {
                msg ("%s continued", s->name);
                continue;
            }
        }
    } while (pid < 0 && errno == EINTR);
    if (pid < 0)
        err ("wait for %s", s->name);
    else
        s->pid = 0; /* prevent _free_server () from sending signal */
    _remove_server (s);
    return NULL;
}

static int
_push_arg (Server *s, const char *fmt, ...)
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
    if (!(s->av = realloc (s->av, sizeof (char) * (s->ac + 2)))) {
        np_uerror (ENOMEM);
        goto done;
    }
    s->av[s->ac++] = arg;
    s->av[s->ac] = NULL;
    ret = 1;
done:
    return ret;
}

/* Exec child diod server.
 * We converted the config registry back in to a (temporary) config file
 * which we now tell diod to use.  This is to pick up any changes from
 * the diodctl command line.  Then we override those based on the specific
 * needs of diod running as a child of diodctl.
 */
static int
_build_server_args (Server *s)
{
    int ret = 0;

    assert (path_config != NULL);

    if (_push_arg (s, "diod-%d", s->uid) < 0)
        goto done;
    if (_push_arg (s, "-c%s", path_config) < 0)
        goto done;
    if (_push_arg (s, "-x") < 0)
        goto done;
    if (_push_arg (s, "-f") < 0)
        goto done;
    if (_push_arg (s, "-F%d", s->nfds) < 0)
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


/* Spawn a new diod daemon running as [uid].
 * We are holding serverlist->lock.
 */
static void
_new_server (Npuser *user, char *ip)
{
    int i, error;
    Server *s = NULL;

    if (!(s = _alloc_server ()))
        goto done;

    s->uid = user->uid;

    if (!diod_sock_setup_alloc (ip, &s->fds, &s->nfds, &s->name)) {
        msg ("failed to allocate port for user %d on %s", user->uid, ip);
        np_uerror (EIO);
        goto done;
    }
    if (!_build_server_args (s))
        goto done;

    switch (s->pid = fork ()) {
        case -1:
            np_uerror (errno);
            break;
        case 0: /* child */
            diod_become_user (user); /* no locks taken here (in our code!) */
            _exec_server (s);
            /*NOTREACHED*/
        default: /* parent */
            break;
    }

    if ((error = pthread_create (&s->wait_thread, NULL, _wait_pid, s))) {
        np_uerror (error);
        goto done; 
    }
    if (!list_append (serverlist->servers, s)) {
        np_uerror (ENOMEM);
        goto done;
    }
done:
    for (i = 0; i < s->nfds; i++)
        close (s->fds[i].fd);
    if (np_haserror () && s != NULL)
        _free_server (s);
}

/* Get servername for a particular user.
 * Copy its description starting at 'offset' into 'data', max 'count' bytes.
 * This backs the read handler for 'server', hence the funny args.
 *
 * FIXME: improbable case: read is split into smaller chunks,
 * and server name changes in between reads.
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
    s = list_find_first (serverlist->servers, (ListFindF)_match_server_byuid,
                         &user->uid);
    if (s) {
        cpylen = strlen (s->name) - offset;
        if (cpylen > count)
            cpylen = count;
        if (cpylen < 0)
            cpylen = 0;
        memcpy (data, s->name + offset, cpylen);
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
diodctl_serv_create (Npuser *user, char *ip)
{
    int err;
    int ret = 0;

    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
    /* FIXME: what if found server is listening on wrong ip? */
    if (!list_find_first (serverlist->servers, (ListFindF)_match_server_byuid,
                          &user->uid)) {
        _new_server (user, ip);
    }

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
