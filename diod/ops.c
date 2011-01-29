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
 *     An Rerror message is sent, constructed from the thread-specific error
 *     state which is set with np_werror ().  Any (Npfcall *)returned is freed.
 *  
 * Normally the wrapper passes through the registered srv->operation's return
 * value, except in special cases noted below (diod_walk).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _XOPEN_SOURCE 600   /* pread/pwrite */
#define _BSD_SOURCE         /* makedev, st_atim etc */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
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
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <assert.h>
#include <stdarg.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_conf.h"
#include "diod_log.h"
#include "diod_trans.h"
#include "diod_upool.h"

#include "ops.h"

typedef struct {
    char            *path;
    int              fd;
    DIR             *dir;
    struct dirent   *dirent;
    int              diroffset;
    struct stat      stat;
    /* atomic I/O */
    void            *rc_data;
    u64              rc_offset;
    u32              rc_length;
    u32              rc_pos;
    void            *wc_data;
    u64              wc_offset;
    u32              wc_length;
    u32              wc_pos;
    /* stats */
    u64              write_ops;
    u64              write_bytes;
    u64              read_ops;
    u64              read_bytes;
    struct timeval   birth;
} Fid;

Npfcall     *diod_attach (Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname);
int          diod_clone  (Npfid *fid, Npfid *newfid);
int          diod_walk   (Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall     *diod_open   (Npfid *fid, u8 mode);
Npfcall     *diod_create (Npfid *fid, Npstr *name, u32 perm, u8 mode, 
                          Npstr *extension);
Npfcall     *diod_read   (Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall     *diod_write  (Npfid *fid, u64 offset, u32 count, u8 *data,
                          Npreq *req);
Npfcall     *diod_clunk  (Npfid *fid);
Npfcall     *diod_remove (Npfid *fid);
Npfcall     *diod_stat   (Npfid *fid);
Npfcall     *diod_wstat  (Npfid *fid, Npstat *stat);
void         diod_flush  (Npreq *req);
void         diod_fiddestroy(Npfid *fid);
void         diod_connclose(Npconn *conn);
void         diod_connopen (Npconn *conn);

#if HAVE_LARGEIO
Npfcall     *diod_aread  (Npfid *fid, u8 datacheck, u64 offset, u32 count,
                          u32 rsize, Npreq *req);
Npfcall     *diod_awrite (Npfid *fid, u64 offset, u32 count, u32 rsize,
                          u8 *data, Npreq *req);
#endif
#if HAVE_DOTL
Npfcall     *diod_statfs (Npfid *fid);
Npfcall     *diod_lopen  (Npfid *fid, u32 mode);
Npfcall     *diod_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode,
                          u32 gid);
Npfcall     *diod_symlink(Npfid *fid, Npstr *name, Npstr *symtgt, u32 gid);
Npfcall     *diod_mknod(Npfid *fid, Npstr *name, u32 mode, u32 major,
                        u32 minor, u32 gid);
Npfcall     *diod_rename (Npfid *fid, Npfid *newdirfid, Npstr *newname);
Npfcall     *diod_readlink(Npfid *fid);
Npfcall     *diod_getattr(Npfid *fid, u64 request_mask);
Npfcall     *diod_setattr (Npfid *fid, u32 valid_mask,
                           struct p9_iattr_dotl *attr);
Npfcall     *diod_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall     *diod_fsync (Npfid *fid);
Npfcall     *diod_lock (Npfid *fid, struct p9_flock *flock);
Npfcall     *diod_getlock (Npfid *fid, struct p9_getlock *getlock);
Npfcall     *diod_link (Npfid *dfid, Npfid *oldfid, Npstr *newpath);
Npfcall     *diod_mkdir (Npfid *fid, Npstr *name, u32 mode, u32 gid);
#endif
static int       _fidstat       (Fid *fid);
static void      _ustat2qid     (struct stat *st, Npqid *qid);
static u32       _umode2npmode  (mode_t umode);
static int       _ustat2npwstat (char *path, struct stat *st, Npwstat *wstat);
static int       _omode2uflags  (u8 mode);
static mode_t    _np2umode       (u32 mode, Npstr *extension);
static void      _fidfree       (Fid *f);
static u32       _read_dir      (Npfid *fid, u8* buf, u64 offset, u32 count);

static int  _create_special     (Npfid *fid, char *path, u32 perm,
                                 Npstr *extension);


static pthread_mutex_t  conn_lock = PTHREAD_MUTEX_INITIALIZER;
static int              conn_count = 0;
static int              server_used = 0;

char *Enoextension = "empty extension while creating special file";
char *Eformat = "incorrect extension format";

void
diod_register_ops (Npsrv *srv)
{
    srv->msize = 65536;
    srv->upool = diod_upool;
    srv->attach = diod_attach;
    srv->clone = diod_clone;
    srv->walk = diod_walk;
    srv->open = diod_open;
    srv->create = diod_create;
    srv->read = diod_read;
    srv->write = diod_write;
    srv->clunk = diod_clunk;
    srv->remove = diod_remove;
    srv->stat = diod_stat;
    srv->wstat = diod_wstat;
    srv->flush = diod_flush;
    srv->fiddestroy = diod_fiddestroy;
    srv->debuglevel = diod_conf_get_debuglevel ();
    srv->debugprintf = msg;
    srv->connclose = diod_connclose;
    srv->connopen = diod_connopen;
#if HAVE_LARGEIO
    srv->aread = diod_aread;
    srv->awrite = diod_awrite;
#endif
#if HAVE_DOTL
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
    srv->proto_version = p9_proto_2000L;
#else
    srv->proto_version = p9_proto_2000u;
#endif
}

/* Update stat info contained in fid.
 * The 9P spec says directories have no length so we fix that up here.
 * Set npfs error state on error.
 */
static int
_fidstat (Fid *fid)
{
    if (lstat (fid->path, &fid->stat) < 0) {
        np_uerror (errno);
        return -1;
    }
    if (S_ISDIR (fid->stat.st_mode))
        fid->stat.st_size = 0;

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
    } else {
        f->read_ops++;
        f->read_bytes += n;
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
    } else {
        f->write_ops++;
        f->write_bytes += n;
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
        f->diroffset = 0;
        f->dirent = NULL;
        f->rc_data = NULL;
        f->wc_data = NULL;
        f->read_ops = 0;
        f->read_bytes = 0;
        f->write_ops = 0;
        f->write_bytes = 0;
        if (gettimeofday (&f->birth, NULL) < 0) {
            np_uerror (errno);
            free (f);
            f = NULL;
        }
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
        if (f->rc_data)
            free(f->rc_data);
        if (f->wc_data)
            free(f->wc_data);
        if (f->path)
            free(f->path);
        free(f);
    }
}

