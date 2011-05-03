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

/* Here is the basic handshake
 * C: Tauth afid uname aname n_uname
 * S: Rauth aqid
 * C: Twrite afid offset count <munge cred>
 * S: Rwrite count
 * C: Tattach fid afid uname aname n_uname
 * S: Rattach qid
 * C: Tclunk afid
 * S: Rclunk
 */

/* FIXME:
 * Handshake should be expanded to include selection from
 * multiple auth methods.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/fsuid.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <assert.h>
#if HAVE_LIBMUNGE
#define GPL_LICENSED 1
#include <munge.h>
#endif

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "list.h"
#include "diod_conf.h"
#include "diod_log.h"
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
#if HAVE_LIBMUNGE
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
#if HAVE_LIBMUNGE
    da->mungecred = NULL;
    da->mungerr = EMUNGE_SUCCESS;
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
    da->magic = 0;
#if HAVE_LIBMUNGE
    if (da->mungecred)
        free (da->mungecred);
    if (da->mungectx)
        munge_ctx_destroy (da->mungectx);
#endif
    free (da);
}

/* returns 1=success (proceed with auth on afid), or
 *         0=fail (auth not required, or other failure).
 */
static int
_auth_start(Npfid *afid, char *aname, Npqid *aqid)
{
    int debug = (afid->conn->srv->flags & SRV_FLAGS_DEBUG_AUTH);
    int ret = 0;

    if (debug)
        msg ("_auth_start: afid %d aname %s", afid ? afid->fid : -1, aname);

#if ! HAVE_LIBMUNGE
    msg ("startauth: no MUNGE support: auth begun but cannot be completed ");
#endif
    assert (afid->aux == NULL);
    if (!(afid->aux = _da_create ()))
        goto done;
    aqid->path = 0;
    aqid->type = P9_QTAUTH;
    aqid->version = 0;
    ret = 1;
done:
    return ret;
}

/* Called from libnpfs internal attach handler.
 * Returns 1=success (auth OK, attach should proceed)
 *         0=failure (auth requirements not met, attach should be denied)
 */
static int
_auth_check(Npfid *fid, Npfid *afid, char *aname)
{
    int debug = (fid->conn->srv->flags & SRV_FLAGS_DEBUG_AUTH);
    da_t da;
    int ret = 0;

    if (debug)
        msg ("_auth_check: fid %d afid %d aname %s",
             fid ? fid->fid : -1, afid ? afid->fid : -1, aname);

    assert (fid != NULL);
    assert (afid != NULL);
    assert (afid->aux != NULL);
    da = afid->aux;
    assert (da->magic == DIOD_AUTH_MAGIC);
    if (da->state != DA_VERIFIED) {
        const char *crederr = "credential is unverified";
#if HAVE_LIBMUNGE
        if (da->mungerr != EMUNGE_SUCCESS)
            crederr = munge_strerror (da->mungerr);
#endif
        msg ("checkauth: attach by %s@%s to %s rejected: %s",
             fid->user->uname, np_conn_get_client_id (fid->conn),
             aname ? aname : "<nil>", crederr);
        np_uerror (EPERM);
        goto done;
    }
    if (afid->user->uid != fid->user->uid) {
        msg ("checkauth: attach by %s@%s to %s rejected: "
             "auth uid=%d != attach uid=%d",
             fid->user->uname, np_conn_get_client_id (fid->conn),
             aname ? aname : "<nil>", afid->user->uid, fid->user->uid);
        np_uerror (EPERM);
        goto done;
    }
    ret = 1;
done:
    return ret;
}

static int
_auth_read(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    int debug = (afid->conn->srv->flags & SRV_FLAGS_DEBUG_AUTH);

    if (debug)
        msg ("_auth_read: afid %d", afid ? afid->fid : -1);
 
    return 0;
}

static int
_auth_write(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    int debug = (afid->conn->srv->flags & SRV_FLAGS_DEBUG_AUTH);
    da_t da = afid->aux;
    int ret = -1;

    if (debug)
        msg ("_auth_write: afid %d", afid ? afid->fid : -1);

    assert (da->magic == DIOD_AUTH_MAGIC);

    if (da->state == DA_VERIFIED) {
        np_uerror (EIO);
        goto done;
    }

#if HAVE_LIBMUNGE
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
                                &da->mungeuid, NULL);
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
    int debug = (afid->conn->srv->flags & SRV_FLAGS_DEBUG_AUTH);
    da_t da = afid->aux;

    if (debug)
        msg ("_auth_clunk: afid %d", afid ? afid->fid : -1);

    if (da)
        _da_destroy (da);
    afid->aux = NULL;

    return 1; /* success */
}

/* This is called from libnpclient user space.
 * It drives the client end of authentication.
 */
int
diod_auth_client_handshake (Npcfid *afid, u32 uid)
{
    int ret = -1; 
#if HAVE_LIBMUNGE
    char *cred = NULL;
    int saved_errno = 0;
    munge_ctx_t ctx = NULL;

    if (!(ctx = munge_ctx_create ())) {
        np_uerror (ENOMEM);
        goto done;
    }
    if (munge_encode (&cred, ctx, NULL, 0) != EMUNGE_SUCCESS) {
        np_uerror (EPERM);
        goto done;
    }
    if (npc_puts (afid, cred) < 0) {
        goto done;
    }
    ret = 0;
done:
    if (ctx)
        munge_ctx_destroy (ctx);
#endif
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
