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
#include <zlib.h>
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

Npfcall *
np_version(Npreq *req, Npfcall *tc)
{
	if (tc->msize < P9_IOHDRSZ + 1) {
		np_uerror(EIO);
		return NULL;
	}

	return (*req->conn->srv->version)(req->conn, tc->msize, &tc->version);
}

Npfcall *
np_auth(Npreq *req, Npfcall *tc)
{
	int n;
	char *uname, *aname;
	Npconn *conn;
	Npsrv *srv;
	Npfid *afid;
	Npfcall *rc;
	Npuser *user;
	Npqid aqid;

	rc = NULL;
	aname = NULL;
	conn = req->conn;
	srv = conn->srv;
	afid = np_fid_find(conn, tc->afid);
	if (afid) {
		np_uerror(EIO);
		goto done;
	}

	afid = np_fid_create(conn, tc->afid, NULL);
	if (!afid)
		goto done;
	else
		np_fid_incref(afid);

	if (tc->uname.len && tc->n_uname==P9_NONUNAME) {
		uname = np_strdup(&tc->uname);
		if (!uname) 
			goto done;

		user = (*srv->upool->uname2user)(srv->upool, uname);
		free(uname);
		if (!user) {
			np_uerror(EIO);
			goto done;
		}
		tc->n_uname = user->uid;
	} else {
		user = (*srv->upool->uid2user)(srv->upool, tc->n_uname);
		if (!user) {
			np_uerror(EIO);
			goto done;
		}
	}

	if (tc->aname.len) {
		aname = np_strdup(&tc->aname);
		if (!aname)
			goto done;
	} else
		aname = NULL;

	afid->user = user;
	afid->type = P9_QTAUTH;
	if (srv->auth && srv->auth->startauth)
		n = (*srv->auth->startauth)(afid, aname, &aqid);
	else
		n = 0;

	if (n) {
		assert((aqid.type & P9_QTAUTH) != 0);
		rc = np_create_rauth(&aqid);
	} else
		np_uerror(EIO);
done:
	free(aname);
	if (!rc)
		np_fid_decref(afid);
	return rc;
}

