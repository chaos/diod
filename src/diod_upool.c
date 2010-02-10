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
 * primary and supplementary groups are stored in Npuser->aux as an integer
 * array suitable for passing directly to setgroups().
 *
 * Summary of refcount strategy:  
 * Tattach: created via diod_u*2user() with refcount 1 (see below)
 * Twalk: if cloning the fid, refcount++ 
 * Tclunk: if fid is destroyed, refcount--
 * User is destroyed when its refcount reaches 0.
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
#define GPL_LICENSED 1
#if HAVE_LIBMUNGE
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

static int
_switch_user (Npuser *u)
{
    int ret = -1;
    gid_t *gids = (gid_t *)u->aux;

    if (geteuid () == u->uid)
        return 0;
    if (_setreuid (0, 0) < 0)
        goto done;
    if (_setgroups (u->ngroups, gids) < 0)
        goto done;
    if (_setregid (-1, u->uid) < 0)
        goto done;
    if (_setreuid (-1, gids[0]) < 0)
        goto done;
    ret = 0;
done:
    return ret;
}

int
diod_switch_user (Npuser *u)
{
    int ret = -1;
    
    if (diod_conf_get_sameuser())
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

/* Populate (gid_t *)u->aux with supplementary groups for user in pwd.
 * Element 0 is the primary group.
 */
static int
_add_supplemental_groups (Npuser *u, struct passwd *pwd)
{
    FILE *f;
    int i, err;
    struct group grp, *grpp;
    char buf[GROUP_BUFSIZE];
    gid_t *gids = (gid_t *)u->aux;
    int ret = -1;

    u->ngroups = 0;
    gids[u->ngroups++] = pwd->pw_gid;
    if (!(f = fopen (PATH_GROUP, "r"))) {
        np_uerror (errno);
        goto done;
    }
    while ((err = fgetgrent_r (f, &grp, buf, sizeof (buf), &grpp)) == 0) {
        for (i = 0; grpp->gr_mem[i] != NULL; i++) {
            if (strcmp (grpp->gr_mem[i], pwd->pw_name) == 0) {
                if (u->ngroups < NGROUPS_MAX) {
                    if (pwd->pw_gid != grpp->gr_gid)
                        gids[u->ngroups++] = grpp->gr_gid;
                } else
                    msg ("user %s exceeded supplementary group max of %d",
                         pwd->pw_name, NGROUPS_MAX);
                break;
            }
        }
    } 
    fclose (f);
    ret = 0;
done:
    return ret;
}

static Npuser *
_alloc_user (Npuserpool *up, struct passwd *pwd)
{
    Npuser *u;
    int err;

    if (!(u = np_malloc (sizeof (*u))))
        goto done;
    if (!(u->aux = (gid_t *)np_malloc (sizeof (gid_t) * (NGROUPS_MAX))))
        goto done;
    if (_add_supplemental_groups (u, pwd) < 0)
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
    u->groups = NULL;       /* not used (npfs/libnpfs/user.c must not free) */
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

#if HAVE_LIBMUNGE
static Npuser *
diod_munge2user (Npuserpool *up, char *uname, char *buf, int buflen)
{
    Npuser *u = NULL;
    munge_ctx_t ctx = NULL;
    munge_err_t err;
    void *cstr = NULL;
    int len;
    struct passwd pw, *pwd;
    uid_t uid;
    gid_t gid;

    if (!(ctx = munge_ctx_create ())) {
        np_uerror (ENOMEM);
        goto done;
    }
    err = munge_decode (uname, ctx, &cstr, &len, &uid, &gid);
    if (err != EMUNGE_SUCCESS) {
        msg ("munge_decode: %s", munge_ctx_strerror(ctx));
        np_uerror (EPERM);
        goto done;
    }
    if ((err = getpwuid_r(uid, &pw, buf, buflen, &pwd)) != 0) {
        np_uerror (err);
        goto done;
    }
    if (!pwd) {
        np_uerror (ESRCH);
        goto done;
    }
    if (pwd->pw_gid != gid) {
        np_uerror (EPERM);
        goto done;
    }
    if (!(u = _alloc_user (up, pwd)))
        goto done;

    np_user_incref (u);
done:        
    if (cstr)    
        free (cstr);
    if (ctx)
        munge_ctx_destroy (ctx);
    return u;
}
#endif

/* N.B. This (or diod_uid2user) is called when handling a 9P attach message.
 */
static Npuser *
diod_uname2user (Npuserpool *up, char *uname)
{
    Npuser *u = NULL; 
    int err;
    struct passwd pw, *pwd;
    char buf[PASSWD_BUFSIZE];

#if HAVE_LIBMUNGE
    if (strncmp (uname, "MUNGE:", 6) == 0)
        return diod_munge2user (up, uname, buf, sizeof(buf));
#endif
    if ((err = getpwnam_r(uname, &pw, buf, sizeof(buf), &pwd)) != 0) {
        np_uerror (err);
        goto done;
    }
    if (!pwd) {
        np_uerror (ESRCH);
        goto done;
    }
    if (!(u = _alloc_user (up, pwd)))
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

    if ((err = getpwuid_r(uid, &pw, buf, sizeof(buf), &pwd)) != 0) {
        np_uerror (err);
        goto done;
    }
    if (!pwd) {
        np_uerror (ESRCH);
        goto done;
    }
    if (!(u = _alloc_user (up, pwd)))
        goto done;
done:
    np_user_incref (u);
    return u;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
