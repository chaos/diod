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

/* diod_ops.c - 9P operations for diod distributed I/O daemon */

/* Initial code borrowed from npfs/fs/ufs.c which is 
 *   Copyright (C) 2005-2008 by Latchesar Ionkov <lucho@ionkov.net>
 */

/* See the body of npfs/libnpfs/srv.c::np_wthread_proc () for
 * request processing flow.
 *
 * When a 9P T-message is received, np_wthread_proc () calls
 * np_process_request (), which calls the registered srv->operation
 * (if any) through its wrapper in npfs/libnpfs/fcall.c.
 * 
 * The R-message sent in reply is determined by the operation's (or actually
 * its wrapper's) return value and thread-specific error state:
 * 
 * (Npfcall *) reply structure returned and error state clear
 *     The reply is returned in a R-message.  This structure is allocated with
 *     an operation-specific np_create_r<op> () function (see npfs.h)
 *
 * NULL returned and error state clear
 *     No reply is sent.
 *
 * Error state set
 *     An Rlerror message is sent, constructed from the thread-specific error
 *     state which is set with np_uerror ().  Any (Npfcall *)returned is freed.
 *  
 * Normally the wrapper passes through the registered srv->operation's return
 * value, except in special cases noted below (diod_walk).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _XOPEN_SOURCE 600   /* pread/pwrite */
#define _BSD_SOURCE         /* makedev, st_atim etc */
#define _ATFILE_SOURCE      /* utimensat */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/fsuid.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <assert.h>
#include <stdarg.h>

#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "list.h"
#include "hash.h"
#include "hostlist.h"

#include "diod_conf.h"
#include "diod_log.h"
#include "diod_auth.h"

#include "ops.h"
#include "exp.h"

typedef struct path_struct *Path;
typedef struct ioctx_struct *IOCtx;
typedef struct pathpool_struct *PathPool;

struct ioctx_struct {
    pthread_mutex_t lock;
    int             refcount;
    int             fd;
    DIR             *dir;
    int             lock_type;
    Npqid           qid;
    u32             iounit;
    u32             open_flags;
    Npuser          *user;
    Npsrv           *srv;
    IOCtx           next;
    IOCtx           prev;
};

struct path_struct {
    pthread_mutex_t lock;
    int             refcount;
    char            *s;
    int             len;
    IOCtx           ioctx;
};

struct pathpool_struct {
    pthread_mutex_t lock;
    hash_t          hash;
};

#define DIOD_FID_FLAGS_ROFS       0x01
#define DIOD_FID_FLAGS_MOUNTPT    0x02
#define DIOD_FID_FLAGS_SHAREFD    0x04

typedef struct {
    Path            path;
    IOCtx           ioctx;
    int             flags;
} Fid;

