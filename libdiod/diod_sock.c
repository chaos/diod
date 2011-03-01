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

/* diod_sock.c - distributed I/O daemon socket operations */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <sys/time.h>
#if HAVE_TCP_WRAPPERS
#include <tcpd.h>
#endif
#include <poll.h>
#include <pthread.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_trans.h"
#include "diod_sock.h"

extern int  hosts_ctl(char *daemon, char *name, char *addr, char *user);
int         allow_severity = LOG_INFO;
int         deny_severity = LOG_WARNING;
#define DAEMON_NAME     "diod"

/* Open/bind sockets for all addresses that can be associated with host:port,
 * and expand pollfd array (*fdsp) to contain the new file descriptors,
 * updating its size (*nfdsp) also.
 * Return the number of file descriptors added (can be 0).
 * This is a helper for diod_sock_listen_hostport_list ().
 */
static int 
_setup_one (char *host, char *port, struct pollfd **fdsp, int *nfdsp, int flags)
{
    struct addrinfo hints, *res = NULL, *r;
    int opt, i, error, fd, nents = 0;
    struct pollfd *fds = *fdsp;
    int nfds = *nfdsp;
    int ret = 0;

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((error = getaddrinfo (host, port, &hints, &res))) {
        msg ("getaddrinfo: %s:%s: %s", host, port, gai_strerror(error));
        goto done;
    }
    if (res == NULL) {
        msg_exit ("listen address has no addrinfo: %s:%s\n", host, port);
        goto done;
    }
    for (r = res; r != NULL; r = r->ai_next)
        nents++;
    if (fds)
        fds = realloc (fds, sizeof(struct pollfd) * (nents + nfds));
    else
        fds = malloc (sizeof(struct pollfd) * nents);
    if (!fds) {
        msg ("out of memory");
        goto done;
    }
    for (r = res; r != NULL; r = r->ai_next, i++) {
        if ((fd = socket (r->ai_family, r->ai_socktype, 0)) < 0) {
            err ("socket: %s:%s", host, port);
            continue;
        }
        opt = 1;
        if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            err ("setsockopt: %s:%s", host, port);
            close (fd);
            continue;
        }
        if (bind (fd, r->ai_addr, r->ai_addrlen) < 0) {
            if (errno != EADDRINUSE || !(flags & DIOD_SOCK_QUIET_EADDRINUSE))
                err ("bind: %s:%s", host, port);
            close (fd);
            continue;
        }
        fds[nfds++].fd = fd;
    }

    *fdsp = fds;
    *nfdsp = nfds;
    ret = nfds;
done:
    if (res)
        freeaddrinfo (res);
    return ret;
}

static int
_listen_fds (struct pollfd *fds, int nfds)
{
    int ret = 0;
    int i;

    for (i = 0; i < nfds; i++) {
        if (listen (fds[i].fd, 5) == 0)
            ret++;
    }
    return ret;
}

/* Set up listen ports based on list of host:port strings.
 * If nport is non-NULL, use it in place of host:port ports.
 * Return the number of file descriptors opened (can return 0).
 */
int
diod_sock_listen_hostport_list (List l, struct pollfd **fdsp, int *nfdsp,
                                char *nport, int flags)
{
    ListIterator itr;
    char *hostport, *host, *port;
    int n, ret = 0;

    if (!(itr = list_iterator_create(l))) {
        msg ("out of memory");
        goto done;
    }
    while ((hostport = list_next(itr))) {
        if (!(host = strdup (hostport))) {
            msg ("out of memory");
            goto done;
        }
        port = strchr (host, ':');
        assert (port != NULL);
        *port++ = '\0';
        if (nport)
            port = nport;
        if ((n = _setup_one (host, port, fdsp, nfdsp, flags)) == 0) {
            free (host);
            goto done;
        }
        ret += n;
        free (host);
    }
    if (!(flags & DIOD_SOCK_SKIPLISTEN))
        ret = _listen_fds (*fdsp, *nfdsp);
done:
    if (itr)
        list_iterator_destroy(itr);
    return ret;
}

/* Listen on the first [nfds] fds after [starting], which are assumed to be
 * open and bound to appropriate addresses.
 * Return 0 on failure, nonzero on success.
 */
int
diod_sock_listen_nfds (struct pollfd **fdsp, int *nfdsp, int nfds, int starting)
{
    struct pollfd *fds;
    int i;
    int ret = 0;

    if (!(fds = malloc (sizeof (struct pollfd) * nfds))) {
        msg ("out of memory");
        goto done;
    }
    for (i = 0; i < nfds; i++)
        fds[i].fd = starting + i;
    *nfdsp = nfds;
    *fdsp = fds;

    ret = _listen_fds (fds, nfds);
done:
    return ret;
}

void
diod_sock_startfd (Npsrv *srv, int fd, char *host, char *ip, char *svc,
                   int blocking)
{
    Npconn *conn;
    Nptrans *trans;

    trans = diod_trans_create (fd, host, ip, svc);
    if (!trans) {
        msg ("diod_trans_create failure: %s:%s", host, svc);
        close (fd);
        return;
    }
                 
    conn = np_conn_create (srv, trans);
    if (!conn) {
        msg ("np_conn_create failure: %s%s", host, svc);
        diod_trans_destroy (trans);
        return;
    }
    if (blocking)
        np_srv_wait_zeroconns (srv);
}

/* Accept one connection on a ready fd and pass it on to the npfs 9P engine.
 * This is a helper for diod_sock_accept_loop ().
 */
