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
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"

Npfcall *
np_version(Npreq *req, Npfcall *tc)
{
	Npsrv *srv = req->conn->srv;
	Npfcall *rc = NULL;
	int msize = tc->u.tversion.msize;

	if (msize < P9_IOHDRSZ + 1) {
		np_uerror(EIO);
		np_logerr(srv, "version: msize is too small");
		return NULL;
	}
	if (msize > req->conn->msize)
		msize = req->conn->msize;
	if (msize < req->conn->msize)
		req->conn->msize = msize; /* conn->msize can only be reduced */
	if (np_strcmp(&tc->u.tversion.version, "9P2000.L") == 0) {
		if (!(rc = np_create_rversion(msize, "9P2000.L"))) {
			np_uerror(ENOMEM);
			np_logerr(srv, "version: out of memory");
		}
	} else {
		np_uerror(EIO);
		np_logerr(srv, "version: unsupported version");
	}
	return rc;
}

static int
_authrequired (Npsrv *srv, Npstr *uname, u32 n_uname, Npstr *aname)
{
	if (!srv->auth || !srv->auth->startauth || !srv->auth->checkauth)
		return 0;
	if (!srv->auth_required)
		return 1;
	return srv->auth_required (uname, n_uname, aname);
}

Npfcall *
np_auth(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npsrv *srv = conn->srv;
	Npfid *afid = req->fid;
	Npfcall *rc = NULL;
	Npqid aqid;
	char a[128];
	int auth_required = _authrequired(srv, &tc->u.tauth.uname,
					       tc->u.tauth.n_uname,
					       &tc->u.tauth.aname);

	if (tc->u.tauth.n_uname != P9_NONUNAME) {
		snprintf (a, sizeof(a), "auth(%d@%s:%.*s)",
			  tc->u.tauth.n_uname,
			  np_conn_get_client_id (conn), 
			  tc->u.tauth.aname.len, tc->u.tauth.aname.str);
	} else {
		snprintf (a, sizeof(a), "auth(%.*s@%s:%.*s)",
			  tc->u.tauth.uname.len, tc->u.tauth.uname.str,
			  np_conn_get_client_id (conn),
			  tc->u.tauth.aname.len, tc->u.tauth.aname.str);
	}
	if (!auth_required) {
		if (!(rc = np_create_rlerror(ENOENT))) {
			np_uerror(ENOMEM);
			np_logerr (srv, "%s: creating response", a);
		}
		goto error;
	}
	if (!afid) {
		np_uerror (EIO);
		np_logerr (srv, "%s: invalid afid (%d)", a, tc->u.tauth.afid);
		goto error;
	}
	np_fid_incref(afid);
	if (!(afid->user = np_attach2user (srv, &tc->u.tauth.uname,
				     		 tc->u.tauth.n_uname))) {
		np_logerr (srv, "%s: user lookup", a);
		goto error;
	}
	afid->type = P9_QTAUTH;
	if (!srv->auth->startauth(afid, afid->aname, &aqid)) {
		np_logerr (srv, "%s: startauth", a);
		goto error;
	}
	NP_ASSERT((aqid.type & P9_QTAUTH));
	if (!(rc = np_create_rauth(&aqid))) {
		np_uerror(ENOMEM);
		np_logerr (srv, "%s: creating response", a);
		goto error;
	}
error:
	return rc;
}