static void
_dumpstats (Fid *f)
{
    FILE *lf = diod_conf_get_statslog ();
    struct timeval death;

    if (lf && f->read_bytes + f->write_bytes > 0) {
        if (gettimeofday (&death, NULL) == 0) {
            fprintf (lf, "%s:%llu:%llu:%llu:%llu:%lf\n", f->path,
                     (unsigned long long)f->read_ops,
                     (unsigned long long)f->read_bytes,
                     (unsigned long long)f->write_ops,
                     (unsigned long long)f->write_bytes,
                     (double)death.tv_sec + 10E-6*(double)death.tv_usec -
                    ((double)f->birth.tv_sec + 10E-6*(double)f->birth.tv_usec));
            fflush (lf);
        }
    }
}

/* This is a courtesy callback from npfs to let us know that
 * the fid we are parasitically attached to is being destroyed.
 */
void
diod_fiddestroy (Npfid *fid)
{
    Fid *f = fid->aux;

    _dumpstats (f);
    _fidfree (f);
    fid->aux = NULL;
}

/* Convert 9P open mode bits to UNIX open mode bits.
 */
static int
_omode2uflags (u8 mode)
{
    int ret = 0;

    switch (mode & 3) {
        case P9_OREAD:
            ret = O_RDONLY;
            break;
        case P9_ORDWR:
            ret = O_RDWR;
            break;
        case P9_OWRITE:
            ret = O_WRONLY;
            break;
        case P9_OEXEC:
            ret = O_RDONLY;
            break;
    }
    if (mode & P9_OTRUNC)
        ret |= O_TRUNC;
    if (mode & P9_OAPPEND)
        ret |= O_APPEND;
    if (mode & P9_OEXCL)
        ret |= O_EXCL;

    return ret;
}

/* Create a 9P qid from a file's stat info.
 * N.B. v9fs maps st_ino = qid->path + 2, presumably since inode 0 and 1
 * are special for Linux but not for Plan 9.  For I/O forwarding we want
 * inodes to be direct mapped, so set qid->path = st_ino - 2.
 */
static void
_ustat2qid (struct stat *st, Npqid *qid)
{
    assert (st->st_ino != 0);
    assert (st->st_ino != 1);
    qid->path = st->st_ino - 2;
    /* FIXME: ramifcations of always-zero version? */
    //qid->version = st->st_mtime ^ (st->st_size << 8);
    qid->version = 0;
    qid->type = 0;
    if (S_ISDIR(st->st_mode))
        qid->type |= P9_QTDIR;
    if (S_ISLNK(st->st_mode))
        qid->type |= P9_QTSYMLINK;
}

#if HAVE_DOTL
static void
_dirent2qid (struct dirent *d, Npqid *qid)
{
    assert (d->d_ino != 0);
    assert (d->d_ino != 1);
    assert (d->d_type != DT_UNKNOWN);
    qid->path = d->d_ino - 2;
    /* FIXME: ramifcations of always-zero version? */
    qid->version = 0;
    qid->type = 0;
    if (d->d_type == DT_DIR)
        qid->type |= P9_QTDIR;
    if (d->d_type == DT_LNK)
        qid->type |= P9_QTSYMLINK;
}
#endif

/* Convert UNIX file mode bits to 9P file mode bits.
 */
static u32
_umode2npmode (mode_t umode)
{
    u32 ret = umode & 0777;

    if (S_ISDIR (umode))
        ret |= P9_DMDIR;

    /* dotu */
    if (S_ISLNK (umode))
        ret |= P9_DMSYMLINK;
    if (S_ISSOCK (umode))
        ret |= P9_DMSOCKET;
    if (S_ISFIFO (umode))
        ret |= P9_DMNAMEDPIPE;
    if (S_ISBLK (umode))
        ret |= P9_DMDEVICE;
    if (S_ISCHR (umode))
        ret |= P9_DMDEVICE;
    if (umode & S_ISUID)
        ret |= P9_DMSETUID;
    if (umode & S_ISGID)
        ret |= P9_DMSETGID;

    return ret;
}