Npfcall     *diod_attach (Npfid *fid, Npfid *afid, Npstr *aname);
int          diod_clone  (Npfid *fid, Npfid *newfid);
int          diod_walk   (Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall     *diod_read   (Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall     *diod_write  (Npfid *fid, u64 offset, u32 count, u8 *data,
                          Npreq *req);
Npfcall     *diod_clunk  (Npfid *fid);
Npfcall     *diod_remove (Npfid *fid);
void         diod_fiddestroy(Npfid *fid);

Npfcall     *diod_statfs (Npfid *fid);
Npfcall     *diod_lopen  (Npfid *fid, u32 mode);
Npfcall     *diod_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode,
                          u32 gid);
Npfcall     *diod_symlink(Npfid *dfid, Npstr *name, Npstr *symtgt, u32 gid);
Npfcall     *diod_mknod(Npfid *dfid, Npstr *name, u32 mode, u32 major,
                        u32 minor, u32 gid);
Npfcall     *diod_rename (Npfid *fid, Npfid *dfid, Npstr *name);
Npfcall     *diod_readlink(Npfid *fid);
Npfcall     *diod_getattr(Npfid *fid, u64 request_mask);
Npfcall     *diod_setattr (Npfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
                        u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec);
Npfcall     *diod_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall     *diod_fsync (Npfid *fid);
Npfcall     *diod_lock (Npfid *fid, u8 type, u32 flags, u64 start, u64 length,
                        u32 proc_id, Npstr *client_id);
Npfcall     *diod_getlock (Npfid *fid, u8 type, u64 start, u64 length,
                        u32 proc_id, Npstr *client_id);
Npfcall     *diod_link (Npfid *dfid, Npfid *fid, Npstr *name);
Npfcall     *diod_mkdir (Npfid *fid, Npstr *name, u32 mode, u32 gid);
int          diod_remapuser (Npfid *fid, Npstr *uname, u32 n_uname,
                             Npstr *aname);
int          diod_auth_required (Npstr *uname, u32 n_uname, Npstr *aname);
char        *diod_get_path (Npfid *fid);
char        *diod_get_files (char *name, void *a);

static void      _ustat2qid     (struct stat *st, Npqid *qid);

static PathPool  _ppool_create (void);
static void      _ppool_destroy (PathPool pp);

static void      _path_decref (Npsrv *srv, Path p);
static Path      _path_incref (Path p);


int
diod_init (Npsrv *srv)
{
    srv->msize = 65536;
    srv->fiddestroy = diod_fiddestroy;
    srv->logmsg = diod_log_msg;
    srv->remapuser = diod_remapuser;
    srv->auth_required = diod_auth_required;
    srv->auth = diod_auth_functions;
    srv->get_path = diod_get_path;

    srv->attach = diod_attach;
    srv->clone = diod_clone;
    srv->walk = diod_walk;
    srv->read = diod_read;
    srv->write = diod_write;
    srv->clunk = diod_clunk;
    srv->remove = diod_remove;
    srv->statfs = diod_statfs;
    srv->lopen = diod_lopen;
    srv->lcreate = diod_lcreate;
    srv->symlink = diod_symlink;
    srv->mknod = diod_mknod;
    srv->rename = diod_rename;
    srv->readlink = diod_readlink;
    srv->getattr = diod_getattr;
    srv->setattr = diod_setattr;
    //srv->xattrwalk = diod_xattrwalk;
    //srv->xattrcreate = diod_xattrcreate;
    srv->readdir = diod_readdir;
    srv->fsync = diod_fsync;
    srv->llock = diod_lock;
    srv->getlock = diod_getlock;
    srv->link = diod_link;
    srv->mkdir = diod_mkdir;
    //srv->renameat = diod_renameat;
    //srv->unlinkat = diod_unlinkat;

    if (!np_ctl_addfile (srv->ctlroot, "exports", diod_get_exports, srv, 0))
        goto error;
    if (!(srv->srvaux = _ppool_create ()))
        goto error;
    if (!np_ctl_addfile (srv->ctlroot, "files", diod_get_files, srv, 0))
        goto error;
    return 0;
error:
    diod_fini (srv);
    return -1;
}

void
diod_fini (Npsrv *srv)
{
    if (srv->srvaux) {
        _ppool_destroy (srv->srvaux);
        srv->srvaux = NULL;
    }
}

static void
_unlink_ioctx (IOCtx *head, IOCtx i)
{
    if (i->prev)
        i->prev->next = i->next;
    else
        *head = i->next;
    if (i->next)
        i->next->prev = i->prev;
    i->prev = i->next = NULL;
}

static void
_link_ioctx (IOCtx *head, IOCtx i)
{
    i->next = *head;
    i->prev = NULL;
    if (*head)
        (*head)->prev = i;
    *head = i;
}

static void
_count_ioctx (IOCtx i, int *shared, int *unique)
{
    for (*unique = *shared = 0; i != NULL; i = i->next) {
        (*unique)++;
        xpthread_mutex_lock (&i->lock);
        (*shared) += i->refcount;
        xpthread_mutex_unlock (&i->lock);
    }
}

static void
_ioctx_free (Npfid *fid)
{
    Fid *f = fid->aux;
    int n;

    if (f->ioctx) {
        xpthread_mutex_lock (&f->ioctx->lock);
        n = --f->ioctx->refcount;
        xpthread_mutex_unlock (&f->ioctx->lock);
        if (n == 0) {
            xpthread_mutex_lock (&f->path->lock);
            _unlink_ioctx (&f->path->ioctx, f->ioctx);
            xpthread_mutex_unlock (&f->path->lock);
            if (f->ioctx->dir)
                (void)closedir(f->ioctx->dir);
            else if (f->ioctx->fd != -1)
                (void)close (f->ioctx->fd);
            pthread_mutex_destroy (&f->ioctx->lock);
            free (f->ioctx);
        }
        f->ioctx = NULL;
    }
}

static int
_ioctx_close (Npfid *fid)
{
    Fid *f = fid->aux;
    int n;
    int rc = 0;

    if (f->ioctx) {
        xpthread_mutex_lock (&f->ioctx->lock);
        n = --f->ioctx->refcount;
        xpthread_mutex_unlock (&f->ioctx->lock);
        if (n == 0) {
            xpthread_mutex_lock (&f->path->lock);
            _unlink_ioctx (&f->path->ioctx, f->ioctx);
            xpthread_mutex_unlock (&f->path->lock);
            if (f->ioctx->dir) {
                rc = closedir(f->ioctx->dir);
                if (rc < 0)
                    np_uerror (errno);
            } else if (f->ioctx->fd != -1) {
                rc = close (f->ioctx->fd);
                if (rc < 0)
                    np_uerror (errno);
            }
            if (f->ioctx->user)
                np_user_decref (f->ioctx->user);
            pthread_mutex_destroy (&f->ioctx->lock);
            free (f->ioctx);
        }
        f->ioctx = NULL;
    }

    return rc;
}

static IOCtx
_ioctx_incref (IOCtx ioctx)
{
    xpthread_mutex_lock (&ioctx->lock);
    ioctx->refcount++;
    xpthread_mutex_unlock (&ioctx->lock);

    return ioctx;
}

static int
_ioctx_open (Npfid *fid, u32 flags, u32 mode)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    struct stat sb;
    IOCtx ip;

    assert (f->ioctx == NULL);

    xpthread_mutex_lock (&f->path->lock);
    if ((f->flags & DIOD_FID_FLAGS_SHAREFD) && ((flags & 3) == O_RDONLY)) {
        for (ip = f->path->ioctx; ip != NULL; ip = ip->next) {
            if (ip->qid.type != P9_QTFILE)
                continue;
            if (ip->open_flags != flags)
                continue;
            if (ip->user->uid != fid->user->uid)
                continue;
            /* NOTE: we could do a stat and check qid? */
            f->ioctx = _ioctx_incref (ip);
            break;
        }
    }
    if (f->ioctx == NULL) {
        f->ioctx = malloc (sizeof (*f->ioctx));
        if (!f->ioctx) {
            np_uerror (ENOMEM);
            goto error;
        }
        pthread_mutex_init (&f->ioctx->lock, NULL);
        f->ioctx->refcount = 1;
        f->ioctx->lock_type = LOCK_UN;
        f->ioctx->dir = NULL;
        f->ioctx->open_flags = flags;
        f->ioctx->srv = srv;
        np_user_incref (fid->user);
        f->ioctx->user = fid->user;
        f->ioctx->prev = f->ioctx->next = NULL;
        f->ioctx->fd = open (f->path->s, flags, mode);
        if (f->ioctx->fd < 0) {
            np_uerror (errno);
            goto error;
        }
        if (fstat (f->ioctx->fd, &sb) < 0) {
            np_uerror (errno);
            goto error;
        }
        f->ioctx->iounit = 0; /* if iounit is 0, v9fs will use msize-P9_IOHDRSZ */
        if (S_ISDIR(sb.st_mode) && !(f->ioctx->dir = fdopendir (f->ioctx->fd))) {
            np_uerror (errno);
            goto error;
        }
        _ustat2qid (&sb, &f->ioctx->qid);
        _link_ioctx (&f->path->ioctx, f->ioctx);
    }
    xpthread_mutex_unlock (&f->path->lock);
    return 0;
error:
    xpthread_mutex_unlock (&f->path->lock);
    _ioctx_free (fid);
    return -1;
}