Npfcall *
np_attach(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npsrv *srv = conn->srv;
	Npfid *fid = req->fid;
	Npfid *afid = NULL;
	Npfcall *rc = NULL;
	char a[128];
	int auth_required = _authrequired(srv, &tc->u.tattach.uname,
					       tc->u.tattach.n_uname,
					       &tc->u.tattach.aname);

	if (tc->u.tattach.n_uname != P9_NONUNAME) {
		snprintf (a, sizeof(a), "attach(%d@%s:%.*s)",
			  tc->u.tattach.n_uname,
			  np_conn_get_client_id (conn), 
			  tc->u.tattach.aname.len, tc->u.tattach.aname.str);
	} else {
		snprintf (a, sizeof(a), "attach(%.*s@%s:%.*s)",
			  tc->u.tattach.uname.len, tc->u.tattach.uname.str,
			  np_conn_get_client_id (conn),
			  tc->u.tattach.aname.len, tc->u.tattach.aname.str);
	}
	if (!fid) {
		np_uerror (EIO);
		np_logerr (srv, "%s: invalid fid (%d)", a, tc->u.tattach.fid);
		goto error;
	}
	if (tc->u.tattach.afid != P9_NOFID) {
		if (!(afid = np_fid_find(conn, tc->u.tattach.afid))) {
			np_uerror(EPERM);
			np_logerr (srv, "%s: invalid afid (%d)", a,
				   tc->u.tattach.afid);
			goto error;
		}
		if (!(afid->type & P9_QTAUTH)) {
			np_uerror(EPERM);
			np_logerr (srv, "%s: invalid afid type", a);
			goto error;
		}
	}
	if (auth_required) {
		if (afid) {
			fid->user = np_afid2user (afid, &tc->u.tattach.uname,
						  tc->u.tattach.n_uname);
			if (!fid->user) {
				np_logerr (srv, "%s: invalid afid user", a);
				goto error;
			}
			if (srv->auth->checkauth(fid, afid, fid->aname) == 0) {
				np_logerr (srv, "%s: checkauth", a);
				goto error;
			}
			np_conn_set_authuser(conn, fid->user->uid);
		} else {
			u32 uid;

			fid->user = np_attach2user (srv, &tc->u.tattach.uname,
						    tc->u.tattach.n_uname);
			if (!fid->user) {
				np_logerr (srv, "%s: user lookup", a);
				goto error;
			}
			if (!(srv->flags & SRV_FLAGS_AUTHCONN)) {
				np_uerror(EPERM);
				np_logerr (srv, "%s: auth required", a);
				goto error;
			}
			if (np_conn_get_authuser(conn, &uid) < 0) {
				np_uerror(EPERM);
				np_logerr (srv, "%s: prior auth required", a);
				goto error;
			}
			if (uid != 0 && uid != fid->user->uid) {
				np_uerror(EPERM);
				np_logerr (srv, "%s: insufficient auth", a);
				goto error;
			}
		}
	} else {
		if (!fid->user) {
			fid->user = np_attach2user (srv, &tc->u.tattach.uname,
						    tc->u.tattach.n_uname);
			if (!fid->user) {
				np_logerr (srv, "%s: user lookup", a);
				goto error;
			}
		}
		if (fid->user->uid == 0)
			np_conn_set_authuser(conn, fid->user->uid);
	}

	/* This is a hook for the server implementation to tweak
	 * fid->user according to its policy.  For example,
	 * it can remap all users to a "squash user".
	 */
	if (srv->remapuser) {
		if (srv->remapuser(fid) < 0) {
			np_logerr (srv, "%s: error remapping user", a);
			goto error;
		}
	}
	/* This is a hook for the server to check the aname against a
	 * list of exports and deny the attach.  On success, as a side-effect,
	 * it may set fid->flags, for example if an aname is exported read-only.
	 * It will be called for "ctl" as well as for any paths that
	 * the server implementation will subsequently receive an attach for.
	 */
	if (srv->exportok) {
		if (!srv->exportok(fid)) {
			np_logerr (srv, "%s: access denied for export", a);
			np_uerror (EPERM);
			goto error;
		}
	}
	if (!strcmp (fid->aname, "ctl")) {
		rc = np_ctl_attach (fid, afid, fid->aname);
	} else {
		if (!srv->attach) {
			np_uerror (ENOSYS);
			goto error;
		}
		rc = (*srv->attach)(fid, afid, &tc->u.tattach.aname);
	}
	if (rc)
		np_fid_incref (fid);
error:
	if (afid)
		np_fid_decref (&afid);
	return rc;
}

