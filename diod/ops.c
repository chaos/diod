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
    /* advisory locking */
    int              lock_type;
    /* export flags */
    int              xflags;
    int              mountpt; /* handle server-side mount point specially */
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
        return -1;

    return 0;
}

/* Allocate our local fid struct which becomes attached to Npfid->aux.
 * Set npfs error state on error.
 */
static Fid *
_fidalloc (void)
{
    Fid *f = malloc (sizeof(*f));

    if (f) {
        f->path = NULL;
        f->fd = -1;
        f->dir = NULL;
        f->dirent = NULL;
        f->lock_type = LOCK_UN;
        f->xflags = 0;
        f->mountpt = 0;
    }
  
    return f;
}

/* Free our local fid struct.
 */
static void
_fidfree (Fid *f)
{
    if (f) {
        if (f->dir)
            (void)closedir(f->dir);
        else if (f->fd != -1)
            (void)close (f->fd);
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
    char *s = malloc (slen);
   
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
    struct stat sb;

    if (aname->len == 0 || *aname->str != '/') {
        np_uerror (EPERM);
        goto error;
    }
    if (!(f = _fidalloc ()) || !(f->path = np_strdup (aname))) {
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
    if (stat (f->path, &sb) < 0) { /* OK to follow symbolic links */
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
    fid->aux = f;
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

    if (!(nf = _fidalloc ()) || !(nf->path = strdup (f->path))) {
        np_uerror (ENOMEM);
        goto error;
    }
    nf->xflags = f->xflags;
    nf->mountpt = f->mountpt;
    newfid->aux = nf;
    return 1;
error:
    errn (np_rerror (), "diod_clone %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
    if (nf)
        _fidfree (nf);
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
    struct dirent *dp;
    char *ppath = NULL;
    int plen = strlen (path) + 4;
    char *name;

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
    while ((dp = readdir (dir))) {
        if (!strcmp (name, dp->d_name))
            break;
    } 
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
    Fid *f = fid->aux;
    struct stat sb, sb2;
    char *npath = NULL;

    if (f->mountpt) {
        np_uerror (ENOENT);
        goto error_quiet;
    }
    if (!(npath = _mkpath (f->path, wname))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (lstat (npath, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (lstat (f->path, &sb2) < 0) {
        np_uerror (errno);
        goto error;
    }
    if (sb.st_dev != sb2.st_dev) {
        if (_statmnt (npath, &sb) < 0)
            goto error;
        f->mountpt = 1;
    }
    free (f->path);
    f->path = npath;
    _ustat2qid (&sb, wqid);
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
    Npfcall *ret;
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
    return NULL;
}

/* Tclunk - close a file.
 */
Npfcall*
diod_clunk (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    int rc;

    if (f->dir) {
        rc = closedir(f->dir);
        f->dir = NULL;
        if (rc < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    } else if (f->fd != -1) {
        rc = close (f->fd);
        f->fd = -1;
        if (rc < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    }
    if (!(ret = np_create_rclunk ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_clunk %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
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

    if (statfs (f->path, &sb) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
    return NULL;
}

Npfcall*
diod_lopen (Npfid *fid, u32 flags)
{
    Fid *f = fid->aux;
    Npfcall *res;
    Npqid qid;
    u32 iounit = 0; /* if iounit is 0, v9fs will use msize-P9_IOHDRSZ */
    struct stat sb;

    if ((f->xflags & XFLAGS_RO) && ((flags & O_WRONLY) || (flags & O_RDWR))) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if ((flags & O_CREAT)) /* can't happen? */
        flags &= ~O_CREAT; /* clear and allow to fail with ENOENT */

    f->fd = open (f->path, flags);
    if (f->fd < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (fstat (f->fd, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (S_ISDIR(sb.st_mode) && !(f->dir = fdopendir (f->fd))) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    //iounit = sb.st_blksize;
    if (!(res = np_create_rlopen (&qid, iounit))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return res;
error:
    errn (np_rerror (), "diod_lopen %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (f->dir)
        (void)closedir (f->dir);
    else if (f->fd != -1)
        (void)close (f->fd); 
    f->dir = NULL;
    f->fd = -1;
    return NULL;
}

Npfcall*
diod_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode, u32 gid)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char *npath = NULL;
    Npqid qid;
    int fd = -1;
    struct stat sb;
    u32 iounit = 0; /* client will use msize-P9_IOHDRSZ */

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(flags & O_CREAT)) /* can't happen? */
        flags |= O_CREAT;
    if (!(npath = _mkpath(f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if ((fd = open (npath, flags, mode)) < 0 || fstat (fd, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    //iounit = sb.st_blksize;
    if (!((ret = np_create_rlcreate (&qid, iounit)))) {
        (void)unlink (npath);
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
    if (fd >= 0)
        (void)close (fd);
    if (npath)
        free (npath);
    return NULL;
}

Npfcall*
diod_symlink(Npfid *fid, Npstr *name, Npstr *symtgt, u32 gid)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char *target = NULL, *npath = NULL;
    Npqid qid;
    struct stat sb;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (!(target = np_strdup (symtgt))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (symlink (target, npath) < 0 || lstat (npath, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rsymlink (&qid)))) {
        (void)unlink (npath);
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
    if (npath)
        free (npath);
    if (target)
        free (target);
    return NULL;
}

Npfcall*
diod_mknod(Npfid *fid, Npstr *name, u32 mode, u32 major, u32 minor, u32 gid)
{
    Npfcall *ret;
    Fid *f = fid->aux;
    char *npath = NULL;
    Npqid qid;
    struct stat sb;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (mknod (npath, mode, makedev (major, minor)) < 0
                                        || lstat (npath, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmknod (&qid)))) {
        (void)unlink (npath);
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
    if (npath)
        free (npath);
    return NULL;
}

/* Trename - rename a file, potentially to another directory
 */
Npfcall*
diod_rename (Npfid *fid, Npfid *dfid, Npstr *name)
{
    Fid *f = fid->aux;
    Fid *d = dfid->aux;
    Npfcall *ret;
    char *npath = NULL;
    int renamed = 0;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(d->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
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
    return NULL;
}

Npfcall*
diod_readlink(Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char target[PATH_MAX + 1];
    int n;

    if ((n = readlink (f->path, target, sizeof(target) - 1)) < 0) {
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
    return NULL;
}

Npfcall*
diod_getattr(Npfid *fid, u64 request_mask)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    Npqid qid;
    struct stat sb;

    if (f->mountpt) {
        if (_statmnt (f->path, &sb) < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    } else {
        if (lstat (f->path, &sb) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
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

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if ((valid & P9_SETATTR_MODE)) { /* N.B. derefs symlinks */
        if (chmod (f->path, mode) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_SETATTR_UID) || (valid & P9_SETATTR_GID)) {
        if (lchown (f->path, (valid & P9_SETATTR_UID) ? uid : -1,
                             (valid & P9_SETATTR_GID) ? gid : -1) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_SETATTR_SIZE)) {
        if (truncate (f->path, size) < 0) {
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
        if (utimensat(-1, f->path, ts, AT_SYMLINK_NOFOLLOW) < 0) {
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
            if (lstat(f->path, &sb) < 0) {
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
        if (utimes (f->path, tvp) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
#endif /* HAVE_UTIMENSAT */
        ctime_updated = 1;
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

static u32
_read_dir_linux (Fid *f, u8* buf, u64 offset, u32 count)
{
    int i, n = 0;

    if (offset == 0)
        rewinddir (f->dir);
    else
        seekdir (f->dir, offset);
    do {
        if (!(f->dirent = readdir (f->dir)))
            break;
        if (f->mountpt && strcmp (f->dirent->d_name, ".") 
                       && strcmp (f->dirent->d_name, ".."))
                continue;
        i = _copy_dirent_linux (f, buf + n, count - n);
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
    return NULL;
}

Npfcall*
diod_fsync (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;

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
    struct stat sb;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (flags & ~P9_LOCK_FLAGS_BLOCK) { /* only one valid flag for now */
        np_uerror (EINVAL);             /*  (which we ignore) */
        goto error;
    }
    if (fstat(f->fd, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    switch (type) {
        case P9_LOCK_TYPE_UNLCK:
            if (flock (f->fd, LOCK_UN) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->lock_type = LOCK_UN;
            } else
                status = P9_LOCK_ERROR;
            break;
        case P9_LOCK_TYPE_RDLCK:
            if (flock (f->fd, LOCK_SH | LOCK_NB) >= 0) {
                status = P9_LOCK_SUCCESS;
                f->lock_type = LOCK_SH;
            } else if (errno == EWOULDBLOCK) {
                status = P9_LOCK_BLOCKED;
            } else
                status = P9_LOCK_ERROR;
            break;
        case P9_LOCK_TYPE_WRLCK:
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
    return NULL;
}

Npfcall*
diod_getlock (Npfid *fid, u8 type, u64 start, u64 length, u32 proc_id,
             Npstr *client_id)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char *cid = NULL;
    struct stat sb;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(cid = np_strdup (client_id))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (fstat(f->fd, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    switch (type) {
        case P9_LOCK_TYPE_RDLCK:
            switch (f->lock_type) {
                case LOCK_EX:
                case LOCK_SH:
                    type = P9_LOCK_TYPE_UNLCK;
                    break;
                case LOCK_UN:
                    if (flock (f->fd, LOCK_SH | LOCK_NB) >= 0) {
                        (void)flock (f->fd, LOCK_UN);
                        type = P9_LOCK_TYPE_UNLCK;
                    } else
                        type = P9_LOCK_TYPE_WRLCK;
                    break;
            }
            break;
        case P9_LOCK_TYPE_WRLCK:
            switch (f->lock_type) {
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
                    if (flock (f->fd, LOCK_EX | LOCK_NB) >= 0) {
                        (void)flock (f->fd, LOCK_UN);
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
          fid->user->uname, np_conn_get_client_id (fid->conn), f->path);
error_quiet:
    if (cid)
        free (cid);
    return NULL;
}

Npfcall*
diod_link (Npfid *dfid, Npfid *fid, Npstr *name)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    Fid *df = dfid->aux;
    char *npath = NULL;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(df->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (link (f->path, npath) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (!((ret = np_create_rlink ()))) {
        (void)unlink (npath);
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
    if (npath)
        free (npath);
    return NULL;
}

Npfcall*
diod_mkdir (Npfid *fid, Npstr *name, u32 mode, u32 gid)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char *npath = NULL;
    Npqid qid;
    struct stat sb;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto error_quiet;
    }
    if (!(npath = _mkpath(f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (mkdir (npath, mode) < 0 || lstat (npath, &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmkdir (&qid)))) {
        (void)rmdir(npath);
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
    if (npath)
        free (npath);
    return NULL;
}

char *
diod_get_path (Npfid *fid)
{
    Fid *f = fid->aux;

    return f ? f->path : NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
