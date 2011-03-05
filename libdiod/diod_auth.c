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

/* Strategy is to authenticate from user space, then pass fd to kernel
 * with afid=n munt option.  If correct code is in place, kernel will
 * skip version and insert afid into attach message.
 */

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

static int
_auth_start(Npfid *afid, char *aname, Npqid *aqid)
{
    //msg ("_auth_start: afid %d aname %s", afid ? afid->fid : -1, aname);

    if (! diod_conf_get_auth_required ())
        return 0;
    aqid->path = 0;
    aqid->type = P9_QTAUTH;
    aqid->version = 0;
    assert (afid->aux == NULL);
    return 1;
}

static int
_auth_check(Npfid *fid, Npfid *afid, char *aname)
{
    munge_ctx_t ctx = NULL;
    munge_err_t err;
    uid_t uid;
    int ret = 0;

    //msg ("_auth_check: fid %d afid %d aname %s",
    //     fid ? fid->fid : -1, afid ? afid->fid : -1, aname);

    assert (fid != NULL);

    /* Auth will be nil in attach if
     * - we fail auth request, indicating auth not required
     * - secondary attach on this conn (v9fs access=user)
     */
    if (!afid) {
        if (!diod_conf_get_auth_required ()) {
            ret = 1;
        } else if (diod_trans_get_authuser (fid->conn->trans, &uid)
                            && (uid == 0 || fid->user->uid == uid)) {
            ret = 1;
        } else {
            msg ("checkauth: unauthenticated attach rejected");
            np_uerror (EPERM);
        }
        goto done;
    }
    if (afid->aux == NULL) {
        msg ("checkauth: incomplete authentication handshake");
        np_uerror (EPERM);
        goto done;
    }
    if (afid->user->uid != fid->user->uid) {
        msg ("checkauth: auth uid=%d != attach uid=%d",
              afid->user->uid, fid->user->uid);
        np_uerror (EPERM);
        goto done;
    }
    if (!(ctx = munge_ctx_create ())) {
        msg ("checkauth: out of memory");
        np_uerror (ENOMEM);
        goto done;
    }
    err = munge_decode ((char *)afid->aux, ctx, NULL, 0, &uid, NULL);
    if (err != EMUNGE_SUCCESS) {
        msg ("checkauth: munge_decode: %s", munge_strerror (err));
        np_uerror (EPERM);
        goto done;
    }
    if (afid->user->uid != uid) {
        msg ("checkauth: munge uid=%d != afid uid=%d", uid, afid->user->uid);
        np_uerror (EPERM);
        goto done;
    }
    diod_trans_set_authuser (fid->conn->trans, uid);
    ret = 1;
done:
    if (ctx)
        munge_ctx_destroy (ctx);
    if (afid && afid->aux) {
        memset (afid->aux, 0, strlen ((char *)afid->aux));
        free (afid->aux);
        afid->aux = NULL;
    }
    return ret;
}

static int
_auth_read(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    //msg ("_auth_read: for afid %d", afid ? afid->fid : -1);
 
    return 0;
}

static int
_auth_write(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    char *credstr;

    //msg ("_auth_write: afid %d", afid ? afid->fid : -1);

    if (afid->aux) /* FIXME: handle multiple writes */
        return 0;
    if (!(credstr = malloc (count + 1))) {
        np_uerror (ENOMEM);
        return 0;
    }
    memcpy (credstr, data, count);
    credstr[count] = '\0';
    afid->aux = credstr;

    return count;
}

static int
_auth_clunk(Npfid *afid)
{
    //msg ("_auth_clunk: afid %d", afid ? afid->fid : -1);

    if (afid->aux) {
        free (afid->aux);
        afid->aux = NULL;
    }
    return 1;
}

int
diod_auth_client_handshake (Npcfid *afid, u32 uid)
{
    char *cred = NULL;
    munge_ctx_t ctx = NULL;
    munge_err_t err;
    int ret = -1; 

    if (!(ctx = munge_ctx_create ())) {
        np_uerror (ENOMEM);
        goto done;
    }
    err = munge_encode (&cred, ctx, NULL, 0);
    if (err != EMUNGE_SUCCESS) {
        np_uerror (EIO);
        goto done;
    }
    if (npc_puts (afid, cred) < 0)
        goto done;
    ret = 1;
done:
    munge_ctx_destroy (ctx);

    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