/* Convert 9P file mode bits to UNIX mode bits.
 */
static mode_t
_np2umode (u32 mode, Npstr *extension)
{
    mode_t ret = mode & 0777;

    if (mode & P9_DMDIR)
        ret |= S_IFDIR;

    /* dotu */
    if (mode & P9_DMSYMLINK)
        ret |= S_IFLNK;
    if (mode & P9_DMSOCKET)
        ret |= S_IFSOCK;
    if (mode & P9_DMNAMEDPIPE)
        ret |= S_IFIFO;
    if (mode & P9_DMDEVICE) {
        if (extension && extension->str[0] == 'c')
            ret |= S_IFCHR;
        else
            ret |= S_IFBLK;
    }

    if (!(ret&~0777))
        ret |= S_IFREG;
    if (mode & P9_DMSETUID)
        ret |= S_ISUID;
    if (mode & P9_DMSETGID)
        ret |= S_ISGID;

    return ret;
}

/* Convert UNIX file stat info to Npwstat (approx. 9P stat structure).
 */
static int
_ustat2npwstat(char *path, struct stat *st, Npwstat *wstat)
{
    int err;
    char ext[256];
    char *s;
    int ret = -1;

    memset (wstat, 0, sizeof(*wstat));
    _ustat2qid (st, &wstat->qid);
    wstat->mode = _umode2npmode (st->st_mode);
    wstat->atime = st->st_atime;
    wstat->mtime = st->st_mtime;
    wstat->length = st->st_size;

    wstat->muid = "";
    wstat->extension = NULL;

    /* dotu */
    wstat->uid = "???";
    wstat->gid = "???";
    wstat->n_uid = st->st_uid;
    wstat->n_gid = st->st_gid;

    if (wstat->mode & P9_DMSYMLINK) {
        err = readlink (path, ext, sizeof(ext) - 1);
        if (err < 0)
            err = 0;
        ext[err] = '\0';
    } else if (wstat->mode & P9_DMDEVICE) {
        snprintf (ext, sizeof(ext), "%c %u %u", 
            S_ISCHR (st->st_mode)?'c':'b',
            major (st->st_rdev), minor (st->st_rdev));
    } else {
        ext[0] = '\0';
    }
    if (!(wstat->extension = _strdup (ext)))
        goto done;

    s = strrchr (path, '/');
    if (s)
        wstat->name = s + 1;
    else
        wstat->name = path;
    ret = 0;
done:
    return ret;
}

/* Exit on last connection close iff there has been some 9P spoken.
 * This is so we can tell the difference between an actual mount
 * and a simple connection attempt by diodctl to test our listen port.
 */
void
diod_connclose (Npconn *conn)
{
    int errnum;

    if (!diod_conf_get_exit_on_lastuse ())
        return;
    if ((errnum = pthread_mutex_lock (&conn_lock)))
        errn_exit (errnum, "diod_connclose: could not take conn_lock mutex");
    conn_count--;
    if (conn_count == 0 && server_used)
        exit (0);
    if ((errnum = pthread_mutex_unlock (&conn_lock)))
        errn_exit (errnum, "diod_connclose: could not drop conn_lock mutex");
}

void
diod_connopen (Npconn *conn)
{
    int errnum;

    if (!diod_conf_get_exit_on_lastuse ())
        return;
    if ((errnum = pthread_mutex_lock (&conn_lock)))
        errn_exit (errnum, "diod_connopen: could not take conn_lock mutex");
    conn_count++; 
    if ((errnum = pthread_mutex_unlock (&conn_lock)))
        errn_exit (errnum, "diod_connopen: could not drop conn_lock mutex");
}

static void
_serverused (void)
{
    int errnum;

    if (!diod_conf_get_exit_on_lastuse ())
        return;
    if ((errnum = pthread_mutex_lock (&conn_lock)))
        errn_exit (errnum, "_serverused: could not take conn_lock mutex");
    server_used = 1; 
    if ((errnum = pthread_mutex_unlock (&conn_lock)))
        errn_exit (errnum, "_serverused: could not drop conn_lock mutex");
}

/* Tattach - announce a new user, and associate her fid with the root dir.
 */
