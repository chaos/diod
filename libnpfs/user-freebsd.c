/*****************************************************************************
 *  Copyright (C) 2010-14 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see http://code.google.com/p/diod.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also: http://www.gnu.org/licenses
 *****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <sys/param.h>
#include <sys/module.h>
#include <sys/syscall.h>

#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"
#include "ganesha-syscalls.h"

/* Load syscall from nfs-ganesha-kmod */

static int sysn_setthreaduid = -1;
static int sysn_setthreadgid = -1;
static int sysn_setthreadgroups = -1;

static int
_get_syscall(const char *modname)
{
	int modid;
	struct module_stat stat;

	stat.version = sizeof(stat);
	if ((modid = modfind(modname)) == -1)
		return -1;
	if (modstat(modid, &stat) != 0)
		return -1;
	return stat.data.intval;
}

int
init_ganesha_syscalls(void)
{
	if ((sysn_setthreaduid = _get_syscall("sys/setthreaduid")) == -1)
		return -1;
	if ((sysn_setthreadgid = _get_syscall("sys/setthreadgid")) == -1)
		return -1;
	if ((sysn_setthreadgroups = _get_syscall("sys/setthreadgroups")) == -1)
		return -1;
	return 0;
}

int
fbsd_setthreaduid(uid_t uid)
{
	if (sysn_setthreaduid == -1) {
		errno = ENOSYS;  /* some sensible error code */
		return -1;
	}
	return syscall(sysn_setthreaduid, uid);
}

int
fbsd_setthreadgid(gid_t gid)
{
	if (sysn_setthreadgid == -1) {
		errno = ENOSYS;
		return -1;
	}
	return syscall(sysn_setthreadgid, gid);
}

int
fbsd_setthreadgroups(unsigned int gidsetsize, gid_t *gidset)
{
	if (sysn_setthreadgroups == -1) {
		errno = ENOSYS;
		return -1;
	}
	return syscall(sysn_setthreadgroups, gidsetsize, gidset);
}

int
np_setfsid (Npreq *req, Npuser *u, u32 gid_override)
{
	Npwthread *wt = req->wthread;
	Npsrv *srv = req->conn->srv;
	int i, ret = -1;
	u32 gid;
	uid_t authuid;

	if (np_conn_get_authuser(req->conn, &authuid) < 0)
		authuid = P9_NONUNAME;

	if ((srv->flags & SRV_FLAGS_SETFSID)) {
		/* gid_override must be one of user's suppl. groups unless
		 * connection was originally authed as root (trusted).
		 */
		if (gid_override != -1 && u->uid != 0 && authuid != 0
				       && !(srv->flags & SRV_FLAGS_NOUSERDB)) {
			for (i = 0; i < u->nsg; i++) {
				if (u->sg[i] == gid_override)
					break;
			}
			if (i == u->nsg) {
				np_uerror (EPERM);
				np_logerr (srv, "np_setfsid(%s): gid_override "
					   "%d not in user's sg list",
					   u->uname, gid_override);
				goto done;
			}
		}
		gid = (gid_override == -1 ? u->gid : gid_override);
		if (wt->fsgid != gid) {
			if (! wt->privcap) {
				/* restore privileged uid */
				if (fbsd_setthreaduid (0) == -1) {
					np_uerror (errno);
					np_logerr (srv, "setthreaduid(0) failed");
					wt->fsuid = P9_NONUNAME;
					goto done;
				}
				wt->privcap = 1;
				wt->fsuid = 0;
			}
			if (fbsd_setthreadgid (gid) == -1) {
				np_uerror (errno);
				np_logerr (srv, "setthreadgid(%s) gid=%d failed",
					   u->uname, gid);
				wt->fsgid = P9_NONUNAME;
				goto done;
			}
			wt->fsgid = gid;
		}
		if (wt->fsuid != u->uid) {
			/* Set suppl groups first! */
			/* Suppl groups need to be part of cred for NFS
			 * forwarding even with DAC_BYPASS.
			 */
			if (! wt->privcap) {
				/* restore privileged uid */
				if (fbsd_setthreaduid (0) == -1) {
					np_uerror (errno);
					np_logerr (srv, "setthreaduid(0) failed");
					wt->fsuid = P9_NONUNAME;
					goto done;
				}
				wt->privcap = 1;
				wt->fsuid = 0;
			}
			if (fbsd_setthreadgroups (u->nsg, u->sg) == -1) {
				np_uerror (errno);
				np_logerr (srv, "setthreadgroups(%s) nsg=%d failed",
					   u->uname, u->nsg);
				wt->fsuid = P9_NONUNAME;
				goto done;
			}
			if (wt->fsuid != u->uid) {
				if (fbsd_setthreaduid (u->uid) == -1) {
					np_uerror (errno);
					np_logerr (srv, "setthreaduid(%s) uid=%d failed",
						   u->uname, u->uid);
					wt->fsuid = P9_NONUNAME;
					goto done;
				}
				/* Track CAP side effects. */
				wt->privcap = (u->uid != 0) ? 0 : 1;
				wt->fsuid = u->uid;
			}
		}
	}
	ret = 0;
done:
	return ret;
}