static void
_accept_one (Npsrv *srv, int fd, int wrap)
{
    struct sockaddr_storage addr;
    socklen_t addr_size = sizeof(addr);
    char host[NI_MAXHOST], ip[NI_MAXHOST], svc[NI_MAXSERV];
    int res;

    fd = accept (fd, (struct sockaddr *)&addr, &addr_size);
    if (fd < 0) {
        if (!(errno == EWOULDBLOCK || errno == ECONNABORTED || errno == EPROTO
                                                            || errno == EINTR))
            err ("accept");
        return;
    }
    if ((res = getnameinfo ((struct sockaddr *)&addr, addr_size,
                            ip, sizeof(ip), svc, sizeof(svc),
                            NI_NUMERICHOST | NI_NUMERICSERV))) {
        msg ("getnameinfo: %s", gai_strerror(res));
        close (fd);
        return;
    }
    if ((res = getnameinfo ((struct sockaddr *)&addr, addr_size,
                            host, sizeof(host), NULL, 0, 0))) {
        msg ("getnameinfo: %s", gai_strerror(res));
        close (fd);
        return;
    }
#if HAVE_TCP_WRAPPERS
    if (wrap) {
        res = hosts_ctl (DAEMON_NAME, host, ip, STRING_UNKNOWN);
        if (!res) {
            msg ("connect denied by wrappers: %s:%s", host, svc);
            close (fd);
            return;
        }
    }
#endif
    diod_sock_startfd (srv, fd, host, ip, svc, 0);
}
 
/* Loop forever, accepting and handling new 9P connections.
 * This comprises the main service loop in diod -L and diodctl daemons.
 */
void
diod_sock_accept_loop (Npsrv *srv, struct pollfd *fds, int nfds, int wrap)
{
    int i;

    while (1) {
        for (i = 0; i < nfds; i++) {
            fds[i].events = POLLIN;
            fds[i].revents = 0;
        }
        if (poll (fds, nfds, -1) < 0) {
            if (errno == EINTR)
                continue; 
            err_exit ("poll");
        }
        for (i = 0; i < nfds; i++) {
            if ((fds[i].revents & POLLIN)) {
                _accept_one (srv, fds[i].fd, wrap);
            }
        }
    }
    /*NOTREACHED*/
}

/* Glue so diod_sock_accept_batch can start diod_sock_accept_loop thread.
 */
typedef struct  {
    Npsrv *srv;
    struct pollfd *fds;
    int nfds;
    int wrap;
} ala_t;

static void *
_accept_thread (void *ap)
{
    ala_t *a = ap;
    diod_sock_accept_loop (a->srv, a->fds, a->nfds, a->wrap);
    return NULL;
}

/* Loop accepting and handling new 9P connections.
 * Quit once connection count goes above zero then drops to zero again.
 */
void
diod_sock_accept_batch (Npsrv *srv, struct pollfd *fds, int nfds, int wrap)
{
    ala_t a;
    pthread_t thd;

    a.srv = srv;
    a.fds = fds;
    a.nfds = nfds;
    a.wrap = wrap;
    pthread_create (&thd, NULL, _accept_thread, &a);
    np_srv_wait_zeroconns (srv);
}

/* Try to connect to host:port.
 * Return fd on success, -1 on failure.
 */
int
diod_sock_connect (char *host, char *port, int maxtries, int retry_wait_ms)
{
    int error, fd = -1;
    struct addrinfo hints, *res = NULL, *r;
    int i;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((error = getaddrinfo (host, port, &hints, &res)) != 0) {
        msg ("getaddrinfo %s:%s: %s", host, port, gai_strerror (error));
        goto done;
    }
    if (!res) {
        msg ("could not look up %s:%s", host, port);
        goto done;
    }
    for (i = 0; i < maxtries; i++) {
        if (i > 0)
            usleep (1000 * retry_wait_ms);
        for (r = res; r != NULL; r = r->ai_next) {
            if (fd != -1)
                close (fd);
            if ((fd = socket (r->ai_family, r->ai_socktype, 0)) < 0)
                continue;
            if (connect (fd, r->ai_addr, r->ai_addrlen) >= 0)
                break;
        }
    }
    if (res)
        freeaddrinfo (res);
done:
    return fd;
}

/* Try to connect to any members of host:port list.
 * If nport is non-NULL, substitute that for :port.
 * Make maxtries attempts, waiting retry_wait_ms milliseconds between attempts.
 * Close any opened connections immediately - this is just a test.
 * Return 1 on success, 0 on failure.
 */
int
diod_sock_tryconnect (List l, char *nport, int maxtries, int retry_wait_ms)
{
    int i, fd, res = 0;
    char *hostport, *host, *port;
    ListIterator itr;

    if (!(itr = list_iterator_create(l))) {
        msg ("out of memory");
        goto done;
    }
    for (i = 0; i < maxtries; i++) {
        if (i > 0) {
            usleep (1000 * retry_wait_ms);
            list_iterator_reset (itr);
        }
        while ((hostport = list_next(itr))) {
            if (!(host = strdup (hostport))) {
                msg ("out of memory");
                goto done;
            }
            port = strchr (host, ':');
            assert (port != NULL);
            *port++ = '\0';
            if (nport)
                port = nport;
            if ((fd = diod_sock_connect (host, port, 1, 0)) >= 0) {
                close (fd);
                res = 1;
                goto done;
            }
        }
    }
done:
    if (itr)
        list_iterator_destroy(itr);
    return res;
}

    
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
