/*****************************************************************************
 *  Copyright (C) 2010-11 Lawrence Livermore National Security, LLC.
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
#include <poll.h>
#ifndef _BSD_SOURCE
#define _BSD_SOURCE         /* daemon () */
#endif
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/resource.h>
#ifndef __MACH__
#include <sys/prctl.h>
#endif
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_sock.h"
#if WITH_RDMATRANS
#include "diod_rdma.h"
#endif

#include "ops.h"

typedef enum { SRV_FILEDES, SRV_SOCKTEST, SRV_NORMAL } srvmode_t;

static void          _daemonize (void);
static void          _setrlimit (void);
static void          _become_user (char *name, uid_t uid, int realtoo);
static void          _service_run (srvmode_t mode, int rfdno, int wfdno);

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif

#define OPTIONS "fr:w:d:l:t:e:Eo:u:SL:npc:NU:s"

#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"foreground",      no_argument,        0, 'f'},
    {"rfdno",           required_argument,  0, 'r'},
    {"wfdno",           required_argument,  0, 'w'},
    {"debug",           required_argument,  0, 'd'},
    {"listen",          required_argument,  0, 'l'},
    {"nwthreads",       required_argument,  0, 't'},
    {"export",          required_argument,  0, 'e'},
    {"export-all",      no_argument,        0, 'E'},
    {"export-opts",     required_argument,  0, 'o'},
    {"no-auth",         no_argument,        0, 'n'},
    {"statfs-passthru", no_argument,        0, 'p'},
    {"no-userdb",       no_argument,        0, 'N'},
    {"runas-uid",       required_argument,  0, 'u'},
    {"allsquash",       no_argument,        0, 'S'},
    {"squashuser",      required_argument,  0, 'U'},
    {"logdest",         required_argument,  0, 'L'},
    {"config-file",     required_argument,  0, 'c'},
    {"socktest",        no_argument,        0, 's'},
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
"   -r,--rfdno             service connected client on read file descriptor\n"
"   -w,--wfdno             service connected client on write file descriptor\n"
"   -l,--listen IP:PORT    set interface to listen on (multiple -l allowed)\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -e,--export PATH       export PATH (multiple -e allowed)\n"
"   -E,--export-all        export all mounted file systems\n"
"   -o,--export-opts       set global export options (comma-seperated)\n"
"   -n,--no-auth           disable authentication check\n"
"   -p,--statfs-passthru   statfs should return underly f_type not V9FS_MAGIC\n"
"   -N,--no-userdb         bypass password/group file lookup\n"
"   -u,--runas-uid UID     only allow UID to attach\n"
"   -S,--allsquash         map all users to the squash user\n"
"   -U,--squashuser USER   set the squash user (default nobody)\n"
"   -L,--logdest DEST      log to DEST, can be syslog, stderr, or file\n"
"   -d,--debug MASK        set debugging mask\n"
"   -c,--config-file FILE  set config file path\n"
"   -s,--socktest          run in test mode where server exits early\n"
    );
    exit (1);
}

int
main(int argc, char **argv)
{
    int c;
    char *copt = NULL;
    srvmode_t mode = SRV_NORMAL;
    int rfdno = -1, wfdno = -1;
   
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
            case 'r':   /* --rfdno */
                mode = SRV_FILEDES;
                rfdno = strtoul (optarg, NULL, 10);
                break;
            case 'w':   /* --wfdno */
                mode = SRV_FILEDES;
                wfdno = strtoul (optarg, NULL, 10);
                break;
            case 'd':   /* --debug MASK */
                diod_conf_set_debuglevel (strtoul (optarg, NULL, 0));
                break;
            case 'l':   /* --listen HOST:PORT or /path/to/socket */
                if (!diod_conf_opt_listen ())
                    diod_conf_clr_listen ();
                if (!strchr (optarg, ':') && optarg[0] != '/')
                    usage ();
                diod_conf_add_listen (optarg);
                break;
            case 't':   /* --nwthreads INT */
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
            case 'o':   /* --export-ops opt,[opt,...] */
                diod_conf_set_exportopts (optarg);
                break;
            case 'n':   /* --no-auth */
                diod_conf_set_auth_required (0);
                break;
            case 'p':   /* --statfs-passthru */
                diod_conf_set_statfs_passthru (0);
                break;
            case 'N':   /* --no-userdb */
                diod_conf_set_userdb (0);
                break;
            case 'S':   /* --allsquash */
                diod_conf_set_allsquash (1);
                break;
            case 'U':   /* --squashuser USER */
                diod_conf_set_squashuser (optarg);
                break;
            case 'u': { /* --runas-uid UID */
                uid_t uid;
                char *end;

                errno = 0;
                uid = strtoul (optarg, &end, 10);
                if (errno != 0)
                    err_exit ("error parsing --runas-uid argument");
                if (*end != '\0')
                    msg_exit ("error parsing --runas-uid argument");
                diod_conf_set_runasuid (uid);
                break;
            }
            case 's':   /* --socktest */
                mode = SRV_SOCKTEST;
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
    if (diod_conf_opt_runasuid () && diod_conf_get_allsquash ())
        msg_exit ("--runas-uid and allsquash cannot be used together");
    if (mode == SRV_FILEDES && (rfdno == -1 || wfdno == -1))
        msg_exit ("--rfdno,wfdno must be used together");

    diod_conf_validate_exports ();

    if (geteuid () == 0)
        _setrlimit ();

    _service_run (mode, rfdno, wfdno);

    diod_conf_fini ();
    diod_log_fini ();

    exit (0);
}