int
np_flush(Npreq *req, Npfcall *tc)
{
	u16 oldtag = tc->u.tflush.oldtag;
	Npreq *creq;
	int ret = 1;
	Nptpool *tp;
	Npsrv *srv = req->conn->srv;

	xpthread_mutex_lock(&req->conn->srv->lock);
	for (tp = req->conn->srv->tpool; tp != NULL; tp = tp->next) {
		for(creq = tp->reqs_first; creq != NULL; creq = creq->next) {
			if (!(creq->conn==req->conn && creq->tag==oldtag))
				continue;
			if ((srv->flags & SRV_FLAGS_DEBUG_FLUSH)) {
				np_logmsg (srv, "flush(early): req type %d",
					   creq->tcall->type);
			}
			np_srv_remove_req(tp, creq);
			np_req_unref(creq);
			goto done;
		}
		for(creq = tp->workreqs; creq != NULL; creq = creq->next) {
			if (!(creq->conn==req->conn && creq->tag==oldtag))
				continue;
			if ((srv->flags & SRV_FLAGS_DEBUG_FLUSH)) {
				np_logmsg (srv, "flush(late): req type %d",
					   creq->tcall->type);
			}
			/* only the most recent flush must be responded to */
			if (creq->flushreq)
				np_req_unref(creq->flushreq);
			creq->flushreq = req;
			ret = 0; /* reply is delayed until after req */
			if (req->conn->srv->flags & SRV_FLAGS_FLUSHSIG)
				pthread_kill (creq->wthread->thread, SIGUSR2);
			goto done;
		}
	}
	if ((srv->flags & SRV_FLAGS_DEBUG_FLUSH))
		np_logmsg (srv, "flush: tag %d not found", oldtag);
done:
	xpthread_mutex_unlock(&req->conn->srv->lock);
	return ret;
}

Npfcall *
np_walk(Npreq *req, Npfcall *tc)
{
	int i;
	Npconn *conn = req->conn;
	Npfid *fid = req->fid;
	Npfid *newfid = NULL;
	Npfcall *rc = NULL;
	Npqid wqids[P9_MAXWELEM];

	if (!fid) {
		np_uerror (EIO);
		np_logerr (conn->srv, "walk: invalid fid");
		goto done;
	}
#if 0
	if (!(fid->type & P9_QTDIR)) {
		np_uerror(ENOTDIR);
		goto done;
	}
#endif
	/* FIXME: error if fid has been opened */

	if (tc->u.twalk.nwname > P9_MAXWELEM) {
		np_uerror(EIO);
		np_logerr (conn->srv, "walk: too many elements");
		goto done;
	}

	if (tc->u.twalk.fid != tc->u.twalk.newfid) {
		newfid = np_fid_create(conn, tc->u.twalk.newfid);
		if (!newfid) {
			if (np_rerror () == EEXIST) {
				np_uerror(EIO);
				np_logmsg (conn->srv,
					   "%s@%s:%s: walk: invalid newfid: %d",
					   fid->user->uname,
					   np_conn_get_client_id (conn),
					   fid->aname, tc->u.twalk.newfid);
			}
			goto done;
		}
		if (fid->type & P9_QTTMP) {
			if (!np_ctl_clone (fid, newfid))
				goto done;
		} else {
			if (!conn->srv->clone) {
				np_uerror (ENOSYS);
				goto done;
			}
			else if (!(*conn->srv->clone)(fid, newfid))
				goto done;
		}
		np_user_incref(fid->user);
		newfid->user = fid->user;
		np_tpool_incref(fid->tpool);
		newfid->tpool = fid->tpool;
		newfid->type = fid->type;
		newfid->flags = fid->flags;
		if (!(newfid->aname = strdup (fid->aname))) {
			np_uerror (ENOMEM);
			np_logerr (conn->srv, "walk: out of memory");
			goto done;
		}
	} else
		newfid = fid;

	if (!(newfid->type & P9_QTTMP)) {
		if (np_setfsid (req, newfid->user, -1) < 0)
			goto done;
	}
	for(i = 0; i < tc->u.twalk.nwname;) {
		if (newfid->type & P9_QTTMP) {
			if (!np_ctl_walk (newfid, &tc->u.twalk.wnames[i],
					  &wqids[i]))
				break;
		} else {
			if (!conn->srv->walk) {
				np_uerror (ENOSYS);
				break;
			}
			if (!(*conn->srv->walk)(newfid, &tc->u.twalk.wnames[i],
						&wqids[i]))
				break;
		}
		newfid->type = wqids[i].type;
		i++;
		if (i<(tc->u.twalk.nwname) && !(newfid->type & P9_QTDIR))
			break;
	}

	if (i == 0 && tc->u.twalk.nwname != 0)
		goto done;

	np_uerror(0);
	rc = np_create_rwalk(i, wqids);
done:
	if (!rc && tc->u.twalk.fid != tc->u.twalk.newfid && newfid != NULL)
		np_fid_decref (&newfid);
	return rc;
}

