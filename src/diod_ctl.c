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
 * - serve /diodctl synthetic file system
 * - file 'exports' contains list of I/O node exports
 * - file 'port' contains server port number
 * - when an attached user reads 'port', if a diod server is already running
 *   for that user, return port.  If not, spawn it and return port.
 * - reap children when they quit (on last trans shutdown)
 */

/* FIXME: s->key is a misnomer, the uid is the key.  Also, we should
 * embed a slurm jobid in the munge payload and use that plus the uid as a key
 * so that children can poop out per job usage stats that we save for later.
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
#include <poll.h>
#include <sys/types.h>
#include <signal.h>

#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_trans.h"
#include "diod_upool.h"
#include "diod_sock.h"

static void          _daemonize (void);
static void          _register_ops (Npsrv *srv);
static Npfile       *_ctl_root_create (void);
static Npfcall      *_ctl_attach (Npfid *fid, Npfid *nafid, Npstr *uname,
                                  Npstr *aname);
static Npfcall      *_ctl_version (Npconn *conn, u32 msize, Npstr *version);
static void          _init_serverlist (void);
static void          _setrlimit (void); 

typedef struct {
    uid_t uid;
    char *key;
    pid_t pid;
} Server;

typedef struct {
    List servers; /* list of (Server *) */
    pthread_mutex_t lock;
} Serverlist;

static Serverlist *serverlist = NULL;

#ifndef NR_OPEN
#define NR_OPEN         1048576 /* works on RHEL 5 x86_64 arch */
#endif
#define BASE_PORT       1942    /* arbitrary non-privileged port */
#define MAX_PORT        (BASE_PORT + 1024)

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
"Usage: diodctl [OPTIONS]\n"
"   -f,--foreground        do not fork and disassociate with tty\n"
"   -d,--debug MASK        set debugging mask\n"
"   -l,--listen IP:PORT    set interface to listen on (just one allowed)\n"
"   -w,--nwthreads INT     set number of I/O worker threads to spawn\n"
"   -c,--config-file FILE  set config file path\n"
"   -e,--export PATH       export PATH (just one allowed)\n"
"   -a,--allowany          disable TCP wrappers checks\n"
"   -m,--no-munge-auth     do not require munge authentication\n"
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

    if (geteuid () != 0)
        msg_exit ("must run as root");
    _setrlimit ();

    srv = np_srv_create (diod_conf_get_nwthreads ());
    if (!srv)
        msg_exit ("out of memory");
    if (!diod_sock_listen_list (&fds, &nfds, diod_conf_get_listen ()))
        msg_exit ("failed to set up listen ports");
    if (!diod_conf_get_foreground ())
        _daemonize ();

    _init_serverlist ();
    _register_ops (srv);
    diod_sock_accept_loop (srv, fds, nfds, diod_conf_get_tcpwrappers ());
    /*NOTREACHED*/

    exit (0);
}

static void
_register_ops (Npsrv *srv)
{
    npfile_init_srv (srv, _ctl_root_create ());
    srv->debuglevel = diod_conf_get_debuglevel ();
    srv->debugprintf = msg;
    srv->upool = diod_upool;
    srv->version = _ctl_version;
    srv->attach = _ctl_attach;
}

/* Free server struct.  Suitable for cast to (ListDelF).
 */
static void
_free_server (Server *s)
{
    if (s->key)
        free (s->key);
    if (s->pid != 0)
        kill (s->pid, SIGTERM);
    free (s);
}

/* Allocate server struct.
 */
static Server *
_alloc_server (void)
{
    Server *s = NULL;

    if (!(s = malloc (sizeof (Server)))) {
        np_uerror (ENOMEM);
        goto done;
    }
    memset (s, 0, sizeof (*s));
done:
    if (np_haserror () && s != NULL)
        _free_server (s);
    return s;
}

/* Initialize the global list of servers.
 */
static void
_init_serverlist (void)
{
    int err;

    if (!(serverlist = malloc (sizeof (Serverlist))))
        msg_exit ("out of memory");
    if (!(serverlist->servers = list_create ((ListDelF)_free_server)))
        msg_exit ("out of memory");
    if ((err = pthread_mutex_init (&serverlist->lock, NULL)))
        msg_exit ("pthread_mutex_init: %s", strerror (err));
}

/* Return nonzero if s is server for uid.
 * Suitable for cast to (ListFindF).
 */
static int
_match_server (Server *s, uid_t *uid)
{
    return (s->uid == *uid);
}

