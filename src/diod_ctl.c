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

/* diod_ctl.c - super server for diod (runs as root on 9pfs port) */

/* What we do:
 * - look like a 9p file system for diod ctl called 'ctl'
 * - file 'exports' contains list of I/O node exports
 * - file 'server' contains server hostname:port
 * - when an attached user reads 'server', if a diod server is already running
 *   for that user, return host:port.  If not, spawn it and return host:port.
 * - reap children when they quit (on last trans shutdown)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _BSD_SOURCE         /* daemon */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
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
#include <pwd.h>
#include <sys/time.h>
#include <sys/resource.h>
#if HAVE_TCP_WRAPPERS
#include <tcpd.h>
#endif
#include <poll.h>

#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_upool.h"

extern int  hosts_ctl(char *daemon, char *name, char *addr, char *user);
int         allow_severity = LOG_INFO;
int         deny_severity = LOG_WARNING;

static void diod_setup_listen (struct pollfd **fdsp, int *nfdsp);
static void diod_service_loop (Npsrv *srv, struct pollfd *fds, int nfds);
static void diod_daemonize (void);

static void diod_ctl_register_ops (Npsrv *srv);

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif

#define DAEMON_NAME     "diod"

#define OPTIONS "fd:l:w:c:e:am"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"foreground",      no_argument,        0, 'f'},
    {"debug",           required_argument,  0, 'd'},
    {"listen",          required_argument,  0, 'l'},
    {"nwthreads",       required_argument,  0, 'w'},
    {"config-file",     required_argument,  0, 'c'},
    {"export",          required_argument,  0, 'e'},
    {"allowany",        no_argument,        0, 'a'},
    {"no-munge-auth",   no_argument,        0, 'm'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void
usage()
{
    fprintf (stderr, 
"Usage: %s [OPTIONS]\n"
"   -f,--foreground        do not fork and disassociate with tty\n"
"   -d,--debug MASK        set debugging mask\n"
"   -l,--listen IP:PORT    set interface to listen on (just one allowed)\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -c,--config-file FILE  set config file path\n"
"   -e,--export PATH       export PATH (just one allowed)\n"
"   -a,--allowany          disable TCP wrappers checks\n"
"   -m,--no-munge-auth     do not require munge authentication\n"
"Note: command line overrides config file\n",
             DAEMON_NAME);
    exit (1);
}

int
main(int argc, char **argv)
{
    Npsrv *srv;
    int c;
    int fopt = 0;
    int dopt = 0;
    int aopt = 0;
    int mopt = 0;
    char *lopt = NULL;
    char *copt = NULL;
    int wopt = 0;
    char *eopt = NULL;
    struct pollfd *fds = NULL;
    int nfds = 0;
   
    diod_log_init (argv[0]); 
    if (!isatty (STDERR_FILENO))
        diod_log_to_syslog();
    diod_conf_init ();

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'f':   /* --foreground */
                fopt = 1;
                break;
            case 'd':   /* --debug MASK */
                dopt = strtoul (optarg, NULL, 0);
                break;
            case 'l':   /* --listen HOST:PORT */
                if (!strchr (optarg, ':'))
                    usage ();
                lopt = optarg;
                break;
            case 'w':   /* --nwthreads INT */
                wopt = strtoul (optarg, NULL, 10);
                break;
            case 'c':   /* --config-file PATH */
                copt = optarg;
                break;
            case 'e':   /* --export PATH */
                eopt = optarg;
                break;
            case 'a':   /* --allowany */
                aopt = 1;
                break;
            case 'm':   /* --no-munge-auth */
                mopt = 1;
                break;
            default:
                usage();
        }
    }
    if (optind < argc)
        usage();

    /* config file overrides defaults */
    diod_conf_init_config_file (copt);

    /* command line overrides config file */
    if (fopt)
        diod_conf_set_foreground (1);
    if (aopt)
        diod_conf_set_tcpwrappers (0);
    if (dopt)
        diod_conf_set_debuglevel (dopt);
    if (lopt)
        diod_conf_set_listen (lopt);
    if (wopt)
        diod_conf_set_nwthreads (strtoul (optarg, NULL, 10));
    if (eopt)  
        diod_conf_set_export (eopt);
    if (mopt)  
        diod_conf_set_munge (0);

    /* sane config? */
    diod_conf_validate_exports ();
#if ! HAVE_TCP_WRAPPERS
    if (diod_conf_get_tcpwrappers ())
        msg_exit ("no TCP wrapper support, yet config enables it");
#endif
#if ! HAVE_LIBMUNGE
    if (diod_conf_get_munge ())
        msg_exit ("no munge support, yet config enables it");