Npfcall *
np_read(Npreq *req, Npfcall *tc)
{
	int n;
	Npconn *conn = req->conn;
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (conn->srv, "read: invalid fid");
		goto done;
	}
	if (tc->u.tread.count + P9_IOHDRSZ > conn->msize) {
		np_uerror(EIO);
		np_logerr (conn->srv, "read: count %u too large",
			   tc->u.tread.count);
		goto done;
	}

	if (fid->type & P9_QTAUTH) {
		if (conn->srv->auth) {
			rc = np_alloc_rread(tc->u.tread.count);
			if (!rc)
				goto done;

			n = conn->srv->auth->read(fid, tc->u.tread.offset, tc->u.tread.count, rc->u.rread.data);
			if (n >= 0) 
				np_set_rread_count(rc, n);
			else {
				free(rc);
				rc = NULL;
			}
		} else
			np_uerror(ENOSYS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		rc = np_ctl_read(fid, tc->u.tread.offset,
				 tc->u.tread.count, req);
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!conn->srv->read) {
			np_uerror(ENOSYS);
			goto done;
		}
		rc = (*conn->srv->read)(fid, tc->u.tread.offset,
					tc->u.tread.count, req);
	}

done:
	return rc;
}

Npfcall *
np_write(Npreq *req, Npfcall *tc)
{
	int n;
	Npconn *conn = req->conn;
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (conn->srv, "write: invalid fid");
		goto done;
	}
	if (fid->type & P9_QTAUTH) {
		if (conn->srv->auth) {
			n = conn->srv->auth->write(fid, tc->u.twrite.offset,
				tc->u.twrite.count, tc->u.twrite.data);
			if (n >= 0)
				if (!(rc = np_create_rwrite(n)))
					np_uerror(ENOMEM);

		} else
			np_uerror(ENOSYS);
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (tc->u.twrite.count + P9_IOHDRSZ > conn->msize) {
		np_uerror(EIO);
		np_logerr (conn->srv, "write: count %u too large",
			   tc->u.twrite.count);
		goto done;
	}

	if (fid->type & P9_QTTMP) {
		rc = np_ctl_write(fid, tc->u.twrite.offset,
				  tc->u.twrite.count,
				  tc->u.twrite.data, req);
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!conn->srv->write) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*conn->srv->write)(fid, tc->u.twrite.offset,
					 tc->u.twrite.count,
					 tc->u.twrite.data, req);
	}
done:
	return rc;
}