/* Spawn a new diod daemon running as [uid].
 * We are holding serverlist->lock.
 */
static void
_new_server (Npuser *user, char *ip)
{
    char name[16];
    char Fopt[16];
    struct pollfd *fds = NULL;
    int i, nfds = 0;
    Server *s = NULL;

    if (!(s = _alloc_server ()))
        goto done;
    s->uid = user->uid;
    if (!diod_sock_setup_alloc (ip, &fds, &nfds, &s->key)) {
        msg ("failed to allocate port for user %d on %s", user->uid, ip);
        np_uerror (EIO);
        goto done;
    }
    snprintf (Fopt, sizeof(Fopt), "-F%d", nfds);
    snprintf (name, sizeof(name), "diod-%d", user->uid); 

    switch (s->pid = fork ()) {
        case -1:
            np_uerror (errno);
            break;
        case 0: /* child */
            msg ("starting diod for uid %d", user->uid);
            diod_become_user (user);
            for (i = 0; i < nfds; i++) {
                (void)close (i);
                dup2 (fds[i].fd, i);
                close (fds[i].fd);
            }
            /* FIXME */
            execl ("./diod", name,
                   "-c../etc/diod.conf",
                   "-f", "-x", Fopt, NULL);
            err_exit ("exec failed"); /* N.B. this is the child exiting */
            break;
        default: /* parent */
            /* FIXME: spawn a thread to waitpid() on the child */
            break;
    }

    if (!list_append (serverlist->servers, s)) {
        np_uerror (ENOMEM);
        goto done;
    }
done:
    for (i = 0; i < nfds; i++)
        close (fds[i].fd);
    if (fds)
        free (fds);
    if (np_haserror () && s != NULL)
        _free_server (s);
}

/* Tversion - negotiate 9P protocol version (9P2000.u or bust).
 */
static Npfcall*
_ctl_version (Npconn *conn, u32 msize, Npstr *version)
{
    Npfcall *ret = NULL;

    if (np_strcmp (version, "9P2000.h") && np_strcmp (version, "9P2000.u")) {
        np_werror ("unsupported 9P version", EIO);
        goto done;
    }
    if (msize < IOHDRSZ + 1) {
        np_werror ("msize too small", EIO);
        goto done;
    }
    if (msize > conn->srv->msize)
        msize = conn->srv->msize;

    np_conn_reset (conn, msize, 1); /* 1 activates 'dotu' */
    if (!(ret = np_create_rversion (msize, "9P2000.u"))) {
        np_uerror (ENOMEM);
        goto done;
    }
done:
    return ret;
}

/* Tattach - announce a new user, and associate her fid with the root dir.
 */
static Npfcall*
_ctl_attach (Npfid *fid, Npfid *nafid, Npstr *uname, Npstr *aname)
{
    Npfile *root = (Npfile *)fid->conn->srv->treeaux;
    Npfcall *ret = NULL;
    Npfilefid *f;
    uid_t auid;

    if (nafid) {    /* 9P Tauth not supported */
        np_werror (Enoauth, EIO);
        goto done;
    }
    if (np_strcmp (aname, "/diodctl") != 0) {
        np_uerror (EPERM);
        goto done;
    }
    /* Munge authentication involves the upool and trans layers:
     * - we ask the upool layer if the user now attaching has a munge cred
     * - we stash the uid of the last successful munge auth in the trans layer
     * - subsequent attaches on the same trans get to leverage the last auth
     * By the time we get here, invalid munge creds have already been rejected.
     */
    if (diod_conf_get_munge ()) {
        if (diod_user_has_mungecred (fid->user)) {
            diod_trans_set_authuser (fid->conn->trans, fid->user->uid);
        } else {
            if (diod_trans_get_authuser (fid->conn->trans, &auid) < 0) {
                np_uerror (EPERM);
                goto done;
            }
            if (auid != 0 && auid != fid->user->uid) {
                np_uerror (EPERM);
                goto done;
            }
        }
    }
    if (!npfile_checkperm (root, fid->user, 4)) {
        np_uerror (EPERM);
        goto done;
    }
    if (!(f = npfile_fidalloc (root, fid))) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (!(ret = np_create_rattach (&root->qid))) {
        np_uerror (ENOMEM);
        goto done;
    }
    fid->aux = f;
    np_fid_incref (fid);

done:
    if (np_haserror ())
        npfile_fiddestroy (fid); /* frees fid->aux as Npfilefid* if not NULL */
    return ret;
}

/* Callback for root dir.
 */
