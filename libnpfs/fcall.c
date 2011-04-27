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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

Npfcall *
np_version(Npreq *req, Npfcall *tc)
{
	if (tc->u.tversion.msize < P9_IOHDRSZ + 1) {
		np_uerror(EIO);
		return NULL;
	}

	return (*req->conn->srv->version)(req->conn,
				tc->u.tversion.msize,
				&tc->u.tversion.version);
}

Npfcall *
np_auth(Npreq *req, Npfcall *tc)
{
	char *aname = NULL;
	Npconn *conn = req->conn;
	Npsrv *srv = conn->srv;
	Npfid *afid = NULL;
	Npfcall *rc = NULL;
	Npqid aqid;

	if ((np_fid_find(conn, tc->u.tauth.afid))) {
		if (srv->msg)
			srv->msg ("auth: afid is in use");
		np_uerror(EIO);
		goto error;
	}
	if (!(afid = np_fid_create(conn, tc->u.tauth.afid, NULL))) {
		if (srv->msg)
			srv->msg ("auth: failed to create afid");
		goto error;
	}
	np_fid_incref(afid);

	if (tc->u.tauth.aname.len > 0) {
		if (!(aname = np_strdup(&tc->u.tauth.aname))) {
			if (srv->msg)
				srv->msg ("auth: out of memory");
			np_uerror (ENOMEM);
			goto error;
		}
	}
	if (!srv->auth || !srv->auth->startauth
		       || !srv->auth->startauth(afid, aname, &aqid)) {
		np_uerror (EIO);
		goto error; /* auth not required */
	}

	afid->type = P9_QTAUTH;	/* triggers auth->clunk on fid free */

	afid->user = np_attach2user (&tc->u.tauth.uname, tc->u.tauth.n_uname);
	if (!afid->user) {
		if (srv->msg) {
			if (tc->u.tauth.n_uname != P9_NONUNAME)
				srv->msg ("auth: user lookup (%d) failed",
					  tc->u.tauth.n_uname);
			else if (tc->u.tauth.uname.len > 0)
				srv->msg ("auth: user lookup (%.*s) failed",
					  tc->u.tauth.uname.len,
					  tc->u.tauth.uname.str);
			else
				srv->msg ("auth: no username specified");
		}
		goto error;
	}
	assert((aqid.type & P9_QTAUTH));
	if (!(rc = np_create_rauth(&aqid))) {
		if (srv->msg)
			srv->msg ("auth: out of memory");
		np_uerror(ENOMEM);
		goto error;
	}
error:
	if (aname)
		free(aname);
	if (afid)
		np_fid_decref(afid);
	return rc;
}

Npfcall *
np_attach(Npreq *req, Npfcall *tc)
{
	char *aname = NULL;
	Npconn *conn = req->conn;
	Npsrv *srv = conn->srv;
	Npfid *fid, *afid = NULL;
	Npfcall *rc = NULL;

	if (np_fid_find(conn, tc->u.tattach.fid)) {
		if (srv->msg)
			srv->msg ("attach: fid is in use");
		np_uerror(EIO);
		goto error;
	}
	if (!(fid = np_fid_create(conn, tc->u.tattach.fid, NULL))) {
		if (srv->msg)
			srv->msg ("attach: failed to create fid");
		goto error;
	}
	np_fid_incref(fid);

	req->fid = fid;
	if (tc->u.tattach.afid != P9_NOFID) {
		if (!(afid = np_fid_find(conn, tc->u.tattach.afid))) {
			if (srv->msg)
				srv->msg ("attach: invalid afid");
			np_uerror(EPERM);
			goto error;
		}
		if (!(afid->type & P9_QTAUTH)) {
			if (srv->msg)
				srv->msg ("attach: invalid afid type");
			np_uerror(EPERM);
			goto error;
		}
		np_fid_incref(afid);
	}

	if (srv->remapuser)
		fid->user = srv->remapuser(&tc->u.tattach.uname,
				           tc->u.tattach.n_uname,
				           &tc->u.tattach.aname);
	if (!fid->user)
		fid->user = np_attach2user (&tc->u.tauth.uname,
					    tc->u.tauth.n_uname);
	if (!fid->user) {
		if (srv->msg) {
			if (tc->u.tattach.n_uname != P9_NONUNAME)
				srv->msg ("attach: user lookup (%d) failed",
					  tc->u.tattach.n_uname);
			else if (tc->u.tattach.uname.len > 0)
			        srv->msg ("attach: user lookup (%.*s) failed",
					  tc->u.tattach.uname.len,
					  tc->u.tattach.uname.str);
			else
				srv->msg ("attach: no username specified");
		}
		goto error;
	}
	if (tc->u.tattach.aname.len) {
		if (!(aname = np_strdup(&tc->u.tattach.aname))) {
			if (srv->msg)
				srv->msg ("attach: out of memory");
			np_uerror (ENOMEM);
			goto error;
		}
	}

	if (srv->auth && srv->auth->checkauth
		      && srv->auth->checkauth(fid, afid, aname) == 0) {
		if (srv->msg)
			srv->msg ("attach: authentication failed");
		goto error;
	}

	rc = (*srv->attach)(fid, afid, &tc->u.tattach.aname);

error:
	if (aname)
		free(aname);
	if (afid)
		np_fid_decref(afid);
	return rc;
}

