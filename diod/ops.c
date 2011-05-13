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


void
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
}

/* Update stat info contained in fid.
 * Set npfs error state on error.
 */
static int
_fidstat (Fid *fid)
{
    if (lstat (fid->path, &fid->stat) < 0) {
        np_uerror (errno);
        return -1;
    }
    return 0;
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

/* Read from specified offset.
 * Set npfs error state on error.
 */
static ssize_t 
_pread (Npfid *fid, void *buf, size_t count, off_t offset)
{
    Fid *f = fid->aux;
    ssize_t n;

    if ((n = pread (f->fd, buf, count, offset)) < 0) {
        np_uerror (errno);
        if (errno == EIO)
            err ("read %s", f->path);
    }
    return n;
}

/* Write to specified offset.
 * Set npfs error state on error.
 */
static ssize_t 
_pwrite (Npfid *fid, void *buf, size_t count, off_t offset)
{
    Fid *f = fid->aux;
    ssize_t n;

    if ((n = pwrite (f->fd, buf, count, offset)) < 0) {
        np_uerror (errno);
        if (errno == EIO)
            err ("write %s", f->path);
    }
    return n;
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
    /* FIXME: ramifcations of always-zero version? */
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
    /* FIXME: ramifcations of always-zero version? */
    qid->version = 0;
    qid->type = 0;
    if (d->d_type == DT_DIR)
        qid->type |= P9_QTDIR;
    if (d->d_type == DT_LNK)
        qid->type |= P9_QTSYMLINK;
}

static int
_match_export_users (Export *x, Npuser *user)
{
    if (!x->users)
        return 1;
    /* FIXME */
    return 0; /* no match */
}


/* FIXME: client_id could be hostname or IP.
 * We probably want both to work for an exports match.
 */
static int
_match_export_hosts (Export *x, Npconn *conn)
{
    char *client_id = np_conn_get_client_id (conn);
    hostlist_t hl = NULL;
    int res = 0; /* no match */

    /* no client_id restrictions */
    if (!x->hosts) {
        res = 1;
        goto done;
    }
    if (!(hl = hostlist_create (x->hosts))) {
        np_uerror (ENOMEM);
        goto done;
    }
    /* client_id found in exports */
    if (hostlist_find (hl, client_id) != -1) {
        res = 1;
        goto done;
    }
done:
    if (hl)
        hostlist_destroy (hl);
    return res;
}

/* N.B. in diod_conf_validate_exports () we already have ensured
 * that export begins with / and contains no /.. elements.
 */
static int
_match_export_path (Export *x, char *path)
{
    int xlen = strlen (x->path);
    int plen = strlen (path);

    /* an export of / matches all */
    if (strcmp (x->path, "/") == 0)
        return 1;
    /* drop trailing slashes from export */
    while (xlen > 0 && x->path[xlen - 1] == '/')
        xlen--;
    /* export is identical to path */
    if (plen == xlen && strncmp (x->path, path, plen) == 0)
        return 1;
    /* export is parent of path */
    if (plen > xlen && path[xlen] == '/')
        return 1;

    return 0; /* no match */
}

static int
_match_exports (char *path, Npconn *conn, Npuser *user, int *xfp)
{
    List exports = diod_conf_get_exports ();
    ListIterator itr;
    Export *x;
    int res = 0; /* DENIED */

    if (!exports) {
        np_uerror (EPERM);
        goto done;
    }
    if (strstr (path, "/..") != NULL) {
        np_uerror (EPERM);
        goto done;
    }
    if (!(itr = list_iterator_create (exports))) {
        np_uerror (ENOMEM);
        goto done;
    }
    while (res == 0 && (x = list_next (itr))) {
        if (!_match_export_path (x, path))
            continue;
        if (!_match_export_hosts (x, conn))
            continue;
        if (!_match_export_users (x, user))
            continue;
        if (xfp)
            *xfp = x->oflags;
        res = 1;
    }
    list_iterator_destroy (itr);
   
    /* If 'exportall' option, check against /proc/mounts
     */
    if (res == 0 && diod_conf_get_exportall ()) {
        if (!(exports = diod_conf_get_mounts ())) {
            np_uerror (ENOMEM);
            goto done;
        }
        if (!(itr = list_iterator_create (exports))) {
            list_destroy (exports);
            np_uerror (ENOMEM);
            goto done;
        }
        while (res == 0 && (x = list_next (itr))) {
            if (_match_export_path (x, path)) {
                res = 1;
                break;
            }
        }
        list_iterator_destroy (itr);
        list_destroy (exports);
    }
    if (res == 0)
        np_uerror (EPERM);
done:
    return res;
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
        msg ("diod_attach: mount attempt for malformed aname");
        goto done;
    }
    if (!(f = _fidalloc ()) || !(f->path = _p9strdup(aname))) {
        msg ("diod_attach: out of memory");
        goto done;
    }
    if (diod_conf_opt_runasuid ()) {
        uid_t onlyuid = diod_conf_get_runasuid ();
        if (fid->user->uid != onlyuid) {
            np_uerror (EPERM);
            msg ("diod_attach: server runs exclusively for uid=%d", onlyuid);
            goto done;
        }
    }
    if (!_match_exports (f->path, fid->conn, fid->user, &f->xflags)) {
        msg ("diod_attach: %s not exported", f->path);
        goto done;
    }
    if (_fidstat (f) < 0) {
        msg ("diod_attach: could not stat mount point");
        goto done;
    }
    _ustat2qid (&f->stat, &qid);
    if ((ret = np_create_rattach (&qid)) == NULL) {
        np_uerror (ENOMEM);
        msg ("diod_attach: out of memory");
        goto done;
    }
    fid->aux = f;
    np_fid_incref (fid);

done:
    if (np_rerror () && f)
        _fidfree (f);
    return ret;
}

