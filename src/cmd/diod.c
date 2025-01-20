/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* diod.c - distributed I/O daemon */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <poll.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef USE_GANESHA_KMOD
#include <sys/syscall.h>
#endif
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/resource.h>
#if HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "src/libnpfs/npfs.h"
#include "src/liblsd/list.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_conf.h"
#include "src/libdiod/diod_sock.h"
#if WITH_RDMA
#include "src/libdiod/diod_rdma.h"
#endif

#include "src/libdiod/diod_ops.h"

#if USE_GANESHA_KMOD
#include "src/libnpfs/ganesha-syscalls.h"
#endif

typedef enum { SRV_FILEDES, SRV_SOCKTEST, SRV_NORMAL } srvmode_t;

static void          _setrlimit (void);
static void          _become_user (char *name, uid_t uid);
static void          _service_run (srvmode_t mode, int rfdno, int wfdno);

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif

static const char *options = "r:w:d:l:t:e:Eo:u:SL:nHpc:NU:s";

static const struct option longopts[] = {
    {"rfdno",              required_argument,  0, 'r'},
    {"wfdno",              required_argument,  0, 'w'},
    {"debug",              required_argument,  0, 'd'},
    {"listen",             required_argument,  0, 'l'},
    {"nwthreads",          required_argument,  0, 't'},
    {"export",             required_argument,  0, 'e'},
    {"export-all",         no_argument,        0, 'E'},
    {"export-opts",        required_argument,  0, 'o'},
    {"no-auth",            no_argument,        0, 'n'},
    {"no-hostname-lookup", no_argument,        0, 'H'},
    {"statfs-passthru",    no_argument,        0, 'p'},
    {"no-userdb",          no_argument,        0, 'N'},
    {"runas-uid",          required_argument,  0, 'u'},
    {"allsquash",          no_argument,        0, 'S'},
    {"squashuser",         required_argument,  0, 'U'},
    {"logdest",            required_argument,  0, 'L'},
    {"config-file",        required_argument,  0, 'c'},
    {"socktest",           no_argument,        0, 's'},
    {0, 0, 0, 0},
};