Npfcall*
diod_attach (Npfid *fid, Npfid *nafid, Npstr *uname, Npstr *aname)
{
    char *host = diod_trans_get_host (fid->conn->trans);
    char *ip = diod_trans_get_ip (fid->conn->trans);
    Npfcall* ret = NULL;
    Fid *f = NULL;
    int err;
    Npqid qid;
    uid_t runasuid;

    if (nafid) {    /* 9P Tauth not supported */
        np_werror (Enoauth, EIO);
        msg ("diod_attach: 9P Tauth is not supported");
        goto done;
    }
    if (aname->len == 0 || *aname->str != '/') {
        np_uerror (EPERM);
        msg ("diod_attach: mount attempt for malformed aname");
        goto done;
    }
    /* If running as a particular user, reject attaches from other users.
     */
    if (diod_conf_get_runasuid (&runasuid) && fid->user->uid != runasuid) {
        np_uerror (EPERM);
        msg ("diod_attach: attach rejected from unauthorized user");
        goto done;
    }
#if HAVE_MUNGE
    /* Munge authentication involves the upool and trans layers:
     * - we ask the upool layer if the user now attaching has a munge cred
     * - we stash the uid of the last successful munge auth in the trans layer
     * - subsequent attaches on the same trans get to leverage the last auth
     * By the time we get here, invalid munge creds have already been rejected.
     */
    if (diod_conf_get_munge ()) {
        int authenticated;

        (void) diod_user_get_authinfo (fid->user, &authenticated, NULL);
        if (authenticated) {
            diod_trans_set_authuser (fid->conn->trans, fid->user->uid, NULL);
        } else {
            uid_t auid;

            if (diod_trans_get_authuser (fid->conn->trans, &auid) < 0) {
                np_uerror (EPERM);
                msg ("diod_attach: attach rejected from unauthenticated user");
                goto done;
            }
            if (auid != 0 && auid != fid->user->uid) {
                np_uerror (EPERM);
                msg ("diod_attach: attach rejected from unauthenticated user");
                goto done;
            }
        }
    }
#endif
    if (!(f = _fidalloc ())) {
        msg ("diod_attach: out of memory");
        goto done;
    }
    if (!(f->path = _p9strdup(aname))) {
        msg ("diod_attach: out of memory");
        goto done;
    }
    if (!diod_conf_match_export (f->path, host, ip, fid->user->uid, &err)) {
        np_uerror (err);
        msg ("diod_attach: %s@%s is not permitted to attach to %s",
              fid->user->uname, host, f->path);
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
    _serverused ();

done:
    if (np_haserror ()) {
        msg ("attach user %s path %.*s host %s(%s): DENIED",
             fid->user->uname, aname->len, aname->str, host, ip);
        if (f)
            _fidfree (f);
    }
    return ret;
}

/* Twalk - walk a file path
 * Called from fcall.c::np_walk () to clone the fid.
 * On error, call np_werror () and return 0.
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
    newfid->aux = nf;
    ret = 1;

done:   
    if (np_haserror ()) {
        if (nf)
            _fidfree (nf);
    }
    return ret;
}

/* Twalk - walk a file path
 * Called from fcall.c::np_walk () on each wname component in succession.
 * On error, call np_werror () and return 0.
 */
int
diod_walk (Npfid *fid, Npstr* wname, Npqid *wqid)
{
    Fid *f = fid->aux;
    int n; 
    struct stat st;
    char *path = NULL;
    int ret = 0;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_walk: error switching user");
        goto done;
    }
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
    if (np_haserror ()) {
        if (path)
            free (path);
    }
    return ret;
}

/* Topen - open a file by fid.
 */
Npfcall*
diod_open (Npfid *fid, u8 mode)
{
    Fid *f = fid->aux;
    Npfcall *res = NULL;
    Npqid qid;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_open: error switching user");
        goto done;
    }
    if (_fidstat (f) < 0)
        goto done;

    if (S_ISDIR (f->stat.st_mode)) {
        f->dir = opendir (f->path);
        if (!f->dir) {
            np_uerror (errno);
            goto done;
        }
    } else {
        f->fd = open (f->path, _omode2uflags(mode));
        if (f->fd < 0) {
            np_uerror (errno);
            goto done;
        }
    }
    /* XXX is this really an error? */
    if (_fidstat (f) < 0) {
        err ("diod_open: could not stat file that we just opened");
        goto done;
    }
    _ustat2qid (&f->stat, &qid);
    if (!(res = np_create_ropen (&qid, 0))) {
        np_uerror (ENOMEM);
        msg ("diod_open: out of memory");
        goto done;
    }

