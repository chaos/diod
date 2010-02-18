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
#define _BSD_SOURCE         /* daemon */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <string.h>
#include <poll.h>

#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_sock.h"

#include "ops.h"

static void _daemonize (void);

#define OPTIONS "fd:l:w:c:e:armxF:"
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
    {"readahead",       no_argument,        0, 'r'},
    {"no-munge-auth",   no_argument,        0, 'm'},
    {"exit-on-lastuse", no_argument,        0, 'x'},
    {"listen-fds",      required_argument,  0, 'F'},
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
"   -d,--debug MASK        set debugging mask\n"
"   -l,--listen IP:PORT    set interface to listen on (just one allowed)\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -c,--config-file FILE  set config file path\n"
"   -e,--export PATH       export PATH (just one allowed)\n"
"   -a,--allowany          disable TCP wrappers checks\n"
"   -r,--readahead         do not disable kernel readahead with fadvise\n"
"   -m,--no-munge-auth     do not require munge authentication\n"
"   -x,--exit-on-lastuse   exit when transport count decrements to zero\n"
"   -F,--listen-fds N      listen for connections on the first N fds\n"
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
    int ropt = 0;
    int mopt = 0;
    int xopt = 0;
    int Fopt = 0;
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
            case 'r':   /* --readahead */
                ropt = 1;
                break;
            case 'm':   /* --no-munge-auth */
                mopt = 1;
                break;
            case 'x':   /* --exit-on-lastuse */
                xopt = 1;
                break;
            case 'F':   /* --listen-fds N */
                Fopt = strtoul (optarg, NULL, 10);
                break;
            default:
                usage();
        }
    }
    if (optind < argc)
        usage();
    if (lopt && Fopt)
        msg_exit ("--listen-fds and --listen are mutually exclusive options");

    /* config file overrides defaults */
    diod_conf_init_config_file (copt);

    /* command line overrides config file */
    if (fopt)
        diod_conf_set_foreground (1);
    if (ropt)
        diod_conf_set_readahead (1);
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
    if (xopt)  
        diod_conf_set_exit_on_lastuse (1);

    /* sane config? */
    if (!diod_conf_get_listen () && Fopt == 0)
        msg_exit ("no listen address specified");
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
    if (Fopt) {
        if (!diod_sock_listen_fds (&fds, &nfds, Fopt))
            msg_exit ("failed to set up listen ports");
    } else {
        if (!diod_sock_listen_list (&fds, &nfds, diod_conf_get_listen ()))
            msg_exit ("failed to set up listen ports");
    }

    if (!diod_conf_get_foreground ())
        _daemonize ();

    diod_register_ops (srv);
    diod_sock_accept_loop (srv, fds, nfds, diod_conf_get_tcpwrappers ());
    /*NOTREACHED*/

    exit (0);
}

static void
_daemonize (void)
{
    char rdir[PATH_MAX];

    snprintf (rdir, sizeof(rdir), "%s/run/diod", X_LOCALSTATEDIR);

    if (chdir (rdir) < 0)
        err_exit ("chdir %s", rdir);
    if (daemon (1, 0) < 0)
        err_exit ("daemon");
    diod_log_to_syslog();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
