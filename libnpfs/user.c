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

static void
_free_user (Npuser *u)
{
	if (u->uname)
		free (u->uname);
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

static Npuser *
_alloc_user (struct passwd *pwd)
{
	Npuser *u;
	int err;

	if (!(u = malloc (sizeof (*u)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	if (!(u->uname = strdup (pwd->pw_name))) {
		np_uerror (ENOMEM);
		goto error;
	}
	u->uid = pwd->pw_uid;
	u->nsg = sizeof (u->sg) / sizeof (u->sg[0]);
	if (getgrouplist(pwd->pw_name, pwd->pw_gid, u->sg, &u->nsg) == -1) {
		np_uerror (EPERM); /* user is in too many groups */
		goto error;		
	}
	if ((err = pthread_mutex_init (&u->lock, NULL)) != 0) {
		np_uerror (err);
		goto error;
	}
	u->refcount = 0;
	return u;
error:
	if (u)
		_free_user (u);
	return NULL; 
}

Npuser *
np_uid2user (uid_t uid)
{
	Npuser *u = NULL; 
	int err;
	struct passwd pw, *pwd;
	int len = sysconf(_SC_GETPW_R_SIZE_MAX);
	char *buf = NULL; 

	if (len == -1)
		len = 4096;
	if (!(buf = malloc (len))) {
		np_uerror (ENOMEM);
		goto error;
	}
	if ((err = getpwuid_r (uid, &pw, buf, len, &pwd)) != 0) {
		np_uerror (err);
		goto error;
	}
	if (!pwd) {
		np_uerror (EPERM);
		goto error;
	}
	if (!(u = _alloc_user (pwd)))
		goto error;
	free (buf);
	np_user_incref (u);
	return u;
error:
	if (u)
		_free_user (u);
	if (buf)
		free (buf);
	return NULL;
}

Npuser *
np_uname2user (char *uname)
{
	Npuser *u = NULL; 
	int err;
	struct passwd pw, *pwd = NULL;
	int len = sysconf(_SC_GETPW_R_SIZE_MAX);
	char *buf = NULL;

	if (len == -1)
		len = 4096;
	if (!(buf = malloc (len))) {
		np_uerror (ENOMEM);
		goto error;
	}
	if ((err = getpwnam_r (uname, &pw, buf, len, &pwd)) != 0) {
		np_uerror (err);
		goto error;
	}
	if (!pwd) {
		np_uerror (EPERM);
		goto error;
	}
	if (!(u = _alloc_user (pwd)))
		goto error;
	free (buf);
	np_user_incref (u);
	return u;
error:
	if (u)
		_free_user (u);
	if (buf)
		free (buf);
	return NULL;
}

Npuser *
np_attach2user (Npsrv *srv, Npstr *uname, u32 n_uname)
{
	Npuser *u = NULL;
	char *s;

	if (n_uname == P9_NONUNAME && uname->len == 0) {
		if (srv->msg)
			srv->msg ("auth/attach: no uname or n_uname");
		np_uerror (EIO);
		goto done;
	}
	if (uname->len > 0) {
		if (!(s = np_strdup (uname))) {
			np_uerror (ENOMEM);
			if (srv->msg)
				srv->msg ("auth/attach: out of memory");
			goto done;
		}
		u = np_uname2user (s);
		if (!u && srv->msg)
			srv->msg ("auth/attach failed to look up user %s", s);
		free (s);
	} else {
		u = np_uid2user (n_uname);
		if (!u && srv->msg)
			srv->msg ("auth/attach failed to look up uid %d", n_uname);
	}
done:
	return u;
}

int
np_setfsid (Npreq *req, Npuser *u, u32 gid_override)
{
	Npwthread *wt = req->wthread;
	Npsrv *srv = req->conn->srv;
	int ret = -1;
	u32 gid;

	if ((wt->flags & WT_FLAGS_SETFSID)) {
		gid = (gid_override == -1 ? u->gid : gid_override);
		if (wt->fsgid != gid) {
			if (setfsgid (gid) < 0) {
				np_uerror (errno);
				if (srv->msg)
					srv->msg ("setfsgid(%s) gid=%d failed",
						  u->uname, gid);
				goto done;
			}
			wt->fsgid = gid;
		}
		if (wt->fsuid != u->uid) {
			if (setgroups (u->nsg, u->sg) < 0) {
				np_uerror (errno);
				if (srv->msg)
					srv->msg ("setgroups(%s) nsg=%d failed",
						  u->uname, u->nsg);
				goto done;
			}
			if (setfsuid (u->uid) < 0) {
				np_uerror (errno);
				if (srv->msg)
					srv->msg ("setfsuid(%s) uid=%d failed",
						  u->uname, u->uid);
				goto done;
			}
			wt->fsuid = u->uid;
		}
	}
	ret = 0;
done:
	return ret;
}
