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
#define _XOPEN_SOURCE 600   /* pread/pwrite/posix_fadvise */
#define _BSD_SOURCE         /* makedev */
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <assert.h>

#include "npfs.h"
#include "list.h"

#include "diod_conf.h"
#include "diod_log.h"
#include "diod_trans.h"
#include "diod_upool.h"

#include "ops.h"

typedef struct {
    char            *path;
    int              omode;
    int              fd;
    DIR             *dir;
    struct dirent   *dirent;
    int              diroffset;
    struct stat      stat;
    void            *rc_data;
    u32              rc_offset;
    u32              rc_length;
    u32              rc_pos;
    void            *wc_data;
    u32              wc_offset;
    u32              wc_length;
    u32              wc_pos;
} Fid;

Npfcall     *diod_version(Npconn *conn, u32 msize, Npstr *version);
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

Npfcall     *diod_aread  (Npfid *fid, u8 datacheck, u64 offset, u32 count,
                          u32 rsize, Npreq *req);
Npfcall     *diod_awrite (Npfid *fid, u64 offset, u32 count, u32 rsize,
                          u8 *data, Npreq *req);
Npfcall     *diod_statfs (Npfid *fid);
Npfcall     *diod_rename (Npfid *fid, Npfid *newdirfid, Npstr *newname);

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
static int  _cache_write_behind (Npfid *fid, void *buf, u32 count,
                                 u32 rsize, u64 offset);
static int  _cache_write        (Npfid *fid, void *buf, u32 rsize, u64 offset);
static int  _cache_read_ahead   (Npfid *fid, void *buf, u32 count,
                                 u32 rsize, u64 offset);
static int  _cache_read         (Npfid *fid, void *buf, u32 rsize, u64 offset);


static pthread_mutex_t  conn_lock = PTHREAD_MUTEX_INITIALIZER;
static int              conn_count = 0;
static int              conn_used = 0;

char *Enoextension = "empty extension while creating special file";
char *Eformat = "incorrect extension format";