Npfcall *
np_clunk(Npreq *req, Npfcall *tc)
{
	Npfcall *rc = NULL;

	if (!req->fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "%s: clunk: invalid fid: %d",
					   np_conn_get_client_id (req->conn),
					   tc->u.tclunk.fid);
		goto done;
	}
	if (req->fid->type & P9_QTAUTH) {
		if (req->conn->srv->auth) {
			/* N.B. fidpool calls auth->clunk on last decref */
			if (!(rc = np_create_rclunk()))
				np_uerror(ENOMEM);
		} else
			np_uerror(ENOSYS);
		goto done;
	}
	if (req->fid->type & P9_QTTMP) {
		rc = np_ctl_clunk (req->fid);
	} else {
		if (!req->conn->srv->clunk) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->clunk)(req->fid);
	}
done:
	/* From clunk(5):
	 * even if the clunk returns an error, the fid is no longer valid.
	 */
	if (req->fid)
		np_fid_decref (&req->fid);
	return rc;
}

Npfcall *
np_remove(Npreq *req, Npfcall *tc)
{
	Npfcall *rc = NULL;

	if (!req->fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "remove: invalid fid");
		goto done;
	}
	if (req->fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (req->fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, req->fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->remove) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->remove)(req->fid);
	}
done:
	/* spec says clunk the fid even if the remove fails
   	 */
	if (req->fid)
		np_fid_decref (&req->fid);
	return rc;
}

Npfcall *
np_statfs(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "statfs: invalid fid");
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (ENOSYS);
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->statfs) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->statfs)(fid);
	}
done:
	return rc;
}

Npfcall *
np_lopen(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "lopen: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		int flags = tc->u.tlopen.flags;
		if ((flags & O_WRONLY) || (flags & O_RDWR)) {
			np_uerror(EROFS);
			goto done;
		}
	}
	if (fid->type & P9_QTTMP) {
		rc = np_ctl_lopen (fid, tc->u.tlopen.flags);
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->lopen) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->lopen)(fid, tc->u.tlopen.flags);
	}
done:
	return rc;
}

Npfcall *
np_lcreate(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "lcreate: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, tc->u.tlcreate.gid) < 0)
			goto done;
		if (!req->conn->srv->lcreate) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->lcreate)(fid,
						&tc->u.tlcreate.name,
						tc->u.tlcreate.flags,
						tc->u.tlcreate.mode,
						tc->u.tlcreate.gid);
	}
	if (rc) {
		//np_fid_omode_set(fid, O_CREAT|O_WRONLY|O_TRUNC);
		fid->type = rc->u.rlcreate.qid.type;
	}
done:
	return rc;
}

Npfcall *
np_symlink(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "symlink: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, tc->u.tsymlink.gid) < 0)
			goto done;
		if (!req->conn->srv->symlink) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->symlink)(fid,
						&tc->u.tsymlink.name,
						&tc->u.tsymlink.symtgt,
						tc->u.tsymlink.gid);
	}
done:
	return rc;
}

Npfcall *
np_mknod(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "mknod: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, tc->u.tmknod.gid) < 0)
			goto done;
		if (!req->conn->srv->mknod) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->mknod)(fid,
						&tc->u.tmknod.name,
						tc->u.tmknod.mode,
						tc->u.tmknod.major,
						tc->u.tmknod.minor,
						tc->u.tmknod.gid);
	}
done:
	return rc;
}

Npfcall *
np_rename(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfid *dfid = NULL;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "rename: invalid fid");
		goto done;
	}
	if (!(dfid = np_fid_find(req->conn, tc->u.trename.dfid))) {
		np_uerror(EIO);
		np_logerr (req->conn->srv, "rename: invalid dfid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->rename) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->rename)(fid, dfid, &tc->u.trename.name);
	}
done:
	if (dfid)
		np_fid_decref (&dfid);
	return rc;
}

Npfcall *
np_readlink(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "readlink: invalid fid");
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->readlink) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->readlink)(fid);
	}
done:
	return rc;
}

Npfcall *
np_getattr(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "getattr: invalid fid");
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		rc = np_ctl_getattr(fid, tc->u.tgetattr.request_mask);
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->getattr) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->getattr)(fid,
						tc->u.tgetattr.request_mask);
	}