done:
    if (np_haserror ()) {
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

/* Create a hard link.
 */
static int
_link (Npfid *fid, char *path, char *ext)
{
    Npfid *ofid;
    Fid *of;
    int nfid;
    int ret = -1;
 
    if (sscanf (ext, "%d", &nfid) == 0) {
        np_werror (Eformat, EIO);
        msg ("diod_create: incorrect hard link extension format");
        goto done;
    }
    ofid = np_fid_find (fid->conn, nfid);
    if (!ofid) {
        np_werror (Eunknownfid, EIO);
        goto done;
    }
    of = ofid->aux;
    if (link (of->path, path) < 0) {
        np_uerror (errno);
        goto done;
    }
    ret = 0;
done:
    return ret;
}

/* Create a device.
 */
static int
_mknod (char *path, char *ext, u32 perm)
{
    int nmode, major, minor;
    char ctype;
    int ret = -1;

    if (sscanf (ext, "%c %u %u", &ctype, &major, &minor) != 3) {
        np_werror (Eformat, EIO);
        msg ("diod_create: incorrect mknod extension format");
        goto done;
    }
    nmode = 0;
    switch (ctype) {
        case 'c':
            nmode = S_IFCHR;
            break;
        case 'b':
            nmode = S_IFBLK;
            break;
        default:
            np_werror (Eformat, EIO);
            msg ("diod_create: incorrect mknod extension ctype");
            goto done;
    }
    nmode |= (perm & 0777);
    if (mknod (path, nmode, makedev(major, minor)) < 0) {
        np_uerror(errno);
        goto done;
    }
    ret = 0;
done:
    return ret;    
}

/* Create a named pipe, symlink, hardlink, or device - helper for diod_create()
 */
static int
_create_special (Npfid *fid, char *path, u32 perm, Npstr *extension)
{
    mode_t umode;
    char *ext = NULL;
    int ret = -1;

    if (!(perm & P9_DMNAMEDPIPE) && !extension->len) {
        np_werror (Enoextension, EIO);
        msg ("diod_create: empty extension for named pipe");
        goto done;
    }

    umode = _np2umode (perm, extension);
    if (!(ext = _p9strdup (extension)))
        goto done;
    if (perm & P9_DMSYMLINK) {
        if (symlink (ext, path) < 0) {
            np_uerror (errno);
            goto done;
        }
    } else if (perm & P9_DMLINK) {
        if (_link (fid, path, ext) < 0)
            goto done;
    } else if (perm & P9_DMDEVICE) {
        if (_mknod (path, ext, perm) < 0)
            goto done;
    } else if (perm & P9_DMNAMEDPIPE) {
        if (mknod (path, S_IFIFO | (umode & 0777), 0) < 0) {
            np_uerror (errno);
            goto done;
        }
    }
    if (!(perm & P9_DMSYMLINK)) {
        if (chmod (path, umode) < 0) {
            np_uerror (errno);
            goto done;
        }
    }
    ret = 0;

done:
    if (ext)
        free(ext);
    return ret;
}

/* Tcreate - create + open (atomically) a new file.
 */
Npfcall*
diod_create (Npfid *fid, Npstr *name, u32 perm, u8 mode, Npstr *extension)
{
    int n;
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    char *npath = NULL;
    Npqid qid;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_create: error switching user");
        goto done;
    }
    if (_fidstat (f) < 0)
        goto done;
    n = strlen (f->path);
    if (!(npath = _malloc (n + name->len + 2))) {
        msg ("diod_create: out of memory");
        goto done;
    }
    memmove (npath, f->path, n);
    npath[n] = '/';
    memmove (npath + n + 1, name->str, name->len);
    npath[n + name->len + 1] = '\0';

    if (perm & P9_DMDIR) {
        if (mkdir (npath, perm & 0777) < 0) {
            np_uerror (errno);
            goto done;
        }
        if (lstat (npath, &f->stat) < 0) {
            np_uerror (errno);
            rmdir (npath);
            goto done;
        }
        f->dir = opendir (npath);
        if (!f->dir) {
            np_uerror (errno);
            remove (npath);
            goto done;
        }
    } else if (perm & (P9_DMNAMEDPIPE|P9_DMSYMLINK|P9_DMLINK|P9_DMDEVICE)) {
        if (_create_special (fid, npath, perm, extension) < 0)
            goto done;

        if (lstat (npath, &f->stat) < 0) {
            np_uerror (errno);
            remove (npath);
            goto done;
        }
    } else {
        f->fd = open (npath, O_CREAT|_omode2uflags (mode), perm & 0777);
        if (f->fd < 0) {
            np_uerror (errno);
            goto done;
        }
        if (lstat (npath, &f->stat) < 0) {
            np_uerror(errno);
            remove(npath);
            goto done;
        }
    }
    free (f->path);
    f->path = npath;
    npath = NULL;
    _ustat2qid (&f->stat, &qid);
    if (!((ret = np_create_rcreate (&qid, 0)))) {
        np_uerror (ENOMEM);
        msg ("diod_create: out of memory");
        goto done;
    }

done:
    if (npath)
        free (npath);
    return ret;
}

/* Form a path from <parent>/<child>, then copy its serialized wstat info
 * into buf.  Returns the number of bytes of buf consumed (0 if there was
 * not enough room).
 */
