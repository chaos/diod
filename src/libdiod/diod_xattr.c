/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

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
#include <stdbool.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/file.h>
#if HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <stdarg.h>
#include <assert.h>

#include "src/libnpfs/npfs.h"
#include "src/liblsd/list.h"
#include "src/liblsd/hash.h"
#include "src/liblsd/hostlist.h"
#include "src/libnpfs/xpthread.h"

#include "diod_conf.h"
#include "diod_log.h"
#include "diod_ioctx.h"
#include "diod_xattr.h"
#include "diod_fid.h"

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

static bool
bounds_check (size_t bufsize, size_t count, off_t offset)
{
    if (offset < 0 || offset > bufsize || count > bufsize - offset)
        return false;
    return true;
}

int
xattr_pwrite (Xattr x, void *buf, size_t count, off_t offset)
{
    if (!(x->flags & XATTR_FLAGS_SET)
        || !bounds_check (x->len, count, offset)) {
        errno = EINVAL;
        return -1;
    }
    memcpy (x->buf + offset, buf, count);
    return count;
}

int
xattr_pread (Xattr x, void *buf, size_t count, off_t offset)
{
    if (!(x->flags & XATTR_FLAGS_GET)
        || !bounds_check (x->len, count, offset)) {
        errno = EINVAL;
        return -1;
    }
    memcpy (buf, x->buf + offset, count);
    return count;
}

static int
_lgetxattr (Xattr x, const char *path)
{
#if !HAVE_SYS_XATTR_H
  return 0;
#else
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
#endif
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
#if !HAVE_SYS_XATTR_H
  return 0;
#else
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
#endif
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

