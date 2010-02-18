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
#include <syslog.h>
#include <sys/time.h>
#if HAVE_TCP_WRAPPERS
#include <tcpd.h>
#endif
#include <poll.h>

#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_trans.h"
#include "diod_sock.h"

extern int  hosts_ctl(char *daemon, char *name, char *addr, char *user);
int         allow_severity = LOG_INFO;
int         deny_severity = LOG_WARNING;
#define DAEMON_NAME     "diod"

#define BASE_PORT       1942    /* arbitrary non-privileged port */
#define MAX_PORT        (BASE_PORT + 1024)

static int 
_setup_one (char *host, char *port, struct pollfd **fdsp, int *nfdsp,
                     int quiet)
{
    struct addrinfo hints, *res, *r;
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
            err ("bind: %s:%s", host, port);
            close (fd);
            continue;
        }
        fds[nfds++].fd = fd;
    }
    freeaddrinfo (res);

    *fdsp = fds;
    *nfdsp = nfds;
    ret = nfds;
done:
    return ret;
}

/* Bind to sockets associated with host:port name.
 * Fd's are placed in pollfd array (allocated/expanded).
 * Returns 0 on failure, nonzero on success.
 */
int 
diod_sock_setup_one (char *host, char *port, struct pollfd **fdsp, int *nfdsp)
{
    return _setup_one (host, port, fdsp, nfdsp, 0);
}

/* Bind to sockets associated with host:port name, where port is 
 * dynamically allocated between BASE_PORT and MAX_PORT.
 * Fd's are placed in pollfd array (allocated/expanded).
 * Returns 0 on failure, nonzero on success.
 * A "host:port" string is placed in kp (caller must free).
 */
int
diod_sock_setup_alloc (char *host, struct pollfd **fdsp, int *nfdsp, char **kp)
{
    int i = BASE_PORT;
    char port[NI_MAXSERV];
    int len = strlen (host) + 16;
    char *key;
    int ret = 0;

    if (!(key = malloc (len))) {
        msg ("out of memory");
        goto done;
    }
    for (i = BASE_PORT; i < MAX_PORT; i++) {
        snprintf (port, sizeof(port), "%d", i);
        if (_setup_one (host, port, fdsp, nfdsp, 1)) {
            snprintf (key, len, "%s:%d\n", host, i);
            ret = 1;
            break;
        }
    }
done:
    if (ret == 1)
        *kp = key;
    else if (key)
        free (key);
    return ret;
}

int
diod_sock_listen (struct pollfd *fds, int nfds)
{
    int ret = 0;
    int i;

    for (i = 0; i < nfds; i++) {
        if (listen (fds[i].fd, 5) == 0)
            ret++;
    }
    return ret;
}

/* Listen on the first nfds file descriptors, which are assumed to be
 * open and bound to appropriate addresses.
 * Return 0 on failure, nonzero on success.
 */
int
diod_sock_listen_fds (struct pollfd **fdsp, int *nfdsp, int nfds)
{
    struct pollfd *fds;
    int i;
    int ret = 0;

    if (!(fds = malloc (sizeof (struct pollfd) * nfds))) {
        msg ("out of memory");
        goto done;
    }
    for (i = 0; i < nfds; i++)
        fds[i].fd = i;
    *nfdsp = nfds;
    *fdsp = fds;

    ret = diod_sock_listen (fds, nfds);
done:
    return ret;
}

/* Set up listen ports based on list of host:port strings.
 * Return 0 on failure, nonzero on success.
 */
int
diod_sock_listen_list (struct pollfd **fdsp, int *nfdsp, List l)
{
    ListIterator itr;
    char *hostport, *host, *port;
    int ret = 0;

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
        *port++ = '\0';
        if (diod_sock_setup_one (host, port, fdsp, nfdsp) == 0)
            goto done;
    }
    list_iterator_destroy(itr);

    ret = diod_sock_listen (*fdsp, *nfdsp);
done:
    return ret;
}

/* Accept one connection on a ready fd and pass it on to the npfs 9P engine.
 */
static void
_accept_one (Npsrv *srv, int fd, int wrap)
{
    struct sockaddr_storage addr;
    socklen_t addr_size = sizeof(addr);
    char host[NI_MAXHOST], ip[NI_MAXHOST], svc[NI_MAXSERV];
    int res;
    Npconn *conn;
    Nptrans *trans;

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
                            host, sizeof(host), NULL, 0,
                            NI_NAMEREQD))) {
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
    trans = diod_trans_create (fd, host, ip, svc);
    if (!trans) {
        msg ("connect denied by diod_trans_create failure: %s:%s", host, svc);
        close (fd);
        return;
    }
                 
    conn = np_conn_create (srv, trans);
    if (!conn) {
        msg ("connect denied by np_conn_create failure: %s%s", host, svc);
        diod_trans_destroy (trans);
        return;
    }

    msg ("accepted connection from %s on port %s", host, svc);
}
 
/* Loop accepting and handling new connections.
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