/* Switch to user/group, load the user's supplementary groups.
 * Print message and exit on failure.
 */
static void
_become_user (char *name, uid_t uid, int realtoo)
{
    int err;
    struct passwd pw, *pwd;
    int len = sysconf(_SC_GETPW_R_SIZE_MAX);
    char *buf;
    int nsg;
    gid_t sg[64];
    char *endptr;

    if (len == -1)
        len = 4096;
    if (!(buf = malloc(len)))
        msg_exit ("out of memory");
    if (name) {
        errno = 0;
        uid = strtoul (name, &endptr, 10);
        if (errno == 0 && *name != '\0' && *endptr == '\0')
            name = NULL;
    }
    if (name) {
        if ((err = getpwnam_r (name, &pw, buf, len, &pwd)) != 0)
            errn_exit (err, "error looking up user %s", name);
        if (!pwd)
            msg_exit ("error looking up user %s", name);
    } else {
        if ((err = getpwuid_r (uid, &pw, buf, len, &pwd)) != 0)
            errn_exit (err, "error looking up uid %d", uid);
        if (!pwd)
            msg_exit ("error looking up uid %d", uid);
    }
    nsg = sizeof (sg) / sizeof(sg[0]);
    if (getgrouplist(pwd->pw_name, pwd->pw_gid, sg, &nsg) == -1)
        err_exit ("user is in too many groups");
    if (syscall(SYS_setgroups, nsg, sg) < 0)
        err_exit ("setgroups");
    if (setregid (realtoo ? pwd->pw_gid : -1, pwd->pw_gid) < 0)
        err_exit ("setreuid");
    if (setreuid (realtoo ? pwd->pw_uid : -1, pwd->pw_uid) < 0)
        err_exit ("setreuid");
    free (buf);
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
#ifndef __MACH__
    if (setrlimit (RLIMIT_LOCKS, &r) < 0)
        err_exit ("setrlimit RLIMIT_LOCKS");
#endif

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

    snprintf (rdir, sizeof(rdir), "%s/run/diod", X_LOCALSTATEDIR);
    if (mkdir (rdir, 0755) < 0 && errno != EEXIST) {
        msg ("failed to find/create %s, running out of /tmp", rdir);
        snprintf (rdir, sizeof(rdir), "/tmp");
    }
    if (chdir (rdir) < 0)
        err_exit ("chdir %s", rdir);
    if (daemon (1, 0) < 0)
        err_exit ("daemon");
}

/**
 ** Service startup
 **/

struct svc_struct {
    srvmode_t mode;
    int rfdno;
    int wfdno;
    Npsrv *srv;
    struct pollfd *fds;
    int nfds;
    pthread_t t;
    int shutdown;
    int reload;
#if WITH_RDMATRANS
    diod_rdma_t rdma;
    pthread_t rdma_t;
#endif
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
        case SIGUSR2:
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

