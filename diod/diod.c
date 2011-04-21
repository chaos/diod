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

/* diod.c - distributed I/O daemon */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE         /* ppoll () */
#endif
#include <poll.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE         /* daemon () */
#endif
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_sock.h"
#include "diod_upool.h"

#include "ops.h"

typedef enum { SRV_STDIN, SRV_DIODCTL, SRV_NORMAL } srvmode_t;

static void          _daemonize (void);
static void          _setrlimit (void);
static void          _service_run (srvmode_t mode, int Fopt);

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif

#define OPTIONS "fsd:l:w:e:EF:u:SL:nc:"

#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"foreground",      no_argument,        0, 'f'},
    {"stdin",           no_argument,        0, 's'},
    {"debug",           required_argument,  0, 'd'},
    {"listen",          required_argument,  0, 'l'},
    {"nwthreads",       required_argument,  0, 'w'},
    {"export",          required_argument,  0, 'e'},
    {"export-all",      no_argument,        0, 'E'},
    {"no-auth",         no_argument,        0, 'n'},
    {"diodctl",         required_argument,  0, 'F'},
    {"runas-uid",       required_argument,  0, 'u'},
    {"allsquash",       no_argument,        0, 'S'},
    {"logdest",         required_argument,  0, 'L'},
    {"config-file",     required_argument,  0, 'c'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void
usage()
{
    fprintf (stderr, 
"Usage: diod [OPTIONS]\n"
"   -f,--foreground        do not fork and disassociate with tty\n"
"   -s,--stdin             service connected client on stdin\n"
"   -l,--listen IP:PORT    set interface to listen on (multiple -l allowed)\n"
"   -F,--diodctl N         listen for connections on the first N fds\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -e,--export PATH       export PATH (multiple -e allowed)\n"
"   -E,--export-all        export all mounted file systems\n"
"   -n,--no-auth           disable authentication check\n"
"   -u,--runas-uid UID     only allow UID to attach\n"
"   -S,--allsquash         map all users to nobody\n"
"   -L,--logdest DEST      log to DEST, can be syslog, stderr, or file\n"
"   -d,--debug MASK        set debugging mask\n"
"   -c,--config-file FILE  set config file path\n"
    );
    exit (1);
}

int
main(int argc, char **argv)
{
    int c;
    int Fopt = 0;
    char *copt = NULL;
    char *end;
    srvmode_t mode = SRV_NORMAL;
   
    diod_log_init (argv[0]); 
    diod_conf_init ();

    /* config file overrides defaults */
    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'c':   /* --config-file PATH */
                copt = optarg;
                break;
            default:
                break;
        }
    }
    diod_conf_init_config_file (copt);

    /* Command line overrides config file.
     */
    optind = 0;
    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'f':   /* --foreground */
                diod_conf_set_foreground (1);
                break;
            case 's':   /* --stdin */
                mode = SRV_STDIN;
                diod_conf_set_foreground (1);
                break;
            case 'd':   /* --debug MASK */
                diod_conf_set_debuglevel (strtoul (optarg, NULL, 0));
                break;
            case 'l':   /* --listen HOST:PORT */
                if (!diod_conf_opt_diodlisten ())
                    diod_conf_clr_diodlisten ();
                if (!strchr (optarg, ':'))
                    usage ();
                diod_conf_add_diodlisten (optarg);
                break;
            case 'w':   /* --nwthreads INT */
                diod_conf_set_nwthreads (strtoul (optarg, NULL, 10));
                break;
            case 'c':   /* --config-file PATH */
                break;
            case 'e':   /* --export PATH */
                if (!diod_conf_opt_exports ())
                    diod_conf_clr_exports ();
                diod_conf_add_exports (optarg);
                break;
            case 'E':   /* --export-all */
                diod_conf_set_exportall (1);
                break;
            case 'n':   /* --no-auth */
                diod_conf_set_auth_required (0);
                break;
            case 'S':   /* --allsquash */
                diod_conf_set_allsquash (1);
                break;
            case 'F':   /* --diodctl N */
                mode = SRV_DIODCTL;
                Fopt = strtoul (optarg, NULL, 10);
                diod_conf_set_foreground (1);
                break;
            case 'u':   /* --runas-uid UID */
                if (geteuid () == 0) {
                    uid_t uid;

                    errno = 0;
                    uid = strtoul (optarg, &end, 10);
                    if (errno != 0)
                        err_exit ("error parsing --runas-uid argument");
                    if (*end != '\0')
                        msg_exit ("error parsing --runas-uid argument");
                    diod_conf_set_runasuid (uid);
                } else 
                    msg_exit ("must be root to run diod as another user");
                break;
            case 'L':   /* --logdest DEST */
                diod_conf_set_logdest (optarg);
                diod_log_set_dest (optarg);
                break;
            default:
                usage();
        }
    }
    if (optind < argc)
        usage();

    diod_conf_validate_exports ();

    if (geteuid () == 0)
        _setrlimit ();

    /* Drop root permission if running as one user.
     * If not root, arrange to run (only) as current effective uid.
     */
    if (diod_conf_get_allsquash ())
        diod_become_squashuser (); /* exits on error */
    else if (geteuid () != 0)
        diod_conf_set_runasuid (geteuid ());
    else if (diod_conf_opt_runasuid ()) {
        uid_t uid = diod_conf_get_runasuid ();

        if (uid != geteuid ())
            diod_become_user (NULL, uid, 1); /* exits on error */
    }

    if (!diod_conf_get_foreground ()) {
        diod_log_set_dest (diod_conf_get_logdest ());
        _daemonize ();
    }

    _service_run (mode, Fopt);

    diod_conf_fini ();
    diod_log_fini ();

    exit (0);
}