static void
_path_free (Path path)
{
    assert (path->ioctx == NULL);
    if (path->s)
        free (path->s);
    pthread_mutex_destroy (&path->lock);
    free (path);
}

static Path
_path_incref (Path path)
{
    xpthread_mutex_lock (&path->lock);
    path->refcount++;
    xpthread_mutex_unlock (&path->lock);

    return path;
}

static void
_path_decref (Npsrv *srv, Path path)
{
    PathPool pp = srv->srvaux;
    int n;

    xpthread_mutex_lock (&pp->lock);

    xpthread_mutex_lock (&path->lock);
    n = --path->refcount;
    xpthread_mutex_unlock (&path->lock);
    if (n == 0) {
        hash_remove (pp->hash, path->s);
        _path_free (path);
    }

    xpthread_mutex_unlock (&pp->lock);
}

static Path
_path_alloc (char *s, int len)
{
    Path path = malloc (sizeof (*path));

    if (path) {
        path->refcount = 1;
        pthread_mutex_init (&path->lock, NULL);
        path->s = s;
        path->len = len;
        path->ioctx = NULL;
    }
    return path;
}

static Path
_path_ref (Npsrv *srv, Npstr *ns)
{
    PathPool pp = srv->srvaux;
    int len = ns->len;
    char *s;
    Path path = NULL;

    xpthread_mutex_lock (&pp->lock);
    if (!(s = np_strdup (ns)))
        goto error;
    path = hash_find (pp->hash, s);
    if (path) {
        _path_incref (path);
        free (s);
    } else {
        assert (errno == 0);
        if (!(path = _path_alloc (s, len))) /* frees 's' on failure */
            goto error;
        if (!hash_insert (pp->hash, path->s, path)) {
            assert (errno == ENOMEM);
            goto error;
        }
    }
    xpthread_mutex_unlock (&pp->lock);
    return path;
error:
    xpthread_mutex_unlock (&pp->lock);
    if (path)
        _path_free (path);
    return NULL;
}

static Path
_path_ref2 (Npsrv *srv, Path opath, Npstr *ns)
{
    PathPool pp = srv->srvaux;
    char *s;
    int len = opath->len + 1 + ns->len;
    Path path = NULL;

    xpthread_mutex_lock (&pp->lock);
    if (!(s = malloc (len + 1)))
        goto error;    
    memcpy (s, opath->s, opath->len);
    s[opath->len] = '/';
    memcpy (s + opath->len + 1, ns->str, ns->len);
    s[len] = '\0';
    path = hash_find (pp->hash, s);
    if (path) {
        _path_incref (path);
        free (s);
    } else {
        assert (errno == 0);
        if (!(path = _path_alloc (s, len))) /* frees 's' on failure */
            goto error;
        if (!hash_insert (pp->hash, path->s, path)) {
            assert (errno == ENOMEM);
            goto error;
        }
    }
    xpthread_mutex_unlock (&pp->lock);
    return path;
error:
    xpthread_mutex_unlock (&pp->lock);
    if (path)
        _path_free (path);
    return NULL;
}

static PathPool
_ppool_create (void)
{
    PathPool pp = malloc (sizeof (*pp));

    if (pp) {
        pthread_mutex_init (&pp->lock, NULL);
        pp->hash = hash_create (1000,
                                (hash_key_f)hash_key_string,
                                (hash_cmp_f)strcmp, NULL);
        if (!pp->hash) {
            free (pp);
            pp = NULL;
        }
    }
    return pp;
}

static void
_ppool_destroy (PathPool pp)
{
    assert (hash_is_empty (pp->hash));
    hash_destroy (pp->hash);
    pthread_mutex_destroy (&pp->lock);
    free (pp);
}

/* Allocate local fid struct and attach to fid->aux.
 */
static Fid *
_fidalloc (Npfid *fid, Npstr *ns)
{
    Fid *f = malloc (sizeof (*f));

    assert (fid->aux == NULL);
    if (f) {
        f->flags = 0;
        f->ioctx = NULL;
        f->path = _path_ref (fid->conn->srv, ns);
        if (!f->path) {
            free (f);
            f = NULL;
        }
    }
    fid->aux = f;
  
    return f;
}

/* Clone newfid->aux from fid->aux.
 */
