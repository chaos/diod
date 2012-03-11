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
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_sock.h"

extern int  hosts_ctl(char *daemon, char *name, char *addr, char *user);
int         allow_severity = LOG_INFO;
int         deny_severity = LOG_WARNING;
#define DAEMON_NAME     "diod"

static int
_disable_nagle(int fd)
{
    int ret, i = 1;
    socklen_t len = sizeof (i);

    i = 1;
    ret = setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &i, len);
    if (ret < 0) {
        err ("setsockopt TCP_NODELAY");
        goto done;
    }
done:
    return ret;
}

/* Protect against resource leakage when cluster compute nodes are rebooted
 * without a clean unmount.  Transport read will get ECONNRESET after ~3m
 * if node is rebooted or ECONNABORTED(?) after ~22m if it stays down.
 *
 * Name             Default Diod    Desc
 * SO_KEEPALIVE     0       1       enable/disable keepalive
 * TCP_KEEPIDLE     7200    120     begin keepalives after idle (s)
 * TCP_KEEPINTVL    75      120     interval between keepalives (s)
 * TCP_KEEPCNT      9       9       number of keepalive attempts
 */
static int
_enable_keepalive(int fd)
{
    int ret, i;
    socklen_t len = sizeof (i);

    i = 1;
    ret = setsockopt (fd, SOL_SOCKET, SO_KEEPALIVE, &i, len);
    if (ret < 0) {
        err ("setsockopt SO_KEEPALIVE");
        goto done;
    }
    i = 120;
    ret = setsockopt (fd, SOL_TCP, TCP_KEEPIDLE, &i, len);
    if (ret < 0) {
        err ("setsockopt SO_KEEPIDLE");
        goto done;
    }
    i = 120;
    ret = setsockopt (fd, SOL_TCP, TCP_KEEPINTVL, &i, len);
    if (ret < 0) {
        err ("setsockopt SO_KEEPINTVL");
        goto done;
    }
    i = 9;
    ret = setsockopt (fd, SOL_TCP, TCP_KEEPCNT, &i, len);
    if (ret < 0) {
        err ("setsockopt SO_KEEPCNT");
        goto done;
    }
done:
    return ret;
}

static int
_enable_reuseaddr(int fd)
{
    int ret, i;
    socklen_t len = sizeof (i);

    i = 1;
    ret = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &i, len);
    if (ret < 0) {
        err ("setsockopt SO_REUSEADDR");
        goto done;;
    }
done:
    return ret;
}

/* Open/bind sockets for all addresses that can be associated with host:port,
 * and expand pollfd array (*fdsp) to contain the new file descriptors,
 * updating its size (*nfdsp) also.
 * Return the number of file descriptors added (can be 0).
 * This is a helper for diod_sock_listen_hostports ().
 */
static int 
_setup_one (char *host, char *port, struct pollfd **fdsp, int *nfdsp)
{
    struct addrinfo hints, *res = NULL, *r;
    int opt, error, fd, nents = 0;
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
    for (r = res; r != NULL; r = r->ai_next) {
        if ((fd = socket (r->ai_family, r->ai_socktype, 0)) < 0) {
            err ("socket: %s:%s", host, port);
            continue;
        }
        opt = 1;
        (void)_enable_reuseaddr (fd);
        if (bind (fd, r->ai_addr, r->ai_addrlen) < 0) {
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
diod_sock_listen_hostports (List l, struct pollfd **fdsp, int *nfdsp,
                                char *nport)
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
        NP_ASSERT (port != NULL);
        *port++ = '\0';
        if (nport)
            port = nport;
        if ((n = _setup_one (host, port, fdsp, nfdsp)) == 0) {
            free (host);
            goto done;
        }
        ret += n;
        free (host);
    }
    ret = _listen_fds (*fdsp, *nfdsp);
done:
    if (itr)
        list_iterator_destroy(itr);
    return ret;
}

void
diod_sock_startfd (Npsrv *srv, int fdin, int fdout, char *client_id)
{
    Npconn *conn;
    Nptrans *trans;

    trans = np_fdtrans_create (fdin, fdout);
    if (!trans) {
        errn (np_rerror (), "error creating transport for %s", client_id);
        (void)close (fdin);
        if (fdin != fdout)
            (void)close (fdout);
        return;
    }
                 
    conn = np_conn_create (srv, trans, client_id);
    if (!conn) {
        errn (np_rerror (), "error creating connection for %s", client_id);
	/* trans is destroyed in np_conn_create on failure */
        return;
    }
}

/* Accept one connection on a ready fd and pass it on to the npfs 9P engine.
 */
void
diod_sock_accept_one (Npsrv *srv, int fd)
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
    (void)_disable_nagle (fd);
    (void)_enable_keepalive (fd);
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
    res = hosts_ctl (DAEMON_NAME, host, ip, STRING_UNKNOWN);
    if (!res) {
        msg ("connect denied by wrappers: %s:%s", host, svc);
        close (fd);
        return;
    }
#endif
    diod_sock_startfd (srv, fd, fd, strlen(host) > 0 ? host : ip);
}
 
/* Connect to host:port.
 * Return fd on success, -1 on failure.
 */
int
diod_sock_connect (char *host, char *port, int flags)
{
    int error, fd = -1;
    struct addrinfo hints, *res = NULL, *r;
    char *errmsg = NULL;
    int errnum = 0;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((error = getaddrinfo (host, port, &hints, &res)) != 0) {
        if (!(flags & DIOD_SOCK_QUIET))
            msg ("getaddrinfo %s:%s: %s", host, port, gai_strerror (error));
        goto done;
    }
    if (!res) {
        if (!(flags & DIOD_SOCK_QUIET))
            msg ("could not look up %s:%s", host, port);
        goto done;
    }
    for (r = res; r != NULL; r = r->ai_next) {
        if ((fd = socket (r->ai_family, r->ai_socktype, 0)) < 0) {
            errnum = errno;
            errmsg = "socket";
            continue;
        }
        (void)_disable_nagle (fd);
        if (connect (fd, r->ai_addr, r->ai_addrlen) < 0) {
            errnum = errno;
            errmsg = "connect";
            (void)close (fd);
            fd = -1;
        }
    }
    if (fd < 0 && errmsg && !(flags & DIOD_SOCK_QUIET))
        errn (errnum, "%s %s", errmsg, host);
    if (res)
        freeaddrinfo (res);
done:
    return fd;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