/* Twalk - walk a file path
 * Called from fcall.c::np_walk () to clone the fid.
 * On error, call np_uerror () and return 0.
 */
int
diod_clone (Npfid *fid, Npfid *newfid)
{
    Fid *f = fid->aux;
    Fid *nf = _fidalloc ();
    int ret = 0;

    if (!nf) {
        msg ("diod_clone: out of memory");
        goto done;
    }
    if (!(nf->path = _strdup (f->path))) {
        msg ("diod_clone: out of memory");
        goto done;
    }
    nf->xflags = f->xflags;
    newfid->aux = nf;
    ret = 1;

done:   
    if (np_rerror ()) {
        if (nf)
            _fidfree (nf);
    }
    return ret;
}

/* Twalk - walk a file path
 * Called from fcall.c::np_walk () on each wname component in succession.
 * On error, call np_uerror () and return 0.
 */
int
diod_walk (Npfid *fid, Npstr* wname, Npqid *wqid)
{
    Fid *f = fid->aux;
    int n; 
    struct stat st;
    char *path = NULL;
    int ret = 0;

    if (_fidstat (f) < 0)
        goto done;
    n = strlen (f->path);
    if (!(path = _malloc (n + wname->len + 2))) {
        msg ("diod_walk: out of memory");
        goto done;
    }
    memcpy (path, f->path, n);
    path[n] = '/';
    memcpy (path + n + 1, wname->str, wname->len);
    path[n + wname->len + 1] = '\0';
    if (lstat (path, &st) < 0) {
        np_uerror (errno);
        goto done;
    }
    /* N.B. inodes would not be unique if we could cross over to another
     * file system.  But with the code below, ls -l returns ??? for mount
     * point dirs, which would otherwise have a "foreign" inode number.
     * How does NFS make them appear as empty directories?  That would be
     * prettier.
     */
    if (st.st_dev != f->stat.st_dev) { 
        np_uerror (EXDEV);
        goto done;
    }
    free (f->path);
    f->path = path;
    _ustat2qid (&st, wqid);
    ret = 1;

done:
    if (np_rerror ()) {
        if (path)
            free (path);
    }
    return ret;
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

/* Tread - read from a file or directory.
 */
Npfcall*
diod_read (Npfid *fid, u64 offset, u32 count, Npreq *req)
{
    int n;
    Npfcall *ret = NULL;

    if (!(ret = np_alloc_rread (count))) {
        np_uerror (ENOMEM);
        msg ("diod_read: out of memory");
        goto done;
    }
    n = _pread (fid, ret->u.rread.data, count, offset);
    if (np_rerror ()) {
        free (ret);
        ret = NULL;
    } else
        np_set_rread_count (ret, n);
done:
    return ret;
}

/* Twrite - write to a file.
 */
Npfcall*
diod_write (Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
    int n;
    Npfcall *ret = NULL;
    Fid *f = fid->aux;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if ((n = _pwrite (fid, data, count, offset)) < 0)
        goto done;
    if (!(ret = np_create_rwrite (n))) {
        np_uerror (ENOMEM);
        msg ("diod_write: out of memory");
        goto done;
    }
done:
    return ret;
}

/* Tclunk - close a file.
 */
Npfcall*
diod_clunk (Npfid *fid)
{
    Npfcall *ret;

    if (!(ret = np_create_rclunk ())) {
        np_uerror (ENOMEM);
        msg ("diod_clunk: out of memory");
    }

    return ret;
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
        goto done;
    }
    if (remove (f->path) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (!(ret = np_create_rremove ())) {
        np_uerror (ENOMEM);
        msg ("diod_remove: out of memory");
        goto done;
    }
done:
    return ret;
}

/* Tflush - abort in flight operations (npfs handles most of this).
 */
void
diod_flush(Npreq *req)
{
    return;
}

/* Tstatfs - read file system  information (9P2000.L)
 * N.B. must call statfs() and statvfs() as
 * only statvfs provides (unsigned long) f_fsid
 */
Npfcall*
diod_statfs (Npfid *fid)
{
    Fid *f = fid->aux;
    struct statfs sb;
    struct statvfs svb;
    Npfcall *ret = NULL;

    if (_fidstat(f) < 0)
        goto done;
    if (statfs (f->path, &sb) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (statvfs (f->path, &svb) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (!(ret = np_create_rstatfs(sb.f_type, sb.f_bsize, sb.f_blocks,
                                  sb.f_bfree, sb.f_bavail, sb.f_files,
                                  sb.f_ffree, (u64) svb.f_fsid,
                                  sb.f_namelen))) {
        np_uerror (ENOMEM);
        msg ("diod_statfs: out of memory");
        goto done;
    }

done:
    return ret;
}

Npfcall*
diod_lopen (Npfid *fid, u32 flags)
{
    Fid *f = fid->aux;
    Npfcall *res = NULL;
    Npqid qid;

    if ((f->xflags & XFLAGS_RO) && ((flags & O_WRONLY) || (flags & O_RDWR))) {
        np_uerror (EROFS);
        goto done;
    }
    /* Clients must use lcreate otherwise we don't have enough
     * information to construct the file mode (need client umask and gid).
     * Clear the flag and allow open to fail with ENOENT.
     */
    if ((flags & O_CREAT))
        flags &= ~O_CREAT;

    if (_fidstat (f) < 0)
        goto done;

    if (S_ISDIR (f->stat.st_mode)) {
        f->dir = opendir (f->path);
        if (!f->dir) {
            np_uerror (errno);
            goto done;
        }
    } else {
        f->fd = open (f->path, flags);
        if (f->fd < 0) {
            np_uerror (errno);
            goto done;
        }
    }
    if (_fidstat (f) < 0) {
        err ("diod_lopen: could not stat file that we just opened");
        goto done;
    }
    _ustat2qid (&f->stat, &qid);
    if (!(res = np_create_rlopen (&qid, f->stat.st_blksize))) {
        np_uerror (ENOMEM);
        msg ("diod_lopen: out of memory");
        goto done;
    }

done:
    if (np_rerror ()) {
        if (f->dir) {
            closedir (f->dir);
            f->dir = NULL;
        }
        if (f->fd != -1) {
            close (f->fd);
            f->fd = -1;
        }
    }
    return res;
}

Npfcall*
diod_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode, u32 gid)
{
    Npfcall *ret = NULL;
    Fid *f = fid->aux;
    char *npath = NULL;
    Npqid qid;
    mode_t saved_umask;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!(npath = _mkpath(f->path, name))) {
        msg ("diod_lcreate: out of memory");
        goto done;
    }
    /* assert (f->fd == -1); */
    saved_umask = umask(0);
    if ((f->fd = creat (npath, mode)) < 0) {
        np_uerror (errno);
        goto done;
    }
    umask(saved_umask);
    free (f->path);
    f->path = npath;
    npath = NULL;
    if (_fidstat (f) < 0) {
        np_uerror (errno);
        goto done;
    }
    _ustat2qid (&f->stat, &qid);
    if (!((ret = np_create_rlcreate (&qid, f->stat.st_blksize)))) {
        np_uerror (ENOMEM);
        msg ("diod_lcreate: out of memory");
        goto done;
    }
done:
    if (npath)
        free (npath);
    return ret;
}

Npfcall*
diod_symlink(Npfid *dfid, Npstr *name, Npstr *symtgt, u32 gid)
{
    Npfcall *ret = NULL;
    Fid *df = dfid->aux;
    char *target = NULL, *npath = NULL;
    Npqid qid;
    struct stat sb;
    mode_t saved_umask;

    if ((df->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!(npath = _mkpath(df->path, name))) {
        msg ("diod_symlink: out of memory");
        goto done;
    }
    if (!(target = _p9strdup (symtgt))) {
        msg ("diod_symlink: out of memory");
        goto done;
    }
    saved_umask = umask(0);
    if (symlink (target, npath) < 0) {
        np_uerror (errno);
        goto done;
    }
    umask(saved_umask);
    if (lstat (npath, &sb) < 0) {
        np_uerror (errno);
        rmdir (npath);
        goto done;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rsymlink (&qid)))) {
        np_uerror (ENOMEM);
        msg ("diod_symlink: out of memory");
        goto done;
    }
done:
    if (npath)
        free (npath);
    if (target)
        free (target);
    return ret;
}

