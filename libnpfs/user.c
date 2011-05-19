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
#include <assert.h>

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

typedef struct {
        pthread_mutex_t lock;
        Npuser* users;
	int ttl;
} Npusercache;

static void
_usercache_add (Npsrv *srv, Npuser *u)
{
	Npusercache *uc = srv->usercache;

	u->next = uc->users;
	uc->users = u;
	np_user_incref (u);
}

static Npuser *
_usercache_del (Npsrv *srv, Npuser *prev, Npuser *u)
{
	Npusercache *uc = srv->usercache;
	Npuser *tmp = u->next;

	if (prev)
		prev->next = tmp;
	else
		uc->users = tmp;
	np_user_decref (u);

	return tmp;
}

/* expire entries after 60s */
static void
_usercache_expire (Npsrv *srv)
{
	Npusercache *uc = srv->usercache;
	time_t now = time (NULL);
	Npuser *u = uc->users;
	Npuser *prev = NULL;

	while (u) {
		if (now - u->t >= uc->ttl) {
			u = _usercache_del (srv, prev, u);
			continue;
		}
		prev = u;
		u = u->next;
	}
}

static Npuser *
_usercache_lookup (Npsrv *srv, char *uname, uid_t uid)
{
	Npusercache *uc = srv->usercache;
	Npuser *u;

	_usercache_expire (srv);
	for (u = uc->users; u != NULL; u = u->next) {
		if (!uname && uid == u->uid)
			break;
		if (uname && !strcmp (uname, u->uname)) 
			break;
	}
	return u;
}

static char *
_get_usercache (void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Npusercache *uc = srv->usercache;
	Npuser *u;
	time_t now = time (NULL);
	char *s = NULL;
	int len = 0;

	pthread_mutex_lock (&uc->lock);
	_usercache_expire (srv);
	for (u = uc->users; u != NULL; u = u->next) {
		int ttl = uc->ttl - (now - u->t);

		if (aspf (&s, &len, "%s(%d,%d+%d) %d\n", u->uname,
			  u->uid, u->gid, u->nsg, u->uid ? ttl : 0) < 0) {
			np_uerror (ENOMEM);
			pthread_mutex_unlock (&uc->lock);
			goto error;
		}
	}
	pthread_mutex_unlock (&uc->lock);
	return s;
error:
	if (s)
		free (s);
	return NULL;
}

int
np_usercache_create (Npsrv *srv)
{
	Npusercache *uc;

	assert (srv->usercache == NULL);
	if (!(uc = malloc (sizeof (*uc)))) {
		np_uerror (ENOMEM);
		return -1;
	}
	uc->users = NULL;
	pthread_mutex_init (&uc->lock, NULL);
	uc->ttl	= 60;
	srv->usercache = uc;

	if (!np_ctl_addfile (srv->ctlroot, "usercache", _get_usercache, srv)) {
		free (srv->usercache);
		return -1;
	}

	return 0;
}

void
np_usercache_destroy (Npsrv *srv)
{
	Npusercache *uc;
	Npuser *u;

	assert (srv->usercache != NULL);
	uc = srv->usercache;

	u = uc->users;
	while (u)
		u = _usercache_del (srv, NULL, u);
	free (uc);
	srv->usercache = NULL;
}

static void
_free_user (Npuser *u)
{
	if (u->uname)
		free (u->uname);
	if (u->sg)
		free (u->sg);
	free (u);
}

void
np_user_incref(Npuser *u)
{
	if (!u)
		return;

	pthread_mutex_lock (&u->lock);
	u->refcount++;
	pthread_mutex_unlock (&u->lock);
}

void
np_user_decref(Npuser *u)
{
	if (!u)
		return;

	pthread_mutex_lock (&u->lock);
	u->refcount--;
	if (u->refcount > 0) {
		pthread_mutex_unlock (&u->lock);
		return;
	}

	pthread_mutex_destroy(&u->lock);
	_free_user (u);
}

/* This needs to be called with the usercache lock held.
 * I don't think it's thread safe. -jg
 */
static int
_getgrouplist (Npsrv *srv, Npuser *u)
{
	int i, ret = -1;
	gid_t *sgcpy;

	u->nsg = sysconf(_SC_NGROUPS_MAX);
	if (u->nsg < 65536)
		u->nsg = 65536;
	if (!(u->sg = malloc (u->nsg * sizeof (gid_t)))) {
		np_uerror (ENOMEM);
		np_logerr (srv, "_alloc_user: %s", u->uname);
		goto done;
	}
	if (getgrouplist(u->uname, u->gid, u->sg, &u->nsg) == -1) {
		np_logerr (srv, "_alloc_user: %s: getgrouplist", u->uname);
		if (np_rerror () == 0)
			np_uerror (EPERM);
		goto done;
	}
	if ((sgcpy = malloc (u->nsg * sizeof (gid_t)))) {
		for (i = 0; i < u->nsg; i++)
			sgcpy[i] = u->sg[i];
		free (u->sg);
		u->sg = sgcpy;
	}
	ret = 0;
done:
	return ret;
}

