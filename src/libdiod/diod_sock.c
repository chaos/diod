/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

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
#include <sys/un.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <poll.h>
#include <pthread.h>

#include "src/libnpfs/npfs.h"
#include "src/liblsd/list.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_sock.h"

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
    ret = setsockopt (fd, IPPROTO_TCP, TCP_KEEPIDLE, &i, len);
    if (ret < 0) {
        err ("setsockopt SO_KEEPIDLE");
        goto done;
    }
    i = 120;
    ret = setsockopt (fd, IPPROTO_TCP, TCP_KEEPINTVL, &i, len);
    if (ret < 0) {
        err ("setsockopt SO_KEEPINTVL");
        goto done;
    }
    i = 9;
    ret = setsockopt (fd, IPPROTO_TCP, TCP_KEEPCNT, &i, len);
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

static int
_poll_add (struct pollfd **fdsp, int *nfdsp, int fd)
{
    int nfds = *nfdsp;
    struct pollfd *fds = *fdsp;

    if (fds)
        fds = realloc (fds, sizeof(struct pollfd) * (nfds + 1));
    else
        fds = malloc (sizeof(struct pollfd) * (nfds + 1));
    if (!fds)
        goto nomem;
    fds[nfds++].fd = fd;
    *fdsp = fds;
    *nfdsp = nfds;
    return 0;
nomem:
    errno = ENOMEM;
    err ("diod_sock_listen");
    return -1;
}

/* Open/bind sockets for all addresses that can be associated with host:port,
 * and expand pollfd array (*fdsp) to contain the new file descriptors,
 * updating its size (*nfdsp) also.
 * Return the number of file descriptors added (can be 0).
 * This is a helper for diod_sock_listen ().
 */
static int
_setup_one_inet (char *host, char *port, struct pollfd **fdsp, int *nfdsp)
{
    struct addrinfo hints, *res = NULL, *r;
    int error, fd;
    int count = 0;

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
    for (r = res; r != NULL; r = r->ai_next) {
        if ((fd = socket (r->ai_family, r->ai_socktype, 0)) < 0) {
            err ("socket: %s:%s", host, port);
            continue;
        }
        (void)_enable_reuseaddr (fd);
        if (bind (fd, r->ai_addr, r->ai_addrlen) < 0) {
            err ("bind: %s:%s", host, port);
            close (fd);
            continue;
        }
        if (_poll_add (fdsp, nfdsp, fd) < 0)
            break;
        count++;
    }
    if (count > 0)
        msg ("Listening on %s:%s", host, port);
done:
    if (res)
        freeaddrinfo (res);
    return count;
}

static int
_setup_one_unix (char *path, struct pollfd **fdsp, int *nfdsp)
{
    struct sockaddr_un addr;
    int e, fd = -1;
    mode_t oldumask;

    if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
        err ("socket");
        goto error;
    }
    if ((remove (path) < 0 && errno != ENOENT)) {
        err ("remove %s", path);
        goto error;
    }
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    oldumask = umask (0111);
    e = bind (fd, (struct sockaddr *)&addr, sizeof (struct sockaddr_un));
    umask (oldumask);
    if (e < 0) {
        err ("bind %s", path);
        goto error;
    }
    if (_poll_add (fdsp, nfdsp, fd) < 0)
        goto error;
    msg ("Listening on %s", path);
    return 1;
error:
    if (fd != -1)
        close (fd);
    return 0;
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

/* Set up listen ports based on list of strings, which can be either
 * host:port or /path/to/unix_domain_socket format.
 * Return the number of file descriptors opened (can return 0).
 */
int
diod_sock_listen (List l, struct pollfd **fdsp, int *nfdsp)
{
    ListIterator itr;
    char *s, *host, *port;
    int n, ret = 0;

    if (!(itr = list_iterator_create(l))) {
        msg ("out of memory");
        goto done;
    }
    while ((s = list_next(itr))) {
        if (s[0] == '/') {
            if ((n = _setup_one_unix (s, fdsp, nfdsp)) == 0)
                goto done;
            ret += n;
        } else {
            char *hostend;
            int ipv6 = 0;

            if (s[0] == '[') {
                ipv6 = 1;
                s++;
            }

            if (!(host = strdup (s))) {
                msg ("out of memory");
                goto done;
            }
            if (ipv6) {
                hostend = strchr (host, ']');
                NP_ASSERT (hostend != NULL);
                *hostend++ = '\0';
            } else {
                hostend = host;
            }
            port = strchr (hostend, ':');
            NP_ASSERT (port != NULL);
            *port++ = '\0';
            if ((n = _setup_one_inet (host, port, fdsp, nfdsp)) == 0) {
                free (host);
                goto done;
            }
            ret += n;
            free (host);
        }
    }
    ret = _listen_fds (*fdsp, *nfdsp);
done:
    if (itr)
        list_iterator_destroy(itr);
    return ret;
}

void
diod_sock_startfd (Npsrv *srv, int fdin, int fdout, char *client_id, int flags)
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

    conn = np_conn_create (srv, trans, client_id, flags);
    if (!conn) {
        errn (np_rerror (), "error creating connection for %s", client_id);
        /* trans is destroyed in np_conn_create on failure */
        return;
    }
}

/* Accept one connection on a ready fd and pass it on to the npfs 9P engine.
 */