Npfcall*
diod_mknod(Npfid *dfid, Npstr *name, u32 mode, u32 major, u32 minor, u32 gid)
{
    Npfcall *ret = NULL;
    Fid *df = dfid->aux;
    char *npath = NULL;
    Npqid qid;
    struct stat sb;
    mode_t saved_umask;

    if ((df->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!(npath = _mkpath(df->path, name))) {
        msg ("diod_mknod: out of memory");
        goto done;
    }
    saved_umask = umask(0);
    if (mknod (npath, mode, makedev (major, minor)) < 0) {
        np_uerror (errno);
        goto done;
    }
    umask(saved_umask);
    if (lstat (npath, &sb) < 0) {
        np_uerror (errno);
        rmdir (npath);
        goto done;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rsymlink (&qid)))) {
        np_uerror (ENOMEM);
        msg ("diod_mknod: out of memory");
        goto done;
    }
done:
    if (npath)
        free (npath);
    return ret;
}

/* Trename - rename a file, potentially to another directory (9P2000.L)
 */
Npfcall*
diod_rename (Npfid *fid, Npfid *dfid, Npstr *name)
{
    Fid *f = fid->aux;
    Fid *d = dfid->aux;
    Npfcall *ret = NULL;
    char *newpath = NULL;
    int newpathlen;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!(ret = np_create_rrename ())) {
        np_uerror (ENOMEM);
        msg ("diod_rename: out of memory");
        goto done;
    }
    newpathlen = name->len + strlen (d->path) + 2;
    if (!(newpath = _malloc (newpathlen))) {
        msg ("diod_rename: out of memory");
        goto done;
    }
    snprintf (newpath, newpathlen, "%s/%s", d->path, name->str);
    if (rename (f->path, newpath) < 0) {
        np_uerror (errno);
        goto done;
    }
    free (f->path);
    f->path = newpath;
