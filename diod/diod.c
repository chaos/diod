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
#include <sys/resource.h>
#include <string.h>
#include <poll.h>

#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_sock.h"
#include "diod_upool.h"

#include "ops.h"

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif

#define OPTIONS "d:l:w:e:E:amxF:u:A:L:s:"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"debug",           required_argument,  0, 'd'},
    {"listen",          required_argument,  0, 'l'},
    {"nwthreads",       required_argument,  0, 'w'},
    {"export",          required_argument,  0, 'e'},
    {"export-file",     required_argument,  0, 'E'},
    {"allowany",        no_argument,        0, 'a'},
    {"no-munge-auth",   no_argument,        0, 'm'},
    {"exit-on-lastuse", no_argument,        0, 'x'},
    {"listen-fds",      required_argument,  0, 'F'},
    {"runas-uid",       required_argument,  0, 'u'},
    {"atomic-max",      required_argument,  0, 'A'},
    {"log-to",          required_argument,  0, 'L'},
    {"stats",           required_argument,  0, 's'},
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
"   -d,--debug MASK        set debugging mask\n"
"   -l,--listen IP:PORT    set interface to listen on (multiple -l allowed)\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -e,--export PATH       export PATH (multiple -e allowed)\n"
"   -a,--allowany          disable TCP wrappers checks\n"
"   -m,--no-munge-auth     do not require munge authentication\n"
"   -x,--exit-on-lastuse   exit when transport count decrements to zero\n"
"   -F,--listen-fds N      listen for connections on the first N fds\n"
"   -u,--runas-uid UID     only allow UID to attach\n"
"   -E,--export-file PATH  read exports from PATH (one per line)\n"
"   -A,--atomic-max INT    set the maximum atomic I/O size, in megabytes\n"
"   -L,--log-to DEST       log to DEST, can be syslog, stderr, or file\n"
"   -s,--stats FILE        log detailed I/O stats to FILE\n"
    );
    exit (1);
}

int
main(int argc, char **argv)
{
    Npsrv *srv;
    int c;
    int Fopt = 0;
    int eopt = 0;
    int lopt = 0;
    struct pollfd *fds = NULL;
    int nfds = 0;
    uid_t uid;
    List hplist;
    unsigned long amax;
   
    diod_log_init (argv[0]); 
    diod_conf_init ();

    /* Command line overrides defaults.
     * Diod does not look at the config file.
     */
    optind = 0;
    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'd':   /* --debug MASK */
                diod_conf_set_debuglevel (strtoul (optarg, NULL, 0));
                break;
            case 'l':   /* --listen HOST:PORT */
                if (!lopt) {
                    diod_conf_clr_diodlisten ();
                    lopt = 1;
                }
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
                if (!eopt) {
                    diod_conf_clr_export ();
                    eopt = 1;
                }
                diod_conf_add_export (optarg);
                break;
            case 'E':   /* --export-file PATH */
                if (!eopt) {
                    diod_conf_clr_export ();
                    eopt = 1;
                }
                diod_conf_read_exports (optarg);
                break;
            case 'a':   /* --allowany */
                diod_conf_set_tcpwrappers (0);
                break;
            case 'm':   /* --no-munge-auth */
                diod_conf_set_munge (0);
                break;
            case 'x':   /* --exit-on-lastuse */
                diod_conf_set_exit_on_lastuse (1);
                break;
            case 'F':   /* --listen-fds N */
                Fopt = strtoul (optarg, NULL, 10);
                break;
            case 'u':   /* --runas-uid UID */
                errno = 0;
                uid = strtoul (optarg, NULL, 10);
                if (errno != 0)
                    err_exit ("--runas-uid argument");
                diod_conf_set_runasuid (uid);
                break;
            case 'A':   /* --atomic-max INT */
                errno = 0;
                amax = strtoul (optarg, NULL, 10);
                if (errno != 0)
                    err_exit ("--atomic-max argument");
                diod_conf_set_atomic_max (amax);
                break;
            case 'L':   /* --log-to DEST */
                diod_log_set_dest (optarg);
                break;
            case 's':   /* --stats PATH */
                diod_conf_set_statslog (optarg);
                break;
            default:
                usage();
        }
    }
    if (optind < argc)
        usage();

    /* sane config? */
    if (!diod_conf_get_diodlisten () && Fopt == 0)
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

    /* drop privileges, unless running for multiple users */
    if (diod_conf_get_runasuid (&uid))
        diod_become_user (NULL, uid, 1);
    else if (geteuid () != 0)
        msg_exit ("if not running as root, you must specify --runas-uid");

    srv = np_srv_create (diod_conf_get_nwthreads ());
    if (!srv)
        msg_exit ("out of memory");
    if (Fopt) {
        if (!diod_sock_listen_nfds (&fds, &nfds, Fopt, 3))
            msg_exit ("failed to set up listen ports");
    } else {
        hplist = diod_conf_get_diodlisten ();
        if (!diod_sock_listen_hostport_list (hplist, &fds, &nfds, NULL, 0))
            msg_exit ("failed to set up listen ports");
    }

    diod_register_ops (srv);
    diod_sock_accept_loop (srv, fds, nfds, diod_conf_get_tcpwrappers ());
    /*NOTREACHED*/

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
