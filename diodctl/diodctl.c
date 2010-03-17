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

/* diodctl.c - super server for diod (runs as root on 9pfs port) */

/* What we do:
 * - serve /diodctl synthetic file system
 * - file 'exports' contains list of I/O node exports
 * - file 'server' contains server ip:port
 * - when an attached user reads 'server', if a diod server is already running
 *   for that user, return ip:port.  If not, spawn it and return ip:port.
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
#include <sys/wait.h>
#include <sys/param.h>
#include <string.h>
#include <sys/resource.h>
#include <poll.h>
#include <assert.h>

#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_upool.h"
#include "diod_sock.h"

#include "ops.h"
#include "serv.h"

static void          _daemonize (void);
static void          _setrlimit (void); 

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif

#define OPTIONS "fd:l:w:c:e:amD:p"
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
    {"diod-path",       required_argument,  0, 'D'},
    {"allow-private",   no_argument,        0, 'p'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void
usage()
{
    fprintf (stderr, 
"Usage: diodctl [OPTIONS]\n"
"   -f,--foreground        do not fork and disassociate with tty\n"
"   -d,--debug MASK        set debugging mask\n"
"   -l,--listen IP:PORT    set interface to listen on (just one allowed)\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -c,--config-file FILE  set config file path\n"
"   -e,--export PATH       export PATH (just one allowed)\n"
"   -a,--allowany          disable TCP wrappers checks\n"
"   -m,--no-munge-auth     do not require munge authentication\n"
"   -D,--diod-path PATH    set path to diod executable\n"
"   -p,--allow-private     spawn private copies of diod for users\n"
"Note: command line overrides config file\n");
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
    int popt = 0;
    char *lopt = NULL;
    char *copt = NULL;
    int wopt = 0;
    char *eopt = NULL;
    char *Dopt = NULL;
    struct pollfd *fds = NULL;
    int nfds = 0;
    List hplist;
   
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
            case 'D':   /* --diod-path PATH */
                Dopt = optarg;
                break;
            case 'p':   /* --allow-private */
                popt = 1;
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
        diod_conf_set_diodctllisten (lopt);
    if (wopt)
        diod_conf_set_nwthreads (strtoul (optarg, NULL, 10));
    if (eopt)  
        diod_conf_set_export (eopt);
    if (mopt)  
        diod_conf_set_munge (0);
    if (Dopt)  
        diod_conf_set_diodpath (Dopt);
    if (popt)  
        diod_conf_set_allowprivate (1);

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

    if (geteuid () != 0)
        msg_exit ("must run as root");
    _setrlimit ();

    if (!diod_conf_get_foreground ())
        _daemonize ();

    srv = np_srv_create (diod_conf_get_nwthreads ());
    if (!srv)
        msg_exit ("out of memory");

    hplist = diod_conf_get_diodctllisten ();
    if (!diod_sock_listen_hostport_list (hplist, &fds, &nfds, NULL, 0))
        msg_exit ("failed to set up listen ports");

    /* FIXME: temp file created by diod_conf_mkconfig () needs cleanup */
    diodctl_serv_init (diod_conf_mkconfig ());
    diodctl_register_ops (srv);
    diod_sock_accept_loop (srv, fds, nfds, diod_conf_get_tcpwrappers ());
    /*NOTREACHED*/

    exit (0);
}

static void
_daemonize (void)
{
    char rdir[PATH_MAX];

    snprintf (rdir, sizeof(rdir), "%s/run/diod", X_LOCALSTATEDIR);

    if (chdir (rdir) < 0 && chdir ("/") < 0)
        err_exit ("chdir /");
    if (daemon (1, 0) < 0)
        err_exit ("daemon");
    diod_log_to_syslog();
}

/* Remove any resource limits that might hamper our (non-root) children.
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