Npfcall *
np_attach(Npreq *req, Npfcall *tc)
{
	char *uname, *aname;
	Npconn *conn;
	Npsrv *srv;
	Npfid *fid, *afid;
	Npfcall *rc;
	Npuser *user;

	rc = NULL;
	aname = NULL;
	conn = req->conn;
	srv = conn->srv;
	afid = NULL;
	fid = np_fid_find(conn, tc->fid);
	if (fid) {
		np_uerror(EIO);
		goto done;
	}

	fid = np_fid_create(conn, tc->fid, NULL);
	if (!fid)
		goto done;
	else 
		np_fid_incref(fid);

	req->fid = fid;
	afid = np_fid_find(conn, tc->afid);
	if (!afid) {
		if (tc->afid != P9_NOFID) {
			np_uerror(EIO);
			goto done;
		}
	} else {
		if (!(afid->type & P9_QTAUTH)) {
			np_uerror(EIO);
			goto done;
		}
		np_fid_incref(afid);
	}

	if (tc->uname.len && tc->n_uname==P9_NONUNAME) {
		uname = np_strdup(&tc->uname);
		if (!uname) 
			goto done;

		user = srv->upool->uname2user(srv->upool, uname);
		free(uname);
		if (!user) {
			np_uerror(EIO);
			goto done;
		}

		tc->n_uname = user->uid;
	} else {
		user = srv->upool->uid2user(srv->upool, tc->n_uname);
		if (!user) {
			np_uerror(EIO);
			goto done;
		}
	}

	fid->user = user;
	if (tc->aname.len) {
		aname = np_strdup(&tc->aname);
		if (!aname)
			goto done;
	} else
		aname = NULL;

	if (conn->srv->auth && conn->srv->auth->checkauth
	&& !(*conn->srv->auth->checkauth)(fid, afid, aname))
		goto done;

	rc = (*conn->srv->attach)(fid, afid, &tc->uname, &tc->aname);

done:
	free(aname);
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
	oldtag = tc->oldtag;
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
	fid = np_fid_find(conn, tc->fid);
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
	if (tc->nwname > P9_MAXWELEM) {
		np_uerror(EIO);
		goto done;
	}

	if (tc->fid != tc->newfid) {
//		printf("newfid conn %p fid %d\n", conn, tc->newfid); 
		newfid = np_fid_find(conn, tc->newfid);
		if (newfid) {
			np_uerror(EIO);
			goto done;
		}
		newfid = np_fid_create(conn, tc->newfid, NULL);
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
	for(i = 0; i < tc->nwname;) {
		if (!(*conn->srv->walk)(newfid, &tc->wnames[i], &wqids[i]))
			break;

		newfid->type = wqids[i].type;
		i++;
		if (i<(tc->nwname) && !(newfid->type & P9_QTDIR))
			break;
	}

	if (i==0 && tc->nwname!=0)
		goto done;

	np_uerror(0);
	if (tc->fid != tc->newfid)
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
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (tc->count + P9_IOHDRSZ > conn->msize) {
		np_uerror(EIO);
		goto done;
	}

	if (fid->type & P9_QTAUTH) {
		if (conn->srv->auth) {
			rc = np_alloc_rread(tc->count);
			if (!rc)
				goto done;

			n = conn->srv->auth->read(fid, tc->offset, tc->count, rc->data);
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
	rc = (*conn->srv->read)(fid, tc->offset, tc->count, req);

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
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (fid->type & P9_QTAUTH) {
		if (conn->srv->auth) {
			n = conn->srv->auth->write(fid, tc->offset,
				tc->count, tc->data);
			if (n >= 0)
				rc = np_create_rwrite(n);

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
	if (tc->count + P9_IOHDRSZ > conn->msize) {
		np_uerror(EIO);
		goto done;
	}

	rc = (*conn->srv->write)(fid, tc->offset, tc->count, tc->data, req);

done:
	return rc;
}

Npfcall *
np_clunk(Npreq *req, Npfcall *tc)
{
	int n;
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (fid->type & P9_QTAUTH) {
		if (conn->srv->auth) {
			n = conn->srv->auth->clunk(fid);
			if (n)
				rc = np_create_rclunk();
		} else
			np_uerror(EIO);

		goto done;
	}
	rc = (*conn->srv->clunk)(fid);

	if (rc && rc->type == P9_RCLUNK)
		np_fid_decref(fid);

done:
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
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	rc = (*conn->srv->remove)(fid);
	if (rc && rc->type == P9_RREMOVE)
		np_fid_decref(fid);

done:
	return rc;
}

#if HAVE_LARGEIO
Npfcall *
np_aread(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfid *fid = np_fid_find(conn, tc->u.taread.fid);
	Npfcall *rc = NULL;

	if (!fid) {
		np_uerror(EIO);
		goto done;
	}
	np_fid_incref(fid);
	req->fid = fid;
	rc = (*conn->srv->aread)(fid,
				tc->u.taread.datacheck,
				tc->u.taread.offset,
				tc->u.taread.count,
				tc->u.taread.rsize, req);
done:
	return rc;
}

Npfcall *
np_awrite(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfid *fid = np_fid_find(conn, tc->u.tawrite.fid);
	Npfcall *rc = NULL;
	u32 check;

	if (!fid) {
		np_uerror(EIO);
		goto done;
	}
	np_fid_incref(fid);
	req->fid = fid;
	if (tc->u.tawrite.datacheck == P9_CHECK_ADLER32) {
		check = adler32(0L, Z_NULL, 0);
		check = adler32(check, tc->u.tawrite.data, tc->u.tawrite.rsize);
		if (tc->u.tawrite.check != check) {
			np_uerror(EAGAIN);
			goto done;
		}
	}
	rc = (*conn->srv->awrite)(fid,
				tc->u.tawrite.offset,
				tc->u.tawrite.count,
				tc->u.tawrite.rsize,
				tc->u.tawrite.data, req);
done:
	return rc;
}
#endif

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
	assert(0); /*FIXME*/
}

Npfcall *
np_xattrcreate(Npreq *req, Npfcall *tc)
{
	assert(0); /*FIXME*/
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
	rc = (*req->conn->srv->mkdir)(fid,
					&tc->u.tmkdir.name,
					tc->u.tmkdir.mode,
				 	tc->u.tmkdir.gid);
done:
	return rc;
}
