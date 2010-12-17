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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <zlib.h>
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
	if (tc->msize < IOHDRSZ + 1) {
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
	afid->type = Qtauth;
	if (srv->auth && srv->auth->startauth)
		n = (*srv->auth->startauth)(afid, aname, &aqid);
	else
		n = 0;

	if (n) {
		assert((aqid.type & Qtauth) != 0);
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
		if (tc->afid!=NOFID) {
			np_werror(Eunknownfid, EIO);
			goto done;
		}
	} else {
		if (!(afid->type & Qtauth)) {
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

	// if not found, return Rflush
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
	Npqid wqids[MAXWELEM];

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
	/* FIXME: this test used to lack parens and always fail.
   	 * After I fixed it, it succeeds walking a mount point.
   	 * Disabled like before for now. --jg
 	 */
	if (!(fid->type & Qtdir)) {
		np_werror(Enotdir, ENOTDIR);
		goto done;
	}
#endif
	if (fid->omode != (u16) ~0) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}
	if (tc->nwname > MAXWELEM) {
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

		if (i<(tc->nwname) && !(newfid->type & Qtdir))
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

	req->fid = fid;
	if (fid->omode != (u16)~0) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (fid->type&Qtdir && tc->mode != Oread) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	rc = (*conn->srv->open)(fid, tc->mode);
	fid->omode = tc->mode;
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

	req->fid = fid;
	if (fid->omode != (u16)~0) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (!(fid->type & Qtdir)) {
		np_werror(Enotdir, ENOTDIR);
		goto done;
	}

	if (tc->perm&Dmdir && tc->mode!=Oread) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	if (tc->perm&(Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice|Dmsocket)
					&& !np_conn_extend(fid->conn)) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	rc = (*conn->srv->create)(fid, &tc->name, tc->perm, tc->mode, 
		&tc->extension);
	if (rc && rc->type == Rcreate) {
		fid->omode = tc->mode;
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
	if (tc->count+IOHDRSZ > conn->msize) {
		np_werror(Etoolarge, EIO);
		goto done;
	}

	if (fid->type&Qtauth) {
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

	if (fid->omode==(u16)~0 || (fid->omode&3)==Owrite) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (fid->type&Qtdir && tc->offset && tc->offset != fid->diroffset) {
		np_werror(Ebadoffset, EIO);
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
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(fid);

	req->fid = fid;
	if (fid->type&Qtauth) {
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

	if (fid->omode==(u16)~0 || fid->type&Qtdir || (fid->omode&3)==Oread) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (tc->count+IOHDRSZ > conn->msize) {
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
	if (fid->type&Qtauth) {
		if (conn->srv->auth) {
			n = conn->srv->auth->clunk(fid);
			if (n)
				rc = np_create_rclunk();
		} else
			np_werror(Ebadusefid, EIO);

		goto done;
	}

	if (fid->omode!=(u16)~0 && fid->omode==Orclose) {
		rc = (*conn->srv->remove)(fid);
		if (rc->type == Rerror)
			goto done;
		free(rc);
		rc = np_create_rclunk();
	} else
		rc = (*conn->srv->clunk)(fid);

	if (rc && rc->type == Rclunk)
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
	if (rc && rc->type == Rremove)
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

	if ((fid->type & Qtdir) && !(stat->mode & Dmdir)) {
		np_werror(Edirchange, EPERM);
		goto done;
	}

	if (!(fid->type & Qtdir) && (stat->mode & Dmdir)) {
		np_werror(Edirchange, EPERM);
		goto done;
	}

	rc = (*conn->srv->wstat)(fid, &tc->stat);
done:
//	np_fid_decref(fid);
	return rc;
}

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
	if (tc->rsize+AIOHDRSZ > conn->msize) {
		np_werror(Etoolarge, EIO);
		goto done;
	}

	if (fid->type&Qtauth) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (fid->omode==(u16)~0 || (fid->omode&3)==Owrite) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (fid->type&Qtdir && tc->offset && tc->offset != fid->diroffset) {
		np_werror(Ebadoffset, EIO);
		goto done;
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
	if (fid->type&Qtauth) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (fid->omode==(u16)~0 || fid->type&Qtdir || (fid->omode&3)==Oread) {
		np_werror(Ebadusefid, EIO);
		goto done;
	}

	if (tc->rsize+AIOHDRSZ > conn->msize) {
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

Npfcall *
np_statfs(Npreq *req, Npfcall *tc)
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

	rc = (*conn->srv->statfs)(fid);

done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_lock(Npreq *req, Npfcall *tc)
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

	rc = (*conn->srv->plock)(fid, tc->cmd, &tc->lock);

done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_flock(Npreq *req, Npfcall *tc)
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

	rc = (*conn->srv->flock)(fid, tc->cmd);
done:
//	np_fid_decref(fid);
	return rc;
}

Npfcall *
np_rename(Npreq *req, Npfcall *tc)
{
	Npconn *conn;
	Npfid *fid, *newdirfid = NULL;
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

	newdirfid = np_fid_find(conn, tc->newdirfid);
	if (!newdirfid) {
		np_werror(Eunknownfid, EIO);
		goto done;
	} else 
		np_fid_incref(newdirfid);

	rc = (*conn->srv->rename)(fid, newdirfid, &tc->newname);
done:
	np_fid_decref(newdirfid);
//	np_fid_decref(fid);
	return rc;
}
