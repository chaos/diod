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
#define _GNU_SOURCE     /* asprintf, unshare */
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <mntent.h>
#include <sys/mount.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <libgen.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "9p.h"
#include "npfs.h"
#include "list.h"
#include "diod_log.h"
//#include "diod_upool.h"
//#include "diod_sock.h"
#include "opt.h"
#include "util.h"

typedef struct {
    List exports;
    char *port;
} query_t;

/* Create a directory, recursively creating parents as needed.
 * Return success (0) if the directory already exists, or creation was
 * successful, (-1) on failure.
 */
int
util_mkdir_p (char *path, mode_t mode)
{
    struct stat sb;
    char *cpy;
    int res = 0;

    if (stat (path, &sb) == 0) {
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }
    if (!(cpy = strdup (path))) {
        errno = ENOMEM;
        return -1;
    }
    res = util_mkdir_p (dirname (cpy), mode);
    free (cpy);
    if (res == 0)
        res = mkdir (path, mode);

    return res;
}

/* Add an entry for [dev] mounted on [dir] to /etc/mtab.
 * Return success (1) or failure (0).
 */
int
util_update_mtab (char *dev, char *dir)
{
    uid_t saved_euid = geteuid ();
    FILE *f;
    int ret = 0;
    struct mntent mnt;

    mnt.mnt_fsname = dev;
    mnt.mnt_dir = dir;
    mnt.mnt_type = "diod";
    mnt.mnt_opts = MNTOPT_DEFAULTS;
    mnt.mnt_freq = 0;
    mnt.mnt_passno = 0;

    if (seteuid (0) < 0) {
        err ("failed to set effective uid to root");
        goto done;
    }
    if (!(f = setmntent (_PATH_MOUNTED, "a"))) {
        err (_PATH_MOUNTED);
        goto done;
    }
    if (addmntent (f, &mnt) != 0) {
        msg ("failed to add entry to %s", _PATH_MOUNTED);
        endmntent (f);
        goto done;
    }
    endmntent (f);
    if (seteuid (saved_euid) < 0) {
        err ("failed to restore effective uid to %d", saved_euid);
        goto done;
    }
    ret = 1;
done:
    return ret;
}

/* Mount 9p file system [source] on [target] with options [data].
 * Swap effective (user) and real (root) uid's for the duration of mount call.
 * Exit on error.
 */
void
util_mount (const char *source, const char *target, const void *data)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (mount (source, target, "9p", 0, data))
        err_exit ("mount");
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

/* Mount file system [target].
 * Swap effective (user) and real (root) uid's for the duration of umount call.
 * Exit on error.
 */
void
util_umount (const char *target)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (umount (target) < 0)
        err_exit ("umount %s", target);
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

/* Unshare file system name space.
 * Swap effective (user) and real (root) uid's for the duration of unshare call.
 * Exit on error.
 */
void
util_unshare (void)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (unshare (CLONE_NEWNS) < 0)
        err_exit ("failed to unshare name space");
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

/* Given [device] in host:aname format, parse out the host and aname.
 * Caller must free the resulting strings.
 * Exit on error.
 */
void
util_parse_device (char *device, char **anamep, char **hostp)
{
    char *host, *p, *aname;

    if (!(host = strdup (device)))
        msg_exit ("out of memory");
    if (!(p = strchr (host, ':')))
        msg_exit ("device is not in host:directory format");
    *p++ = '\0';
    if (!(aname = strdup (p)))
        msg_exit ("out of memory");
    
    *hostp = host;
    *anamep = aname;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