/* Remove any resource limits.
 * Exit on error
 */
static void
_setrlimit (void)
{
    struct rlimit r, r2;

    r.rlim_cur = r.rlim_max = RLIM_INFINITY;
    if (setrlimit (RLIMIT_FSIZE, &r) < 0)
        err_exit ("setrlimit RLIMIT_FSIZE");

    r.rlim_cur = r.rlim_max = NR_OPEN;
    r2.rlim_cur = r2.rlim_max = sysconf(_SC_OPEN_MAX);
    if (setrlimit (RLIMIT_NOFILE, &r) < 0)
        if (errno != EPERM || setrlimit (RLIMIT_NOFILE, &r2) < 0)
            err_exit ("setrlimit RLIMIT_NOFILE");

    r.rlim_cur = r.rlim_max = RLIM_INFINITY;
    if (setrlimit (RLIMIT_LOCKS, &r) < 0)
        err_exit ("setrlimit RLIMIT_LOCKS");

    r.rlim_cur = r.rlim_max = RLIM_INFINITY;
    if (setrlimit (RLIMIT_CORE, &r) < 0)
        err_exit ("setrlimit RLIMIT_CORE");

    r.rlim_cur = r.rlim_max = RLIM_INFINITY;
    if (setrlimit (RLIMIT_AS, &r) < 0)
        err_exit ("setrlimit RLIMIT_AS");
}

/* Create run directory if it doesn't exist and chdir there.
 * Disassociate from parent's controlling tty.  Switch logging to syslog.
 * Exit on error.
 */
static void
_daemonize (void)
{
    char rdir[PATH_MAX];
    struct stat sb;

    snprintf (rdir, sizeof(rdir), "%s/run/diod", X_LOCALSTATEDIR);
    if (stat (rdir, &sb) < 0) {
        if (mkdir (rdir, 0755) < 0) {
            msg ("failed to find/create %s, running out of /tmp", rdir);
            snprintf (rdir, sizeof(rdir), "/tmp");
        }
    } else if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s is not a directory", rdir);

    if (chdir (rdir) < 0)
        err_exit ("chdir %s", rdir);
    if (daemon (1, 1) < 0)
        err_exit ("daemon");
}

/**
 ** Service startup
 **/

struct svc_struct {
    Npsrv *srv;
    struct pollfd *fds;
    int nfds;
    pthread_t t;
    int shutdown;
    int reload;
};
static struct svc_struct ss;