done:
	return rc;
}

Npfcall *
np_setattr(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "setattr: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		rc = np_ctl_setattr(fid,
				    tc->u.tsetattr.valid,
				    tc->u.tsetattr.mode,
				    tc->u.tsetattr.uid,
				    tc->u.tsetattr.gid,
				    tc->u.tsetattr.size,
				    tc->u.tsetattr.atime_sec,
				    tc->u.tsetattr.atime_nsec,
				    tc->u.tsetattr.mtime_sec,
				    tc->u.tsetattr.mtime_nsec);
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->setattr) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->setattr)(fid,
						tc->u.tsetattr.valid,
						tc->u.tsetattr.mode,
						tc->u.tsetattr.uid,
						tc->u.tsetattr.gid,
						tc->u.tsetattr.size,
						tc->u.tsetattr.atime_sec,
						tc->u.tsetattr.atime_nsec,
						tc->u.tsetattr.mtime_sec,
						tc->u.tsetattr.mtime_nsec);
	}
done:
	return rc;
}

Npfcall *
np_xattrwalk(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfid *fid = req->fid;
	Npfid *attrfid = NULL;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (conn->srv, "xattrwalk: invalid fid");
		goto done;
	}

	attrfid = np_fid_create(conn, tc->u.txattrwalk.attrfid);
	if (!attrfid) {
		if (np_rerror () == EEXIST) {
			np_uerror(EIO);
			np_logmsg (conn->srv,
				   "%s@%s:%s: xattrwalk: invalid newfid: %d",
				   fid->user->uname,
				   np_conn_get_client_id (conn),
				   fid->aname, tc->u.txattrwalk.attrfid);
		}
		goto done;
	}
	/* FIXME: this block should be factored from here and np_walk()
	 * into common helper function - I am rusty on diod and don't want
	 * to touch np_walk() right now. -jg
	 */
	np_user_incref(fid->user);
	attrfid->user = fid->user;
	np_tpool_incref(fid->tpool);
	attrfid->tpool = fid->tpool;
	attrfid->type = fid->type;
	attrfid->flags = fid->flags;
	if (!(attrfid->aname = strdup (fid->aname))) {
		np_uerror (ENOMEM);
		np_logerr (conn->srv, "xattrwalk: out of memory");
		goto done;
	}
	/* FIXME: end of block that should be factored */

	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->xattrwalk) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->xattrwalk)(fid, attrfid,
						  &tc->u.txattrwalk.name);
	}
done:
	if (!rc && attrfid)
		np_fid_decref (&attrfid);
	return rc;
}

Npfcall *
np_xattrcreate(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "xattrcreate: invalid fid");
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->xattrcreate) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->xattrcreate)(fid,
						    &tc->u.txattrcreate.name,
						    tc->u.txattrcreate.size,
						    tc->u.txattrcreate.flag);
	}
done:
	return rc;
}

Npfcall *
np_readdir(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "readdir: invalid fid");
		goto done;
	}
	if (tc->u.treaddir.count + P9_READDIRHDRSZ > req->conn->msize) {
		np_uerror(EIO);
		np_logerr (req->conn->srv, "readdir: count %u too large",
			   tc->u.treaddir.count);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		rc = np_ctl_readdir(fid, tc->u.treaddir.offset,
				    tc->u.treaddir.count, req);
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->readdir) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->readdir)(fid, tc->u.treaddir.offset,
						tc->u.treaddir.count,
						req);
	}
done:
	return rc;
}

Npfcall *
np_fsync(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "fsync: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->fsync) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->fsync)(fid);
	}
done:
	return rc;
}

Npfcall *
np_lock(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "lock: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->llock) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->llock)(fid,
						tc->u.tlock.type,
						tc->u.tlock.flags,
						tc->u.tlock.start,
						tc->u.tlock.length,
						tc->u.tlock.proc_id,
						&tc->u.tlock.client_id);
	}