static Fid *
_fidclone (Npfid *newfid, Npfid *fid)
{
    Fid *f = fid->aux;
    Fid *nf = malloc (sizeof (*f));

    assert (newfid->aux == NULL);
    if (nf) {
        nf->flags = f->flags;
        nf->ioctx = NULL;
        nf->path = _path_incref (f->path);
    }
    newfid->aux = nf;
  
    return nf;
}

/* Destroy local fid structure.
 * This is called when fid we are parasitically attached to is bieng destroyed.
 */
void
diod_fiddestroy (Npfid *fid)
{
    Fid *f = fid->aux;

    if (f) {
        if (f->ioctx)
            _ioctx_free (fid);
        if (f->path)
            _path_decref (fid->conn->srv, f->path);
        free(f);
        fid->aux = NULL;
    }
}

/* Create a 9P qid from a file's stat info.
 * N.B. v9fs maps st_ino = qid->path + 2
 */
static void
_ustat2qid (struct stat *st, Npqid *qid)
{
    qid->path = st->st_ino;
    //qid->version = st->st_mtime ^ (st->st_size << 8);
    qid->version = 0;
    qid->type = 0;
    if (S_ISDIR(st->st_mode))
        qid->type |= P9_QTDIR;
    if (S_ISLNK(st->st_mode))
        qid->type |= P9_QTSYMLINK;
}

static void
_dirent2qid (struct dirent *d, Npqid *qid)
{
    assert (d->d_type != DT_UNKNOWN);
    qid->path = d->d_ino;
    qid->version = 0;
    qid->type = 0;
    if (d->d_type == DT_DIR)
        qid->type |= P9_QTDIR;
    if (d->d_type == DT_LNK)
        qid->type |= P9_QTSYMLINK;
}

int
diod_remapuser (Npfid *fid, Npstr *uname, u32 n_uname, Npstr *aname)
{
    int ret = 0;

    if (diod_conf_get_allsquash ()) {
        char *squash = diod_conf_get_squashuser ();
        Npuser *user = NULL;

        if (!(user = np_uname2user (fid->conn->srv, squash))) {
            ret = -1;
            goto done;
        }
        if (fid->user)
            np_user_decref (fid->user);
        fid->user = user; 
    }
done:
    return ret;
}

int
diod_auth_required (Npstr *uname, u32 n_uname, Npstr *aname)
{
    return diod_conf_get_auth_required ();
}

/* Tattach - attach a new user (fid->user) to aname.
 *   diod_auth.c::diod_checkauth first authenticates/authorizes user
 */
Npfcall*
diod_attach (Npfid *fid, Npfid *afid, Npstr *aname)
{
    Npfcall* ret = NULL;
    Fid *f = NULL;
    Npqid qid;
    struct stat sb;
    int xflags;

    if (aname->len == 0 || *aname->str != '/') {
        np_uerror (EPERM);
        goto error;
    }
    if (!(f = _fidalloc (fid, aname))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (diod_conf_opt_runasuid ()) {
        if (fid->user->uid != diod_conf_get_runasuid ()) {
            np_uerror (EPERM);
            goto error;
        }
    }
    if (!diod_match_exports (f->path->s, fid->conn, fid->user, &xflags))
        goto error;
    if ((xflags & XFLAGS_RO))
        f->flags |= DIOD_FID_FLAGS_ROFS;
    if ((xflags & XFLAGS_SHAREFD))
        f->flags |= DIOD_FID_FLAGS_SHAREFD;
    if (stat (f->path->s, &sb) < 0) { /* OK to follow symbolic links */
        np_uerror (errno);
        goto error;
    }
    if (!S_ISDIR (sb.st_mode)) {
        np_uerror (ENOTDIR);
        goto error;
    }
    _ustat2qid (&sb, &qid);
    if ((ret = np_create_rattach (&qid)) == NULL) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_attach %s@%s:%.*s", fid->user->uname,
          np_conn_get_client_id (fid->conn), aname->len, aname->str);
    diod_fiddestroy (fid);
    return NULL;
}

/* Twalk - walk a file path
 * Called from fcall.c::np_walk () to clone the fid.
 * On error, call np_uerror () and return 0.
 */