static Npuser *
_alloc_user (Npsrv *srv, struct passwd *pwd)
{
	Npuser *u;

	if (!(u = malloc (sizeof (*u)))) {
		np_uerror (ENOMEM);
		np_logerr (srv, "_alloc_user: %s", pwd->pw_name);
		goto error;
	}
	u->sg = NULL;
	u->nsg = 0;
	if (!(u->uname = strdup (pwd->pw_name))) {
		np_uerror (ENOMEM);
		np_logerr (srv, "_alloc_user: %s", pwd->pw_name);
		goto error;
	}
	u->uid = pwd->pw_uid;
	u->gid = pwd->pw_gid;
	if (u->uid != 0 && _getgrouplist(srv, u) < 0)
		goto error;
	pthread_mutex_init (&u->lock, NULL);
	u->refcount = 0;
	u->t = time (NULL);
	u->next = NULL;
	if (srv->flags & SRV_FLAGS_DEBUG_USER)
		np_logmsg (srv, "user lookup: %d", u->uid);
	return u;
error:
	if (u)
		_free_user (u);
	return NULL; 
}

/* Create an Npuser struct for a user, without requiring
 * that user to be in the password/group file.
 * N.B. gid is assumed to be same as uid.
 */
static Npuser *
_alloc_nouserdb (Npsrv *srv, uid_t uid, char *name)
{
	Npuser *u = NULL;
	char ustr[32] = "root";

	if (name) {
		if (strcmp (name, "root") != 0) {
			np_uerror (EPERM);
			goto error;
		}
		uid = 0;
	}
	if (uid != 0)
		snprintf (ustr, sizeof (ustr), "%d", uid);
	if (!(u = malloc (sizeof (*u)))) {
		np_uerror (ENOMEM);
		np_logerr (srv, "_alloc_nouserdb: %s", ustr);
		goto error;
	}
	u->sg = NULL;
	if (!(u->uname = strdup (ustr))) {
		np_uerror (ENOMEM);
		np_logerr (srv, "_alloc_nouserdb: %s", ustr);
		goto error;
	}
	u->uid = uid;
	u->gid = (gid_t)uid;
	u->nsg = 1;
	if (!(u->sg = malloc (sizeof (gid_t) * u->nsg))) {
		np_uerror (ENOMEM);
		np_logerr (srv, "_alloc_nouserdb: %s", ustr);
		goto error;
	}
	u->sg[0] = u->gid;
	pthread_mutex_init (&u->lock, NULL);
	if (srv->flags & SRV_FLAGS_DEBUG_USER)
		np_logmsg (srv, "user lookup: %d", u->uid);
	u->refcount = 0;
	u->t = time (NULL);
	u->next = NULL;
	return u;
error:
	if (u)
		_free_user (u);
	return NULL;
}

static Npuser *
_real_lookup_byuid (Npsrv *srv, uid_t uid)
{
	Npuser *u = NULL;
	int err, len;
	struct passwd pw, *pwd;
	char *buf = NULL; 

	if (srv->flags & SRV_FLAGS_NOUSERDB) {
		if (!(u = _alloc_nouserdb (srv, uid, NULL)))
			goto error;
	} else {
		len = sysconf(_SC_GETPW_R_SIZE_MAX);
		if (len < 4096)
			len = 4096;
		if (!(buf = malloc (len))) {
			np_uerror (ENOMEM);
			np_logerr (srv, "uid2user");
			goto error;
		}
		if ((err = getpwuid_r (uid, &pw, buf, len, &pwd)) != 0) {
			np_uerror (err);
			np_logerr (srv, "uid2user: unable to lookup %d", uid);
			goto error;
		}
		if (!pwd) {
			np_logmsg (srv, "uid2user: unable to lookup %d", uid);
			np_uerror (EPERM);
			goto error;
		}
		if (!(u = _alloc_user (srv, pwd)))
			goto error;
		free (buf);
	}
	return u;
error:
	if (u)
		_free_user (u);
	if (buf)
		free (buf);
	return NULL;
}

static Npuser *
_real_lookup_byname (Npsrv *srv, char *uname)
{
	Npuser *u = NULL; 
	int err, len;
	struct passwd pw, *pwd = NULL;
	char *buf = NULL;

	if (srv->flags & SRV_FLAGS_NOUSERDB) {
		if (!(u = _alloc_nouserdb (srv, P9_NONUNAME, uname)))
			goto error;
	} else {
		len= sysconf(_SC_GETPW_R_SIZE_MAX);
		if (len < 4096)
			len = 4096;
		if (!(buf = malloc (len))) {
			np_uerror (ENOMEM);
			np_logerr (srv, "uname2user: %s", uname);
			goto error;
		}
		if ((err = getpwnam_r (uname, &pw, buf, len, &pwd)) != 0) {
			np_uerror (err);
			np_logerr (srv, "uname2user: %s: getpwnam_r", uname);
			goto error;
		}
		if (!pwd) {
			np_logmsg (srv, "uname2user: %s lookup failure", uname);
			np_uerror (EPERM);
			goto error;
		}
		if (!(u = _alloc_user (srv, pwd)))
			goto error;
		free (buf);
	}
	return u;
error:
	if (u)
		_free_user (u);
	if (buf)
		free (buf);
	return NULL;
}


