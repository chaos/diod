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
#include <poll.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_sock.h"
#include "diod_upool.h"

#include "ops.h"

static void          _daemonize (void);
static void          _setrlimit (void);

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif

#define OPTIONS "fd:l:w:e:EF:u:L:s:nc:"

#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"foreground",      no_argument,        0, 'f'},
    {"debug",           required_argument,  0, 'd'},
    {"listen",          required_argument,  0, 'l'},
    {"nwthreads",       required_argument,  0, 'w'},
    {"export",          required_argument,  0, 'e'},
    {"export-all",      no_argument,        0, 'E'},
    {"no-auth",         no_argument,        0, 'n'},
    {"listen-fds",      required_argument,  0, 'F'},
    {"runas-uid",       required_argument,  0, 'u'},
    {"logdest",         required_argument,  0, 'L'},
    {"stats",           required_argument,  0, 's'},
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
"   -l,--listen IP:PORT    set interface to listen on (multiple -l allowed)\n"
"   -F,--listen-fds N      listen for connections on the first N fds\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -e,--export PATH       export PATH (multiple -e allowed)\n"
"   -E,--export-all        export all mounted file systems\n"
"   -n,--no-auth           disable authentication check\n"
"   -u,--runas-uid UID     only allow UID to attach\n"
"   -L,--logdest DEST      log to DEST, can be syslog, stderr, or file\n"
"   -d,--debug MASK        set debugging mask\n"
"   -s,--stats FILE        log detailed I/O stats to FILE\n"
"   -c,--config-file FILE  set config file path\n"
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
    char *copt = NULL;
    char *end;
    int client_on_stdin = 1;
   
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
                    diod_conf_clr_exports ();
                    eopt = 1;
                }
                diod_conf_add_exports (optarg);
                break;
            case 'E':   /* --export-all */
                diod_conf_set_exportall (1);
                break;
            case 'n':   /* --no-auth */
                diod_conf_set_auth_required (0);
                break;
            case 'F':   /* --listen-fds N */
                Fopt = strtoul (optarg, NULL, 10);
                client_on_stdin = 0;
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
            case 's':   /* --stats PATH */
                diod_conf_set_statslog (optarg);
                break;
            default:
                usage();
        }
    }
    if (optind < argc)
        usage();

    diod_conf_validate_exports ();

    if (client_on_stdin) {
        List hplist = diod_conf_get_diodlisten ();

        if (hplist && list_count (hplist) > 0)
            client_on_stdin = 0; 
    }
    if (client_on_stdin)
        diod_conf_set_foreground (1);

    if (geteuid () == 0)
        _setrlimit ();

    if (!diod_conf_get_foreground ()) {
        char *logdest = diod_conf_get_logdest ();

        if (!Fopt)
            _daemonize ();
        diod_log_set_dest (logdest ? logdest : "syslog");
    }
    diod_conf_arm_sighup ();

    /* Drop root permission if running as one user.
     * If not root, arrange to run (only) as current effective uid.
     */
    if (geteuid () != 0)
        diod_conf_set_runasuid (geteuid ());
    else if (diod_conf_opt_runasuid ()) {
        uid_t uid = diod_conf_get_runasuid ();

        if (uid != geteuid ())
            diod_become_user (NULL, uid, 1); /* exits on error */
    }

    srv = np_srv_create (diod_conf_get_nwthreads ());
    if (!srv)
        err_exit ("np_srv_create");
    diod_register_ops (srv);

    /* Client inherited on fd=0.  Exit on disconnect.
     */
    if (client_on_stdin) {
        diod_sock_startfd (srv, 0, "stdin", "0.0.0.0", "0", 1);

    /* Listen on bound sockets 0 thru value of Fopt inherited
     * from diodctl.  Exit after a period of 30s with no connections.
     */
    } else if (Fopt) {
        struct pollfd *fds = NULL;
        int nfds = 0;

        if (!diod_sock_listen_nfds (&fds, &nfds, Fopt, 3))
            msg_exit ("failed to set up listen ports");
        diod_sock_accept_batch (srv, fds, nfds);

    /* Listen on interface/port designation from -L or config file.
     * Run forever.
     */
    } else {
        List hplist = diod_conf_get_diodlisten ();
        struct pollfd *fds = NULL;
        int nfds = 0;
    
        if (!diod_sock_listen_hostport_list (hplist, &fds, &nfds, NULL, 0))
            msg_exit ("failed to set up listen ports");
        diod_sock_accept_loop (srv, fds, nfds);
        /*NOTREACHED*/
    }

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
 * Disassociate from parents controlling tty.  Switch logging to syslog.
 * Exit on error.
 */
static void
_daemonize (void)
{
    char rdir[PATH_MAX];
    struct stat sb;

    snprintf (rdir, sizeof(rdir), "%s/run/diod", X_LOCALSTATEDIR);
    if (stat (rdir, &sb) < 0) {
        if (mkdir (rdir, 0755) < 0)
            err_exit ("mkdir %s", rdir);
    } else if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s is not a directory", rdir);

    if (chdir (rdir) < 0)
        err_exit ("chdir %s", rdir);
    if (daemon (1, 0) < 0)
        err_exit ("daemon");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