void
diod_register_ops (Npsrv *srv)
{
    srv->dotu = 1;
    srv->msize = 65536;
    srv->upool = diod_upool;
    srv->version = diod_version;
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

    /* 9p2000.h */
    srv->aread = diod_aread;
    srv->awrite = diod_awrite;
    srv->statfs = diod_statfs;
    //srv->plock = diod_lock;
    //srv->flock = diod_flock;
    srv->rename = diod_rename;
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

    if ((n = pread (f->fd, buf, count, offset)) < 0)
        np_uerror (errno);

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

    if ((n = pwrite (f->fd, buf, count, offset)) < 0)
        np_uerror (errno);

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
        f->omode = -1;
        f->fd = -1;
        f->dir = NULL;
        f->diroffset = 0;
        f->dirent = NULL;
        f->rc_data = NULL;
        f->wc_data = NULL;
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

/* This is a courtesy callback from npfs to let us know that
 * the fid we are parasitically attached to is being destroyed.
 */
void
diod_fiddestroy (Npfid *fid)
{
    _fidfree ((Fid *)fid->aux);
    fid->aux = NULL;
}

/* Convert 9P open mode bits to UNIX open mode bits.
 */
static int
_omode2uflags (u8 mode)
{
    int ret = 0;

    switch (mode & 3) {
        case Oread:
            ret = O_RDONLY;
            break;
        case Ordwr:
            ret = O_RDWR;
            break;
        case Owrite:
            ret = O_WRONLY;
            break;
        case Oexec:
            ret = O_RDONLY;
            break;
    }
    if (mode & Otrunc)
        ret |= O_TRUNC;
    if (mode & Oappend)
        ret |= O_APPEND;
    if (mode & Oexcl)
        ret |= O_EXCL;

    return ret;
}

/* Create a 9P qid from a file's stat info.
 * N.B. v9fs converts qid->path to inode numbers (must be unique per mount!)
 */
static void
_ustat2qid (struct stat *st, Npqid *qid)
{
    int n;

    qid->path = 0;
    n = sizeof (qid->path);
    if (n > sizeof (st->st_ino))
        n = sizeof (st->st_ino);
    memmove (&qid->path, &st->st_ino, n);

    qid->version = st->st_mtime ^ (st->st_size << 8);

    qid->type = 0;
    if (S_ISDIR(st->st_mode))
        qid->type |= Qtdir;
    if (S_ISLNK(st->st_mode))
        qid->type |= Qtsymlink;
}

/* Convert UNIX file mode bits to 9P file mode bits.
 */
static u32
_umode2npmode (mode_t umode)
{
    u32 ret = umode & 0777;

    if (S_ISDIR (umode))
        ret |= Dmdir;

    /* dotu */
    if (S_ISLNK (umode))
        ret |= Dmsymlink;
    if (S_ISSOCK (umode))
        ret |= Dmsocket;
    if (S_ISFIFO (umode))
        ret |= Dmnamedpipe;
    if (S_ISBLK (umode))
        ret |= Dmdevice;
    if (S_ISCHR (umode))
        ret |= Dmdevice;
    if (umode & S_ISUID)
        ret |= Dmsetuid;
    if (umode & S_ISGID)
        ret |= Dmsetgid;

    return ret;
}

/* Convert 9P file mode bits to UNIX mode bits.
 */
static mode_t
_np2umode (u32 mode, Npstr *extension)
{
    mode_t ret = mode & 0777;

    if (mode & Dmdir)
        ret |= S_IFDIR;

    /* dotu */
    if (mode & Dmsymlink)
        ret |= S_IFLNK;
    if (mode & Dmsocket)
        ret |= S_IFSOCK;
    if (mode & Dmnamedpipe)
        ret |= S_IFIFO;
    if (mode & Dmdevice) {
        if (extension && extension->str[0] == 'c')
            ret |= S_IFCHR;
        else
            ret |= S_IFBLK;
    }

    if (!(ret&~0777))
        ret |= S_IFREG;
    if (mode & Dmsetuid)
        ret |= S_ISUID;
    if (mode & Dmsetgid)
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

    if (wstat->mode & Dmsymlink) {
        err = readlink (path, ext, sizeof(ext) - 1);
        if (err < 0)
            err = 0;

        ext[err] = '\0';
    } else if (wstat->mode & Dmdevice) {
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
        errn_exit (errnum, "diod_connopen: could not take conn_lock mutex");
    conn_count--;
    if (conn_count == 0 && conn_used) {
        msg ("exiting on last use");
        exit (0);
    }
    if ((errnum = pthread_mutex_unlock (&conn_lock)))
        errn_exit (errnum, "diod_connopen: could not drop conn_lock mutex");
}

void
diod_connopen (Npconn *conn)
{
    int errnum;

    if (!diod_conf_get_exit_on_lastuse ())
        return;
    if (!diod_conf_get_exit_on_lastuse ())
        return;
    if ((errnum = pthread_mutex_lock (&conn_lock)))
        errn_exit (errnum, "diod_connopen: could not take conn_lock mutex");
    conn_count++; 
    if ((errnum = pthread_mutex_unlock (&conn_lock)))
        errn_exit (errnum, "diod_connopen: could not drop conn_lock mutex");
}

static void
_connused (Npconn *conn)
{
    int errnum;

    if (!diod_conf_get_exit_on_lastuse ())
        return;
    if ((errnum = pthread_mutex_lock (&conn_lock)))
        errn_exit (errnum, "diod_connopen: could not take conn_lock mutex");
    conn_used++; 
    if ((errnum = pthread_mutex_unlock (&conn_lock)))
        errn_exit (errnum, "diod_connopen: could not drop conn_lock mutex");
}

/* Tversion - negotiate 9P protocol version.
 */
Npfcall*
diod_version (Npconn *conn, u32 msize, Npstr *version)
{
    Npfcall *ret = NULL;

    if (np_strcmp (version, "9P2000.h") != 0) { /* implies 'dotu' as well */
        np_werror ("unsupported 9P version", EIO);
        goto done;
    }
    if (msize < AIOHDRSZ + 1) {
        np_werror ("msize too small", EIO);
        goto done;
    }
    if (msize > conn->srv->msize)
        msize = conn->srv->msize;

    np_conn_reset (conn, msize, 1); /* 1 activates 'dotu' */
    if (!(ret = np_create_rversion (msize, "9P2000.h"))) {
        np_uerror (ENOMEM);
        goto done;
    }
    _connused (conn);
done:
    return ret;
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
    uid_t auid, runasuid;

    if (nafid) {    /* 9P Tauth not supported */
        np_werror (Enoauth, EIO);
        goto done;
    }
    if (aname->len == 0 || *aname->str != '/') {
        np_uerror (EPERM);
        goto done;
    }
    /* If running as a particular user, reject attaches from other users.
     */
    if (diod_conf_get_runasuid (&runasuid) && fid->user->uid != runasuid) {
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
    if (!(f = _fidalloc ()))
        goto done;
    if (!(f->path = _p9strdup(aname)))
        goto done;
    if (!diod_conf_match_export (f->path, host, ip, fid->user->uid, &err)) {
        np_uerror (err);
        goto done;
    }
    if (_fidstat (f) < 0)
        goto done;
    _ustat2qid (&f->stat, &qid);
    if ((ret = np_create_rattach (&qid)) == NULL) {
        np_uerror (ENOMEM);
        goto done;
    }
    fid->aux = f;
    np_fid_incref (fid);

done:
    msg ("attach user %s path %s host %s(%s): %s",
         fid->user->uname, (f && f->path) ? f->path : "<unknown>", host, ip,
         np_haserror () ? "DENIED" : "ALLOWED");
    if (np_haserror ()) {
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

    if (!nf)
        goto done;

    if (!(nf->path = _strdup (f->path)))
        goto done;
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

    if (!diod_switch_user (fid->user))
        goto done;
    if (_fidstat (f) < 0)
        goto done;
    n = strlen (f->path);
    if (!(path = _malloc (n + wname->len + 2)))
        goto done;
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

    if (!diod_switch_user (fid->user))
        goto done;
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
        if (!(mode & Owrite) && !diod_conf_get_readahead ()) {
            if (posix_fadvise (f->fd, 0, 0, POSIX_FADV_RANDOM) < 0)
                err ("posix_fadvise %s", f->path);
        }
    }
    if (_fidstat (f) < 0) /* XXX why stat again when we just did it? */
        goto done;
    f->omode = mode;
    _ustat2qid (&f->stat, &qid);
    if (!(res = np_create_ropen (&qid, 0))) {
        np_uerror (ENOMEM);
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
    Fid *f = fid->aux;
    mode_t umode;
    char *ext = NULL;
    int ret = -1;

    if (!(perm & Dmnamedpipe) && !extension->len) {
        np_werror (Enoextension, EIO);
        goto done;
    }

    umode = _np2umode (perm, extension);
    if (!(ext = _p9strdup (extension)))
        goto done;
    if (perm & Dmsymlink) {
        if (symlink (ext, path) < 0) {
            np_uerror (errno);
            goto done;
        }
    } else if (perm & Dmlink) {
        if (_link (fid, path, ext) < 0)
            goto done;
    } else if (perm & Dmdevice) {
        if (_mknod (path, ext, perm) < 0)
            goto done;
    } else if (perm & Dmnamedpipe) {
        if (mknod (path, S_IFIFO | (umode & 0777), 0) < 0) {
            np_uerror (errno);
            goto done;
        }
    }
    f->omode = 0;
    if (!(perm & Dmsymlink)) {
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
    int n, omode = mode;
    Fid *f = fid->aux;
    Npfcall *ret = NULL;
    char *npath = NULL;
    Npqid qid;

    if (!diod_switch_user (fid->user))
        goto done;
    if (_fidstat (f) < 0)
        goto done;
    n = strlen (f->path);
    if (!(npath = _malloc (n + name->len + 2)))
        goto done;
    memmove (npath, f->path, n);
    npath[n] = '/';
    memmove (npath + n + 1, name->str, name->len);
    npath[n + name->len + 1] = '\0';

    if (perm & Dmdir) {
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
    } else if (perm & (Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice)) {
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
        if (!(mode & Owrite) && !diod_conf_get_readahead ()) {
            if (posix_fadvise (f->fd, 0, 0, POSIX_FADV_RANDOM) < 0)
                err ("posix_fadvise %s", npath);
        }
    }
    free (f->path);
    f->path = npath;
    f->omode = omode;
    npath = NULL;
    _ustat2qid (&f->stat, &qid);
    if (!((ret = np_create_rcreate (&qid, 0)))) {
        np_uerror (ENOMEM);
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

    if (!(path = _malloc (plen + strlen (child) + 2)))
        goto done;
    sprintf (path, "%s/%s", parent, child);
    if (lstat (path, &st) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (_ustat2npwstat (path, &st, &wstat) < 0)
        goto done;
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

    if (!diod_switch_user (fid->user))
        goto done;
    if (!(ret = np_alloc_rread (count))) {
        np_uerror (ENOMEM);
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

    if (!diod_switch_user (fid->user))
        goto done;
    if ((n = _pwrite (fid, data, count, offset)) < 0)
        goto done;
    if (!(ret = np_create_rwrite (n))) {
        np_uerror (ENOMEM);
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

    /* FIXME: should we close the file here rather than diod_fiddestroy()? */
    if (!(ret = np_create_rclunk ()))
        np_uerror (ENOMEM);

    return ret;
}

/* Tremove - remove a file or directory.
 */
Npfcall*
diod_remove (Npfid *fid)
{
    Fid *f = fid->aux;
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user))
        goto done;
    if (remove (f->path) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (!(ret = np_create_rremove ())) {
        np_uerror (ENOMEM);
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

    if (!diod_switch_user (fid->user))
        goto done;
    wstat.extension = NULL;
    if (_fidstat (f) < 0)
        goto done;
    if (_ustat2npwstat (f->path, &f->stat, &wstat) < 0)
        goto done;
    if (!(ret = np_create_rstat(&wstat, 1))) { /* 1 for dotu */
        np_uerror (ENOMEM);
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

    if (!diod_switch_user (fid->user))
        goto done;
    if (_fidstat (f) < 0)
        goto done;
    if (st->name.len != 0) {
        np_werror ("Rename via wstat is deprecated in 9P2000.h", EIO);
        goto done;
    }
    if (!(ret = np_create_rwstat())) {
        np_uerror (ENOMEM);
        goto done;
    }

    /* chmod */
    if (st->mode != (u32)~0) {
        mode_t umode = _np2umode(st->mode, &st->extension);

        if ((st->mode & Dmdir) && !S_ISDIR(f->stat.st_mode)) {
            np_werror(Edirchange, EIO);
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

    /* chown */
    if (st->n_uid != (u32)~0 || st->n_gid != (u32)~0) {
        uid_t uid = st->n_uid != (u32)~0 ? st->n_uid : -1;
        gid_t gid = st->n_gid != (u32)~0 ? st->n_uid : -1;

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

    /* fsync - designated by a "do nothing" wstat, see stat(5.plan9) */
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

/* helper for diod_aread */
static int
_cache_read (Npfid *fid, void *buf, u32 rsize, u64 offset)
{
    Fid *f = fid->aux;
    int ret = 0;

    if (f->rc_data) {
        if (f->rc_offset + f->rc_pos == offset) {
            ret = f->rc_length - f->rc_pos;
            if (ret > rsize)
                ret = rsize;
            memcpy (buf, f->rc_data + f->rc_pos, ret);
            f->rc_pos += ret;
        }
        if (ret == 0 || f->rc_pos == f->rc_length) {
            free (f->rc_data);
            f->rc_data = NULL;
        }
    }
    return ret;
}

/* helper for diod_aread */
static int
_cache_read_ahead (Npfid *fid, void *buf, u32 count, u32 rsize, u64 offset)
{
    Fid *f = fid->aux;

    if (!f->rc_data || f->rc_length < count) {
        if (f->rc_data)
            free(f->rc_data);
        if (!(f->rc_data = _malloc (count)))
            return -1;
    }
    f->rc_length = _pread (fid, f->rc_data, count, offset);
    f->rc_offset = offset;
    f->rc_pos = 0;
    if (f->rc_length < 0) {
        free (f->rc_data);
        f->rc_data = NULL;
        return -1;
    } 
    return _cache_read (fid, buf, rsize, offset);
}

/* Taread - atomic read (9p2000.h).
 */
Npfcall*
diod_aread (Npfid *fid, u8 datacheck, u64 offset, u32 count, u32 rsize,
            Npreq *req)
{
    int n;
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user))
        goto done;
    if (!(ret = np_create_raread (rsize))) {
        np_uerror (ENOMEM);
        goto done;
    }
    n = _cache_read (fid, ret->data, rsize, offset);
    if (n == 0) {
        if (count > rsize)
            n = _cache_read_ahead (fid, ret->data, count, rsize, offset);
        else
            n = _pread (fid, ret->data, rsize, offset);
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
_cache_write (Npfid *fid, void *buf, u32 rsize, u64 offset)
{
    Fid *f = fid->aux;
    int ret = 0, i = 0, n;

    if (f->wc_data) {
        if (f->wc_offset + f->wc_pos == offset) {
            ret = f->wc_length - f->wc_pos;
            if (ret > rsize)
                ret = rsize;
            memcpy (f->wc_data + f->wc_pos, buf, ret);
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
                i += n;
            }
            free (f->wc_data);
            f->wc_data = NULL;
        }
    }
    return ret;
}

/* helper for diod_awrite */
static int
_cache_write_behind (Npfid *fid, void *buf, u32 count, u32 rsize, u64 offset)
{
    Fid *f = fid->aux;

    if (!(f->wc_data = _malloc (count)))
        return -1;
    f->wc_length = count;
    f->wc_offset = offset;
    f->wc_pos = 0;

    return _cache_write (fid, buf, rsize, offset);
}

/* Tawrite - atomic write (9p2000.h).
 */
Npfcall*
diod_awrite (Npfid *fid, u64 offset, u32 count, u32 rsize, u8 *data, Npreq *req)
{
    int n;
    Npfcall *ret = NULL;

    if (!diod_switch_user (fid->user))
        goto done;
    n = _cache_write (fid, data, rsize, offset);
    if (n == 0) {
        if (count > rsize)
            n = _cache_write_behind (fid, data, count, rsize, offset);
        else {
            n = _pwrite (fid, data, rsize, offset);
            if (n < 0)
                goto done;
        }
    }

    if (!(ret = np_create_rawrite (n))) {
        np_uerror (ENOMEM);
        goto done;
    }

done:
    return ret;
}

/* Tstatfs - read file system  information (9p2000.h)
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

    if (!diod_switch_user (fid->user))
        goto done;
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
        goto done;
    }

done:
    return ret;
}

/* Trename - rename a file, potentially to another directory (9p2000.h)
 */
Npfcall*
diod_rename (Npfid *fid, Npfid *newdirfid, Npstr *newname)
{
    Fid *f = fid->aux;
    Fid *d = newdirfid->aux;
    Npfcall *ret = NULL;
    char *newpath = NULL;
    int newpathlen;

    if (!diod_switch_user (fid->user))
        goto done;
    if (!(ret = np_create_rrename ())) {
        np_uerror (ENOMEM);
        goto done;
    }
    newpathlen = newname->len + strlen (d->path) + 2;
    if (!(newpath = _malloc (newpathlen)))
        goto done;
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
