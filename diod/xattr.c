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

/* FIXME: removexattr(2)? */

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
#include <attr/xattr.h>
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

struct xattr_struct {
    char *buf;
    ssize_t len;
};

int
xattr_pread (Xattr xattr, void *buf, size_t count, off_t offset)
{
    int len = xattr->len - offset;

    if (len > count)
        len = count;
    if (len < 0)
        len = 0;
    memcpy (buf, xattr->buf + offset, len);
    return len;
}

static ssize_t
_lgetxattr (const char *path, const char *name, char **bp)
{
    ssize_t len;
    char *buf = NULL;

    len = lgetxattr (path, name, NULL, 0);
    if (len < 0) {
        np_uerror (errno);
        return -1;
    }
    buf = malloc (len);
    if (!buf) {
        np_uerror (ENOMEM);
        return -1;
    }
    len = lgetxattr (path, name, buf, len);
    if (len < 0) {
        np_uerror (errno);
        free (buf);
        return -1; 
    }
    *bp = buf;
    return len;
}

static ssize_t
_llistxattr (const char *path, char **bp)
{
    ssize_t len;
    char *buf;

    len = llistxattr (path, NULL, 0);
    msg ("listxattr returned %ld", len);
    if (len < 0) {
        np_uerror (errno);
        return -1;
    }
    buf = malloc (len);
    if (!buf) {
        np_uerror (ENOMEM);
        return -1;
    }
    len = llistxattr (path, buf, len);
    if (len < 0) {
        np_uerror (errno);
        free (buf);
        return -1; 
    }
    *bp = buf;
    return len;
}

int
xattr_open (Npfid *fid, Npstr *name, u64 *sizep)
{
    Fid *f = fid->aux;
    char *s;
   
    assert (f->xattr == NULL);
 
    f->xattr = malloc (sizeof (struct xattr_struct));
    if (!f->xattr) {
        np_uerror (ENOMEM);
        goto error;
    }
    f->xattr->buf = NULL;
    f->xattr->len = 0;

    if (name && name->len > 0) {
        msg ("xattr_open: will do getxattr");

        if (!(s = np_strdup (name))) {
            np_uerror (ENOMEM);
            goto error;
        }
        f->xattr->len = _lgetxattr (path_s (f->path), s, &f->xattr->buf);
        free (s);
    } else {
        msg ("xattr_open: will do listxattr");
        f->xattr->len = _llistxattr (path_s (f->path), &f->xattr->buf);
    }
    if (f->xattr->len < 0)
        goto error;
    *sizep = (u64)f->xattr->len;
    return 0;
error:
    if (f->xattr) {
        if (f->xattr->buf)
            free (f->xattr->buf);
        free (f->xattr);
        f->xattr = NULL;
    }
    return -1;    
}

int
xattr_close (Npfid *fid)
{
    Fid *f = fid->aux;

    if (f->xattr) {
        if (f->xattr->buf)
            free (f->xattr->buf);
        free (f->xattr);
    }
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

