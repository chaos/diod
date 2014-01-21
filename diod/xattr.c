/*****************************************************************************
 *  Copyright (C) 2013 Lawrence Livermore National Security, LLC.
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

/* xattr.c - support setxattr(2), getxattr(2), listxattr(2) */

/* FIXME: attr removal? */

#if HAVE_CONFIG_H
#include "config.h"
#endif
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
#ifndef __MACH__
#include <attr/xattr.h>
#else
#include <sys/xattr.h>
#endif
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
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <stdarg.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"
#include "hash.h"
#include "hostlist.h"
#include "xpthread.h"

#include "diod_conf.h"
#include "diod_log.h"

#include "ioctx.h"
#include "xattr.h"
#include "fid.h"

#define XATTR_FLAGS_GET     1
#define XATTR_FLAGS_SET     2

struct xattr_struct {
    char *name;
    char *buf;
    ssize_t len;
    int flags;
    u32 setflags;
};

static void _xattr_destroy (Xattr *xp)
{
    Xattr x = *xp;
    if (x) {
        if (x->buf)
            free (x->buf);
        if (x->name)
            free (x->name);
        free (x);
    }
    *xp = NULL;
}

static Xattr _xattr_create (Npstr *name, size_t size, int flags, u32 setflags)
{
    Xattr x;

    x = malloc (sizeof (struct xattr_struct));
    if (!x)
        goto nomem;
    memset (x, 0, sizeof (struct xattr_struct));
    x->flags = flags;
    x->setflags = setflags;
    if (name && name->len > 0) {
        x->name = np_strdup (name);
        if (!x->name)
            goto nomem;
    }
    if (size > 0) {
        x->buf = malloc (size);
        if (!x->buf)
            goto nomem;
        x->len = size;
    }
    return x;
nomem:
    _xattr_destroy (&x);
    np_uerror (ENOMEM);
    return NULL;
}


int
xattr_pwrite (Xattr x, void *buf, size_t count, off_t offset)
{
    int len = count;

    if (!(x->flags & XATTR_FLAGS_SET)) {
        np_uerror (EINVAL);
        return -1;
    } 
    if (len > x->len - offset)
        len = x->len - offset;
    if (len < 0)
        len = 0;
    memcpy (x->buf + offset, buf, len);
    return len;
}

int
xattr_pread (Xattr x, void *buf, size_t count, off_t offset)
{
    int len = x->len - offset;

    if (!(x->flags & XATTR_FLAGS_GET)) {
        np_uerror (EINVAL);
        return -1;
    }
    if (len > count)
        len = count;
    if (len < 0)
        len = 0;
    memcpy (buf, x->buf + offset, len);
    return len;
}

#ifdef __MACH__
#define lgetxattr(path, name, value, size) getxattr(path, name, value, size, 0, XATTR_NOFOLLOW)
#define lsetxattr(path, name, value, size, options) setxattr(path, name, value, size, 0, XATTR_NOFOLLOW | options )
#define llistxattr(path, name, size) listxattr(path, name, size, XATTR_NOFOLLOW)
#define lremovexattr(path, name) removexattr(path, name, XATTR_NOFOLLOW)
#endif

static int
_lgetxattr (Xattr x, const char *path)
{
    ssize_t len;

    if (x->name)
        len = lgetxattr (path, x->name, NULL, 0);
    else
        len = llistxattr (path, NULL, 0);
    if (len < 0) {
        np_uerror (errno);
        return -1;
    }
    assert (x->buf == NULL);
    x->buf = malloc (len);
    if (!x->buf) {
        np_uerror (ENOMEM);
        return -1;
    }
    if (x->name)
        x->len = lgetxattr (path, x->name, x->buf, len);
    else
        x->len = llistxattr (path, x->buf, len);
    if (x->len < 0) {
        np_uerror (errno);
        return -1; 
    }
    return 0;
}

int
xattr_open (Npfid *fid, Npstr *name, u64 *sizep)
{
    Fid *f = fid->aux;
   
    assert (f->xattr == NULL);

    f->xattr = _xattr_create (name, 0, XATTR_FLAGS_GET, 0);
    if (_lgetxattr (f->xattr, path_s (f->path)) < 0)
        goto error;
    *sizep = (u64)f->xattr->len;
    return 0;
error:
    _xattr_destroy (&f->xattr);
    return -1;    
}

int xattr_create (Npfid *fid, Npstr *name, u64 size, u32 setflags)
{
    Fid *f = fid->aux;

    assert (f->xattr == NULL);

    f->xattr = _xattr_create (name, size, XATTR_FLAGS_SET, setflags);
    if (!f->xattr)
        goto error;
    return 0;
error:
    _xattr_destroy (&f->xattr);
    return -1;
}

int
xattr_close (Npfid *fid)
{
    Fid *f = fid->aux;
    int rc = 0;

    if (f->xattr) {
        if ((f->xattr->flags & XATTR_FLAGS_SET)) {
            if (f->xattr->len > 0) {
                if (lsetxattr (path_s (f->path), f->xattr->name, f->xattr->buf,
                               f->xattr->len, f->xattr->setflags) < 0) {
                    np_uerror (errno);
                    rc = -1;
                }
            } else if (f->xattr->len == 0) {
                if (lremovexattr (path_s (f->path), f->xattr->name) < 0) {
                    np_uerror (errno);
                    rc = -1;
                }
            }
        }
        _xattr_destroy (&f->xattr);
    }
    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