static Npfile *
_root_first (Npfile *dir)
{
    if (dir->dirfirst)
        npfile_incref(dir->dirfirst);

    return dir->dirfirst;
}

/* Callback for root dir.
 */
static Npfile *
_root_next (Npfile *dir, Npfile *prevchild)
{
    if (prevchild->next)
        npfile_incref (prevchild->next);

    return prevchild->next;
}

/* Handle a read from the 'exports' file.
 */
static int
_exports_read (Npfilefid *f, u64 offset, u32 count, u8 *data, Npreq *req)
{
    char *buf = f->file->aux;
    int cpylen = strlen (buf) - offset;

    if (cpylen > count)
        cpylen = count;
    if (cpylen < 0)
        cpylen = 0;
        memcpy (data, buf + offset, cpylen);
    return cpylen;
}

/* Handle a read from the 'server' file.
 */
static int
_server_read (Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
    Npfid *fid = file->fid;
    int cpylen = 0;
    Server *s;
    int err;

    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
    s = list_find_first (serverlist->servers, (ListFindF)_match_server,
                         &fid->user->uid);
    if (s) {
        cpylen = strlen (s->key) - offset;
        if (cpylen > count)
            cpylen = count;
        if (cpylen < 0)
            cpylen = 0;
        memcpy (data, s->key + offset, cpylen);
    } else
        np_uerror (ESRCH);
    if ((err = pthread_mutex_unlock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
done:
    return cpylen;
}

/* Handle a write to the 'ctl' file.
 * Content of the write is ignored since we only have one action.
 */
static int
_ctl_write (Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
    Npfid *fid = file->fid;
    char *ip = diod_trans_get_ip (fid->conn->trans);
    int cpylen = 0;
    int err;

    if ((err = pthread_mutex_lock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }

    /* FIXME: what if found server is listening on wrong ip? */
    if (!list_find_first (serverlist->servers, (ListFindF)_match_server,
                          &fid->user->uid)) {
        _new_server (fid->user, ip);
    }

    if ((err = pthread_mutex_unlock (&serverlist->lock))) {
        np_uerror (err);
        goto done;
    }
done:
    if (!np_haserror ())
        cpylen = count;
    return cpylen;
}

/* A no-op (no error) wstat.
 */
static int
_noop_wstat (Npfile* file, Npstat* stat)
{
    return 1; /* 0 = fail */
}

static Npdirops root_ops = {
        .first = _root_first,
        .next =  _root_next,
};
static Npfileops exports_ops = {
        .read  = _exports_read,
};
static Npfileops server_ops = {
        .read  = _server_read,
};
static Npfileops ctl_ops = {
        .write = _ctl_write,
        .wstat = _noop_wstat, /* required: mtime is set before a write */
};

/* Create the file system representation for /diodctl.
 */
static Npfile *
_ctl_root_create (void)
{
    Npfile *root, *exports, *server, *ctl;
    Npuser *user;
    char *tmpstr;

    if (!(user = diod_upool->uid2user (diod_upool, 0)))
        msg_exit ("out of memory");

    if (!(tmpstr = strdup ("")))
        msg_exit ("out of memory");
    if (!(root = npfile_alloc (NULL, tmpstr, 0555|Dmdir, 0, &root_ops, NULL)))
        msg_exit ("out of memory");
    root->parent = root;
    npfile_incref(root);
    root->atime = time(NULL);
    root->mtime = root->atime;
    root->uid = user;
    root->gid = NULL;
    root->muid = user;

    if (!(tmpstr = strdup ("exports")))
        msg_exit ("out of memory");
    if (!(exports = npfile_alloc(root, tmpstr, 0444, 1, &exports_ops, NULL)))
        msg_exit ("out of memory");
    npfile_incref(exports);
    if (!(exports->aux = diod_conf_cat_exports ()))
        msg_exit ("out of memory");

    if (!(tmpstr = strdup ("server")))
        msg_exit ("out of memory");
    if (!(server = npfile_alloc(root, tmpstr, 0444, 1, &server_ops, NULL)))
        msg_exit ("out of memory");
    npfile_incref(server);

    if (!(tmpstr = strdup ("ctl")))
        msg_exit ("out of memory");
    if (!(ctl = npfile_alloc(root, tmpstr, 0666, 1, &ctl_ops, NULL)))
        msg_exit ("out of memory");
    npfile_incref(ctl);

    root->dirfirst = exports;
    exports->next = server;
    server->next = ctl;
    root->dirlast = ctl;

    return root;
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