Npfcall*
np_flush(Npreq *req, Npfcall *tc)
{
	u16 oldtag;
	Npreq *creq;
	Npconn *conn;
	Npfcall *ret;

	ret = NULL;
	conn = req->conn;
	oldtag = tc->u.tflush.oldtag;
	pthread_mutex_lock(&conn->srv->lock);
	// check pending requests
	for(creq = conn->srv->reqs_first; creq != NULL; creq = creq->next) {
		if (creq->conn==conn && creq->tag==oldtag) {
			np_srv_remove_req(conn->srv, creq);
			pthread_mutex_lock(&creq->lock);
			np_conn_respond(creq); /* doesn't send anything */
			pthread_mutex_unlock(&creq->lock);
			np_req_unref(creq);
			ret = np_create_rflush();
			creq = NULL;
			goto done;
		}
	}

	// check working requests
	creq = conn->srv->workreqs;
	while (creq != NULL) {
		if (creq->conn==conn && creq->tag==oldtag) {
			np_req_ref(creq);
			pthread_mutex_lock(&creq->lock);
			req->flushreq = creq->flushreq;
			creq->flushreq = req;
			pthread_mutex_unlock(&creq->lock);
			goto done;
		}
		creq = creq->next;
	}

	// if not found, return P9_RFLUSH
	if (!creq)
		ret = np_create_rflush();

done:
	pthread_mutex_unlock(&conn->srv->lock);

	// if working request found, try to flush it
	if (creq && req->conn->srv->flush) {
		(*req->conn->srv->flush)(creq);
		np_req_unref(creq);
	}

	return ret;
}

Npfcall *
np_walk(Npreq *req, Npfcall *tc)
{
	int i;
	Npconn *conn;
	Npfid *fid, *newfid;
	Npfcall *rc;
	Npqid wqids[P9_MAXWELEM];

	rc = NULL;
	conn = req->conn;
	newfid = NULL;
	fid = np_fid_find(conn, tc->u.twalk.fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
#if 0
	if (!(fid->type & P9_QTDIR)) {
		np_uerror(ENOTDIR);
		goto done;
	}
#endif
	/* FIXME: error if fid has been opened */
	if (tc->u.twalk.nwname > P9_MAXWELEM) {
		np_uerror(EIO);
		goto done;
	}

	if (tc->u.twalk.fid != tc->u.twalk.newfid) {
//		printf("newfid conn %p fid %d\n", conn, tc->newfid); 
		newfid = np_fid_find(conn, tc->u.twalk.newfid);
		if (newfid) {
			np_uerror(EIO);
			goto done;
		}
		newfid = np_fid_create(conn, tc->u.twalk.newfid, NULL);
		if (!newfid) {
			np_uerror(ENOMEM);
			goto done;
		}

		if (!(*conn->srv->clone)(fid, newfid))
			goto done;

		np_user_incref(fid->user);
		newfid->user = fid->user;
		newfid->type = fid->type;
	} else
		newfid = fid;

	np_fid_incref(newfid);
	if (np_setfsid (req, newfid->user, -1) < 0)
		goto done;
	for(i = 0; i < tc->u.twalk.nwname;) {
		if (!(*conn->srv->walk)(newfid, &tc->u.twalk.wnames[i],
					&wqids[i]))
			break;

		newfid->type = wqids[i].type;
		i++;
		if (i<(tc->u.twalk.nwname) && !(newfid->type & P9_QTDIR))
			break;
	}

	if (i==0 && tc->u.twalk.nwname!=0)
		goto done;

	np_uerror(0);
	if (tc->u.twalk.fid != tc->u.twalk.newfid)
		np_fid_incref(newfid);
	rc = np_create_rwalk(i, wqids);

done:
	np_fid_decref(newfid);
	return rc;
}

Npfcall *
np_read(Npreq *req, Npfcall *tc)
{
	int n;
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->u.tread.fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (tc->u.tread.count + P9_IOHDRSZ > conn->msize) {
		np_uerror(EIO);
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
			np_uerror(EIO);

		goto done;
	}
	if ((fid->type & P9_QTDIR)) {
		np_uerror(EIO);
		goto done;
	}
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*conn->srv->read)(fid, tc->u.tread.offset, tc->u.tread.count, req);

done:
	return rc;
}

