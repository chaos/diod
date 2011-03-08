/*****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security, LLC.
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

/* diod_auth.c - munge authentication for diod */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/fsuid.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <assert.h>
#if HAVE_MUNGE
#define GPL_LICENSED 1
#include <munge.h>
#endif

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "list.h"
#include "diod_conf.h"
#include "diod_log.h"
#include "diod_trans.h"
#include "diod_auth.h"

static int _auth_start(Npfid *afid, char *aname, Npqid *aqid);
static int _auth_check(Npfid *fid, Npfid *afid, char *aname);
static int _auth_read(Npfid *fid, u64 offset, u32 count, u8 *data);
static int _auth_write(Npfid *fid, u64 offset, u32 count, u8 *data);
static int _auth_clunk(Npfid *fid);

static Npauth _auth = {
    .startauth      = _auth_start,
    .checkauth      = _auth_check,
    .read           = _auth_read,
    .write          = _auth_write,
    .clunk          = _auth_clunk,
};

Npauth *diod_auth = &_auth;

/* Auth state associated with afid->aux.
 */
#define DIOD_AUTH_MAGIC 0x54346666
struct diod_auth_struct {
    int magic;
    enum { DA_UNVERIFIED, DA_VERIFIED } state;
#if HAVE_MUNGE
    char *mungecred;
    munge_ctx_t mungectx;
    munge_err_t mungerr;
    uid_t mungeuid;
#endif
};
typedef struct diod_auth_struct *da_t;

static da_t
_da_create (void)
{
    da_t da = NULL;

    if (!(da = malloc (sizeof (*da)))) {
        np_uerror (ENOMEM);
        goto done;
    }
    da->magic = DIOD_AUTH_MAGIC;
    da->state = DA_UNVERIFIED;
#if HAVE_MUNGE
    da->mungecred = NULL;
    if (!(da->mungectx = munge_ctx_create ())) {
        np_uerror (ENOMEM);
        free (da);
        da = NULL;
        goto done;
    }
#endif
done:
    return da;
}

static void
_da_destroy (da_t da)
{
    assert (da->magic == DIOD_AUTH_MAGIC);
#if HAVE_MUNGE
    if (da->mungecred)
        free (da->mungecred);
    if (da->mungectx)
        munge_ctx_destroy (da->mungectx);
#endif
    da->magic = 0;
    free (da);
}

/* returns 1=success (proceed with auth on afid), or
 *         0=fail (auth not required, or other failure).
 */
static int
_auth_start(Npfid *afid, char *aname, Npqid *aqid)
{
    int ret = 0;
    //msg ("_auth_start: afid %d aname %s", afid ? afid->fid : -1, aname);

    if (! diod_conf_get_auth_required ())
        goto done;
#if ! HAVE_MUNGE
    msg ("warning: auth started but server was not built with MUNGE support");
#endif
    aqid->path = 0;
    aqid->type = P9_QTAUTH;
    aqid->version = 0;
    assert (afid->aux == NULL);
    if (!(afid->aux = _da_create ()))
        goto done;
    ret = 1;
done:
    return ret;
}

/* afid will be NULL in attach (and here) if
 * - we fail auth request, indicating auth not required
 * - primary attach of kernel on this conn after user space hand-off
 * - secondary attach on this conn (v9fs access=user)
 */
static int
_auth_check(Npfid *fid, Npfid *afid, char *aname)
{
    da_t da;
    int ret = 0;

    //msg ("_auth_check: fid %d afid %d aname %s",
    //     fid ? fid->fid : -1, afid ? afid->fid : -1, aname);

    assert (fid != NULL);

    if (!afid) {
        u32 uid;

        if (!diod_conf_get_auth_required ()) {
            ret = 1;
        } else if (diod_trans_get_authuser (fid->conn->trans, &uid) == 0
                            && (uid == 0 || fid->user->uid == uid)) {
            ret = 1;
        } else {
            msg ("checkauth: unauthenticated attach rejected");
            np_uerror (EPERM);
        }
        goto done;
    }

    da = afid->aux;
    assert (da->magic == DIOD_AUTH_MAGIC);
    if (da->state != DA_VERIFIED) {
        msg ("checkauth: failed or incomplete auth handshake");
        np_uerror (EPERM);
        goto done;
    }
    if (afid->user->uid != fid->user->uid) {
        msg ("checkauth: auth uid=%d != attach uid=%d",
              afid->user->uid, fid->user->uid);
        np_uerror (EPERM);
        goto done;
    }
    diod_trans_set_authuser (fid->conn->trans, afid->user->uid);
    ret = 1;
done:
    return ret;
}

static int
_auth_read(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    //msg ("_auth_read: afid %d", afid ? afid->fid : -1);
 
    return 0;
}

static int
_auth_write(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    da_t da = afid->aux;
    int ret = -1;

    //msg ("_auth_write: afid %d", afid ? afid->fid : -1);

    assert (da->magic == DIOD_AUTH_MAGIC);

    if (da->state == DA_VERIFIED) {
        np_uerror (EIO);
        goto done;
    }

#if HAVE_MUNGE
    if (offset == 0 && !da->mungecred) {
        da->mungecred = malloc (count + 1); 
    } else if (da->mungecred && offset == strlen (da->mungecred)) {
        da->mungecred = realloc (da->mungecred, offset + count + 1);
    } else {
        np_uerror (EIO);
        goto done;
    }
    if (!da->mungecred) {
        np_uerror (ENOMEM);
        goto done;
    }
    memcpy (da->mungecred + offset, data, count);
    da->mungecred[offset + count] = '\0';
    da->mungerr = munge_decode (da->mungecred, da->mungectx, NULL, 0,
                                &da->mungeuid, NULL)
    if (da->mungerr == EMUNGE_SUCCESS && afid->user->uid == da->mungeuid)
        da->state = DA_VERIFIED;
#endif
    ret = count;
done:
    return ret;
}

static int
_auth_clunk(Npfid *afid)
{
    da_t da = afid->aux;

    //msg ("_auth_clunk: afid %d", afid ? afid->fid : -1);

    if (da) {
        _da_destroy (da);
        afid->aux = NULL;
    }
    return 1; /* success */
}

int
diod_auth_client_handshake (Npcfid *afid, u32 uid)
{
    int ret = -1; 
#if HAVE_MUNGE
    char *cred = NULL;
    int saved_errno = 0;
    munge_ctx_t ctx = NULL;

    if (!(ctx = munge_ctx_create ())) {
        saved_errno = ENOMEM;
        goto done;
    }
    if (munge_encode (&cred, ctx, NULL, 0) != EMUNGE_SUCCESS) {
        saved_errno = EPERM;
        goto done;
    }
    if (npc_puts (afid, cred) < 0) {
        saved_errno = errno;
        goto done;
    }
    ret = 0;
done:
    if (ctx)
        munge_ctx_destroy (ctx);
    errno = saved_errno;
#endif
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