#endif

    srv = np_srv_create (diod_conf_get_nwthreads ());
    if (!srv)
        msg_exit ("out of memory");

    diod_setup_listen (&fds, &nfds);

    if (!diod_conf_get_foreground ())
        diod_daemonize ();

    diod_ctl_register_ops (srv);
    diod_service_loop (srv, fds, nfds);
    /*NOTREACHED*/

    exit (0);
}

/* Given a ip:port to listen on, open sockets and expand pollfd array
 * to include new the fds.  May be called more than once, e.g. if there are
 * multiple ip:port pairs to listen on.
 * N.B. host 0.0.0.0 is ipv4 for any interface.
 */
static void
diod_setup_listen_one (char *host, char *port, struct pollfd **fdsp, int *nfdsp)
{
    struct addrinfo hints, *res, *r;
    int opt, i, error, fd, nents = 0;
    struct pollfd *fds = *fdsp;
    int nfds = *nfdsp;

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((error = getaddrinfo (host, port, &hints, &res)))
        msg_exit ("getaddrinfo: %s:%s: %s", host, port, gai_strerror(error));
    if (res == NULL)
        msg_exit ("listen address has no addrinfo: %s:%s\n", host, port);

    for (r = res; r != NULL; r = r->ai_next)
        nents++;
    if (fds)
        fds = realloc (fds, sizeof(struct pollfd) * (nents + nfds));
    else
        fds = malloc (sizeof(struct pollfd) * nents);
    if (!fds)
            msg_exit ("out of memory");

    for (r = res; r != NULL; r = r->ai_next, i++) {
        if ((fd = socket (r->ai_family, r->ai_socktype, 0)) < 0)
            err_exit ("socket: %s:%s", host, port);
        opt = 1;
        if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            err ("setsockopt: %s:%s", host, port);
            close (fd);
            exit (1);
        }
        if (bind (fd, r->ai_addr, r->ai_addrlen) < 0) {
            err ("bind: %s:%s", host, port);
            close (fd);
            exit (1);
        }
        if (listen (fd, 5) < 0) {
            err ("listen: %s:%s", host, port);
            close (fd);
            exit (1);
        }
        fds[nfds++].fd = fd;
    }
    freeaddrinfo (res);

    *fdsp = fds;
    *nfdsp = nfds;
}

/* Set up listen ports.
 */
static void
diod_setup_listen (struct pollfd **fdsp, int *nfdsp)
{
    List l = diod_conf_get_listen ();
    ListIterator itr;
    char *hostport, *host, *port;

    itr = list_iterator_create(l);
    while ((hostport = list_next(itr))) {
        if ((host = strdup (hostport)) == NULL)
            msg_exit ("out of memory");
        port = strchr (host, ':');
        *port++ = '\0';
        diod_setup_listen_one (host, port, fdsp, nfdsp);
    }
    list_iterator_destroy(itr);
}


/* Accept one connection on a ready fd and pass it on to the npfs 9P engine.
 */
static void
diod_accept_one (Npsrv *srv, int fd)
{
    struct sockaddr_storage addr;
    socklen_t addr_size = sizeof(addr);
    char host[NI_MAXHOST], ip[NI_MAXHOST], svc[NI_MAXSERV];
    int res;
    Npconn *conn;
    Nptrans *trans;

    fd = accept (fd, (struct sockaddr *)&addr, &addr_size);
    if (fd < 0) {
        if (errno == EWOULDBLOCK || errno == ECONNABORTED || errno == EPROTO
                                                          || errno == EINTR)
            return; /* client died between its connect and our accept */
        else 
            err_exit ("accept");
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
    if (diod_conf_get_tcpwrappers ()) {
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
static void
diod_service_loop (Npsrv *srv, struct pollfd *fds, int nfds)
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
                diod_accept_one (srv, fds[i].fd);
            }
        }
    }
    /*NOTREACHED*/
}

static void
diod_daemonize (void)
{
    char rdir[PATH_MAX];

    snprintf (rdir, sizeof(rdir), "%s/run/%s", X_LOCALSTATEDIR, DAEMON_NAME);

    if (chdir (rdir) < 0)
        err_exit ("chdir %s", rdir);
    if (daemon (1, 0) < 0)
        err_exit ("daemon");
    diod_log_to_syslog();
}

static void
diod_ctl_register_ops (Npsrv *srv)
{
    srv->dotu = 1;
    srv->msize = 65536;
    srv->upool = diod_upool;
/*
    srv->attach = diod_attach;
    srv->clone = diod_clone;
    srv->walk = diod_walk;
    srv->open = diod_open;
    srv->read = diod_read;
    srv->clunk = diod_clunk;
    srv->stat = diod_stat;
    srv->flush = diod_flush;
    srv->fiddestroy = diod_fiddestroy;
*/

    srv->debuglevel = diod_conf_get_debuglevel ();
    srv->debugprintf = msg;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