Npfcall *
np_write(Npreq *req, Npfcall *tc)
{
	int n;
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->u.twrite.fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (fid->type & P9_QTAUTH) {
		if (conn->srv->auth) {
			n = conn->srv->auth->write(fid, tc->u.twrite.offset,
				tc->u.twrite.count, tc->u.twrite.data);
			if (n >= 0)
				if (!(rc = np_create_rwrite(n)))
					np_uerror(ENOMEM);

			goto done;
		} else {
			np_uerror(EIO);
			goto done;
		}
	}
	if ((fid->type & P9_QTDIR)) {
		np_uerror(EPERM);
		goto done;
	}
	if (tc->u.twrite.count + P9_IOHDRSZ > conn->msize) {
		np_uerror(EIO);
		goto done;
	}

	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*conn->srv->write)(fid, tc->u.twrite.offset, tc->u.twrite.count,
				tc->u.twrite.data, req);

done:
	return rc;
}

Npfcall *
np_clunk(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid;

	fid = np_fid_find(conn, tc->u.tclunk.fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (fid->type & P9_QTAUTH) {
		if (conn->srv->auth) {
			/* N.B. fidpool calls auth->clunk on last decref */
			if (!(rc = np_create_rclunk()))
				np_uerror(ENOMEM);
		} else
			np_uerror(EIO);

		goto done;
	}
	rc = (*conn->srv->clunk)(fid);

done:
	if (rc && rc->type == P9_RCLUNK)
		np_fid_decref(fid);

	return rc;
}

Npfcall *
np_remove(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->u.tremove.fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*conn->srv->remove)(fid);
	if (fid) /* spec says clunk the fid even if the remove fails */
		np_fid_decref(fid);

done:
	return rc;
}

static Npfid *
_getfid_incref(Npreq *req, u32 fid)
{
	Npfid *f = NULL;

	if (!(f = np_fid_find(req->conn, fid)))
		goto done;
	np_fid_incref(f);
	req->fid = f;
done:
	if (f == NULL)
		np_uerror(EIO);
	return f;
}
	
Npfcall *
np_statfs(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tstatfs.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->statfs)(fid);
done:
	return rc;
}

Npfcall *
np_lopen(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tlopen.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->lopen)(fid, tc->u.tlopen.mode);
done:
	return rc;
}

Npfcall *
np_lcreate(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tlcreate.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, tc->u.tlcreate.gid) < 0)
		goto done;
	rc = (*req->conn->srv->lcreate)(fid,
					&tc->u.tlcreate.name,
					tc->u.tlcreate.flags,
					tc->u.tlcreate.mode,
					tc->u.tlcreate.gid);
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
	Npfid *fid = _getfid_incref(req, tc->u.tsymlink.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, tc->u.tsymlink.gid) < 0)
		goto done;
	rc = (*req->conn->srv->symlink)(fid,
					&tc->u.tsymlink.name,
					&tc->u.tsymlink.symtgt,
					tc->u.tsymlink.gid);