static void
usage()
{
    fprintf (stderr,
"Usage: diod [OPTIONS]\n"
"   -r,--rfdno              service connected client on read file descriptor\n"
"   -w,--wfdno              service connected client on write file descriptor\n"
"   -l,--listen IP:PORT     set interface to listen on (multiple -l allowed)\n"
"   -t,--nwthreads INT      set number of I/O worker threads to spawn\n"
"   -e,--export PATH        export PATH (multiple -e allowed)\n"
"   -E,--export-all         export all mounted file systems\n"
"   -o,--export-opts        set global export options (comma-seperated)\n"
"   -n,--no-auth            disable authentication check\n"
"   -H,--no-hostname-lookup disable hostname lookups\n"
"   -p,--statfs-passthru    statfs should return underly f_type not V9FS_MAGIC\n"
"   -N,--no-userdb          bypass password/group file lookup\n"
"   -u,--runas-uid UID      only allow UID to attach\n"
"   -S,--allsquash          map all users to the squash user\n"
"   -U,--squashuser USER    set the squash user (default nobody)\n"
"   -L,--logdest DEST       log to DEST, can be stdout, stderr, or file\n"
"   -d,--debug MASK         set debugging mask\n"
"   -c,--config-file FILE   set config file path\n"
"   -s,--socktest           run in test mode where server exits early\n"
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

    diod_log_init (NULL);
    diod_conf_init ();

    /* config file overrides defaults */
    opterr = 0;
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
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
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
        switch (c) {
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
                if (optarg[0] == '[') {
                    char *end = strchr (optarg, ']');

                    if (!end || !strchr (end, ':'))
                        usage ();
                }
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
            case 'H':   /* --no-hostname-lookup */
                diod_conf_set_hostname_lookup (0);
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

/* Look up name (which may be a stringified numerical uid) or if name is NULL,
 * look up uid.  Then drop root and load the credentials of the new user.
 * It is a fatal error if the user cannot be found in the password file.
 * This function is not safe to call after any threads have been spawned that
 * could do user lookups.  For example, call before np_srv_create ().
 */
static void
_become_user (char *name, uid_t uid)
{
    struct passwd *pw;
    int nsg;
    gid_t sg[64];

    if (name) { // handle stringified uid
        char *endptr;
        errno = 0;
        uid = strtoul (name, &endptr, 10);
        if (errno == 0 && *name != '\0' && *endptr == '\0')
            name = NULL;
    }
    if (name) {
        if (!(pw = getpwnam (name)))
            msg_exit ("error looking up user %s", name);
    }
    else {
        if (!(pw = getpwuid (uid)))
            msg_exit ("error looking up uid %d", uid);
    }
    if (pw->pw_uid == 0)
        return; // nothing to do for root=>root transition
    nsg = sizeof (sg) / sizeof(sg[0]);
    if (getgrouplist(pw->pw_name, pw->pw_gid, sg, &nsg) == -1)
        err_exit ("user %s is in too many groups", pw->pw_name);
    if (setgroups (nsg, sg) < 0)
        err_exit ("setgroups");
    if (setregid (pw->pw_gid, pw->pw_gid) < 0)
        err_exit ("setreuid");
    if (setreuid (pw->pw_uid, pw->pw_uid) < 0)
        err_exit ("setreuid");

    msg ("Dropped root, running as %s", pw->pw_name);
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
#ifdef RLIMIT_LOCKS
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
#if WITH_RDMA
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
    int lookup = diod_conf_get_hostname_lookup ();
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
        if (ppoll (ss.fds, ss.nfds, NULL, &sigs) < 0) {
            if (errno == EINTR)
                continue;
            err_exit ("ppoll");
        }
        for (i = 0; i < ss.nfds; i++) {
            if ((ss.fds[i].revents & POLLIN)) {
                diod_sock_accept_one (ss.srv, ss.fds[i].fd, lookup);
            }
        }
    }
    return NULL;
}

#if WITH_RDMA
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

#if defined(MULTIUSER) && !defined(USE_GANESHA_KMOD)
/* POSIX setgroups(2) is per process but in Linux the underlying system call
 * is per-thread and the per-process bit is handled in glibc, so we can use
 * SYS_setgroups directly in the server thread pool when switching users.
 * This assumption is tenuous though, so we should quickly check it during
 * server startup, in case a future kernel update invalidates it.
 *
 * If we can't use it, a warning is issued and life goes on with group access
 * checks based on the user's primary group.
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
    if ((n = getgroups (0, NULL)) < 0)
        err_exit ("getgroups");
    if (n == 0)
        rc = 1;
    if (syscall(SYS_setgroups, nsg, sg) < 0)
        err_exit ("setgroups");
    free (sg);
    return rc;
}
#endif

/* Look up user name of effective uid.
 * The result is only valid until the next call, and this is not thread safe.
 */
static const char *
_geteuser (void)
{
    static char idstr[16];
    struct passwd *pw = getpwuid (geteuid ());

    if (!pw) {
        snprintf (idstr, sizeof (idstr), "%d", geteuid ());
        return idstr;
    }
    return pw->pw_name;
}

static void
_service_run (srvmode_t mode, int rfdno, int wfdno)
{
    List l = diod_conf_get_listen ();
    int nwthreads = diod_conf_get_nwthreads ();
    int flags = diod_conf_get_debuglevel ();
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
            break;
    }

    /* manipulate squash/runas users if not root */
    if (geteuid () != 0) {
        if (diod_conf_get_allsquash ()) {
            struct passwd *pw = getpwuid (geteuid ());
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
            if (diod_conf_opt_runasuid () && ruid != geteuid ())
                msg ("changing runasuid %d to %d "
                     "since you are not root", ruid, geteuid ());
            diod_conf_set_runasuid (geteuid ());
        }
    }

    /* drop root */
    if (geteuid () == 0) {
        if (diod_conf_get_allsquash ())
            _become_user (diod_conf_get_squashuser (), -1);
        else if (diod_conf_opt_runasuid ())
            _become_user (NULL, diod_conf_get_runasuid ());
    }

    /* report */
    if (diod_conf_opt_runasuid ()) {
        const char *user = _geteuser ();
        msg ("Only %s can attach and access files as %s", user, user);
    }
    else if (diod_conf_get_allsquash ())
        msg ("Anyone can attach and access files as %s", _geteuser ());
    else
        msg ("Anyone can attach and access files as themselves");

    /* clear umask */
    umask (0);

    flags |= SRV_FLAGS_LOOSEFID;        /* work around buggy clients */
    flags |= SRV_FLAGS_AUTHCONN;
    //flags |= SRV_FLAGS_FLUSHSIG;      /* XXX temporarily off */
    if (geteuid () == 0) {
#if MULTIUSER
        flags |= SRV_FLAGS_SETFSID;
        flags |= SRV_FLAGS_DAC_BYPASS;
#ifndef USE_GANESHA_KMOD
        if (_test_setgroups ())
            flags |= SRV_FLAGS_SETGROUPS;
        else {
            msg ("warning: supplemental group membership will be ignored."
                "  Some accesses might be inappropriately denied.");
        }
#else
        if (init_ganesha_syscalls() < 0)
            msg ("nfs-ganesha-kmod not loaded: changing user/group will fail");
        /* SRV_FLAGS_SETGROUPS is ignored in user-freebsd.c */
#endif
#else
        msg ("warning: cannot change user/group (built with --disable-multiuser)");
#endif
    }

#if WITH_RDMA
    /* RDMA needs to be initialized after user transitions.
     * See chaos/diod#107.
     */
    if (mode == SRV_NORMAL) {
        ss.rdma = diod_rdma_create ();
        diod_rdma_listen (ss.rdma);
    }
#endif

    /* Process dumpable flag may have been cleared by uid manipulation above.
     * Set it here, then maintain it in user.c::np_setfsid () as uids are
     * further manipulated.
     */
#if HAVE_SYS_PRCTL_H
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
#if WITH_RDMA
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
#if WITH_RDMA
    if ((n = pthread_join (ss.rdma_t, NULL)))
        errn_exit (n, "pthread_join _service_loop_rdma");
#endif

    np_srv_shutdown(ss.srv);
    diod_fini (ss.srv);
    np_srv_destroy (ss.srv);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