static u32
_copy_dirent (char *parent, int plen, char *child, u8 *buf, u32 buflen)
{
    char *path; 
    int ret = 0;
    struct stat st;
    Npwstat wstat;

    if (!(path = _malloc (plen + strlen (child) + 2))) {
        msg ("diod_read: out of memory");
        goto done;
    }
    sprintf (path, "%s/%s", parent, child);
    if (lstat (path, &st) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (_ustat2npwstat (path, &st, &wstat) < 0) {
        msg ("diod_read: out of memory");
        goto done;
    }
    ret = np_serialize_stat (&wstat, buf, buflen, 1); /* 1 for dotu */
    free (wstat.extension);

done:
    if (path)
        free (path);
    return ret;
}

/* Read some number of dirents into buf.
 * If buf is too small, leave the last dirent in f->dirent for next time.
 */
static u32
_read_dir (Npfid *fid, u8* buf, u64 offset, u32 count)
{
    Fid *f = fid->aux;
    int plen = strlen (f->path);
    int i, n = 0;

    if (count == 0 || (offset != f->diroffset && offset != 0)) {
        np_uerror (EINVAL);
        goto done;
    }
    if (offset == 0 && f->diroffset != 0) {
        f->diroffset = 0;
        f->dirent = NULL;
        rewinddir (f->dir);
    } 
    do {
        if (!f->dirent)
            f->dirent = readdir (f->dir);
        if (!f->dirent)
                break;
        i = _copy_dirent (f->path, plen, f->dirent->d_name,
                          buf + n, count - n - 1);
        if (i > 0)
            f->dirent = NULL;
        n += i;
    } while (i > 0 && n < count);

    f->diroffset += n;
done:
    return n;
}

/* Tread - read from a file or directory.
 */
Npfcall*
diod_read (Npfid *fid, u64 offset, u32 count, Npreq *req)
{
    int n;
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_read: error switching user");
        goto done;
    }
    if (!(ret = np_alloc_rread (count))) {
        np_uerror (ENOMEM);
        msg ("diod_read: out of memory");
        goto done;
    }
    if (f->dir)
        n = _read_dir (fid, ret->data, offset, count);
    else
        n = _pread (fid, ret->data, count, offset);
    if (np_haserror ()) {
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

    if (!diod_switch_user (fid->user)) {
        msg ("diod_write: error switching user");
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

    if (!diod_switch_user (fid->user)) {
        msg ("diod_remove: error switching user");
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

/* Tstat - stat a file.
 */
Npfcall*
diod_stat (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    Npwstat wstat;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_stat: error switching user");
        goto done;
    }
    wstat.extension = NULL;
    if (_fidstat (f) < 0)
        goto done;
    if (_ustat2npwstat (f->path, &f->stat, &wstat) < 0) {
        msg ("diod_stat: out of memory");
        goto done;
    }
    if (!(ret = np_create_rstat(&wstat, 1))) { /* 1 for dotu */
        np_uerror (ENOMEM);
        msg ("diod_stat: out of memory");
        goto done;
    }
done:
    if (wstat.extension)
        free (wstat.extension);
    return ret;
}

/* Twstat - update a file's metadata.
 */
Npfcall*
diod_wstat(Npfid *fid, Npstat *st)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_wstat: error switching user");
        goto done;
    }
    if (_fidstat (f) < 0)
        goto done;
    if (!(ret = np_create_rwstat())) {
        np_uerror (ENOMEM);
        msg ("diod_wstat: out of memory");
        goto done;
    }

    /* rename */
    if (st->name.len != 0) {
        char *p = strrchr(f->path, '/');
        char *npath;

        if (!p)
            p = f->path + strlen(f->path);
        if (!(npath = _malloc(st->name.len + (p - f->path) + 2))) {
            msg ("diod_wstat: out of memory");
            goto done;
        }
        memcpy(npath, f->path, p - f->path);
        npath[p - f->path] = '/';
        memcpy(npath + (p - f->path) + 1, st->name.str, st->name.len);
        npath[(p - f->path) + 1 + st->name.len] = 0;
        if (strcmp(npath, f->path) != 0) {
            if (rename(f->path, npath) < 0) {
                np_uerror (errno);
                free (npath);
                goto done;
            }
            free (f->path);
            f->path = npath;
        }
    }

    /* chmod */
    if (st->mode != (u32)~0) {
        mode_t umode = _np2umode(st->mode, &st->extension);

        if ((st->mode & P9_DMDIR) && !S_ISDIR(f->stat.st_mode)) {
            np_werror(Edirchange, EIO);
            msg ("diod_wstat: Dmdir chmod on non-directory");
            goto done;
        }
        if (chmod(f->path, umode) < 0) {
            np_uerror(errno);
            goto done;
        }
    }

    /* utime */
    if (st->mtime != (u32)~0 || st->atime != (u32)~0) {
        struct utimbuf tb;
        struct stat sb;

        if (!(st->mtime != (u32)~0 && st->atime != (u32)~0)) {
            if (stat(f->path, &sb) < 0) {
                np_uerror(errno);
                goto done;
            }
            tb.actime = sb.st_atime;
            tb.modtime = sb.st_mtime;
        }
        if (st->mtime != (u32)~0)
            tb.modtime = st->mtime;
        if (st->atime != (u32)~0)
            tb.actime = st->atime;
        if (utime(f->path, &tb) < 0) {
            np_uerror(errno);
            goto done;
        }
    }

    /* chgrp */
    if (st->n_uid != (u32)~0 || st->n_gid != (u32)~0) {
        uid_t uid = st->n_uid != (u32)~0 ? st->n_uid : -1;
        gid_t gid = st->n_gid != (u32)~0 ? st->n_gid : -1;

        if (chown(f->path, uid, gid) < 0) {
            np_uerror(errno);
            goto done;
        }
    }

    /* truncate */
    if (st->length != ~0) {
        if (truncate(f->path, st->length) < 0) {
            np_uerror(errno);
            goto done;
        }
    }

    /* fsync - designated by a "do nothing" wstat, see 9p.stat(5) */
    if (st->mode == (u32)~0 && st->mtime == (u32)~0 && st->atime == (u32)~0
                            && st->n_uid == (u32)~0 && st->n_gid == (u32)~0
                            && st->length == ~0     && st->name.len == 0) {
        if (fsync (f->fd) < 0) {
            np_uerror (errno);
            goto done;
        }
    }
done:
    if (np_haserror ()) {
        if (ret) {
            free (ret);
            ret = NULL;
        }
    }
    return ret;
}

/* Tflush - abort in flight operations (npfs handles most of this).
 */
void
diod_flush(Npreq *req)
{
    /* FIXME: need to clear aread/awrite/readdir state ? */
    return;
}

#if HAVE_LARGEIO
/* helper for diod_aread */
static int
_cache_read (Npfid *fid, void *buf, u32 rsize, u64 offset, int debug)
{
    Fid *f = fid->aux;
    int ret = 0;

    if (f->rc_data) {
        if (f->rc_offset + f->rc_pos == offset) {
            ret = f->rc_length - f->rc_pos;
            if (ret > rsize)
                ret = rsize;
            memcpy (buf, f->rc_data + f->rc_pos, ret);
            if (debug)
                msg ("aread:   read %d bytes from cache", ret);
            f->rc_pos += ret;
        }
        if (ret == 0 || f->rc_pos == f->rc_length) {
            free (f->rc_data);
            f->rc_data = NULL;
            if (debug)
                msg ("aread:   freed %u byte cache", f->rc_length);
        }
    }
    return ret;
}

/* helper for diod_aread */
static int
_cache_read_ahead (Npfid *fid, void *buf, u32 count, u32 rsize, u64 offset,
                   int debug)
{
    Fid *f = fid->aux;
    u32 atomic_max = (u32)diod_conf_get_atomic_max () * 1024 * 1024;

    if (count > atomic_max)
        count = atomic_max;

    if (!f->rc_data || f->rc_length < count) {
        assert (f->rc_data == NULL);
        if (!(f->rc_data = _malloc (count))) {
            msg ("diod_aread: out of memory (allocating %d bytes)", count);
            return -1;
        }
        if (debug)
            msg ("aread:  allocated %u byte cache", count);
    }
    f->rc_length = _pread (fid, f->rc_data, count, offset);
    f->rc_offset = offset;
    f->rc_pos = 0;
    if (f->rc_length < 0) {
        free (f->rc_data);
        f->rc_data = NULL;
        return -1;
    } 
    if (debug)
        msg ("aread:   read %d bytes at offset %llu", f->rc_length, 
             (unsigned long long)offset);
    return _cache_read (fid, buf, rsize, offset, debug);
}

/* Taread - atomic read
 */
Npfcall*
diod_aread (Npfid *fid, u8 datacheck, u64 offset, u32 count, u32 rsize,
            Npreq *req)
{
    int debug = (diod_conf_get_debuglevel () & DEBUG_ATOMIC);
    int n;
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_aread: error switching user");
        goto done;
    }
    if (!(ret = np_create_raread (rsize))) {
        np_uerror (ENOMEM);
        msg ("diod_aread: out of memory");
        goto done;
    }
    if (debug)
        msg ("aread: rsize %u count %u offset %llu", rsize, count,
             (unsigned long long)offset);
    n = _cache_read (fid, ret->data, rsize, offset, debug);
    if (n == 0) {
        if (count > rsize)
            n = _cache_read_ahead (fid, ret->data, count, rsize, offset, debug);
        else {
            n = _pread (fid, ret->data, rsize, offset);
            if (debug)
                msg ("aread: directly read %d bytes at offset %llu", n,
                     (unsigned long long)offset);
        }
    }
    if (np_haserror ()) {
        if (ret) {
            free (ret);
            ret = NULL;
        }
    } else
        np_finalize_raread (ret, n, datacheck);
done:
    return ret;
}