void
diod_sock_accept_one (Npsrv *srv, int fd, int lookup)
{
    struct sockaddr_storage addr = {0};
    socklen_t addr_size = sizeof(addr);
    char host[NI_MAXHOST], ip[NI_MAXHOST], svc[NI_MAXSERV];
    int res, port;
    int flags = 0;

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
    if (addr.ss_family != AF_UNIX) {
        (void)_disable_nagle (fd);
        (void)_enable_keepalive (fd);
    }
    host[0] = '\0';
    if (lookup && (res = getnameinfo ((struct sockaddr *)&addr, addr_size,
                                      host, sizeof(host), NULL, 0, 0))) {
        msg ("getnameinfo: %s", gai_strerror(res));
        close (fd);
        return;
    }
    port = strtoul (svc, NULL, 10);
    if (port < IPPORT_RESERVED && port >= IPPORT_RESERVED / 2)
        flags |= CONN_FLAGS_PRIVPORT;
    diod_sock_startfd (srv, fd, fd, strlen(host) > 0 ? host : ip, flags);
}

/* Bind socket to a local IPv4 port < 1024.
 */
static int
_bind_priv_inet4 (int sockfd)
{
    struct sockaddr_in in;
    int port;
    int rc = -1;

    memset (&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_addr.s_addr = INADDR_ANY;

    for (port = IPPORT_RESERVED - 1; port >= IPPORT_RESERVED / 2; port--) {
        in.sin_port = htons ((ushort)port);
        rc = bind(sockfd, (struct sockaddr *) &in, sizeof(in));
        if (rc == 0 || (rc < 0 && errno != EADDRINUSE))
            break;
    }
    if (rc < 0 && errno == EADDRINUSE)
        errno = EAGAIN;
    return rc;
}

/* Bind socket to a local IPv6 port < 1024.
 */
static int
_bind_priv_inet6 (int sockfd)
{
    struct sockaddr_in6 in;
    int port;
    int rc = -1;

    memset (&in, 0, sizeof(in));
    in.sin6_family = AF_INET6;
    in.sin6_addr = in6addr_any;

    for (port = IPPORT_RESERVED - 1; port >= IPPORT_RESERVED / 2; port--) {
        in.sin6_port = htons ((ushort)port);
        rc = bind(sockfd, (struct sockaddr *) &in, sizeof(in));
        if (rc == 0 || (rc < 0 && errno != EADDRINUSE))
            break;
    }
    if (rc < 0 && errno == EADDRINUSE)
        errno = EAGAIN;
    return rc;
}

/* Connect to host:port.
 * Return fd on success, -1 on failure.
 */
int
diod_sock_connect_inet (char *host, char *port, int flags)
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
        if (flags & DIOD_SOCK_PRIVPORT) {
            if (r->ai_family == AF_INET) {
                if (_bind_priv_inet4 (fd) < 0) {
                    errnum = errno;
                    errmsg = "_bind_resv_inet4";
                    (void)close (fd);
                    fd = -1;
                    continue;
                }
            } else if (r->ai_family == AF_INET6) {
                if (_bind_priv_inet6 (fd) < 0) {
                    errnum = errno;
                    errmsg = "_bind_resv_inet6";
                    (void)close (fd);
                    fd = -1;
                    continue;
                }
            } else {
                errnum = EINVAL;
                errmsg = "protocol";
                (void)close (fd);
                fd = -1;
                continue;
            }
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

int
diod_sock_connect_unix (char *path, int flags)
{
    struct sockaddr_un addr;
    int fd = -1;

    if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0) {
        if (!(flags & DIOD_SOCK_QUIET))
            err ("socket");
        goto error;
    }
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);
    if (connect (fd, (struct sockaddr *)&addr, sizeof (struct sockaddr_un))<0) {
        if (!(flags & DIOD_SOCK_QUIET))
            err ("connect %s", path);
        goto error;
    }
    return fd;
error:
    if (fd != -1)
        close (fd);
    return -1;
}

static int _diod_sock_connect_inet6(char *name, int flags)
{
    char *hoststart;
    char *hostend;
    char *port;

    hoststart = name + 1;
    if ((hostend = strchr (hoststart, ']'))) {
        port = strchr (hostend, ':');
        *hostend = '\0';
    } else {
        errno = EINVAL;
        if (!(flags & DIOD_SOCK_QUIET))
            err ("diod_sock_connect invalid address %s", name);
        return -1;
    }
    if (port) {
        port++;
        return diod_sock_connect_inet (hoststart, port, flags);
    } else {
        return diod_sock_connect_inet (hoststart, "564", flags);
    }
}

int
diod_sock_connect (char *name, int flags)
{
    int fd = -1;
    char *host = NULL;
    char *port;

    if (!name)
        fd = diod_sock_connect_inet ("localhost", "564", flags);
    else if (name[0] == '/')
        fd = diod_sock_connect_unix (name, flags);
    else if (!strchr (name, ':'))
        fd = diod_sock_connect_inet (name, "564", flags);
    else {
        if (!(host = strdup (name))) {
            errno = ENOMEM;
            if (!(flags & DIOD_SOCK_QUIET))
                err ("diod_sock_connect %s", name);
            goto done;
        }
        if (host[0] == '[') {
            fd = _diod_sock_connect_inet6(host, flags);
            goto done;
        }
        if (!(port = strchr (host, ':'))) {
            errno = EINVAL;
            if (!(flags & DIOD_SOCK_QUIET))
                err ("diod_sock_connect %s", name);
            goto done;
        }
        *port++ = '\0';
        fd = diod_sock_connect_inet (host, port, flags);
    }
done:
    if (host)
        free (host);
    return fd;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