done:
	return rc;
}

Npfcall *
np_mknod(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tmknod.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, tc->u.tmknod.gid) < 0)
		goto done;
	rc = (*req->conn->srv->mknod)(fid,
					&tc->u.tmknod.name,
					tc->u.tmknod.mode,
					tc->u.tmknod.major,
					tc->u.tmknod.minor,
					tc->u.tmknod.gid);
done:
	return rc;
}

Npfcall *
np_rename(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.trename.fid);
	Npfid *dfid = NULL;
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (!(dfid = np_fid_find(req->conn, tc->u.trename.dfid))) {
		np_uerror(EIO);
		goto done;
	}
	np_fid_incref(dfid);
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->rename)(fid, dfid, &tc->u.trename.name);
done:
	np_fid_decref(dfid);
	return rc;
}

Npfcall *
np_readlink(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.treadlink.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->readlink)(fid);
done:
	return rc;
}

Npfcall *
np_getattr(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tgetattr.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->getattr)(fid, tc->u.tgetattr.request_mask);
done:
	return rc;
}

Npfcall *
np_setattr(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tsetattr.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
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
done:
	return rc;
}

Npfcall *
np_xattrwalk(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.txattrwalk.fid);
	Npfid *attrfid;
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (!(attrfid = np_fid_find(req->conn, tc->u.txattrwalk.attrfid))) {
		np_uerror(EIO);
		goto done;
	}
	np_fid_incref(attrfid);
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->xattrwalk)(fid, attrfid, &tc->u.txattrwalk.name);
done:
	return rc;
}

Npfcall *
np_xattrcreate(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.txattrcreate.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->xattrcreate)(fid,
					    &tc->u.txattrcreate.name,
					    tc->u.txattrcreate.size,
					    tc->u.txattrcreate.flag);
done:
	return rc;
}

Npfcall *
np_readdir(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.treaddir.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (tc->u.treaddir.count + P9_READDIRHDRSZ > req->conn->msize) {
		np_uerror(EIO);
		goto done;
	}
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->readdir)(fid, tc->u.treaddir.offset,
					tc->u.treaddir.count,
					req);
done:
	return rc;
}

Npfcall *
np_fsync(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tfsync.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->fsync)(fid);
done:
	return rc;
}

Npfcall *
np_lock(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tlock.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->llock)(fid,
					tc->u.tlock.type,
					tc->u.tlock.flags,
					tc->u.tlock.start,
					tc->u.tlock.length,
					tc->u.tlock.proc_id,
					&tc->u.tlock.client_id);
done:
	return rc;
}

Npfcall *
np_getlock(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tgetlock.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->getlock)(fid,
					tc->u.tgetlock.type,
					tc->u.tgetlock.start,
					tc->u.tgetlock.length,
					tc->u.tgetlock.proc_id,
					&tc->u.tgetlock.client_id);
done:
	return rc;
}

Npfcall *
np_link(Npreq *req, Npfcall *tc)
{
	Npfid *dfid = _getfid_incref(req, tc->u.tlink.dfid);
	Npfid *fid;
	Npfcall *rc = NULL;

	if (!dfid)
		goto done;
	if (!(fid = np_fid_find(req->conn, tc->u.tlink.fid))) {
		np_uerror(EIO);
		goto done;
	}
	np_fid_incref(fid);
	if (np_setfsid (req, fid->user, -1) < 0)
		goto done;
	rc = (*req->conn->srv->link)(dfid, fid, &tc->u.tlink.name);
	np_fid_decref(fid);
done:
	return rc;
}

Npfcall *
np_mkdir(Npreq *req, Npfcall *tc)
{
	Npfid *fid = _getfid_incref(req, tc->u.tmkdir.fid);
	Npfcall *rc = NULL;

	if (!fid)
		goto done;
	if (np_setfsid (req, fid->user, tc->u.tmkdir.gid) < 0)
		goto done;
	rc = (*req->conn->srv->mkdir)(fid,
					&tc->u.tmkdir.name,
					tc->u.tmkdir.mode,
				 	tc->u.tmkdir.gid);
done:
	return rc;
}