static void
_sighand (int sig)
{
    switch (sig) {
        case SIGHUP:
            msg ("caught SIGHUP: reloading config");
            ss.reload = 1;
            break;
        case SIGTERM:
            msg ("caught SIGTERM: shutting down");
            ss.shutdown = 1;
            break;
        case SIGUSR1:
            ss.shutdown = 1;
            break;
        default:
            msg ("caught signal %d: ignoring", sig);
            break;
    }
}

/* Thread to handle SIGHUP, SIGTERM, and new connections on listen ports.
 */
static void *
_service_loop (void *arg)
{
    sigset_t sigs;
    int i;

    sigfillset (&sigs);
    sigdelset (&sigs, SIGHUP);
    sigdelset (&sigs, SIGTERM);
    sigdelset (&sigs, SIGUSR1);

    if (ss.nfds == 0)
        diod_sock_startfd (ss.srv, 0, "stdin", "0.0.0.0", "0");
    while (!ss.shutdown) {
        if (ss.reload) {
            diod_conf_init_config_file (NULL);
            ss.reload = 0;
        }
        for (i = 0; i < ss.nfds; i++) {
            ss.fds[i].events = POLLIN;
            ss.fds[i].revents = 0;
        }
        if (ppoll (ss.fds, ss.nfds, NULL, &sigs) < 0) {
            if (errno == EINTR)
                continue;
            err_exit ("ppoll");
        }
        for (i = 0; i < ss.nfds; i++) {
            if ((ss.fds[i].revents & POLLIN)) {
                diod_sock_accept_one (ss.srv, ss.fds[i].fd);
            }
        }
    }
    np_srv_shutdown (ss.srv);
    return NULL;
}

/* Set up signal handlers for SIGHUP and SIGTERM and block them.
 * Threads will inherit this signal mask; _service_loop () will unblock.
 */
static void
_service_sigsetup (void)
{
    struct sigaction sa;
    sigset_t sigs;

    sa.sa_flags = 0;
    sigemptyset (&sa.sa_mask);
    sa.sa_handler = _sighand;
    if (sigaction (SIGHUP, &sa, NULL) < 0)
        err_exit ("sigaction");
    if (sigaction (SIGTERM, &sa, NULL) < 0)
        err_exit ("sigaction");
    if (sigaction (SIGUSR1, &sa, NULL) < 0)
        err_exit ("sigaction");

    sigemptyset (&sigs);
    sigaddset (&sigs, SIGHUP);
    sigaddset (&sigs, SIGTERM);
    sigaddset (&sigs, SIGUSR1);
    if ((sigprocmask (SIG_BLOCK, &sigs, NULL) < 0))
        err_exit ("sigprocmask");
}

static void
_service_run (srvmode_t mode, int Fopt)
{
    List l = diod_conf_get_diodlisten ();
    int nt = diod_conf_get_nwthreads ();
    int n;

    ss.shutdown = 0;
    ss.reload = 0;
    _service_sigsetup ();

    if (!(ss.srv = np_srv_create (nt)))
        err_exit ("np_srv_create");
    diod_register_ops (ss.srv);

    ss.fds = NULL;
    ss.nfds = 0;
    switch (mode) {
        case SRV_STDIN:
            break;
        case SRV_DIODCTL:
            if (!diod_sock_listen_nfds (&ss.fds, &ss.nfds, Fopt, 3))
                msg_exit ("failed to set up listen ports");
            break;
        case SRV_NORMAL:
            if (!diod_sock_listen_hostports (l, &ss.fds, &ss.nfds, NULL, 0))
                msg_exit ("failed to set up listen ports");
            break;
    }

    if ((n = pthread_create (&ss.t, NULL, _service_loop, NULL))) {
        errno = n;
        err_exit ("pthread_create _service_loop");
    }

    switch (mode) {
        case SRV_STDIN:
            np_srv_wait_conncount (ss.srv, 1);
            pthread_kill (ss.t, SIGUSR1);
            break;
        case SRV_DIODCTL:
            np_srv_wait_timeout (ss.srv, 30);
            pthread_kill (ss.t, SIGUSR1);
            break;
        case SRV_NORMAL:
            break;
    }
    if ((n = pthread_join (ss.t, NULL))) {
        errno = n;
        err_exit ("pthread_join _service_loop");
    }

    np_srv_destroy (ss.srv);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