done:
    if (np_rerror ()) {
        if (newpath)
            free (newpath);
        if (ret) {
            free (ret);
            ret = NULL;
        }
    }
    return ret;
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
        goto done;
    }
    target[n] = '\0';
    if (!(ret = np_create_rreadlink(target))) {
        np_uerror (ENOMEM);
        msg ("diod_readlink: out of memory");
        goto done;
    }
done:
    return ret;
}

Npfcall*
diod_getattr(Npfid *fid, u64 request_mask)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    Npqid qid;
    //u64 result_mask = P9_GETATTR_BASIC;
    u64 valid = request_mask;

    if (_fidstat (f) < 0)
        goto done;
    _ustat2qid (&f->stat, &qid);
    if (!(ret = np_create_rgetattr(valid, &qid,
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
        msg ("diod_getattr: out of memory");
        goto done;
    }
done:
    return ret;
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
        goto done;
    }

    if ((valid & P9_SETATTR_MODE) || (valid & P9_SETATTR_SIZE)) {
        if (_fidstat(f) < 0)
            goto done;
        fidstat_updated = 1;
        if (S_ISLNK(f->stat.st_mode)) {
            msg ("diod_setattr: unhandled mode/size update on symlink");
            np_uerror(EINVAL);
            goto done;
        }
    }

    /* chmod (N.B. dereferences symlinks) */
    if ((valid & P9_SETATTR_MODE)) {
        if (chmod (f->path, mode) < 0) {
            np_uerror(errno);
            goto done;
        }
        ctime_updated = 1;
    }

    /* chown */
    if ((valid & P9_SETATTR_UID) || (valid & P9_SETATTR_GID)) {
        if (lchown (f->path, (valid & P9_SETATTR_UID) ? uid : -1,
                             (valid & P9_SETATTR_GID) ? gid : -1) < 0) {
            np_uerror(errno);
            goto done;
        }
        ctime_updated = 1;
    }

    /* truncate (N.B. dereferences symlinks */
    if ((valid & P9_SETATTR_SIZE)) {
        if (truncate (f->path, size) < 0) {
            np_uerror(errno);
            goto done;
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
            goto done;
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
                goto done;
            fidstat_updated = 1;
            if (gettimeofday (&now, NULL) < 0) {
                np_uerror (errno);
                goto done;
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
            goto done;
        }
        ctime_updated = 1;
#endif
    }
    if ((valid & P9_SETATTR_CTIME) && !ctime_updated) {
        if (lchown (f->path, -1, -1) < 0) {
            np_uerror (errno);
            goto done;
        }
    }
    if (!(ret = np_create_rsetattr())) {
        np_uerror (ENOMEM);
        msg ("diod_setattr: out of memory");
        goto done;
    }
done:
    return ret;
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
        if (!(f->dirent = readdir (f->dir)))
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
        msg ("diod_readdir: out of memory");
        goto done;
    }
    n = _read_dir_linux (f, ret->u.rreaddir.data, offset, count);
    if (np_rerror ()) {
        free (ret);
        ret = NULL;
    } else
        np_finalize_rreaddir (ret, n);
done:
    return ret;
}

