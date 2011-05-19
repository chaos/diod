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
#include <sys/statvfs.h>
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
#include "list.h"
#include "hostlist.h"

#include "diod_conf.h"
#include "diod_log.h"
#include "diod_auth.h"

#include "ops.h"
#include "exp.h"

typedef struct {
    char            *path;
    int              fd;
    DIR             *dir;
    struct dirent   *dirent;
    struct stat      stat;
    /* advisory locking */
    int              lock_type;
    /* export flags */
    int              xflags;
} Fid;

Npfcall     *diod_attach (Npfid *fid, Npfid *afid, Npstr *aname);
int          diod_clone  (Npfid *fid, Npfid *newfid);
int          diod_walk   (Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall     *diod_read   (Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall     *diod_write  (Npfid *fid, u64 offset, u32 count, u8 *data,
                          Npreq *req);
Npfcall     *diod_clunk  (Npfid *fid);
Npfcall     *diod_remove (Npfid *fid);
void         diod_flush  (Npreq *req);
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

static int       _fidstat       (Fid *fid);
static void      _ustat2qid     (struct stat *st, Npqid *qid);
static void      _fidfree       (Fid *f);

int
diod_register_ops (Npsrv *srv)
{
    srv->msize = 65536;
    srv->fiddestroy = diod_fiddestroy;
    srv->logmsg = diod_log_msg;
    srv->remapuser = diod_remapuser;
    srv->auth_required = diod_auth_required;
    srv->auth = diod_auth_functions;

    srv->attach = diod_attach;
    srv->clone = diod_clone;
    srv->walk = diod_walk;
    srv->read = diod_read;
    srv->write = diod_write;
    srv->clunk = diod_clunk;
    srv->remove = diod_remove;
    srv->flush = diod_flush;
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

    if (!np_ctl_addfile (srv->ctlroot, "exports", diod_get_exports, srv))
        return -1;

    return 0;
}

/* Update stat info contained in fid.
 * Set npfs error state on error.
 */
static int
_fidstat (Fid *fid)
{
    int n;
    int fd;

    if (fid->fd != -1)
        n = fstat (fid->fd, &fid->stat);
    else if (fid->dir != NULL && (fd = dirfd (fid->dir)) >= 0)
        n = fstat (fd, &fid->stat);
    else
        n = lstat (fid->path, &fid->stat);
    if (n < 0)
        np_uerror (errno);
    return n;
}

/* Strdup an Npstr.
 * Set npfs error state on error.
 */
static char *
_p9strdup (Npstr *s9)
{
    char *s = np_strdup (s9);

    if (!s)
        np_uerror (ENOMEM);

    return s;
}

/* Strdup a regular string.
 * Set npfs error state on error.
 */
static char *
_strdup (char *str)
{
    char *s = strdup (str);

    if (!s)
        np_uerror (ENOMEM);

    return s;
}

/* Malloc memory.
 * Set npfs error state on error.
 */
static void *
_malloc (size_t size)
{
    void *p = malloc (size);

    if (!p)
        np_uerror (ENOMEM);

    return p;
}

/* Allocate our local fid struct which becomes attached to Npfid->aux.
 * Set npfs error state on error.
 */
static Fid *
_fidalloc (void)
{
    Fid *f = _malloc (sizeof(*f));

    if (f) {
        f->path = NULL;
        f->fd = -1;
        f->dir = NULL;
        f->dirent = NULL;
        f->lock_type = LOCK_UN;
        f->xflags = 0;
    }
  
    return f;
}

/* Free our local fid struct.
 */
static void
_fidfree (Fid *f)
{
    if (f) {
        if (f->fd != -1)
            close (f->fd);
        if (f->dir) 
            closedir(f->dir);
        if (f->path)
            free(f->path);
        free(f);
    }
}

/* This is a courtesy callback from npfs to let us know that
 * the fid we are parasitically attached to is being destroyed.
 */
void
diod_fiddestroy (Npfid *fid)
{
    Fid *f = fid->aux;

    _fidfree (f);
    fid->aux = NULL;
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

static char *
_mkpath(char *dirname, Npstr *name)
{
    int slen = strlen(dirname) + name->len + 2;
    char *s = _malloc (slen);
   
    if (s)
        snprintf (s, slen, "%s/%.*s", dirname, name->len, name->str);
    return s;
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

    if (aname->len == 0 || *aname->str != '/') {
        np_uerror (EPERM);
        goto error;
    }
    if (!(f = _fidalloc ()) || !(f->path = _p9strdup(aname))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (diod_conf_opt_runasuid ()) {
        if (fid->user->uid != diod_conf_get_runasuid ()) {
            np_uerror (EPERM);
            goto error;
        }
    }
    if (!diod_match_exports (f->path, fid->conn, fid->user, &f->xflags))
        goto error;
    if (_fidstat (f) < 0)
        goto error;
    _ustat2qid (&f->stat, &qid);
    if ((ret = np_create_rattach (&qid)) == NULL) {
        np_uerror (ENOMEM);
        goto error;
    }
    fid->aux = f;
    np_fid_incref (fid);
    return ret;
error:
    errn (np_rerror (), "diod_attach %s@%s:%.*s", fid->user->uname,
          np_conn_get_client_id (fid->conn), aname->len, aname->str);
    if (f)
        _fidfree (f);
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
    Fid *nf = NULL;

    if (!(nf = _fidalloc ()))
        goto error;
    if (!(nf->path = _strdup (f->path)))
        goto error;
    nf->xflags = f->xflags;
    newfid->aux = nf;
    return 1;
error:
    errn (np_rerror (), "diod_clone %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
    if (nf)
        _fidfree (nf);
    return 0;
}

/* Twalk - walk a file path
 * Called from fcall.c::np_walk () on each wname component in succession.
 * On error, call np_uerror () and return 0.
 */
int
diod_walk (Npfid *fid, Npstr* wname, Npqid *wqid)
{
    Fid *f = fid->aux;
    struct stat st;
    char *npath;

    if (!(npath = _mkpath (f->path, wname)))
        goto error;
    if (lstat (npath, &st) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    /* N.B. inodes would not be unique if we could cross over to another
     * file system.  But with the code below, ls -l returns ??? for mount
     * point dirs, which would otherwise have a "foreign" inode number.
     * How does NFS make them appear as empty directories?  That would be
     * prettier.
     */
    if (_fidstat (f) < 0)
        goto error;
    if (st.st_dev != f->stat.st_dev) { 
        np_uerror (EXDEV);
        goto error;
    }
    free (f->path);
    f->path = npath;
    _ustat2qid (&st, wqid);
    return 1;
error:
    errn (np_rerror (), "diod_walk %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path,
          wname->len, wname->str);
error_quiet:
    if (npath)
        free (npath);
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

    if (!(ret = np_alloc_rread (count))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if ((n = pread (f->fd, ret->u.rread.data, count, offset)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    np_set_rread_count (ret, n);
    return ret;
error:
    errn (np_rerror (), "diod_read %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
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
    Npfcall *ret = NULL;
    ssize_t n;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if ((n = pwrite (f->fd, data, count, offset)) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (ret)
        free (ret);
    return NULL;
}

/* Tclunk - close a file.
 */
Npfcall*
diod_clunk (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

    if (!(ret = np_create_rclunk ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_clunk %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
    if (ret)
        free (ret);
    return NULL;
}

/* Tremove - remove a file or directory.
 */
Npfcall*
diod_remove (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (remove (f->path) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (ret)
        free (ret);
    return NULL;
}

/* Tflush - abort in flight operations (npfs handles most of this).
 */
void
diod_flush(Npreq *req)
{
    return;
}

/* Tstatfs - read file system information.
 * N.B. must call statfs() for f_type and statvfs() for unsigned long f_fsid
 */
Npfcall*
diod_statfs (Npfid *fid)
{
    Fid *f = fid->aux;
    struct statfs sb;
    struct statvfs svb;
    Npfcall *ret = NULL;

    if (statfs (f->path, &sb) < 0) {
        np_uerror (errno);
        goto error;
    }
    if (statvfs (f->path, &svb) < 0) {
        np_uerror (errno);
        goto error;
    }
    if (!(ret = np_create_rstatfs(sb.f_type, sb.f_bsize, sb.f_blocks,
                                  sb.f_bfree, sb.f_bavail, sb.f_files,
                                  sb.f_ffree, (u64) svb.f_fsid,
                                  sb.f_namelen))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_statfs %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_lopen (Npfid *fid, u32 flags)
{
    Fid *f = fid->aux;
    Npfcall *res = NULL;
    Npqid qid;

    if ((f->xflags & XFLAGS_RO) && ((flags & O_WRONLY) || (flags & O_RDWR))) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if ((flags & O_CREAT)) /* can't happen? */
        flags &= ~O_CREAT; /* clear and allow to fail with ENOENT */

    if (_fidstat (f) < 0)
        goto error_quiet;

    if (S_ISDIR (f->stat.st_mode)) {
        f->dir = opendir (f->path);
        if (!f->dir) {
            np_uerror (errno);
            goto error_quiet;
        }
    } else {
        f->fd = open (f->path, flags);
        if (f->fd < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    }
    if (_fidstat (f) < 0)
        goto error; /* can't happen? */
    _ustat2qid (&f->stat, &qid);
    if (!(res = np_create_rlopen (&qid, f->stat.st_blksize))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return res;
error:
    errn (np_rerror (), "diod_lopen %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (f->dir) {
        (void)closedir (f->dir);
        f->dir = NULL;
    }
    if (f->fd != -1) {
        (void)close (f->fd); 
        f->fd = -1;
    }
    if (res)
        free (res);
    return NULL;
}

Npfcall*
diod_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode, u32 gid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    char *npath = NULL;
    Npqid qid;
    int fd = -1;
    struct stat sb;
    mode_t saved_umask;
    int created = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(f->path, name)))
        goto error;
    saved_umask = umask(0);
    if ((fd = creat (npath, mode)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    created = 1;
    umask(saved_umask);
    if (fstat (fd, &sb) < 0) {
        np_uerror (errno);
        goto error; /* shouldn't happen? */
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rlcreate (&qid, sb.st_blksize)))) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (f->path);
    f->path = npath;
    f->fd = fd;
    return ret;
error:
    errn (np_rerror (), "diod_lcreate %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path,
          name->len, name->str);
error_quiet:
    if (fd >= 0) {
        (void)close (fd);
    }
    if (created && npath)
        (void)unlink (npath);
    if (npath)
        free (npath);
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_symlink(Npfid *fid, Npstr *name, Npstr *symtgt, u32 gid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    char *target = NULL, *npath = NULL;
    Npqid qid;
    struct stat sb;
    mode_t saved_umask;
    int created = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(f->path, name)))
        goto error;
    if (!(target = _p9strdup (symtgt)))
        goto error;
    saved_umask = umask(0);
    if (symlink (target, npath) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    created = 1;
    umask(saved_umask);
    if (lstat (npath, &sb) < 0) {
        np_uerror (errno);
        goto error; /* shouldn't happen? */
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rsymlink (&qid)))) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (npath);
    free (target);
    return ret;
error:
    errn (np_rerror (), "diod_symlink %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path,
          name->len, name->str);
error_quiet:
    if (created && npath)
        (void)unlink (npath);
    if (npath)
        free (npath);
    if (target)
        free (target);
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_mknod(Npfid *fid, Npstr *name, u32 mode, u32 major, u32 minor, u32 gid)
{
    Npfcall *ret = NULL;
    Fid *f = fid->aux;
    char *npath = NULL;
    Npqid qid;
    struct stat sb;
    mode_t saved_umask;
    int created = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(f->path, name)))
        goto error;
    saved_umask = umask(0);
    if (mknod (npath, mode, makedev (major, minor)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    created = 1;
    umask(saved_umask);
    if (lstat (npath, &sb) < 0) {
        np_uerror (errno);
        goto error; /* shouldn't happen? */
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rsymlink (&qid)))) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (npath);
    return ret;
error:
    errn (np_rerror (), "diod_mknod %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path,
          name->len, name->str);
error_quiet:
    if (created && npath)
        (void)unlink (npath);
    if (npath)
        free (npath);
    if (ret)
        free (ret);
    return NULL;
}

/* Trename - rename a file, potentially to another directory
 */
Npfcall*
diod_rename (Npfid *fid, Npfid *dfid, Npstr *name)
{
    Fid *f = fid->aux;
    Fid *d = dfid->aux;
    Npfcall *ret = NULL;
    char *npath = NULL;
    int renamed = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(d->path, name)))
        goto error;
    if (rename (f->path, npath) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    renamed = 1;
    if (!(ret = np_create_rrename ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (f->path);
    f->path = npath;
    return ret;
error:
    errn (np_rerror (), "diod_rename %s@%s:%s to %s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path,
          d->path, name->len, name->str);
error_quiet:
    if (renamed && npath)
        (void)rename (npath, f->path);
    if (npath)
        free (npath);
    if (ret)
        free (f);
    return NULL;
}

Npfcall*
diod_readlink(Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    char target[PATH_MAX + 1];
    int n;

    if ((n = readlink (f->path, target, sizeof(target))) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_getattr(Npfid *fid, u64 request_mask)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    Npqid qid;

    if (_fidstat (f) < 0)
        goto error_quiet;
    _ustat2qid (&f->stat, &qid);
    if (!(ret = np_create_rgetattr(request_mask, &qid,
                                    f->stat.st_mode,
                                    f->stat.st_uid,
                                    f->stat.st_gid,
                                    f->stat.st_nlink,
                                    f->stat.st_rdev,
                                    f->stat.st_size,
                                    f->stat.st_blksize,
                                    f->stat.st_blocks,
                                    f->stat.st_atim.tv_sec,
                                    f->stat.st_atim.tv_nsec,
                                    f->stat.st_mtim.tv_sec,
                                    f->stat.st_mtim.tv_nsec,
                                    f->stat.st_ctim.tv_sec,
                                    f->stat.st_ctim.tv_nsec,
                                    0, 0, 0, 0))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_getattr %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_setattr (Npfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
              u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec)
{
    Npfcall *ret = NULL;
    Fid *f = fid->aux;
    int fidstat_updated = 0;
    int ctime_updated = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }

    if ((valid & P9_SETATTR_MODE) || (valid & P9_SETATTR_SIZE)) {
        if (_fidstat(f) < 0)
            goto error_quiet;
        fidstat_updated = 1;
        if (S_ISLNK(f->stat.st_mode)) {
            msg ("diod_setattr: unhandled mode/size update on symlink");
            np_uerror(EINVAL);
            goto error;
        }
    }

    /* chmod (N.B. dereferences symlinks) */
    if ((valid & P9_SETATTR_MODE)) {
        if (chmod (f->path, mode) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }

    /* chown */
    if ((valid & P9_SETATTR_UID) || (valid & P9_SETATTR_GID)) {
        if (lchown (f->path, (valid & P9_SETATTR_UID) ? uid : -1,
                             (valid & P9_SETATTR_GID) ? gid : -1) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }

    /* truncate (N.B. dereferences symlinks) */
    if ((valid & P9_SETATTR_SIZE)) {
        if (truncate (f->path, size) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }

    /* utimes */
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

        if (utimensat(-1, f->path, ts, AT_SYMLINK_NOFOLLOW) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
#else /* HAVE_UTIMENSAT */
        struct timeval tv[2], now, *tvp;
        /* N.B. this utimes () implementation loses atomicity and precision.
         */
        if ((valid & P9_SETATTR_ATIME) && !(valid & P9_SETATTR_ATIME_SET)
         && (valid & P9_SETATTR_MTIME) && !(valid & P9_SETATTR_MTIME_SET)) {
            tvp = NULL; /* set both to now */
        } else {
            if (!fidstat_updated && _fidstat(f) < 0)
                goto error_quiet;
            fidstat_updated = 1;
            if (gettimeofday (&now, NULL) < 0) {
                np_uerror (errno);
                goto error_quiet;
            }
            if (!(valid & P9_SETATTR_ATIME)) {
                tv[0].tv_sec = f->stat.st_atim.tv_sec;
                tv[0].tv_usec = f->stat.st_atim.tv_nsec / 1000;
            } else if (!(valid & P9_SETATTR_ATIME_SET)) {
                tv[0].tv_sec = now.tv_sec;
                tv[0].tv_usec = now.tv_usec;
            } else {
                tv[0].tv_sec = atime_sec;
                tv[0].tv_usec = atime_nsec / 1000;
            }

            if (!(valid & P9_SETATTR_MTIME)) {
                tv[1].tv_sec = f->stat.st_mtim.tv_sec;
                tv[1].tv_usec = f->stat.st_mtim.tv_nsec / 1000;
            } else if (!(valid & P9_SETATTR_MTIME_SET)) {
                tv[1].tv_sec = now.tv_sec;
                tv[1].tv_usec = now.tv_usec;
            } else {
                tv[1].tv_sec = mtime_sec;
                tv[1].tv_usec = mtime_nsec / 1000;
            }
            tvp = tv;
        }
        if (utimes (f->path, tvp) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
#endif
    }
    if ((valid & P9_SETATTR_CTIME) && !ctime_updated) {
        if (lchown (f->path, -1, -1) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path, valid);
error_quiet:
    if (ret)
        free (ret);
    return NULL;
}

static u32
_copy_dirent_linux (Fid *f, u8 *buf, u32 buflen)
{
    Npqid qid;
    u32 ret = 0;

    if (f->dirent->d_type == DT_UNKNOWN) {
        char path[PATH_MAX + 1];
        struct stat sb;
        snprintf (path, sizeof(path), "%s/%s", f->path, f->dirent->d_name);
        if (lstat (path, &sb) < 0) {
            np_uerror (errno);
            goto done;
        }
        _ustat2qid (&sb, &qid);
    } else  {
        _dirent2qid (f->dirent, &qid);
    }
    ret = np_serialize_p9dirent(&qid, f->dirent->d_off, f->dirent->d_type,
                                      f->dirent->d_name, buf, buflen);
done:
    return ret;
}

/* FIXME: seekdir(previous d_off) OK?
 * If not, substitute saved_dir_position for d_off in last returned.
 * If so, get rid of saved_dir_position and 2nd seekdir.
 */
static u32
_read_dir_linux (Fid *f, u8* buf, u64 offset, u32 count)
{
    int i, n = 0;
    off_t saved_dir_pos;

    if (offset == 0)
        rewinddir (f->dir);
    else
        seekdir (f->dir, offset);
    do {
        saved_dir_pos = telldir (f->dir);
        if (!(f->dirent = readdir (f->dir))) /* FIXME: use readdir_r */
            break;
        i = _copy_dirent_linux (f, buf + n, count - n);
        if (i == 0) {
            seekdir (f->dir, saved_dir_pos);
            break;
        }
        n += i;
    } while (n < count);
    return n;
}

Npfcall*
diod_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
    int n;
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_fsync (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (fsync(f->fd) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (ret)
        free (ret);
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
    Npfcall *ret = NULL;
    u8 status = P9_LOCK_ERROR;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (flags & ~P9_LOCK_FLAGS_BLOCK) { /* only one valid flag for now */
        np_uerror (EINVAL);             /*  (which we ignore) */
        goto error;
    }
    if (_fidstat(f) < 0)
        goto error_quiet;
    switch (type) {
        case F_UNLCK:
            if (flock (f->fd, LOCK_UN) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->lock_type = LOCK_UN;
            } else
                status = P9_LOCK_ERROR;
            break;
        case F_RDLCK:
            if (flock (f->fd, LOCK_SH | LOCK_NB) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->lock_type = LOCK_SH;
            } else if (errno == EWOULDBLOCK) {
                status = P9_LOCK_BLOCKED;
            } else
                status = P9_LOCK_ERROR;
            break;
        case F_WRLCK:
            if (flock (f->fd, LOCK_EX | LOCK_NB) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->lock_type = LOCK_EX;
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_getlock (Npfid *fid, u8 type, u64 start, u64 length, u32 proc_id,
             Npstr *client_id)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    char *cid = _p9strdup (client_id);

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!cid) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (_fidstat(f) < 0)
        goto error_quiet;
    switch (type) {
        case F_RDLCK:
            switch (f->lock_type) {
                case LOCK_EX:
                case LOCK_SH:
                    type = LOCK_UN;
                    break;
                case LOCK_UN:
                    if (flock (f->fd, LOCK_SH | LOCK_NB) >= 0) {
                        (void)flock (f->fd, LOCK_UN);
                        type = LOCK_UN;
                    } else
                        type = LOCK_EX;
                    break;
            }
            break;
        case F_WRLCK:
            switch (f->lock_type) {
                case LOCK_EX:
                    type = LOCK_UN;
                    break;
                case LOCK_SH:
                    /* Rather than upgrade the lock to LOCK_EX and risk
                     * not reacquiring the LOCK_SH afterwards, lie about
                     * the lock being available.  Getlock is racy anyway.
                     */
                    type = LOCK_UN;
                    break;
                case LOCK_UN:
                    if (flock (f->fd, LOCK_EX | LOCK_NB) >= 0) {
                        (void)flock (f->fd, LOCK_UN);
                        type = LOCK_UN;
                    } else
                        type = LOCK_EX; /* could also be LOCK_SH actually */
            }
            break;
        default:
            np_uerror (EINVAL);
            goto error;
    }
    if (type != LOCK_UN) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (ret)
        free (ret);
    if (cid)
        free (cid);
    return NULL;
}

Npfcall*
diod_link (Npfid *dfid, Npfid *fid, Npstr *name)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    Fid *df = dfid->aux;
    char *npath = NULL;
    int created = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(df->path, name)))
        goto error;
    if (link (f->path, npath) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    created = 1;
    if (!((ret = np_create_rlink ()))) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (npath);
    return ret;
error:
    errn (np_rerror (), "diod_link %s@%s:%s %s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path,
          df->path, name->len, name->str);
error_quiet:
    if (created && npath)
        (void)unlink (npath);
    if (npath)
        free (npath);
    if (ret)
        free (ret);
    return NULL;
}

Npfcall*
diod_mkdir (Npfid *fid, Npstr *name, u32 mode, u32 gid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    char *npath = NULL;
    Npqid qid;
    struct stat sb;
    mode_t saved_umask;
    int created = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(f->path, name)))
        goto error;
    saved_umask = umask(0);
    if (mkdir (npath, mode) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    created = 1;
    umask(saved_umask);
    if (lstat (npath, &sb) < 0) {
        np_uerror (errno);
        goto error; /* shouldn't happen? */
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmkdir (&qid)))) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (npath);
    return ret;
error:
    errn (np_rerror (), "diod_mkdir %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path,
          name->len, name->str);
error_quiet:
    if (created && npath)
        (void)rmdir(npath);
    if (npath)
        free (npath);
    if (ret)
        free (ret);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