int
diod_clone (Npfid *fid, Npfid *newfid)
{
    Fid *f = fid->aux;

    if (!(_fidclone (newfid, fid))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return 1;
error:
    errn (np_rerror (), "diod_clone %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
    return 0;
}

/* Special stat for a mount point that fixes up st_dev and st_ino
 * to be what should be "underneath" the mount.
 */
static int
_statmnt (char *path, struct stat *sb)
{
    DIR *dir = NULL;
    struct stat sbp;
    struct dirent dbuf, *dp;
    char *ppath = NULL;
    int plen = strlen (path) + 4;
    char *name;
    int err;

    if (stat (path, sb) < 0) {
        np_uerror (errno);
        goto error;
    }
    if (!(ppath = malloc (plen))) {
        np_uerror (ENOMEM);
        goto error;
    }
    snprintf (ppath, plen, "%s/..", path);
    if (stat (ppath, &sbp) < 0) {
        np_uerror (errno);
        goto error;
    }
    if (!(dir = opendir (ppath))) {
        np_uerror (errno);
        goto error;
    }
    name = strrchr (path, '/');
    name = name ? name + 1 : path;
    do {
        err = readdir_r (dir, &dbuf, &dp);
        if (err > 0) {
            np_uerror (err);
            goto error;
        }
    } while (dp != NULL && strcmp (name, dp->d_name) != 0);
    if (!dp) {
        np_uerror (ENOENT);
        goto error;
    }
    sb->st_dev = sbp.st_dev;
    sb->st_ino = dp->d_ino;
    (void)closedir (dir);
    free (ppath);
    return 0;
error:
    if (ppath)
        free (ppath);
    if (dir)
        (void)closedir (dir);
    return -1;
}

/* Twalk - walk a file path
 * Called from fcall.c::np_walk () on each wname component in succession.
 * On error, call np_uerror () and return 0.
 */
int
diod_walk (Npfid *fid, Npstr* wname, Npqid *wqid)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    struct stat sb, sb2;
    Path npath = NULL;

    if ((f->flags & DIOD_FID_FLAGS_MOUNTPT)) {
        np_uerror (ENOENT);
        goto error_quiet;
    }
    if (!(npath = _path_ref2 (srv, f->path, wname))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (lstat (npath->s, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (lstat (f->path->s, &sb2) < 0) {
        np_uerror (errno);
        goto error;
    }
    if (sb.st_dev != sb2.st_dev) {
        if (_statmnt (npath->s, &sb) < 0)
            goto error;
        f->flags |= DIOD_FID_FLAGS_MOUNTPT;
    }
    _path_decref (srv, f->path);
    f->path = npath; 
    _ustat2qid (&sb, wqid);
    return 1;
error:
    errn (np_rerror (), "diod_walk %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s,
          wname->len, wname->str);
error_quiet:
    if (npath)
        _path_decref (srv, npath);
    return 0;
}

/* Tread - read from a file or directory.
 */
Npfcall*
diod_read (Npfid *fid, u64 offset, u32 count, Npreq *req)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    ssize_t n;

    if (!f->ioctx) {
        np_uerror (EBADF);
        goto error;
    }
    if (!(ret = np_alloc_rread (count))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if ((n = pread (f->ioctx->fd, ret->u.rread.data, count, offset)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    np_set_rread_count (ret, n);
    return ret;
error:
    errn (np_rerror (), "diod_read %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    if (ret)
        free (ret);
    return NULL; 
}

/* Twrite - write to a file.
 */
Npfcall*
diod_write (Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    ssize_t n;

    if (!f->ioctx) {
        np_uerror (EBADF);
        goto error;
    }
    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if ((n = pwrite (f->ioctx->fd, data, count, offset)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (!(ret = np_create_rwrite (n))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_write %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    return NULL;
}

/* Tclunk - close a file.
 */
Npfcall*
diod_clunk (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;

    if (f->ioctx) {
        if (_ioctx_close (fid) < 0)
            goto error_quiet;
    }
    if (!(ret = np_create_rclunk ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_clunk %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    return NULL;
}

/* Tremove - remove a file or directory.
 */
Npfcall*
diod_remove (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (remove (f->path->s) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (!(ret = np_create_rremove ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_remove %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    return NULL;
}

/* Tstatfs - read file system information.
 */
Npfcall*
diod_statfs (Npfid *fid)
{
    Fid *f = fid->aux;
    struct statfs sb;
    Npfcall *ret;
    u64 fsid;

    if (statfs (f->path->s, &sb) < 0) {
        np_uerror (errno);
        goto error;
    }
    fsid = (u64)sb.f_fsid.__val[0] | ((u64)sb.f_fsid.__val[1] << 32);
    if (!(ret = np_create_rstatfs(sb.f_type, sb.f_bsize, sb.f_blocks,
                                  sb.f_bfree, sb.f_bavail, sb.f_files,
                                  sb.f_ffree, fsid,
                                  sb.f_namelen))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_statfs %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
    return NULL;
}

Npfcall*
diod_lopen (Npfid *fid, u32 flags)
{
    Fid *f = fid->aux;
    Npfcall *res;

    if ((f->flags & DIOD_FID_FLAGS_ROFS) && ((flags & O_WRONLY)
                                          || (flags & O_RDWR))) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if ((flags & O_CREAT)) /* can't happen? */
        flags &= ~O_CREAT; /* clear and allow to fail with ENOENT */

    if (f->ioctx != NULL) {
        np_uerror (EINVAL);
        errn (errno, "diod_lopen: fid is already open");
        goto error_quiet; 
    }
    if (_ioctx_open (fid, flags, 0) < 0) {
        if (np_rerror () == ENOMEM)
            goto error;
        goto error_quiet;
    }
    if (!(res = np_create_rlopen (&f->ioctx->qid, f->ioctx->iounit))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return res;
error:
    errn (np_rerror (), "diod_lopen %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    _ioctx_free (fid); 
    return NULL;
}

Npfcall*
diod_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode, u32 gid)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Npfcall *ret;
    Path opath = NULL;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(flags & O_CREAT)) /* can't happen? */
        flags |= O_CREAT;
    if (f->ioctx != NULL) {
        np_uerror (EINVAL);
        goto error; 
    }
    opath = f->path;
    if (!(f->path = _path_ref2 (srv, opath, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (_ioctx_open (fid, flags, mode) < 0) {
        if (np_rerror () == ENOMEM)
            goto error;
        goto error_quiet;
    }
    if (!((ret = np_create_rlcreate (&f->ioctx->qid, f->ioctx->iounit)))) {
        (void)unlink (f->path->s);
        np_uerror (ENOMEM);
        goto error;
    }
    _path_decref (srv, opath);
    return ret;
error:
    errn (np_rerror (), "diod_lcreate %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          opath ? opath->s : f->path->s, name->len, name->str);
error_quiet:
    _ioctx_free (fid);
    if (opath) {
        if (f->path)
            _path_decref (srv, f->path);
        f->path = opath;
    }
    return NULL;
}

Npfcall*
diod_symlink(Npfid *fid, Npstr *name, Npstr *symtgt, u32 gid)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Npfcall *ret;
    char *target = NULL;
    Path npath = NULL;
    Npqid qid;
    struct stat sb;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _path_ref2 (srv, f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (!(target = np_strdup (symtgt))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (symlink (target, npath->s) < 0 || lstat (npath->s, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rsymlink (&qid)))) {
        (void)unlink (npath->s);
        np_uerror (ENOMEM);
        goto error;
    }
    _path_decref (srv, npath);
    free (target);
    return ret;
error:
    errn (np_rerror (), "diod_symlink %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s,
          name->len, name->str);
error_quiet:
    if (npath)
        _path_decref (srv, npath);
    if (target)
        free (target);
    return NULL;
}

Npfcall*
diod_mknod(Npfid *fid, Npstr *name, u32 mode, u32 major, u32 minor, u32 gid)
{
    Npsrv *srv = fid->conn->srv;
    Npfcall *ret;
    Fid *f = fid->aux;
    Path npath = NULL;
    Npqid qid;
    struct stat sb;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _path_ref2 (srv, f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (mknod (npath->s, mode, makedev (major, minor)) < 0
                                        || lstat (npath->s, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmknod (&qid)))) {
        (void)unlink (npath->s);
        np_uerror (ENOMEM);
        goto error;
    }
    _path_decref (srv, npath);
    return ret;
error:
    errn (np_rerror (), "diod_mknod %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s,
          name->len, name->str);
error_quiet:
    if (npath)
        _path_decref (srv, npath);
    return NULL;
}

/* Trename - rename a file, potentially to another directory
 */
Npfcall*
diod_rename (Npfid *fid, Npfid *dfid, Npstr *name)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Fid *d = dfid->aux;
    Npfcall *ret;
    Path npath = NULL;
    int renamed = 0;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _path_ref2 (srv, d->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (rename (f->path->s, npath->s) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    renamed = 1;
    if (!(ret = np_create_rrename ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    _path_decref (srv, f->path);
    f->path = npath;
    return ret;
error:
    errn (np_rerror (), "diod_rename %s@%s:%s to %s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s,
          d->path->s, name->len, name->str);
error_quiet:
    if (renamed && npath)
        (void)rename (npath->s, f->path->s);
    if (npath)
        _path_decref (srv, npath);
    return NULL;
}

Npfcall*
diod_readlink(Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char target[PATH_MAX + 1];
    int n;

    if ((n = readlink (f->path->s, target, sizeof(target) - 1)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    target[n] = '\0';
    if (!(ret = np_create_rreadlink(target))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_readlink %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    return NULL;
}

Npfcall*
diod_getattr(Npfid *fid, u64 request_mask)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    Npqid qid;
    struct stat sb;

    if ((f->flags & DIOD_FID_FLAGS_MOUNTPT)) {
        if (_statmnt (f->path->s, &sb) < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    } else {
        if (lstat (f->path->s, &sb) < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    }
    _ustat2qid (&sb, &qid);
    if (!(ret = np_create_rgetattr(request_mask, &qid,
                                    sb.st_mode,
                                    sb.st_uid,
                                    sb.st_gid,
                                    sb.st_nlink,
                                    sb.st_rdev,
                                    sb.st_size,
                                    sb.st_blksize,
                                    sb.st_blocks,
                                    sb.st_atim.tv_sec,
                                    sb.st_atim.tv_nsec,
                                    sb.st_mtim.tv_sec,
                                    sb.st_mtim.tv_nsec,
                                    sb.st_ctim.tv_sec,
                                    sb.st_ctim.tv_nsec,
                                    0, 0, 0, 0))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_getattr %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    return NULL;
}

Npfcall*
diod_setattr (Npfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
              u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec)
{
    Npfcall *ret;
    Fid *f = fid->aux;
    int ctime_updated = 0;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if ((valid & P9_SETATTR_MODE)) { /* N.B. derefs symlinks */
        if (chmod (f->path->s, mode) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_SETATTR_UID) || (valid & P9_SETATTR_GID)) {
        if (lchown (f->path->s, (valid & P9_SETATTR_UID) ? uid : -1,
                                (valid & P9_SETATTR_GID) ? gid : -1) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_SETATTR_SIZE)) {
        if (truncate (f->path->s, size) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_SETATTR_ATIME) || (valid & P9_SETATTR_MTIME)) {
#if HAVE_UTIMENSAT
        struct timespec ts[2];

        if (!(valid & P9_SETATTR_ATIME)) {
            ts[0].tv_sec = 0;
            ts[0].tv_nsec = UTIME_OMIT;
        } else if (!(valid & P9_SETATTR_ATIME_SET)) {
            ts[0].tv_sec = 0;
            ts[0].tv_nsec = UTIME_NOW;
        } else {
            ts[0].tv_sec = atime_sec;
            ts[0].tv_nsec = atime_nsec;
        }
        if (!(valid & P9_SETATTR_MTIME)) {
            ts[1].tv_sec = 0;
            ts[1].tv_nsec = UTIME_OMIT;
        } else if (!(valid & P9_SETATTR_MTIME_SET)) {
            ts[1].tv_sec = 0;
            ts[1].tv_nsec = UTIME_NOW;
        } else {
            ts[1].tv_sec = mtime_sec;
            ts[1].tv_nsec = mtime_nsec;
        }
        if (utimensat(-1, f->path->s, ts, AT_SYMLINK_NOFOLLOW) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
#else /* HAVE_UTIMENSAT */
        struct timeval tv[2], now, *tvp;
        struct stat sb;
        if ((valid & P9_SETATTR_ATIME) && !(valid & P9_SETATTR_ATIME_SET)
         && (valid & P9_SETATTR_MTIME) && !(valid & P9_SETATTR_MTIME_SET)) {
            tvp = NULL; /* set both to now */
        } else {
            if (lstat(f->path->s, &sb) < 0) {
                np_uerror (errno);
                goto error_quiet;
            }
            if (gettimeofday (&now, NULL) < 0) {
                np_uerror (errno);
                goto error_quiet;
            }
            if (!(valid & P9_SETATTR_ATIME)) {
                tv[0].tv_sec = sb.st_atim.tv_sec;
                tv[0].tv_usec = sb.st_atim.tv_nsec / 1000;
            } else if (!(valid & P9_SETATTR_ATIME_SET)) {
                tv[0].tv_sec = now.tv_sec;
                tv[0].tv_usec = now.tv_usec;
            } else {
                tv[0].tv_sec = atime_sec;
                tv[0].tv_usec = atime_nsec / 1000;
            }

            if (!(valid & P9_SETATTR_MTIME)) {
                tv[1].tv_sec = sb.st_mtim.tv_sec;
                tv[1].tv_usec = sb.st_mtim.tv_nsec / 1000;
            } else if (!(valid & P9_SETATTR_MTIME_SET)) {
                tv[1].tv_sec = now.tv_sec;
                tv[1].tv_usec = now.tv_usec;
            } else {
                tv[1].tv_sec = mtime_sec;
                tv[1].tv_usec = mtime_nsec / 1000;
            }
            tvp = tv;
        }
        if (utimes (f->path->s, tvp) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
#endif /* HAVE_UTIMENSAT */
        ctime_updated = 1;
    }
    if ((valid & P9_SETATTR_CTIME) && !ctime_updated) {
        if (lchown (f->path->s, -1, -1) < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    }
    if (!(ret = np_create_rsetattr())) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_setattr %s@%s:%s (valid=0x%x)",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s,
          valid);
error_quiet:
    return NULL;
}

static u32
_copy_dirent_linux (Fid *f, struct dirent *dp, u8 *buf, u32 buflen)
{
    Npqid qid;
    u32 ret = 0;

    if (dp->d_type == DT_UNKNOWN) {
        char path[PATH_MAX + 1];
        struct stat sb;
        snprintf (path, sizeof(path), "%s/%s", f->path->s, dp->d_name);
        if (lstat (path, &sb) < 0) {
            np_uerror (errno);
            goto done;
        }
        _ustat2qid (&sb, &qid);
    } else  {
        _dirent2qid (dp, &qid);
    }
    ret = np_serialize_p9dirent(&qid, dp->d_off, dp->d_type,
                                      dp->d_name, buf, buflen);
done:
    return ret;
}

static u32
_read_dir_linux (Fid *f, u8* buf, u64 offset, u32 count)
{
    struct dirent dbuf, *dp;
    int i, n = 0, err;

    if (offset == 0)
        rewinddir (f->ioctx->dir);
    else
        seekdir (f->ioctx->dir, offset);
    do {
        err = readdir_r (f->ioctx->dir, &dbuf, &dp);
        if (err > 0) {
            np_uerror (err);
            break;
        }
        if (err == 0 && dp == NULL)
            break;
        if ((f->flags & DIOD_FID_FLAGS_MOUNTPT) && strcmp (dp->d_name, ".")
                                                && strcmp (dp->d_name, ".."))
                continue;
        i = _copy_dirent_linux (f, dp, buf + n, count - n);
        if (i == 0)
            break;
        n += i;
    } while (n < count);
    return n;
}

Npfcall*
diod_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
    int n;
    Fid *f = fid->aux;
    Npfcall *ret;

    if (!f->ioctx || !f->ioctx->dir) {
        np_uerror (EBADF);
        goto error;
    }
    if (!(ret = np_create_rreaddir (count))) {
        np_uerror (ENOMEM);
        goto error;
    }
    n = _read_dir_linux (f, ret->u.rreaddir.data, offset, count);
    if (np_rerror ()) {
        free (ret);
        ret = NULL;
    } else
        np_finalize_rreaddir (ret, n);
    return ret;
error:
    errn (np_rerror (), "diod_readdir %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
    return NULL;
}

Npfcall*
diod_fsync (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!f->ioctx) {
        np_uerror (EBADF);
        goto error;
    }
    if (fsync(f->ioctx->fd) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (!((ret = np_create_rfsync ()))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_fsync %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    return NULL;
}

/* Locking note:
 * Implement POSIX locks in terms of BSD flock locks.
 * This at least gets distributed whole-file locking to work.
 * Strategies for distributed record locking will deadlock.
 */
Npfcall*
diod_lock (Npfid *fid, u8 type, u32 flags, u64 start, u64 length, u32 proc_id,
           Npstr *client_id)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    u8 status = P9_LOCK_ERROR;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (flags & ~P9_LOCK_FLAGS_BLOCK) { /* only one valid flag for now */
        np_uerror (EINVAL);             /*  (which we ignore) */
        goto error;
    }
    if (!f->ioctx) {
        np_uerror (EBADF);
        goto error;
    }
    switch (type) {
        case P9_LOCK_TYPE_UNLCK:
            if (flock (f->ioctx->fd, LOCK_UN) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->ioctx->lock_type = LOCK_UN;
            } else
                status = P9_LOCK_ERROR;
            break;
        case P9_LOCK_TYPE_RDLCK:
            if (flock (f->ioctx->fd, LOCK_SH | LOCK_NB) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->ioctx->lock_type = LOCK_SH;
            } else if (errno == EWOULDBLOCK) {
                status = P9_LOCK_BLOCKED;
            } else
                status = P9_LOCK_ERROR;
            break;
        case P9_LOCK_TYPE_WRLCK:
            if (flock (f->ioctx->fd, LOCK_EX | LOCK_NB) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->ioctx->lock_type = LOCK_EX;
            } else if (errno == EWOULDBLOCK) {
                status  = P9_LOCK_BLOCKED;
            }
            break;
        default:
            np_uerror (EINVAL);
            goto error;
    }
    if (!((ret = np_create_rlock (status)))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_lock %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    return NULL;
}

Npfcall*
diod_getlock (Npfid *fid, u8 type, u64 start, u64 length, u32 proc_id,
             Npstr *client_id)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char *cid = NULL;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!f->ioctx) {
        np_uerror (EBADF);
        goto error;
    }
    if (!(cid = np_strdup (client_id))) {
        np_uerror (ENOMEM);
        goto error;
    }
    switch (type) {
        case P9_LOCK_TYPE_RDLCK:
            switch (f->ioctx->lock_type) {
                case LOCK_EX:
                case LOCK_SH:
                    type = P9_LOCK_TYPE_UNLCK;
                    break;
                case LOCK_UN:
                    if (flock (f->ioctx->fd, LOCK_SH | LOCK_NB) >= 0) {
                        (void)flock (f->ioctx->fd, LOCK_UN);
                        type = P9_LOCK_TYPE_UNLCK;
                    } else
                        type = P9_LOCK_TYPE_WRLCK;
                    break;
            }
            break;
        case P9_LOCK_TYPE_WRLCK:
            switch (f->ioctx->lock_type) {
                case LOCK_EX:
                    type = P9_LOCK_TYPE_UNLCK;
                    break;
                case LOCK_SH:
                    /* Rather than upgrade the lock to LOCK_EX and risk
                     * not reacquiring the LOCK_SH afterwards, lie about
                     * the lock being available.  Getlock is racy anyway.
                     */
                    type = P9_LOCK_TYPE_UNLCK;
                    break;
                case LOCK_UN:
                    if (flock (f->ioctx->fd, LOCK_EX | LOCK_NB) >= 0) {
                        (void)flock (f->ioctx->fd, LOCK_UN);
                        type = P9_LOCK_TYPE_UNLCK;
                    } else
                        type = P9_LOCK_TYPE_WRLCK; /* could also be LOCK_SH actually */
            }
            break;
        default:
            np_uerror (EINVAL);
            goto error;
    }
    if (type != P9_LOCK_TYPE_UNLCK && type != F_UNLCK) {
        /* FIXME: need to fake up start, length, proc_id, cid? */
    }
    if (!((ret = np_create_rgetlock(type, start, length, proc_id, cid)))) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (cid);
    return ret;
error:
    errn (np_rerror (), "diod_getlock %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s);
error_quiet:
    if (cid)
        free (cid);
    return NULL;
}

Npfcall*
diod_link (Npfid *dfid, Npfid *fid, Npstr *name)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Npfcall *ret;
    Fid *df = dfid->aux;
    Path npath = NULL;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _path_ref2 (srv, df->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (link (f->path->s, npath->s) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (!((ret = np_create_rlink ()))) {
        (void)unlink (npath->s);
        np_uerror (ENOMEM);
        goto error;
    }
    _path_decref (srv, npath);
    return ret;
error:
    errn (np_rerror (), "diod_link %s@%s:%s %s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s,
          df->path->s, name->len, name->str);
error_quiet:
    if (npath)
        _path_decref (srv, npath);
    return NULL;
}

Npfcall*
diod_mkdir (Npfid *fid, Npstr *name, u32 mode, u32 gid)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Npfcall *ret;
    Path npath = NULL;
    Npqid qid;
    struct stat sb;

    if ((f->flags & DIOD_FID_FLAGS_ROFS)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _path_ref2 (srv, f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (mkdir (npath->s, mode) < 0 || lstat (npath->s, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmkdir (&qid)))) {
        (void)rmdir(npath->s);
        np_uerror (ENOMEM);
        goto error;
    }
    _path_decref (srv, npath);
    return ret;
error:
    errn (np_rerror (), "diod_mkdir %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path->s,
          name->len, name->str);
error_quiet:
    if (npath)
        _path_decref (srv, npath);
    return NULL;
}

char *
diod_get_path (Npfid *fid)
{
    Fid *f = fid->aux;

    return f ? f->path->s : NULL;
}

typedef struct {
    int len;
    char *s;
} DynStr;

static int
_get_one_file (Path path, char *s, DynStr *ds)
{
    int unique, shared;

    xpthread_mutex_lock (&path->lock);
    _count_ioctx (path->ioctx, &shared, &unique);
    aspf (&ds->s, &ds->len, "%d %d %d %s\n", path->refcount, shared, unique, s);
    xpthread_mutex_unlock (&path->lock);
    return 0;
}

char *
diod_get_files (char *name, void *a)
{
    Npsrv *srv = a;
    PathPool pp = srv->srvaux;
    DynStr ds = { .s = NULL, .len = 0 };

    xpthread_mutex_lock (&pp->lock);
    hash_for_each (pp->hash, (hash_arg_f)_get_one_file, &ds);
    xpthread_mutex_unlock (&pp->lock);

    return ds.s;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