Npuser *
np_uname2user (Npsrv *srv, char *uname)
{
	Npusercache *uc = srv->usercache;
	Npuser *u = NULL;

	pthread_mutex_lock (&uc->lock);
	if (!(u = _usercache_lookup (srv, uname, P9_NONUNAME)))
		if ((u = _real_lookup_byname (srv, uname)))
			_usercache_add (srv, u);
	pthread_mutex_unlock (&uc->lock);
	if (u)
		np_user_incref (u);
	return u;
}

Npuser *
np_uid2user (Npsrv *srv, uid_t uid)
{
	Npusercache *uc = srv->usercache;
	Npuser *u = NULL;

	pthread_mutex_lock (&uc->lock);
	if (!(u = _usercache_lookup (srv, NULL, uid)))
		if ((u = _real_lookup_byuid (srv, uid)))
			_usercache_add (srv, u);
	pthread_mutex_unlock (&uc->lock);
	if (u)
		np_user_incref (u);
	return u;
}

void
np_usercache_flush (Npsrv *srv)
{
	Npusercache *uc = srv->usercache;
	Npuser *u;

	pthread_mutex_lock (&uc->lock);
	u = uc->users;
	while (u)
		u = _usercache_del (srv, NULL, u);
	pthread_mutex_unlock (&uc->lock);
}

Npuser *
np_attach2user (Npsrv *srv, Npstr *uname, u32 n_uname)
{
	Npuser *u = NULL;
	char *s;

	if (n_uname != P9_NONUNAME) {
		u = np_uid2user (srv, n_uname);
	} else {
		if (uname->len == 0) {
			np_uerror (EIO);
			goto done;
		}
		s = np_strdup (uname);
		if (!s) {
			np_uerror (ENOMEM);
			goto done;
		}
		u = np_uname2user (srv, s);
		free (s);
	}
done:
	return u;
}

/* Take another reference on afid->user and return it.
 * If afid was for a different user return NULL.
 */
Npuser *
np_afid2user (Npfid *afid, Npstr *uname, u32 n_uname)
{
	Npuser *u = NULL;

	if (n_uname != P9_NONUNAME) {
		if (n_uname != afid->user->uid) {
			np_uerror (EPERM);
			goto done;
		}
	} else {
		if (np_strcmp (uname, afid->user->uname) != 0) {
			np_uerror (EPERM);
			goto done;
		}
	}
	u = afid->user;
	np_user_incref (u);
done:
	return u;
}

/* Note: it is possible for setfsuid/setfsgid to fail silently,
 * e.g. if user doesn't have CAP_SETUID/CAP_SETGID.
 */
int
np_setfsid (Npreq *req, Npuser *u, u32 gid_override)
{
	Npwthread *wt = req->wthread;
	Npsrv *srv = req->conn->srv;
	int i, ret = -1;
	u32 gid;

	if ((srv->flags & SRV_FLAGS_SETFSID)) {
		if (gid_override != -1 && u->uid != 0
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
			gid_t ret;

			if ((ret = setfsgid (gid)) < 0) {
				np_uerror (errno);
				np_logerr (srv, "setfsgid(%s) gid=%d failed",
					   u->uname, gid);
				wt->fsgid = P9_NONUNAME;
				goto done;
			}
			if (ret != wt->fsgid) {
				np_uerror (errno);
				np_logerr (srv, "setfsgid(%s) gid=%d failed"
					   "returned %d, expected %d",
					   u->uname, gid, ret, wt->fsgid);
				wt->fsgid = P9_NONUNAME;
				goto done;
			}
			wt->fsgid = gid;
		}
		/* Supplemental groups shouldn't matter for root.
		 */
		if (u->uid != 0 && wt->sguid != u->uid) {
			if (setgroups (u->nsg, u->sg) < 0) {
				np_uerror (errno);
				np_logerr (srv, "setgroups(%s) nsg=%d failed",
					   u->uname, u->nsg);
				wt->sguid = P9_NONUNAME;
				goto done;
			}
			wt->sguid = u->uid;
		}
		if (wt->fsuid != u->uid) {
			uid_t ret;

			if ((ret = setfsuid (u->uid)) < 0) {
				np_uerror (errno);
				np_logerr (srv, "setfsuid(%s) uid=%d failed",
					   u->uname, u->uid);
				wt->fsuid = P9_NONUNAME;
				goto done;
			}
			if (ret != wt->fsuid) {
				np_uerror (EPERM);
				np_logerr (srv, "setfsuid(%s) uid=%d failed: "
					   "returned %d, expected %d",
					   u->uname, u->uid, ret, wt->fsuid);
				wt->fsuid = P9_NONUNAME;
				goto done;
			}
			wt->fsuid = u->uid;
		}
	}
	ret = 0;
done:
	return ret;
}
