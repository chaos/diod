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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef __MACH__
#define _DARWIN_C_SOURCE    /* fs stuff */
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

#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "list.h"
#include "hash.h"
#include "hostlist.h"

#include "diod_conf.h"
#include "diod_log.h"

#include "ioctx.h"
#include "xattr.h"
#include "fid.h"

/* Allocate local fid struct and attach to fid->aux.
 */
Fid *
diod_fidalloc (Npfid *fid, Npstr *ns)
{
    Fid *f = malloc (sizeof (*f));

    NP_ASSERT (fid->aux == NULL);
    if (f) {
        f->flags = 0;
        f->ioctx = NULL;
        f->xattr = NULL;
        f->path = path_create (fid->conn->srv, ns);
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
Fid *
diod_fidclone (Npfid *newfid, Npfid *fid)
{
    Fid *f = fid->aux;
    Fid *nf = malloc (sizeof (*f));

    NP_ASSERT (newfid->aux == NULL);
    if (nf) {
        nf->flags = f->flags;
        nf->ioctx = NULL;
        nf->xattr = NULL;
        nf->path = path_incref (f->path);
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
            ioctx_close (fid, 0);
        if (f->xattr)
            xattr_close (fid);
        if (f->path)
            path_decref (fid->conn->srv, f->path);
        free(f);
        fid->aux = NULL;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