    switch (ss.mode) {
        case SRV_FILEDES:
            diod_sock_startfd (ss.srv, ss.rfdno, ss.wfdno, "stdin", 0);
            break;
        case SRV_NORMAL:
        case SRV_SOCKTEST:
            break;
    }
    while (!ss.shutdown) {
        if (ss.reload) {
            diod_conf_init_config_file (NULL);
            np_usercache_flush (ss.srv);
            ss.reload = 0;
        }
        for (i = 0; i < ss.nfds; i++) {
            ss.fds[i].events = POLLIN;
            ss.fds[i].revents = 0;
        }
#ifndef __MACH__
        if (ppoll (ss.fds, ss.nfds, NULL, &sigs) < 0) {
            if (errno == EINTR)
                continue;
            err_exit ("ppoll");
        }
#else
        sigset_t oset;
        int val;
        val = pthread_sigmask(SIG_SETMASK, &sigs, &oset);
        if (val) {
            errno = val;
            err_exit("ppoll");
        }
        val = poll (ss.fds, ss.nfds, -1);
        pthread_sigmask(SIG_SETMASK, &oset, NULL);
        if (val < 0) {
            if (errno == EINTR)
                continue;
            err_exit ("ppoll");
        }
#endif
        for (i = 0; i < ss.nfds; i++) {
            if ((ss.fds[i].revents & POLLIN)) {
                diod_sock_accept_one (ss.srv, ss.fds[i].fd);
            }
        }
    }
    return NULL;
}

#if WITH_RDMATRANS
static void *
_service_loop_rdma (void *arg)
{
    while (!ss.shutdown) {
        msg ("waiting on rdma connection");
        diod_rdma_accept_one (ss.srv, ss.rdma);
    }
    return NULL;
}
#endif

/* Set up signal handlers for SIGHUP and SIGTERM and block them.
 * Threads will inherit this signal mask; _service_loop () will unblock.
 * Install handler for SIGUSR2 and don't block - this signal is used to
 * interrupt I/O operations when handling a 9p flush.
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
    if (sigaction (SIGUSR2, &sa, NULL) < 0)
        err_exit ("sigaction");

    sigemptyset (&sigs);
    sigaddset (&sigs, SIGHUP);
    sigaddset (&sigs, SIGTERM);
    sigaddset (&sigs, SIGUSR1);
    if ((sigprocmask (SIG_BLOCK, &sigs, NULL) < 0))
        err_exit ("sigprocmask");
}

/* Test whether setgroups applies to thread or process.
 * On RHEL6 (2.6.32 based) it applies to threads and we can use it.
 * On Ubuntu 11 (2.6.38 based) it applies to the whole process and we can't.
 */
void *
_test_setgroups_thread (void *arg)
{
    gid_t sg[] = { 42, 37, 63 };

    if (syscall(SYS_setgroups, 3, sg) < 0)
        err_exit ("setgroups");
    return NULL;
}
static int
_test_setgroups (void)
{
    long ngroups_max = sysconf(_SC_NGROUPS_MAX);
    gid_t *sg;
    int nsg, n;
    int rc = 0;
    pthread_t t;
    int err;

    if (ngroups_max < 0)
        ngroups_max = 64;
    if (!(sg = malloc (ngroups_max * sizeof (gid_t))))
        msg_exit ("out of memory");
    if ((nsg = getgroups (ngroups_max, sg)) < 0)
        err_exit ("getgroups");
    if (syscall(SYS_setgroups, 0, NULL) < 0) /* clear groups */
        err_exit ("setgroups");
    if ((err = pthread_create (&t, NULL, _test_setgroups_thread, NULL)))
        err_exit ("pthread_create"); 
    if ((err = pthread_join (t, NULL)))
        err_exit ("pthread_join");
    if ((n = getgroups (ngroups_max, sg)) < 0)
        err_exit ("getgroups");
    if (n == 0)
        rc = 1;
    if (syscall(SYS_setgroups, nsg, sg) < 0)
        err_exit ("setgroups");
    free (sg);
    return rc;
}