done:
	return rc;
}

Npfcall *
np_getlock(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "getlock: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->getlock) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->getlock)(fid,
						tc->u.tgetlock.type,
						tc->u.tgetlock.start,
						tc->u.tgetlock.length,
						tc->u.tgetlock.proc_id,
						&tc->u.tgetlock.client_id);
	}
done:
	return rc;
}

Npfcall *
np_link(Npreq *req, Npfcall *tc)
{
	Npfid *dfid = req->fid;
	Npfid *fid = NULL;
	Npfcall *rc = NULL;

	if (!dfid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "link: invalid dfid");
		goto done;
	}
	if (!(fid = np_fid_find(req->conn, tc->u.tlink.fid))) {
		np_uerror(EIO);
		np_logerr (req->conn->srv, "link: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, -1) < 0)
			goto done;
		if (!req->conn->srv->link) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->link)(dfid, fid, &tc->u.tlink.name);
	}
done:
	if (fid)
		np_fid_decref (&fid);
	return rc;
}

Npfcall *
np_mkdir(Npreq *req, Npfcall *tc)
{
	Npfid *fid = req->fid;
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "mkdir: invalid fid");
		goto done;
	}
	if (fid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (fid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	} else {
		if (np_setfsid (req, fid->user, tc->u.tmkdir.gid) < 0)
			goto done;
		if (!req->conn->srv->mkdir) {
			np_uerror (ENOSYS);
			goto done;
		}
		rc = (*req->conn->srv->mkdir)(fid,
						&tc->u.tmkdir.name,
						tc->u.tmkdir.mode,
						tc->u.tmkdir.gid);
	}
done:
	return rc;
}

/* renameat and unlinkat are new, post 2.6.38.  Kernels will fall back
 * to rename and remove if they get EOPNOTSUPP error.
 * FIXME: implement these
 */

Npfcall *
np_renameat (Npreq *req, Npfcall *tc)
{
	Npfid *olddirfid = req->fid;
	Npfid *newdirfid = NULL;
	Npfcall *rc = NULL;

	if (!olddirfid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "renameat: invalid olddirfid");
		goto done;
	}
	if (olddirfid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (olddirfid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	}
	if (!(newdirfid = np_fid_find(req->conn, tc->u.trenameat.newdirfid))) {
		np_uerror(EIO);
		np_logerr (req->conn->srv, "renameat: invalid newdirfid");
		goto done;
	}
	if (np_setfsid (req, newdirfid->user, -1) < 0)
		goto done;
	if (!req->conn->srv->renameat) {
		np_uerror (EOPNOTSUPP); /* v9fs expects this not ENOSYS for this op */
		goto done;
	}
	rc = (*req->conn->srv->renameat)(olddirfid, &tc->u.trenameat.oldname,
					 newdirfid, &tc->u.trenameat.newname);
done:
	if (newdirfid)
		np_fid_decref (&newdirfid);
	return rc;
}

Npfcall *
np_unlinkat (Npreq *req, Npfcall *tc)
{
	Npfid *dirfid = req->fid;
	Npfcall *rc = NULL;

	if (!dirfid) {
		np_uerror (EIO);
		np_logerr (req->conn->srv, "unlinkat: invalid dirfid");
		goto done;
	}
	if (dirfid->flags & FID_FLAGS_ROFS) {
		np_uerror(EROFS);
		goto done;
	}
	if (dirfid->type & P9_QTTMP) {
		np_uerror (EPERM);
		goto done;
	}
	if (np_setfsid (req, dirfid->user, -1) < 0)
		goto done;
	if (!req->conn->srv->unlinkat) {
		np_uerror (EOPNOTSUPP); /* v9fs expects this not ENOSYS for this op */
		goto done;
	}
	rc = (*req->conn->srv->unlinkat)(dirfid, &tc->u.tunlinkat.name);
done:
	return rc;
}
