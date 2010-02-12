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

/* diod_upool.c - user lookup for diod distributed I/O daemon */

/* This upool implementation does no caching of /etc/passwd and /etc/group.
 * Instead it creates Npuser's anew at attach time, shares them among cloned
 * fids, and deletes them when the refcount goes to zero (e.g. when root is
 * clunked at umount time).  Npgroup's are not used at all.  Instead the user's
 * primary and supplementary groups are stored a private struct linked to
 * Npuser->aux in the form of an array suitable for passing directly to
 * setgroups().
 *
 * Summary of refcount strategy:  
 * Tattach: created via diod_u*2user() with refcount 1 (see below)
 * Twalk: if cloning the fid, refcount++ 
 * Tclunk: if fid is destroyed, refcount--
 * User is destroyed when its refcount reaches 0.
 * 
 * Secure authentication is accomplished with munge.  All attaches are denied
 * until an attach is received with a valid munge cred in the uname field.
 * If that attach succedes, subsequent non-munge attaches on the same
 * transport (set up per mount) succeed if the original attach was root or
 * the same user.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <assert.h>
#if HAVE_LIBMUNGE
#define GPL_LICENSED 1
#include <munge.h>
#endif

#include "npfs.h"

#include "list.h"
#include "diod_conf.h"
#include "diod_log.h"
#include "diod_upool.h"

#ifndef NGROUPS_MAX
#define NGROUPS_MAX     256  /* FIXME: sysconf(_SC_NGROUPS_MAX); */
#endif
#define PASSWD_BUFSIZE  4096 /* FIXME: sysconf(_SC_GETPW_R_SIZE_MAX) ? */
#define GROUP_BUFSIZE   4096
#define PATH_GROUP      "/etc/group"
#define SQUASH_UID      65534
#define SQUASH_GID      65534

static Npuser       *diod_uname2user (Npuserpool *, char *uname);
static Npuser       *diod_uid2user (Npuserpool *, u32 uid);
static void          diod_udestroy (Npuserpool *up, Npuser *u);

static Npuserpool upool = {
    .uname2user     = diod_uname2user,
    .uid2user       = diod_uid2user,
    .gname2group    = NULL,
    .gid2group      = NULL,
    .ismember       = NULL,
    .udestroy       = diod_udestroy,
    .gdestroy       = NULL,
};

/* Private Duser struct, linked to Npuser->aux.
 */
#define DUSER_MAGIC 0x3455DDDF
typedef struct {
    int         magic;
    int         munged;         /* user supplied a valid munge cred */
    gid_t       gid;            /* primary gid */
    int         nsg;            /* number of supplementary groups */
    gid_t       sg[NGROUPS_MAX];/* supplementary gid array */
} Duser;

Npuserpool *diod_upool = &upool;

static int
_setreuid (uid_t ruid, uid_t euid)
{
    int ret;

    if ((ret = setreuid (ruid, euid)) < 0)
        np_uerror (errno);

    return ret;
}

static int
_setregid (gid_t rgid, gid_t egid)
{
    int ret;

    if ((ret = setregid (rgid, egid)) < 0)
        np_uerror (errno);

    return ret;
}

static int
_setgroups (size_t size, const gid_t *list)
{
    int ret;

    if ((ret = setgroups (size, list)) < 0)
        np_uerror (errno);

    return ret;
}

/* Change user to root.
 */
static int
_switch_root (void)
{
    int ret = -1;

    if (geteuid () == 0)
        return 0;
    if (_setreuid (0, 0) < 0)
        goto done;
    if (_setregid (0, 0) < 0)
        goto done;
    ret = 0;
done:
    return ret;
}

/* Change user to squash user.
 */
static int
_switch_squash (void)
{
    int ret = -1;

    if (geteuid () == SQUASH_UID) 
        return 0;
    if (_setreuid (0, 0) < 0)
        goto done;
    if (_setgroups (0, NULL) < 0)
        goto done;
    if (_setregid (-1, SQUASH_UID) < 0)
        goto done;
    if (_setreuid (-1, SQUASH_GID) < 0)
        goto done;
    ret = 0;
done:
    return ret;
}

/* Change user to some other user.
 */
static int
_switch_user (Npuser *u)
{
    Duser *d = (Duser *)u->aux;
    int ret = -1;

    assert (d->magic == DUSER_MAGIC);

    if (geteuid () == u->uid)
        return 0;
    if (_setreuid (0, 0) < 0)
        goto done;
    if (_setgroups (d->nsg, d->sg) < 0)
        goto done;
    if (_setregid (-1, u->uid) < 0)
        goto done;
    if (_setreuid (-1, d->gid) < 0)
        goto done;
    ret = 0;
done:
    return ret;
}

/* Change user/group/supplementary groups, if needed.
 * Only users that have successfully attached will be seen here (9P invariant).
 */
int
diod_switch_user (Npuser *u)
{
    int ret = -1;
    
    if (diod_conf_get_user() == NULL)
        return 0;
    if (u->uid == 0) {
        if (diod_conf_get_rootsquash ())
            ret =_switch_squash ();
        else
            ret = _switch_root ();
    } else
        ret = _switch_user (u);
    return ret;
}

