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

char *Eunknownfid = "unknown fid";
char *Ennomem = "no memory";	/* avoid conflict with libspfs' Enomem */
char *Enoauth = "no authentication required";
char *Enotimpl = "not implemented";
char *Einuse = "fid already exists";
char *Ebadusefid = "bad use of fid";
char *Enotdir = "not a directory";
char *Etoomanywnames = "too many wnames";
char *Eperm = "permission denied";
char *Etoolarge = "i/o count too large";
char *Ebadoffset = "bad offset in directory read";
char *Edirchange = "cannot convert between files and directories";
char *Enotfound = "file not found";
char *Eopen = "file alread exclusively opened";
char *Eexist = "file or directory already exists";
char *Enotempty = "directory not empty";
char *Eunknownuser = "unknown user";

Npfcall *
np_version(Npreq *req, Npfcall *tc)
{
	if (tc->msize < P9_IOHDRSZ + 1) {
		np_werror("msize too small", EIO);
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
		np_werror(Einuse, EIO);
		goto done;
	}

	afid = np_fid_create(conn, tc->afid, NULL);
	if (!afid)
		goto done;
	else
		np_fid_incref(afid);

	if (tc->uname.len && tc->n_uname==~0) {
		uname = np_strdup(&tc->uname);
		if (!uname) 
			goto done;

		user = (*srv->upool->uname2user)(srv->upool, uname);
		free(uname);
		if (!user) {
			np_werror(Eunknownuser, EIO);
			goto done;
		}
		tc->n_uname = user->uid;
	} else {
		user = (*srv->upool->uid2user)(srv->upool, tc->n_uname);
		if (!user) {
			np_werror(Eunknownuser, EIO);
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
		np_werror(Enoauth, EIO);
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
		np_werror(Einuse, EIO);
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
			np_werror(Eunknownfid, EIO);
			goto done;
		}
	} else {
		if (!(afid->type & P9_QTAUTH)) {
			np_werror(Ebadusefid, EIO);
			goto done;
		}
		np_fid_incref(afid);
	}

	if (tc->uname.len && tc->n_uname==~0) {
		uname = np_strdup(&tc->uname);
		if (!uname) 
			goto done;

		user = srv->upool->uname2user(srv->upool, uname);
		free(uname);
		if (!user) {
			np_werror(Eunknownuser, EIO);
			goto done;
		}

		tc->n_uname = user->uid;
	} else {
		user = srv->upool->uid2user(srv->upool, tc->n_uname);
		if (!user) {
			np_werror(Eunknownuser, EIO);
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
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
#if 0
	if (!(fid->type & P9_QTDIR)) {
		np_werror(Enotdir, ENOTDIR);
		goto done;
	}
#endif
	if (!np_fid_omode_isclear(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if (tc->nwname > P9_MAXWELEM) {
		np_werror(Etoomanywnames, EIO);
		goto done;
	}

	if (tc->fid != tc->newfid) {
//		printf("newfid conn %p fid %d\n", conn, tc->newfid); 
		newfid = np_fid_find(conn, tc->newfid);
		if (newfid) {
			np_werror(Einuse, EIO);
			goto done;
		}
		newfid = np_fid_create(conn, tc->newfid, NULL);
		if (!newfid) {
			np_werror(Ennomem, ENOMEM);
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

	np_werror(NULL, 0);
	if (tc->fid != tc->newfid)
		np_fid_incref(newfid);
	rc = np_create_rwalk(i, wqids);

done:
	np_fid_decref(newfid);
	return rc;
}

Npfcall *
np_open(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (np_fid_proto_dotl(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	req->fid = fid;
	if (!np_fid_omode_isclear(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if ((fid->type & P9_QTDIR) && (tc->mode & 3) != P9_OREAD) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	rc = (*conn->srv->open)(fid, tc->mode);
	if (rc)
		np_fid_omode_set(fid, tc->mode);
done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_create(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (np_fid_proto_dotl(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	req->fid = fid;
	if (!np_fid_omode_isclear(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
#if 0
	if (!(fid->type & P9_QTDIR)) {
		np_werror(Enotdir, ENOTDIR);
		goto done;
	}
#endif
	if (tc->perm & P9_DMDIR && tc->mode != P9_OREAD) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	if (tc->perm & (P9_DMNAMEDPIPE | P9_DMSYMLINK | P9_DMLINK 
				       | P9_DMDEVICE  |P9_DMSOCKET)
			&& !np_conn_extend(fid->conn)) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	rc = (*conn->srv->create)(fid, &tc->name, tc->perm, tc->mode, 
		&tc->extension);
	if (rc && rc->type == P9_RCREATE) {
		np_fid_omode_set(fid, tc->mode);
		fid->type = rc->qid.type;
	}

done:
//	np_fid_decref(fid);
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
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (tc->count + P9_IOHDRSZ > conn->msize) {
		np_werror(Etoolarge, EIO);
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
			np_werror(Ebadusefid, EIO);

		goto done;
	}

	if (np_fid_omode_isclear(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if (np_fid_omode_noread(fid)) {
		np_werror(Eperm, EPERM);
		goto done;
	}
	if ((fid->type & P9_QTDIR)) {
		if (np_fid_proto_dotl(fid)) {
			np_werror(Eperm, EIO);
			goto done;
		}
		if (tc->offset && tc->offset != fid->diroffset) {
			np_werror(Ebadoffset, EIO);
			goto done;
		}
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
		np_werror(Eunknownfid, EIO);
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
			np_werror(Ebadusefid, EIO);
			goto done;
		}
	}

	if (np_fid_omode_isclear(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if ((fid->type & P9_QTDIR) || np_fid_omode_nowrite(fid)) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	if (tc->count + P9_IOHDRSZ > conn->msize) {
		np_werror(Etoolarge, EIO);
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
		np_werror(Eunknownfid, EIO);
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
			np_werror(Ebadusefid, EIO);

		goto done;
	}

	if (!np_fid_omode_isclear(fid) && np_fid_omode_rclose(fid)) {
		rc = (*conn->srv->remove)(fid);
		if (rc->type == P9_RERROR)
			goto done;
		free(rc);
		rc = np_create_rclunk();
	} else
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
		np_werror(Eunknownfid, EIO);
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

Npfcall *
np_stat(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	rc = (*conn->srv->stat)(fid);

done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_wstat(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;
	Npstat *stat;

	rc = NULL;
	conn = req->conn;
	stat = &tc->stat;
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (stat->type != (u16)~0 || stat->dev != (u32)~0
	|| stat->qid.version != (u32)~0
	|| stat->qid.path != (u64)~0 ) {
                np_werror(Eperm, EPERM);
                goto done;
        }
#if 0
	if ((fid->type & P9_QTDIR) && !(stat->mode & P9_DMDIR)) {
		np_werror(Edirchange, EPERM);
		goto done;
	}
	if (!(fid->type & P9_QTDIR) && (stat->mode & P9_DMDIR)) {
		np_werror(Edirchange, EPERM);
		goto done;
	}
#endif
	rc = (*conn->srv->wstat)(fid, &tc->stat);
done:
//	np_fid_decref(fid);
	return rc;
}

#if HAVE_LARGEIO
Npfcall *
np_aread(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (tc->rsize + P9_AIOHDRSZ > conn->msize) {
		np_werror(Etoolarge, EIO);
		goto done;
	}

	if (fid->type & P9_QTAUTH) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (np_fid_omode_isclear(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if (np_fid_omode_noread(fid)) {
		np_werror(Eperm, EPERM);
		goto done;
	}
	if ((fid->type & P9_QTDIR)) {
		if (np_fid_proto_dotl(fid)) {
			np_werror(Eperm, EIO);
			goto done;
		}
		if (tc->offset && tc->offset != fid->diroffset) {
			np_werror(Ebadoffset, EIO);
			goto done;
		}
	}
		
	rc = (*conn->srv->aread)(fid, tc->datacheck, tc->offset, tc->count, tc->rsize, req);

done:
	return rc;
}

Npfcall *
np_awrite(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid;
	Npfcall *rc;
	u32 check;

	rc = NULL;
	conn = req->conn;
	fid = np_fid_find(conn, tc->fid);
	if (!fid) {
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (fid->type & P9_QTAUTH) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if (np_fid_omode_isclear(fid)) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if ((fid->type & P9_QTDIR) || np_fid_omode_nowrite(fid)) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	if (tc->rsize + P9_AIOHDRSZ > conn->msize) {
		np_werror(Etoolarge, EIO);
		goto done;
	}

	if (tc->datacheck == P9_CHECK_ADLER32) {
		check = adler32(0L, Z_NULL, 0);
		check = adler32(check, tc->data, tc->rsize);
		if (tc->check != check) {
			np_werror("write checksum verification failure",
				  EAGAIN);
			goto done;
		}
	}

	rc = (*conn->srv->awrite)(fid, tc->offset, tc->count, tc->rsize, tc->data, req);

done:
	return rc;
}
#endif

#if HAVE_DOTL
Npfcall *
np_statfs(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tstatfs.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	req->fid = fid;
	rc = (*conn->srv->statfs)(fid);
done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_lopen(Npreq *req, Npfcall *tc)
{
	Npfcall *rc = NULL;
	Npconn *conn = req->conn;
	Npfid *fid = np_fid_find(conn, tc->u.tlopen.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
	req->fid = fid;
	if (!np_fid_omode_isclear(fid)) {
		np_uerror(EIO);
		goto done;
	}
	if ((fid->type & P9_QTDIR)
			&& (tc->u.tlopen.mode & O_ACCMODE) != O_RDONLY) {
		np_uerror(EPERM);
		goto done;
	}


	rc = (*conn->srv->lopen)(fid, tc->u.tlopen.mode);
	if (rc)
		np_fid_omode_set(fid, tc->u.tlopen.mode);
done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_lcreate(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tlcreate.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
	req->fid = fid;
done:
	return rc;
}

Npfcall *
np_symlink(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tsymlink.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_mknod(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tmknod.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_rename(Npreq *req, Npfcall *tc)
{
	Npfcall *rc = NULL;
	Npconn *conn = req->conn;
	Npfid *newdirfid = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.trename.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	req->fid = fid;
	newdirfid = np_fid_find(conn, tc->u.trename.newdirfid);
	if (!newdirfid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(newdirfid);
	rc = (*conn->srv->rename)(fid, newdirfid, &tc->u.trename.name);
done:
	np_fid_decref(newdirfid);
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_readlink(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.treadlink.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_getattr(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tgetattr.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	req->fid = fid;
	rc = (*conn->srv->getattr)(fid, tc->u.tgetattr.request_mask);
done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_setattr(Npreq *req, Npfcall *tc)
{
	Npfcall *rc = NULL;
	/* FIXME */
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
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.treaddir.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	req->fid = fid;
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
	if (tc->u.treaddir.count + P9_READDIRHDRSZ > conn->msize) {
		np_uerror(EIO);
		goto done;
	}
	//if (!(fid->type & P9_QTDIR)) {
	//	np_werror(Eperm, EPERM);
	//	goto done;
	//}
	if (np_fid_omode_isclear(fid)) {
		np_uerror(EIO);
		goto done;
	}
	if (np_fid_omode_noread(fid)) {
		np_uerror(EPERM);
		goto done;
	}
	//if (tc->u.treaddir.offset && tc->u.treaddir.offset != fid->diroffset) {
	//	np_werror(Ebadoffset, EIO);
	//	goto done;
	//}
	rc = (*conn->srv->readdir)(fid, tc->u.treaddir.offset,
					tc->u.treaddir.count, req);
done:
	return rc;
}

Npfcall *
np_fsync(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tfsync.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_lock(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tlock.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_getlock(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tgetlock.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_link(Npreq *req, Npfcall *tc)
{
	Npfcall *rc = NULL;
	return rc;
}

Npfcall *
np_mkdir(Npreq *req, Npfcall *tc)
{
	Npconn *conn = req->conn;
	Npfcall *rc = NULL;
	Npfid *fid = np_fid_find(conn, tc->u.tmkdir.fid);

	if (!fid) {
		np_uerror(EIO);
		goto done;
	} else 
		np_fid_incref(fid);
	if (!np_fid_proto_dotl(fid)) {
		np_uerror(EIO);
		goto done;
	}
done:
	return rc;
}
#endif