Npfcall*
diod_fsync (Npfid *fid)
{
    Npfcall *ret = NULL;
    Fid *f = fid->aux;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (f->fd == -1) { /* FIXME: should this be silently ignored? */
        np_uerror (EBADF);
        goto done;
    }
    if (fsync(f->fd) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (!((ret = np_create_rfsync ()))) {
        np_uerror (ENOMEM);
        msg ("diod_fsync: out of memory");
        goto done;
    }
done:
    return ret;
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
    Npfcall *ret = NULL;
    Fid *f = fid->aux;
    char *cid = _p9strdup (client_id);
    u8 status = P9_LOCK_ERROR;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!cid) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (flags & ~P9_LOCK_FLAGS_BLOCK) { /* only one valid flag for now */
        np_uerror (EINVAL);             /*  (which we ignore) */
        goto done;
    }
    if (_fidstat(f) < 0)
        goto done;
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
            goto done;
    }
    if (!((ret = np_create_rlock (status)))) {
        np_uerror (ENOMEM);
        msg ("diod_lock: out of memory");
        goto done;
    }
done:
    if (cid)
        free (cid);
    return ret;
}

Npfcall*
diod_getlock (Npfid *fid, u8 type, u64 start, u64 length, u32 proc_id,
             Npstr *client_id)
{
    Npfcall *ret = NULL;
    char *cid = _p9strdup (client_id);
    Fid *f = fid->aux;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!cid) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (_fidstat(f) < 0)
        goto done;
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
            goto done;
    }
    if (type != LOCK_UN) {
        /* FIXME: need to fake up start, length, proc_id, cid? */
    }
    if (!((ret = np_create_rgetlock(type, start, length, proc_id, cid)))) {
        np_uerror (ENOMEM);
        msg ("diod_getlock: out of memory");
        goto done;
    }
done:
    if (cid)
        free (cid);
    return ret;
}

Npfcall*
diod_link (Npfid *dfid, Npfid *fid, Npstr *name)
{
    Npfcall *ret = NULL;
    Fid *df = dfid->aux;
    Fid *f = fid->aux;
    char *npath = NULL;

    if ((f->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!(npath = _mkpath(df->path, name))) {
        msg ("diod_link: out of memory");
        goto done;
    }
    if (link (f->path, npath) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (!((ret = np_create_rlink ()))) {
        np_uerror (ENOMEM);
        msg ("diod_link: out of memory");
        goto done;
    }
done:
    if (npath)
        free (npath);
    return ret;
}

Npfcall*
diod_mkdir (Npfid *dfid, Npstr *name, u32 mode, u32 gid)
{
    Npfcall *ret = NULL;
    Fid *df = dfid->aux;
    char *npath = NULL;
    Npqid qid;
    struct stat sb;
    mode_t saved_umask;

    if ((df->xflags & XFLAGS_RO)) {
        np_uerror (EROFS);
        goto done;
    }
    if (!(npath = _mkpath(df->path, name))) {
        msg ("diod_mkdir: out of memory");
        goto done;
    }
    saved_umask = umask(0);
    if (mkdir (npath, mode) < 0) {
        np_uerror (errno);
        goto done;
    }
    umask(saved_umask);
    if (lstat (npath, &sb) < 0) {
        np_uerror (errno);
        rmdir (npath);
        goto done;
    }
    _ustat2qid (&sb, &qid);
    if (!((ret = np_create_rmkdir (&qid)))) {
        np_uerror (ENOMEM);
        msg ("diod_mkdir: out of memory");
        goto done;
    }
done:
    if (npath)
        free (npath);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
