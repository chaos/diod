/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* hardwired to UNIX scheme -jg */

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

/* N.B. the usercache lock also protects getgrouplist (), libc calls into
 * nss code, etc. that may or may not be thread safe.
 */
static struct Npusercache {
        pthread_mutex_t lock;
        Npuser* users;
} usercache = { PTHREAD_MUTEX_INITIALIZER, NULL };
static const int usercache_ttl = 60;

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

	pthread_mutex_lock(&u->lock);
	u->refcount++;
	pthread_mutex_unlock(&u->lock);
}

void
np_user_decref(Npuser *u)
{
	if (!u)
		return;

	pthread_mutex_lock(&u->lock);
	u->refcount--;
	if (u->refcount > 0) {
		pthread_mutex_unlock(&u->lock);
		return;
	}

	pthread_mutex_destroy(&u->lock);
	_free_user (u);
}

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
	if (!(u->uname = strdup (pwd->pw_name))) {
		np_uerror (ENOMEM);
		np_logerr (srv, "_alloc_user: %s", pwd->pw_name);
		goto error;
	}
	u->uid = pwd->pw_uid;
	u->gid = pwd->pw_gid;
	if (_getgrouplist(srv, u) < 0)
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

static Npuser *
_real_lookup_byuid (Npsrv *srv, uid_t uid)
{
	Npuser *u = NULL;
	int err, len;
	struct passwd pw, *pwd;
	char *buf = NULL; 

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
	return u;
error:
	if (u)
		_free_user (u);
	if (buf)
		free (buf);
	return NULL;
}

static void
_usercache_add (Npsrv *srv, Npuser *u)
{
	u->next = usercache.users;
	usercache.users = u;
	np_user_incref (u);
}

static Npuser *
_usercache_del (Npsrv *srv, Npuser *prev, Npuser *u)
{
	Npuser *tmp = u->next;

	if (prev)
		prev->next = tmp;
	else
		usercache.users = tmp;
	np_user_decref (u);

	return tmp;
}

static Npuser *
_usercache_lookup (Npsrv *srv, char *uname, uid_t uid)
{
	time_t now = time (NULL);
	Npuser *u = usercache.users;
	Npuser *prev = NULL;

	while (u) {
		if (now - u->t >= usercache_ttl) {
			u = _usercache_del (srv, prev, u);
			continue;
		}
		if (uname && !strcmp (uname, u->uname)) 
			break;
		if (!uname && uid == u->uid)
			break;
		prev = u;
		u = u->next;
	}
	return u;
}

Npuser *
np_uname2user (Npsrv *srv, char *uname)
{
	Npuser *u = NULL;
	int err;

	if ((err = pthread_mutex_lock (&usercache.lock))) {
		np_uerror (err);
		np_logerr (srv, "uname2user: unable to lock usercache");
		goto error;
	}
	if (!(u = _usercache_lookup (srv, uname, P9_NONUNAME)))
		if ((u = _real_lookup_byname (srv, uname)))
			_usercache_add (srv, u);
	if ((err = pthread_mutex_unlock (&usercache.lock))) {
		np_uerror (err);
		np_logerr (srv, "uname2user: unable to unlock usercache");
		goto error;
	}
	if (u)
		np_user_incref (u);
	return u;
error:
	return NULL;
}

Npuser *
np_uid2user (Npsrv *srv, uid_t uid)
{
	Npuser *u = NULL;
	int err;

	if ((err = pthread_mutex_lock (&usercache.lock))) {
		np_uerror (err);
		np_logerr (srv, "uid2user: unable to lock usercache");
		goto error;
	}
	if (!(u = _usercache_lookup (srv, NULL, uid)))
		if ((u = _real_lookup_byuid (srv, uid)))
			_usercache_add (srv, u);
	if ((err = pthread_mutex_unlock (&usercache.lock))) {
		np_uerror (err);
		np_logerr (srv, "uid2user: unable to unlock usercache");
		goto error;
	}
	if (u)
		np_user_incref (u);
	return u;
error:
	return NULL;
}

void
np_usercache_flush (Npsrv *srv)
{
	Npuser *u;
	int err;

	if ((err = pthread_mutex_lock (&usercache.lock))) {
		np_uerror (err);
		np_logerr (srv, "usercache_flush: unable to lock usercache");
		return;
	}
	u = usercache.users;
	while (u)
		u = _usercache_del (srv, NULL, u);
	if ((err = pthread_mutex_unlock (&usercache.lock))) {
		np_uerror (err);
		np_logerr (srv, "usercache_flush: unable to unlock usercache");
		return;
	}
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
	uid_t ret_u, exp_u;
	gid_t ret_g, exp_g;
	int ret = -1;
	u32 gid;

	if ((srv->flags & SRV_FLAGS_SETFSID)) {
		gid = (gid_override == -1 ? u->gid : gid_override);
		if (wt->fsgid != gid) {
			exp_g = wt->fsgid == P9_NONUNAME ? 0 : wt->fsgid;
			if ((ret_g = setfsgid (gid)) < 0) {
				np_uerror (errno);
				np_logerr (srv, "setfsgid(%s) gid=%d failed",
					   u->uname, gid);
				goto done;
			}
			if (ret_g != exp_g) {
				np_uerror (errno);
				np_logerr (srv, "setfsgid(%s) gid=%d failed"
					   "returned %d, expected %d",
					   u->uname, gid, ret_g, exp_g);
				goto done;
			}
			wt->fsgid = gid;
		}
		if (wt->fsuid != u->uid) {
			if (setgroups (u->nsg, u->sg) < 0) {
				np_uerror (errno);
				np_logerr (srv, "setgroups(%s) nsg=%d failed",
					   u->uname, u->nsg);
				goto done;
			}
			exp_u = wt->fsuid == P9_NONUNAME ? 0 : wt->fsuid;
			if ((ret_u = setfsuid (u->uid)) < 0) {
				np_uerror (errno);
				np_logerr (srv, "setfsuid(%s) uid=%d failed",
					   u->uname, u->uid);
				goto done;
			}
			if (ret_u != exp_u) {
				np_uerror (EPERM);
				np_logerr (srv, "setfsuid(%s) uid=%d failed: "
					   "returned %d, expected %d",
					   u->uname, u->uid, ret_u, exp_u);
				goto done;
			}
			wt->fsuid = u->uid;
		}
	}
	ret = 0;
done:
	return ret;
}
