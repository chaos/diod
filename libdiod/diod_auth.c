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
#ifndef __MACH__
#include <sys/fsuid.h>
#endif
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
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

static int startauth(Npfid *afid, char *aname, Npqid *aqid);
static int checkauth(Npfid *fid, Npfid *afid, char *aname);
static int readafid(Npfid *fid, u64 offset, u32 count, u8 *data);
static int writeafid(Npfid *fid, u64 offset, u32 count, u8 *data);
static int clunkafid(Npfid *fid);

static Npauth _auth = {
    .startauth      = startauth,
    .checkauth      = checkauth,
    .read           = readafid,
    .write          = writeafid,
    .clunk          = clunkafid,
};
Npauth *diod_auth_functions = &_auth;

/* Auth state associated with afid->aux.
 */
#define DIOD_AUTH_MAGIC 0x54346666
struct diod_auth_struct {
    int magic;
    char *datastr;
#if HAVE_LIBMUNGE
    munge_ctx_t mungectx;
    munge_err_t mungerr;
    uid_t mungeuid;
    gid_t mungegid;
#endif
};
typedef struct diod_auth_struct *da_t;

/* Create implementation-specific auth state.
 */
static da_t
_da_create (void)
{
    da_t da = NULL;

    if (!(da = malloc (sizeof (*da)))) {
        np_uerror (ENOMEM);
        goto done;
    }
    da->magic = DIOD_AUTH_MAGIC;
    da->datastr = NULL;
#if HAVE_LIBMUNGE
    if (!(da->mungectx = munge_ctx_create ())) {
        np_uerror (ENOMEM);
        free (da);
        da = NULL;
        goto done;
    }
    da->mungeuid = -1;
    da->mungegid = -1;
#endif
done:
    return da;
}

/* Destroy implementation-specific auth state.
 */
static void
_da_destroy (da_t da)
{
    NP_ASSERT (da->magic == DIOD_AUTH_MAGIC);
    da->magic = 0;
    if (da->datastr)
        free (da->datastr);
#if HAVE_LIBMUNGE
    if (da->mungectx)
        munge_ctx_destroy (da->mungectx);
#endif
    free (da);
}

/* Startauth is called in the handling of a TAUTH request.
 * Perform implementation-specific auth initialization and fill in qid.
 * Returns 1=success (send RAUTH with qid)
 *         0=fail (send RLERROR with np_rerror ())
 */
static int
startauth(Npfid *afid, char *aname, Npqid *aqid)
{
    int ret = 0;

    if (!afid || afid->aux != NULL || !aqid) {
        msg ("startauth: invalid arguments");
        np_uerror (EIO);
        goto done;
    }
    if (!(afid->aux = _da_create ()))
        goto done;
    aqid->path = 0;
    aqid->type = P9_QTAUTH;
    aqid->version = 0;
    ret = 1;
done:
    return ret;
}

/* Checkauth is called in the handling of a TATTACH request with valid afid.
 * Validate the credential that should now be dangling off afid.
 * Returns 1=success (auth is good, send RATTACH)
 *         0=failure (send RLERROR with np_rerror ())
 */
static int
checkauth(Npfid *fid, Npfid *afid, char *aname)
{
    da_t da;
    int ret = 0;
    char a[128];

    if (!fid || !afid || !afid->aux) {
        msg ("checkauth: invalid arguments");
        np_uerror (EIO);
        goto done;
    }
    da = afid->aux;
    NP_ASSERT (da->magic == DIOD_AUTH_MAGIC);

    snprintf (a, sizeof(a), "checkauth(%s@%s:%s)", fid->user->uname,
              np_conn_get_client_id (fid->conn), aname ? aname : "<NULL>");
#if HAVE_LIBMUNGE
    if (!da->datastr) {
        msg ("%s: munge cred missing", a);
        np_uerror (EPERM);
        goto done;
    }
    da->mungerr = munge_decode (da->datastr, da->mungectx, NULL, 0,
                                &da->mungeuid, &da->mungegid);
    if (da->mungerr != EMUNGE_SUCCESS) {
        msg ("%s: munge cred decode: %s", a, munge_strerror (da->mungerr));
        np_uerror (EPERM);
        goto done;
    }
    NP_ASSERT (afid->user->uid == fid->user->uid); /* enforced in np_attach */
    if (afid->user->uid != da->mungeuid) {
        msg ("%s: munge cred (%d:%d) does not authenticate uid=%d", a,
             da->mungeuid, da->mungegid, afid->user->uid);
        np_uerror (EPERM);
        goto done;
    }
    ret = 1;
#else
    msg ("%s: diod was not built with support for auth services", a);
    np_uerror (EPERM);
#endif
done:
    return ret;
}

static int
readafid(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    msg ("readafid: called unexpectedly");
    np_uerror (EIO);
    return -1; /* error */
}

static int
writeafid(Npfid *afid, u64 offset, u32 count, u8 *data)
{
    da_t da;
    int ret = -1;

    if (!afid || !afid->aux || !data || count == 0) {
        msg ("writeafid: invalid arguments");
        np_uerror (EIO);
        goto done;
    }
    da = afid->aux;
    NP_ASSERT (da->magic == DIOD_AUTH_MAGIC);

    if (offset == 0 && !da->datastr) {
        da->datastr = malloc (count + 1); 
    } else if (da->datastr && offset == strlen (da->datastr)) {
        da->datastr = realloc (da->datastr, offset + count + 1);
    } else {
        msg ("writeafid: write at unexpected offset");
        np_uerror (EIO);
        goto done;
    }
    if (!da->datastr) {
        msg ("writeafid: out of memory");
        np_uerror (ENOMEM);
        goto done;
    }
    memcpy (da->datastr + offset, data, count);
    da->datastr[offset + count] = '\0';

    ret = count;
done:
    return ret;
}

/* clunkafid is called when the afid is being freed.
 * Free implementation specific storage associated with afid.
 */
static int
clunkafid(Npfid *afid)
{
    if (afid && afid->aux) {
        da_t da = afid->aux;

        _da_destroy (da);
         afid->aux = NULL;
    }
    return 1; /* success */
}

/* This is called from libnpclient user space.
 * It drives the client end of authentication.
 */
int
diod_auth (Npcfid *afid, u32 uid)
{
    int ret = -1; 
#if HAVE_LIBMUNGE
    char *cred = NULL;
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