/* helper for diod_awrite */
static int
_cache_write (Npfid *fid, void *buf, u32 rsize, u64 offset, int debug)
{
    Fid *f = fid->aux;
    int ret = 0, i = 0, n;

    if (f->wc_data) {
        if (f->wc_offset + f->wc_pos == offset) {
            ret = f->wc_length - f->wc_pos;
            if (ret > rsize)
                ret = rsize;
            memcpy (f->wc_data + f->wc_pos, buf, ret);
            if (debug)
                msg ("awrite:   cached %d bytes", ret);
            f->wc_pos += ret;
        }
        if (f->wc_pos == f->wc_length) {
            while (i < f->wc_pos) {
                n = _pwrite (fid, f->wc_data + i,
                            f->wc_pos - i, f->wc_offset + i);
                if (n < 0) {
                    free (f->wc_data);
                    f->wc_data = NULL;
                    return -1;
                }
                if (debug)
                    msg ("awrite:   wrote %d bytes at offset %llu",
                         f->wc_pos - i,
                         (unsigned long long)f->wc_offset + i);
                i += n;
            }
            free (f->wc_data);
            if (debug)
                msg ("awrite:   freed %u byte cache", f->wc_length);
            f->wc_data = NULL;
        }
    }
    return ret;
}

/* helper for diod_awrite */
static int
_cache_write_behind (Npfid *fid, void *buf, u32 count, u32 rsize, u64 offset,
                     int debug)
{
    Fid *f = fid->aux;
    u32 atomic_max = (u32)diod_conf_get_atomic_max () * 1024 * 1024;

    if (count > atomic_max)
        count = atomic_max;
    assert (f->wc_data == NULL);
    if (!(f->wc_data = _malloc (count))) {
        msg ("diod_awrite: out of memory (allocating %d bytes)", count);
        return -1;
    }
    if (debug)
        msg ("awrite:  allocated %u byte cache", count);
    f->wc_length = count;
    f->wc_offset = offset;
    f->wc_pos = 0;

    return _cache_write (fid, buf, rsize, offset, debug);
}

/* Tawrite - atomic write
 */
Npfcall*
diod_awrite (Npfid *fid, u64 offset, u32 count, u32 rsize, u8 *data, Npreq *req)
{
    int debug = (diod_conf_get_debuglevel () & DEBUG_ATOMIC);
    int n;
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_awrite: error switching user");
        goto done;
    }
    if (debug)
        msg ("awrite: rsize %u count %u offset %llu", rsize, count,
             (unsigned long long)offset);
    n = _cache_write (fid, data, rsize, offset, debug);
    if (n == 0) {
        if (count > rsize)
            n = _cache_write_behind (fid, data, count, rsize, offset, debug);
        else {
            n = _pwrite (fid, data, rsize, offset);
            if (n < 0)
                goto done;
            if (debug)
                msg ("awrite: directly wrote %d bytes at offset %llu", n,
                     (unsigned long long)offset);
        }
    }

    if (!(ret = np_create_rawrite (n))) {
        np_uerror (ENOMEM);
        msg ("diod_awrite: out of memory");
        goto done;
    }

