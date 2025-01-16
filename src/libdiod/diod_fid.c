/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

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
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <stdarg.h>

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "src/libnpfs/xpthread.h"
#include "src/liblsd/list.h"
#include "src/liblsd/hash.h"
#include "src/liblsd/hostlist.h"

#include "diod_conf.h"
#include "diod_log.h"
#include "diod_ioctx.h"
#include "diod_xattr.h"
#include "diod_fid.h"

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