static Duser *
_alloc_duser (struct passwd *pwd, int munged)
{
    FILE *f;
    int i, err;
    struct group grp, *grpp;
    char buf[GROUP_BUFSIZE];
    Duser *d = NULL;

    if (!(d = np_malloc (sizeof (*d))))
        goto done;
    d->magic = DUSER_MAGIC;
    d->munged = munged;
    d->gid = pwd->pw_gid;
    d->nsg = 0;
    if (!(f = fopen (PATH_GROUP, "r"))) {
        np_uerror (errno);
        goto done;
    }
    /* the primary group will be added too if in /etc/group (this is ok) */
    while ((err = fgetgrent_r (f, &grp, buf, sizeof (buf), &grpp)) == 0) {
        for (i = 0; grpp->gr_mem[i] != NULL; i++) {
            if (strcmp (grpp->gr_mem[i], pwd->pw_name) == 0) {
                if (d->nsg < NGROUPS_MAX) {
                    d->sg[d->nsg++] = grpp->gr_gid;
                } else
                    msg ("user %s exceeded supplementary group max of %d",
                         pwd->pw_name, NGROUPS_MAX);
                break;
            }
        }
    } 
    fclose (f);
done:
    if (np_haserror () && d != NULL) {
        free (d);
        d = NULL;
    }
    return d;
}

static Npuser *
_alloc_user (Npuserpool *up, struct passwd *pwd, int munged)
{
    Npuser *u;
    int err;

    if (!(u = np_malloc (sizeof (*u))))
        goto done;
    if (!(u->aux = _alloc_duser (pwd, munged)))
        goto done;
    if ((err = pthread_mutex_init (&u->lock, NULL)) != 0) {
        np_uerror (err);
        goto done;
    }
    u->refcount = 0;
    u->upool = up;
    u->uid = pwd->pw_uid;

    u->uname = NULL;        /* not used */
    u->dfltgroup = NULL;    /* not used */
    u->groups = NULL;       /* not used */
    u->ngroups = 0;         /* not used */
    u->next = NULL;         /* not used */
done:
    if (np_haserror () && u != NULL) {
        if (u->aux)
            free (u->aux);
        free (u);
        u = NULL;
    }
    return u; 
}

/* Called from npfs/libnpfs/user.c::np_user_decref ().
 * Caller frees u, we must free the rest.
 */
static void
diod_udestroy (Npuserpool *up, Npuser *u)
{
    if (u->aux)
        free (u->aux);
}

int
diod_user_has_mungecred (Npuser *u)
{
    Duser *d = u->aux;

    assert (d->magic == DUSER_MAGIC);

    return d->munged;
}

static int
_decode_mungecred (char *uname, uid_t *uidp)
{
    int ret = -1;
#if HAVE_LIBMUNGE
    munge_ctx_t ctx;
    munge_err_t err;
    uid_t uid;

    if (!(ctx = munge_ctx_create ())) {
        np_uerror (ENOMEM);
        goto done;
    }
    err = munge_decode (uname, ctx, NULL, NULL, &uid, NULL);
    memset (uname, 0, strlen (uname));
    /* FIXME: there is another copy in Npfcall->uname to sanitize */
    /* FIXME: the cred will be logged if DEBUG_9P_TRACE is enabled */
    munge_ctx_destroy (ctx);
    if (err != EMUNGE_SUCCESS) {
        msg ("munge_decode: %s", munge_strerror (err));
        np_uerror (EPERM);
        goto done;
    }
    ret = 0;
    *uidp = uid;
done:
#else
    np_uerror (EPERM);
#endif
    return ret;
}

/* N.B. This (or diod_uid2user) is called when handling a 9P attach message.
 */
static Npuser *
diod_uname2user (Npuserpool *up, char *uname)
{
    Npuser *u = NULL; 
    int err;
    struct passwd pw, *pwd = NULL;
    char buf[PASSWD_BUFSIZE];
    uid_t uid;
    int munged = 0;

    if (diod_conf_get_munge () && strncmp (uname, "MUNGE:", 6) == 0) {
        if (_decode_mungecred (uname, &uid) < 0)
            goto done;
        if ((err = getpwuid_r (uid, &pw, buf, sizeof(buf), &pwd)) != 0) {
            np_uerror (err);
            goto done;
        }
        munged = 1;
    } else {
        if ((err = getpwnam_r (uname, &pw, buf, sizeof(buf), &pwd)) != 0) {
            np_uerror (err);
            goto done;
        }
    }
    if (!pwd) {
        np_uerror (ESRCH);
        goto done;
    }
    if (!(u = _alloc_user (up, pwd, munged)))
        goto done;
    np_user_incref (u);
done:
    return u;
}

/* This (or diod_uname2user) is called when handling a 9P attach message.
 */
static Npuser *
diod_uid2user(Npuserpool *up, u32 uid)
{
    Npuser *u = NULL; 
    int err;
    struct passwd pw, *pwd;
    char buf[PASSWD_BUFSIZE];

    if ((err = getpwuid_r (uid, &pw, buf, sizeof(buf), &pwd)) != 0) {
        np_uerror (err);
        goto done;
    }
    if (!pwd) {
        np_uerror (ESRCH);
        goto done;
    }
    if (!(u = _alloc_user (up, pwd, 0)))
        goto done;
done:
    np_user_incref (u);
    return u;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
