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
#ifdef __MACH__
#define _DARWIN_C_SOURCE    /* fs stuff */
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#ifndef __MACH__
#include <sys/statfs.h>
#else
#include <sys/param.h>
#include <sys/mount.h>
#endif
#include <sys/socket.h>
#include <sys/time.h>
#ifndef __MACH__
#include <sys/fsuid.h>
#endif
#include <sys/mman.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <stdarg.h>

/* Legacy system support */
#ifndef O_CLOEXEC
#define O_CLOEXEC   0
#endif

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
#include "ioctx.h"
#include "xattr.h"
#include "fid.h"

Npfcall     *diod_attach (Npfid *fid, Npfid *afid, Npstr *aname);
int          diod_clone  (Npfid *fid, Npfid *newfid);
int          diod_walk   (Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall     *diod_read   (Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall     *diod_write  (Npfid *fid, u64 offset, u32 count, u8 *data,
                          Npreq *req);
Npfcall     *diod_clunk  (Npfid *fid);
Npfcall     *diod_remove (Npfid *fid);

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
Npfcall     *diod_xattrwalk (Npfid *fid, Npfid *attrfid, Npstr *name);
Npfcall     *diod_xattrcreate (Npfid *fid, Npstr *name, u64 attr_size,
                               u32 flags);
int          diod_remapuser (Npfid *fid);
int          diod_exportok (Npfid *fid);
int          diod_auth_required (Npstr *uname, u32 n_uname, Npstr *aname);
char        *diod_get_path (Npfid *fid);
char        *diod_get_files (char *name, void *a);

int
diod_init (Npsrv *srv)
{
    srv->msize = 65536;
    srv->fiddestroy = diod_fiddestroy;
    srv->logmsg = diod_log_msg;
    srv->remapuser = diod_remapuser;
    srv->exportok = diod_exportok;
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
    srv->xattrwalk = diod_xattrwalk;
    srv->xattrcreate = diod_xattrcreate;
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
    if (ppool_init (srv) < 0)
        goto error;
    return 0;
error:
    diod_fini (srv);
    return -1;
}

void
diod_fini (Npsrv *srv)
{
    ppool_fini (srv);
}

/* Create a 9P qid from a file's stat info.
 * N.B. v9fs maps st_ino = qid->path + 2
 */
void
diod_ustat2qid (struct stat *st, Npqid *qid)
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
    NP_ASSERT (d->d_type != DT_UNKNOWN);
    qid->path = d->d_ino;
    qid->version = 0;
    qid->type = 0;
    if (d->d_type == DT_DIR)
        qid->type |= P9_QTDIR;
    if (d->d_type == DT_LNK)
        qid->type |= P9_QTSYMLINK;
}

int
diod_remapuser (Npfid *fid)
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
diod_exportok (Npfid *fid)
{
    int xflags;

    if (!fid || !fid->aname || strlen (fid->aname) == 0)
        return 0;
    if (fid->aname[0] != '/' && strcmp (fid->aname, "ctl") != 0)
        return 0;
    if (!diod_match_exports (fid->aname, fid->conn, fid->user, &xflags))
        return 0;
    if ((xflags & DIOD_FID_FLAGS_ROFS))
        fid->flags |= FID_FLAGS_ROFS;
    return 1;
}

int
diod_auth_required (Npstr *uname, u32 n_uname, Npstr *aname)
{
    int xflags;

    if (!diod_conf_get_auth_required ())
        return 0; /* auth disabled globally */

    if (diod_fetch_xflags (aname, &xflags) && (xflags & XFLAGS_NOAUTH))
        return 0; /* auth disabled for this export */

    return 1; /* auth required */
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

    if (!(f = diod_fidalloc (fid, aname))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (diod_conf_opt_runasuid ()) {
        if (fid->user->uid != diod_conf_get_runasuid ()) {
            np_uerror (EPERM);
            goto error;
        }
    }
    if (diod_fetch_xflags (aname, &xflags)) {
        if ((xflags & XFLAGS_SHAREFD))
            f->flags |= DIOD_FID_FLAGS_SHAREFD;
    }
    if (stat (path_s (f->path), &sb) < 0) { /* OK to follow symbolic links */
        np_uerror (errno);
        goto error;
    }
    /* N.B. removed S_ISDIR (sb.st_mode) || return ENOTDIR check.
     * Allow a regular file or a block device to be exported.
     */
    diod_ustat2qid (&sb, &qid);
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

    if (!(diod_fidclone (newfid, fid))) {
        np_uerror (ENOMEM);
        goto error;
    }
    return 1;
error:
    errn (np_rerror (), "diod_clone %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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
    if (!(npath = path_append (srv, f->path, wname))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (lstat (path_s (npath), &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (lstat (path_s (f->path), &sb2) < 0) {
        np_uerror (errno);
        goto error;
    }
    if (sb.st_dev != sb2.st_dev) {
        if (_statmnt (path_s (npath), &sb) < 0)
            goto error;
        f->flags |= DIOD_FID_FLAGS_MOUNTPT;
    }
    path_decref (srv, f->path);
    f->path = npath; 
    diod_ustat2qid (&sb, wqid);
    return 1;
error:
    errn (np_rerror (), "diod_walk %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
          wname->len, wname->str);
error_quiet:
    if (npath)
        path_decref (srv, npath);
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

    if (!f->ioctx && !(f->flags & DIOD_FID_FLAGS_XATTR)) {
        msg ("diod_read: fid is not open");
        np_uerror (EBADF);
        goto error;
    }
    if (!(ret = np_alloc_rread (count))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (f->flags & DIOD_FID_FLAGS_XATTR) 
        n = xattr_pread (f->xattr, ret->u.rread.data, count, offset);
    else
        n = ioctx_pread (f->ioctx, ret->u.rread.data, count, offset);
    if (n < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    np_set_rread_count (ret, n);
    return ret;
error:
    errn (np_rerror (), "diod_read %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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

    if (!f->ioctx && !(f->flags & DIOD_FID_FLAGS_XATTR)) {
        msg ("diod_write: fid is not open");
        np_uerror (EBADF);
        goto error;
    }
    if (f->flags & DIOD_FID_FLAGS_XATTR)
        n = xattr_pwrite (f->xattr, data, count, offset);
    else
        n = ioctx_pwrite (f->ioctx, data, count, offset);
    if (n < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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

    if (f->flags & DIOD_FID_FLAGS_XATTR) {
        if (xattr_close (fid) < 0)
            goto error_quiet;
    } else if (f->ioctx) {
        if (ioctx_close (fid, 1) < 0)
            goto error_quiet;
    }
    if (!(ret = np_create_rclunk ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_clunk %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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

    if (remove (path_s (f->path)) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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
    u32 type = V9FS_MAGIC;

    if (statfs (path_s (f->path), &sb) < 0) {
        np_uerror (errno);
        goto error;
    }

#ifndef __MACH__
    fsid = (u64)sb.f_fsid.__val[0] | ((u64)sb.f_fsid.__val[1] << 32);
#else
    fsid = (u64)sb.f_fsid.val[0] | ((u64)sb.f_fsid.val[1] << 32);
#endif
    if (diod_conf_get_statfs_passthru ())
        type = sb.f_type;

#ifndef __MACH__
    if (!(ret = np_create_rstatfs(type, sb.f_bsize, sb.f_blocks,
                                  sb.f_bfree, sb.f_bavail, sb.f_files,
                                  sb.f_ffree, fsid,
                                  sb.f_namelen))) {
#else
    if (!(ret = np_create_rstatfs(type, sb.f_bsize, sb.f_blocks,
                                  sb.f_bfree, sb.f_bavail, sb.f_files,
                                  sb.f_ffree, fsid,
                                  PATH_MAX))) {
#endif
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_statfs %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
    return NULL;
}

/* Remap 9p2000.L open flags to linux open flags.
 * (I borrowed liberally from vfs_inode_dotl.c)
 */

struct dotl_openflag_map {
        int open_flag;
        int dotl_flag;
};

static int
_remap_oflags (int flags)
{
    int i;
    int rflags = 0;
    
    struct dotl_openflag_map dotl_oflag_map[] = {
        { O_CREAT,      P9_DOTL_CREATE },
        { O_EXCL,       P9_DOTL_EXCL },
        { O_NOCTTY,     P9_DOTL_NOCTTY },
        { O_TRUNC,      P9_DOTL_TRUNC },
        { O_APPEND,     P9_DOTL_APPEND },
        { O_NONBLOCK,   P9_DOTL_NONBLOCK },
        { O_DSYNC,      P9_DOTL_DSYNC },
        { FASYNC,       P9_DOTL_FASYNC },
#ifndef __MACH__
        { O_DIRECT,     P9_DOTL_DIRECT },
        { O_LARGEFILE,  P9_DOTL_LARGEFILE },
#else
        { 0,     P9_DOTL_DIRECT },
        { 0,  P9_DOTL_LARGEFILE },
#endif
        { O_DIRECTORY,  P9_DOTL_DIRECTORY },
        { O_NOFOLLOW,   P9_DOTL_NOFOLLOW },
#ifndef __MACH__
        { O_NOATIME,    P9_DOTL_NOATIME },
#else
        { 0,    P9_DOTL_NOATIME },
#endif
        { O_CLOEXEC,    P9_DOTL_CLOEXEC },
        { O_SYNC,       P9_DOTL_SYNC},
    };
    int nel = sizeof(dotl_oflag_map)/sizeof(dotl_oflag_map[0]);

    rflags |= (flags & O_ACCMODE);
    for (i = 0; i < nel; i++) {
        if (flags & dotl_oflag_map[i].dotl_flag)
            rflags |= dotl_oflag_map[i].open_flag;
    }
    return rflags;
}

Npfcall*
diod_lopen (Npfid *fid, u32 flags)
{
    Fid *f = fid->aux;
    Npfcall *res;

    flags = _remap_oflags (flags);

#ifndef __MACH__
    if (flags & O_DIRECT) {
        np_uerror (EINVAL); /* O_DIRECT not allowed - see issue 110 */
        goto error_quiet;
    }
#endif
    if ((flags & O_CREAT)) /* can't happen? */
        flags &= ~O_CREAT; /* clear and allow to fail with ENOENT */

    if (f->ioctx != NULL) {
        msg ("diod_lopen: fid is already open");
        np_uerror (EINVAL);
        goto error; 
    }
    if (ioctx_open (fid, flags, 0) < 0) {
        if (np_rerror () == ENOMEM)
            goto error;
        goto error_quiet;
    }
    if (!(res = np_create_rlopen (ioctx_qid (f->ioctx),
                                  ioctx_iounit (f->ioctx)))) {
        (void)ioctx_close (fid, 0);
        np_uerror (ENOMEM);
        goto error;
    }
    return res;
error:
    errn (np_rerror (), "diod_lopen %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
error_quiet:
    return NULL;
}

Npfcall*
diod_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode, u32 gid)
{
    Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Npfcall *ret;
    Path opath = NULL;

    flags = _remap_oflags (flags);

#ifndef __MACH__
    if (flags & O_DIRECT) {
        np_uerror (EINVAL); /* O_DIRECT not allowed - see issue 110 */
        goto error_quiet;
    }
#endif
    if (!(flags & O_CREAT)) /* can't happen? */
        flags |= O_CREAT;

    if (f->ioctx != NULL) {
        msg ("diod_lcreate: fid is already open");
        np_uerror (EINVAL);
        goto error; 
    }
    opath = f->path;
    if (!(f->path = path_append (srv, opath, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (ioctx_open (fid, flags, mode) < 0) {
        if (np_rerror () == ENOMEM)
            goto error;
        goto error_quiet;
    }
    if (!((ret = np_create_rlcreate (ioctx_qid (f->ioctx),
                                     ioctx_iounit (f->ioctx))))) {
        (void)ioctx_close (fid, 0);
        (void)unlink (path_s (f->path));
        np_uerror (ENOMEM);
        goto error;
    }
    path_decref (srv, opath);
    return ret;
error:
    errn (np_rerror (), "diod_lcreate %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          opath ? path_s (opath) : path_s (f->path), name->len, name->str);
error_quiet:
    if (opath) {
        if (f->path)
            path_decref (srv, f->path);
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

    if (!(npath = path_append (srv, f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (!(target = np_strdup (symtgt))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (symlink (target, path_s (npath)) < 0 || lstat (path_s (npath),
                                                       &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    diod_ustat2qid (&sb, &qid);
    if (!((ret = np_create_rsymlink (&qid)))) {
        (void)unlink (path_s (npath));
        np_uerror (ENOMEM);
        goto error;
    }
    path_decref (srv, npath);
    free (target);
    return ret;
error:
    errn (np_rerror (), "diod_symlink %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path), name->len, name->str);
error_quiet:
    if (npath)
        path_decref (srv, npath);
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

    if (!(npath = path_append (srv, f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (mknod (path_s (npath), mode, makedev (major, minor)) < 0
                                        || lstat (path_s (npath), &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    diod_ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmknod (&qid)))) {
        (void)unlink (path_s (npath));
        np_uerror (ENOMEM);
        goto error;
    }
    path_decref (srv, npath);
    return ret;
error:
    errn (np_rerror (), "diod_mknod %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
          name->len, name->str);
error_quiet:
    if (npath)
        path_decref (srv, npath);
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

    if (!(npath = path_append (srv, d->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (rename (path_s (f->path), path_s (npath)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    renamed = 1;
    if (!(ret = np_create_rrename ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    path_decref (srv, f->path);
    f->path = npath;
    return ret;
error:
    errn (np_rerror (), "diod_rename %s@%s:%s to %s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
          path_s (d->path), name->len, name->str);
error_quiet:
    if (renamed && npath)
        (void)rename (path_s (npath), path_s (f->path));
    if (npath)
        path_decref (srv, npath);
    return NULL;
}

Npfcall*
diod_readlink(Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char target[PATH_MAX + 1];
    int n;

    if ((n = readlink (path_s (f->path), target, sizeof(target) - 1)) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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
        if (_statmnt (path_s (f->path), &sb) < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    } else {
        if (lstat (path_s (f->path), &sb) < 0) {
            np_uerror (errno);
            goto error_quiet;
        }
    }
    diod_ustat2qid (&sb, &qid);
#ifdef __MACH__
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif
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
#ifdef __MACH__
#undef st_atim
#undef st_mtim
#undef st_ctim
#endif
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_getattr %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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

    if ((valid & P9_ATTR_MODE)) { /* N.B. derefs symlinks */
        if (chmod (path_s (f->path), mode) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_ATTR_UID) || (valid & P9_ATTR_GID)) {
        if (lchown (path_s (f->path), (valid & P9_ATTR_UID) ? uid : -1,
                                      (valid & P9_ATTR_GID) ? gid : -1) < 0){
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_ATTR_SIZE)) {
        if (truncate (path_s (f->path), size) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
        ctime_updated = 1;
    }
    if ((valid & P9_ATTR_ATIME) || (valid & P9_ATTR_MTIME)) {
#if HAVE_UTIMENSAT
        struct timespec ts[2];

        if (!(valid & P9_ATTR_ATIME)) {
            ts[0].tv_sec = 0;
            ts[0].tv_nsec = UTIME_OMIT;
        } else if (!(valid & P9_ATTR_ATIME_SET)) {
            ts[0].tv_sec = 0;
            ts[0].tv_nsec = UTIME_NOW;
        } else {
            ts[0].tv_sec = atime_sec;
            ts[0].tv_nsec = atime_nsec;
        }
        if (!(valid & P9_ATTR_MTIME)) {
            ts[1].tv_sec = 0;
            ts[1].tv_nsec = UTIME_OMIT;
        } else if (!(valid & P9_ATTR_MTIME_SET)) {
            ts[1].tv_sec = 0;
            ts[1].tv_nsec = UTIME_NOW;
        } else {
            ts[1].tv_sec = mtime_sec;
            ts[1].tv_nsec = mtime_nsec;
        }
        if (utimensat(-1, path_s (f->path), ts, AT_SYMLINK_NOFOLLOW) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
#else /* HAVE_UTIMENSAT */
#ifdef __MACH__
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif
        struct timeval tv[2], now, *tvp;
        struct stat sb;
        if ((valid & P9_ATTR_ATIME) && !(valid & P9_ATTR_ATIME_SET)
         && (valid & P9_ATTR_MTIME) && !(valid & P9_ATTR_MTIME_SET)) {
            tvp = NULL; /* set both to now */
        } else {
            if (lstat(path_s (f->path), &sb) < 0) {
                np_uerror (errno);
                goto error_quiet;
            }
            if (gettimeofday (&now, NULL) < 0) {
                np_uerror (errno);
                goto error_quiet;
            }
            if (!(valid & P9_ATTR_ATIME)) {
                tv[0].tv_sec = sb.st_atim.tv_sec;
                tv[0].tv_usec = sb.st_atim.tv_nsec / 1000;
            } else if (!(valid & P9_ATTR_ATIME_SET)) {
                tv[0].tv_sec = now.tv_sec;
                tv[0].tv_usec = now.tv_usec;
            } else {
                tv[0].tv_sec = atime_sec;
                tv[0].tv_usec = atime_nsec / 1000;
            }

            if (!(valid & P9_ATTR_MTIME)) {
                tv[1].tv_sec = sb.st_mtim.tv_sec;
                tv[1].tv_usec = sb.st_mtim.tv_nsec / 1000;
            } else if (!(valid & P9_ATTR_MTIME_SET)) {
                tv[1].tv_sec = now.tv_sec;
                tv[1].tv_usec = now.tv_usec;
            } else {
                tv[1].tv_sec = mtime_sec;
                tv[1].tv_usec = mtime_nsec / 1000;
            }
            tvp = tv;
        }
#ifdef __MACH__
#undef st_atim
#undef st_mtim
#undef st_ctim
#endif
        if (utimes (path_s (f->path), tvp) < 0) {
            np_uerror(errno);
            goto error_quiet;
        }
#endif /* HAVE_UTIMENSAT */
        ctime_updated = 1;
    }
    if ((valid & P9_ATTR_CTIME) && !ctime_updated) {
        if (lchown (path_s (f->path), -1, -1) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
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
        snprintf (path, sizeof(path), "%s/%s", path_s (f->path), dp->d_name);
        if (lstat (path, &sb) < 0) {
            np_uerror (errno);
            goto done;
        }
        diod_ustat2qid (&sb, &qid);
    } else  {
        _dirent2qid (dp, &qid);
    }
#ifndef __MACH__
    ret = np_serialize_p9dirent(&qid, dp->d_off, dp->d_type,
                                      dp->d_name, buf, buflen);
#else
    long diroffset;
    diroffset = ioctx_telldir(f->ioctx);
    ret = np_serialize_p9dirent(&qid, diroffset, dp->d_type,
                                      dp->d_name, buf, buflen);
#endif
done:
    return ret;
}

static u32
_read_dir_linux (Fid *f, u8* buf, u64 offset, u32 count)
{
    struct dirent dbuf, *dp;
    int i, n = 0, err;

    if (offset == 0)
        ioctx_rewinddir (f->ioctx);
    else
        ioctx_seekdir (f->ioctx, offset);
    do {
        err = ioctx_readdir_r (f->ioctx, &dbuf, &dp);
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

    if (!f->ioctx) {
        msg ("diod_readdir: fid is not open");
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
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
    return NULL;
}

Npfcall*
diod_fsync (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret;

    if (!f->ioctx) {
        msg ("diod_fsync: fid is not open");
        np_uerror (EBADF);
        goto error;
    }
    if (ioctx_fsync (f->ioctx) < 0) {
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
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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

    if (flags & ~P9_LOCK_FLAGS_BLOCK) { /* only one valid flag for now */
        np_uerror (EINVAL);             /*  (which we ignore) */
        goto error;
    }
    if (!f->ioctx) {
        msg ("diod_lock: fid is not open");
        np_uerror (EBADF);
        goto error;
    }
    switch (type) {
        case P9_LOCK_TYPE_UNLCK:
            if (ioctx_flock (f->ioctx, LOCK_UN) == 0)
                status = P9_LOCK_SUCCESS;
            break;
        case P9_LOCK_TYPE_RDLCK:
            if (ioctx_flock (f->ioctx, LOCK_SH | LOCK_NB) == 0)
                status = P9_LOCK_SUCCESS;
            else if (errno == EWOULDBLOCK)
                status = P9_LOCK_BLOCKED;
            break;
        case P9_LOCK_TYPE_WRLCK:
            if (ioctx_flock (f->ioctx, LOCK_EX | LOCK_NB) == 0) 
                status = P9_LOCK_SUCCESS;
            else if (errno == EWOULDBLOCK)
                status  = P9_LOCK_BLOCKED;
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
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
    return NULL;
}

Npfcall*
diod_getlock (Npfid *fid, u8 type, u64 start, u64 length, u32 proc_id,
             Npstr *client_id)
{
    Fid *f = fid->aux;
    Npfcall *ret;
    char *cid = NULL;
    int ftype;

    if (!f->ioctx) {
        msg ("diod_getlock: fid is not open");
        np_uerror (EBADF);
        goto error;
    }
    if (!(cid = np_strdup (client_id))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (type != P9_LOCK_TYPE_RDLCK && type != P9_LOCK_TYPE_WRLCK) {
        np_uerror (EINVAL);
        goto error;
    }
    ftype = (type == P9_LOCK_TYPE_RDLCK) ? LOCK_SH : LOCK_EX;
    ftype = ioctx_testlock (f->ioctx, ftype);    
    type = (ftype == LOCK_EX) ? P9_LOCK_TYPE_WRLCK : P9_LOCK_TYPE_UNLCK;
    if (!((ret = np_create_rgetlock(type, start, length, proc_id, cid)))) {
        np_uerror (ENOMEM);
        goto error;
    }
    free (cid);
    return ret;
error:
    errn (np_rerror (), "diod_getlock %s@%s:%s",
          fid->user->uname, np_conn_get_client_id (fid->conn),
          path_s (f->path));
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

    if (!(npath = path_append (srv, df->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (link (path_s (f->path), path_s (npath)) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    if (!((ret = np_create_rlink ()))) {
        (void)unlink (path_s (npath));
        np_uerror (ENOMEM);
        goto error;
    }
    path_decref (srv, npath);
    return ret;
error:
    errn (np_rerror (), "diod_link %s@%s:%s %s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
          path_s (df->path), name->len, name->str);
error_quiet:
    if (npath)
        path_decref (srv, npath);
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

    if (!(npath = path_append (srv, f->path, name))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (mkdir (path_s (npath), mode) < 0 || lstat (path_s (npath), &sb) < 0) {
        np_uerror (errno);
        goto error_quiet;
    }
    diod_ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmkdir (&qid)))) {
        (void)rmdir(path_s (npath));
        np_uerror (ENOMEM);
        goto error;
    }
    path_decref (srv, npath);
    return ret;
error:
    errn (np_rerror (), "diod_mkdir %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
          name->len, name->str);
error_quiet:
    if (npath)
        path_decref (srv, npath);
    return NULL;
}

Npfcall*
diod_xattrwalk (Npfid *fid, Npfid *attrfid, Npstr *name)
{
    //Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Fid *nf;
    u64 size;
    Npfcall *ret;

    if (!(nf = diod_fidclone (attrfid, fid))) {
        np_uerror (ENOMEM);
        goto error;
    }
    if (xattr_open (attrfid, name, &size) < 0) {
        if (np_rerror () == ENODATA)
            goto error_quiet;
        goto error;
    }
    nf->flags |= DIOD_FID_FLAGS_XATTR;
    if (!(ret = np_create_rxattrwalk (size))) {
        diod_fiddestroy (attrfid);
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
error:
    errn (np_rerror (), "diod_xattrwalk %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
          name->len, name->str);
error_quiet:
    if (attrfid)
        diod_fiddestroy (attrfid);
    return NULL;
}

Npfcall*
diod_xattrcreate (Npfid *fid, Npstr *name, u64 attr_size, u32 flags)
{
    //Npsrv *srv = fid->conn->srv;
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

    if (xattr_create (fid, name, attr_size, flags) < 0)
        goto error;
    f->flags |= DIOD_FID_FLAGS_XATTR;
    if (!(ret = np_create_rxattrcreate ())) {
        np_uerror (ENOMEM);
        goto error;
    }
    return ret;
    
error:
    errn (np_rerror (), "diod_xattrcreate %s@%s:%s/%.*s",
          fid->user->uname, np_conn_get_client_id (fid->conn), path_s (f->path),
          name->len, name->str);
    return NULL;
}

char *
diod_get_path (Npfid *fid)
{
    Fid *f = fid->aux;

    return f ? path_s (f->path) : NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