static void
_service_run (srvmode_t mode, int rfdno, int wfdno)
{
    List l = diod_conf_get_listen ();
    int nwthreads = diod_conf_get_nwthreads ();
    int flags = diod_conf_get_debuglevel ();
    uid_t euid = geteuid ();
    int n;

    ss.mode = mode;
    ss.rfdno = rfdno;
    ss.wfdno = wfdno;
    ss.shutdown = 0;
    ss.reload = 0;
    _service_sigsetup ();

    ss.fds = NULL;
    ss.nfds = 0;
    switch (mode) {
        case SRV_FILEDES:
            break;
        case SRV_NORMAL:
        case SRV_SOCKTEST:
            if (!diod_sock_listen (l, &ss.fds, &ss.nfds))
                msg_exit ("failed to set up listener");
#if WITH_RDMATRANS
            ss.rdma = diod_rdma_create ();
            diod_rdma_listen (ss.rdma);
#endif
            break;
    }

    /* manipulate squash/runas users if not root */
    if (euid != 0) {
        if (diod_conf_get_allsquash ()) {
            struct passwd *pw = getpwuid (euid);
            char *su = diod_conf_get_squashuser ();
            if (!pw)
                msg_exit ("getpwuid on effective uid failed");
            if (strcmp (pw->pw_name, su) != 0) {
                if (strcmp (su, DFLT_SQUASHUSER) != 0)
                    msg ("changing squashuser '%s' to '%s' "
                         "since you are not root", su, pw->pw_name);
                diod_conf_set_squashuser (pw->pw_name); /* fixes issue 41 */
            }
        } else { /* N.B. runasuser cannot be set in the config file */
            uid_t ruid = diod_conf_get_runasuid ();
            if (diod_conf_opt_runasuid () && ruid != euid)
                msg ("changing runasuid %d to %d "
                     "since you are not root", ruid, euid);
            diod_conf_set_runasuid (euid);
        }
    }
        
    if (!diod_conf_get_foreground () && mode != SRV_FILEDES)
        _daemonize (); /* implicit fork - no pthreads before this */
    if (!diod_conf_get_foreground () && mode != SRV_FILEDES)
        diod_log_set_dest (diod_conf_get_logdest ());

    /* drop root */
    if (euid == 0) {
        if (diod_conf_get_allsquash ())
            _become_user (diod_conf_get_squashuser (), -1, 1);
        else if (diod_conf_opt_runasuid ())
            _become_user (NULL, diod_conf_get_runasuid (), 1);
    }

    /* clear umask */
    umask (0);

    flags |= SRV_FLAGS_LOOSEFID;        /* work around buggy clients */
    flags |= SRV_FLAGS_AUTHCONN;
    //flags |= SRV_FLAGS_FLUSHSIG;      /* XXX temporarily off */
    if (geteuid () == 0) {
        flags |= SRV_FLAGS_SETFSID;
        flags |= SRV_FLAGS_DAC_BYPASS;
        if (_test_setgroups ())
            flags |= SRV_FLAGS_SETGROUPS;
        else
            msg ("test_setgroups: groups are per-process (disabling)");
    }

    /* Process dumpable flag may have been cleared by uid manipulation above.
     * Set it here, then maintain it in user.c::np_setfsid () as uids are
     * further manipulated.
     */
#ifndef __MACH__
    if (prctl (PR_SET_DUMPABLE, 1, 0, 0, 0) < 0)
        err_exit ("prctl PR_SET_DUMPABLE failed");
#endif

    if (!diod_conf_get_userdb ())
        flags |= SRV_FLAGS_NOUSERDB;
    if (!(ss.srv = np_srv_create (nwthreads, flags))) /* starts threads */
        errn_exit (np_rerror (), "np_srv_create");
    if (diod_init (ss.srv) < 0)
        errn_exit (np_rerror (), "diod_init");

    if ((n = pthread_create (&ss.t, NULL, _service_loop, NULL)))
        errn_exit (n, "pthread_create _service_loop");
#if WITH_RDMATRANS
    if ((n = pthread_create (&ss.rdma_t, NULL, _service_loop_rdma, NULL)))
        errn_exit (n, "pthread_create _service_loop_rdma");
#endif
    switch (mode) {
        case SRV_FILEDES:
        case SRV_SOCKTEST:
            np_srv_wait_conncount (ss.srv, 1);
            pthread_kill (ss.t, SIGUSR1);
            break;
        case SRV_NORMAL:
            break;
    }
    if ((n = pthread_join (ss.t, NULL)))
        errn_exit (n, "pthread_join _service_loop");
#if WITH_RDMATRANS
    if ((n = pthread_join (ss.rdma_t, NULL)))
        errn_exit (n, "pthread_join _service_loop_rdma");
#endif

    diod_fini (ss.srv);
    np_srv_destroy (ss.srv);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
