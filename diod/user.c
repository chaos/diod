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

/* user.c - user handling for distributed I/O daemon */

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

#include "9p.h"
#include "npfs.h"

#include "list.h"
#include "diod_conf.h"
#include "diod_log.h"

#define SQUASH_UNAME    "nobody"

#define PASSWD_BUFSIZE  4096 /* FIXME: sysconf(_SC_GETPW_R_SIZE_MAX) ? */

/* Switch fsuid to user/group and load supplemental groups.
 * N.B. ../tests/misc/t00, ../tests/misc/t01, and ../tests/misc/t02
 * demonstrate that this works with pthreads work crew.
 */
int
diod_switch_user (Npuser *u, gid_t gid_override)
{
    int ret = 0;

    if (diod_conf_opt_runasuid () || diod_conf_get_allsquash ())
        return 1; /* bail early if running as one user */

    if (setgroups (u->nsg, u->sg) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (setfsgid (gid_override == -1 ? u->gid : gid_override) < 0) {
        np_uerror (errno);
        goto done;
    }
    if (setfsuid (u->uid) < 0) {
        np_uerror (errno);
        goto done;
    }
    ret = 1;
done:
    return ret;
}

/* Switch to user/group, load the user's supplementary groups.
 * Print message and exit on failure.
 */
void
diod_become_user (char *name, uid_t uid, int realtoo)
{
    int err;
    struct passwd pw, *pwd;
    char buf[PASSWD_BUFSIZE];
    int nsg;
    gid_t sg[64];
    char *endptr;

    if (name) {
        errno = 0;
        uid = strtoul (name, &endptr, 10);
        if (errno == 0 && *name != '\0' && *endptr == '\0')
            name = NULL;
    }
    if (name) {
        if ((err = getpwnam_r (name, &pw, buf, sizeof(buf), &pwd)) != 0)
            errn_exit (err, "error looking up user %s", name);
        if (!pwd)
            msg_exit ("error looking up user %s", name);
    } else {
        if ((err = getpwuid_r (uid, &pw, buf, sizeof(buf), &pwd)) != 0)
            errn_exit (err, "error looking up uid %d", uid);
        if (!pwd)
            msg_exit ("error looking up uid %d", uid);
    }
    nsg = sizeof (sg) / sizeof(sg[0]);
    if (getgrouplist(pwd->pw_name, pwd->pw_gid, sg, &nsg) == -1)
        err_exit ("user is in too many groups");
    if (setgroups (nsg, sg) < 0)
        err_exit ("setgroups");
    if (setregid (realtoo ? pwd->pw_gid : -1, pwd->pw_gid) < 0)
        err_exit ("setreuid");
    if (setreuid (realtoo ? pwd->pw_uid : -1, pwd->pw_uid) < 0)
        err_exit ("setreuid");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