done:
    return ret;
}
#endif

#if HAVE_DOTL
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

    if (!diod_switch_user (fid->user)) {
        msg ("diod_statfs: error switching user");
        goto done;
    }
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
diod_lopen (Npfid *fid, u32 mode)
{
    Fid *f = fid->aux;
    Npfcall *res = NULL;
    Npqid qid;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_lopen: error switching user");
        goto done;
    }
    if (_fidstat (f) < 0)
        goto done;

    if (S_ISDIR (f->stat.st_mode)) {
        f->dir = opendir (f->path);
        if (!f->dir) {
            np_uerror (errno);
            goto done;
        }
    } else {
        f->fd = open (f->path, mode);
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
    if (!(res = np_create_rlopen (&qid, 0))) {
        np_uerror (ENOMEM);
        msg ("diod_lopen: out of memory");
        goto done;
    }

done:
    if (np_haserror ()) {
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

    if (!diod_switch_user (fid->user)) {
        msg ("diod_lcreate: error switching user");
        goto done;
    }
done:
    return ret;
}

Npfcall*
diod_symlink(Npfid *fid, Npstr *name, Npstr *symtgt, u32 gid)
{
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_symlink: error switching user");
        goto done;
    }
done:
    return ret;
}

Npfcall*
diod_mknod(Npfid *fid, Npstr *name, u32 mode, u32 major, u32 minor, u32 gid)
{
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_mknod: error switching user");
        goto done;
    }
done:
    return ret;
}

/* Trename - rename a file, potentially to another directory (9P2000.L)
 */
Npfcall*
diod_rename (Npfid *fid, Npfid *newdirfid, Npstr *newname)
{
    Fid *f = fid->aux;
    Fid *d = newdirfid->aux;
    Npfcall *ret = NULL;
    char *newpath = NULL;
    int newpathlen;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_rename: error switching user");
        goto done;
    }
    if (!(ret = np_create_rrename ())) {
        np_uerror (ENOMEM);
        msg ("diod_rename: out of memory");
        goto done;
    }
    newpathlen = newname->len + strlen (d->path) + 2;
    if (!(newpath = _malloc (newpathlen))) {
        msg ("diod_rename: out of memory");
        goto done;
    }
    snprintf (newpath, newpathlen, "%s/%s", d->path, newname->str);
    if (rename (f->path, newpath) < 0) {
        np_uerror (errno);
        goto done;
    }
    free (f->path);
    f->path = newpath;
done:
    if (np_haserror ()) {
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
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_readlink: error switching user");
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
    u64 result_mask = P9_STATS_BASIC;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_getattr: error switching user");
        goto done;
    }
    if (_fidstat (f) < 0)
        goto done;
    _ustat2qid (&f->stat, &qid);
    if (!(ret = np_create_rgetattr(result_mask, &qid, f->stat.st_mode,
            f->stat.st_uid, f->stat.st_gid, f->stat.st_nlink, f->stat.st_rdev,
            f->stat.st_size, f->stat.st_blksize, f->stat.st_blocks,
            f->stat.st_atim.tv_sec, f->stat.st_atim.tv_nsec,
            f->stat.st_mtim.tv_sec, f->stat.st_mtim.tv_nsec,
            f->stat.st_ctim.tv_sec, f->stat.st_ctim.tv_nsec, 0, 0, 0, 0))) {
        np_uerror (ENOMEM);
        msg ("diod_getattr: out of memory");
        goto done;
    }
done:
    return ret;
}

Npfcall*
diod_setattr (Npfid *fid, u32 valid_mask, struct p9_iattr_dotl *attr)
{
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_setattr: error switching user");
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

    /* FIXME: additional stat for DT_UNKNOWN file systems to avoid
     * returning an invalid qid.  Check v9fs to see if this is necessary.
     */
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
    off_t saved_dir_pos;

    /* FIXME: seeking to offset (d_off) is possibly not kosher.
     * Use more complicated method in diod_read above?
     */
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

    if (!diod_switch_user (fid->user)) {
        msg ("diod_readdir: error switching user");
        goto done;
    }
    if (!(ret = np_create_rreaddir (count))) {
        np_uerror (ENOMEM);
        msg ("diod_readdir: out of memory");
        goto done;
    }
    n = _read_dir_linux (f, ret->u.rreaddir.data, offset, count);
    if (np_haserror ()) {
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

    if (!diod_switch_user (fid->user)) {
        msg ("diod_fsync: error switching user");
        goto done;
    }
done:
   return ret;
}

Npfcall*
diod_lock (Npfid *fid, struct p9_flock *flock)
{
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_lock: error switching user");
        goto done;
    }
done:
   return ret;
}

Npfcall*
diod_getlock (Npfid *fid, struct p9_getlock *getlock)
{
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_getlock: error switching user");
        goto done;
    }
done:
   return ret;
}

Npfcall*
diod_link (Npfid *dfid, Npfid *oldfid, Npstr *newpath)
{
    Npfcall *ret = NULL;

    if (!diod_switch_user (dfid->user)) {
        msg ("diod_link: error switching user");
        goto done;
    }
done:
   return ret;
}

Npfcall*
diod_mkdir (Npfid *fid, Npstr *name, u32 mode, u32 gid)
{
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user)) {
        msg ("diod_mkdir: error switching user");
        goto done;
    }
done:
    return ret;
}
#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
