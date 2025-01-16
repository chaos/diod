/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

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
#include <sys/syscall.h>
#include <sys/fsuid.h>
#include <pwd.h>
#include <grp.h>
#if HAVE_LIBCAP
#include <sys/capability.h>
#endif
#include <sys/prctl.h>

#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"

#if HAVE_LIBCAP
/* When handling requests on connections authenticated as root, we consider
 * it safe to disable DAC checks on the server and presume the client is
 * doing it.  This is only done if the server sets SRV_FLAGS_DAC_BYPASS.
 */
static int
_chg_privcap (Npsrv *srv, cap_flag_value_t val)
{
	cap_t cap;
	cap_flag_value_t cur;
	cap_value_t cf[] = { CAP_DAC_OVERRIDE, CAP_CHOWN, CAP_FOWNER };
	int need_set = 0;
	int i, ret = -1;

	if (!(cap = cap_get_proc ())) {
		np_uerror (errno);
		np_logerr (srv, "cap_get_proc failed");
		goto done;
	}
	for (i = 0; i < sizeof(cf) / sizeof(cf[0]); i++) {
		if (cap_get_flag (cap, cf[i], CAP_EFFECTIVE, &cur) < 0) {
			np_uerror (errno);
			np_logerr (srv, "cap_get_flag failed");
			goto done;
		}
		if (cur == val)
			continue;
		need_set = 1;
		if (cap_set_flag (cap, CAP_EFFECTIVE, 1, &cf[i], val) < 0) {
			np_uerror (errno);
			np_logerr (srv, "cap_set_flag failed");
			goto done;
		}
	}
	if (need_set && cap_set_proc (cap) < 0) {
		np_uerror (errno);
		np_logerr (srv, "cap_set_proc failed");
		goto done;
	}
	ret = 0;
done:
	if (cap != NULL && cap_free (cap) < 0) {
		np_uerror (errno);
		np_logerr (srv, "cap_free failed");
	}
	return ret;
}
#endif

/* Note: it is possible for setfsuid/setfsgid to fail silently,
 * e.g. if user doesn't have CAP_SETUID/CAP_SETGID.
 * That should be checked at server startup.
 */
int
np_setfsid (Npreq *req, Npuser *u, u32 gid_override)
{
	Npwthread *wt = req->wthread;
	Npsrv *srv = req->conn->srv;
	int i, n, ret = -1;
	u32 gid;
	uid_t authuid;
	int dumpclrd = 0;

	if (np_conn_get_authuser(req->conn, &authuid) < 0)
		authuid = NONUNAME;

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
			dumpclrd = 1;
			if ((n = setfsgid (gid)) == -1) {
				np_uerror (errno);
				np_logerr (srv, "setfsgid(%s) gid=%d failed",
					   u->uname, gid);
				wt->fsgid = NONUNAME;
				goto done;
			}
			if (n != wt->fsgid) {
				np_uerror (errno);
				np_logerr (srv, "setfsgid(%s) gid=%d failed"
					   "returned %d, expected %d",
					   u->uname, gid, n, wt->fsgid);
				wt->fsgid = NONUNAME;
				goto done;
			}
			wt->fsgid = gid;
		}
		if (wt->fsuid != u->uid) {
			dumpclrd = 1;
			if ((n = setfsuid (u->uid)) == -1) {
				np_uerror (errno);
				np_logerr (srv, "setfsuid(%s) uid=%d failed",
					   u->uname, u->uid);
				wt->fsuid = NONUNAME;
				goto done;
			}
			if (n != wt->fsuid) {
				np_uerror (EPERM);
				np_logerr (srv, "setfsuid(%s) uid=%d failed: "
					   "returned %d, expected %d",
					   u->uname, u->uid, n, wt->fsuid);
				wt->fsuid = NONUNAME;
				goto done;
			}
			/* Track CAP side effects of setfsuid.
			 */
			if (u->uid == 0)
				wt->privcap = 1; /* transiton to 0 sets caps */
			else if (wt->fsuid == 0)
				wt->privcap = 0; /* trans from 0 clears caps */

			/* Suppl groups need to be part of cred for NFS
			 * forwarding even with DAC_BYPASS.  However only
			 * do it if kernel treats sg's per-thread not process.
			 * Addendum: late model glibc attempts to make this
			 * per-process, so for now bypass glibc. See issue 53.
			 */
			if ((srv->flags & SRV_FLAGS_SETGROUPS)) {
				if (syscall(SYS_setgroups, u->nsg, u->sg) < 0) {
					np_uerror (errno);
					np_logerr (srv, "setgroups(%s) nsg=%d failed",
						   u->uname, u->nsg);
					wt->fsuid = NONUNAME;
					goto done;
				}
			}
			wt->fsuid = u->uid;
		}
	}
#if HAVE_LIBCAP
	if ((srv->flags & SRV_FLAGS_DAC_BYPASS) && wt->fsuid != 0) {
		if (!wt->privcap && authuid == 0) {
			if (_chg_privcap (srv, CAP_SET) < 0)
				goto done;
			wt->privcap = 1;
			dumpclrd = 1;
		} else if (wt->privcap && authuid != 0) {
			if (_chg_privcap (srv, CAP_CLEAR) < 0)
				goto done;
			wt->privcap = 0;
			dumpclrd = 1;
		}
	}
#endif
	ret = 0;
done:
	if (dumpclrd && prctl (PR_SET_DUMPABLE, 1, 0, 0, 0) < 0)
        	np_logerr (srv, "prctl PR_SET_DUMPABLE failed");
	return ret;
}
