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
#include "npfs.h"
#include "npfsimpl.h"

struct Reqpool {
	pthread_mutex_t	lock;
	int		reqnum;
	Npreq*		reqlist;
} reqpool = { PTHREAD_MUTEX_INITIALIZER, 0, NULL };

static void np_wthread_create(Npsrv *srv);
static void np_srv_destroy(Npsrv *srv);
static void np_wthread_create(Npsrv *srv);
static void *np_wthread_proc(void *a);

static Npfcall* np_default_version(Npconn *, u32, Npstr *);
static Npfcall* np_default_attach(Npfid *, Npfid *, Npstr *, Npstr *);
static void np_default_flush(Npreq *);
static int np_default_clone(Npfid *, Npfid *);
static int np_default_walk(Npfid *, Npstr*, Npqid *);
static Npfcall* np_default_open(Npfid *, u8);
static Npfcall* np_default_create(Npfid *, Npstr*, u32, u8, Npstr*);
static Npfcall* np_default_read(Npfid *, u64, u32, Npreq *);
static Npfcall* np_default_write(Npfid *, u64, u32, u8*, Npreq *);
static Npfcall* np_default_clunk(Npfid *);
static Npfcall* np_default_remove(Npfid *);
static Npfcall* np_default_stat(Npfid *);
static Npfcall* np_default_wstat(Npfid *, Npstat *);

static Npfcall* np_default_aread(Npfid *, u8, u64, u32, u32, Npreq *);
static Npfcall* np_default_awrite(Npfid *, u64, u32, u32, u8*, Npreq *);
static Npfcall* np_default_statfs(Npfid *);
static Npfcall* np_default_lock(Npfid *, u8, Nplock *);
static Npfcall* np_default_flock(Npfid *, u8);
static Npfcall* np_default_rename(Npfid *, Npfid *, Npstr *);

Npsrv*
np_srv_create(int nwthread)
{
	int i;
	Npsrv *srv;

	srv = malloc(sizeof(*srv));
	pthread_mutex_init(&srv->lock, NULL);
	pthread_cond_init(&srv->reqcond, NULL);
	srv->msize = 8216;
	srv->proto_version = NPFS_PROTO_2000L;
	srv->srvaux = NULL;
	srv->treeaux = NULL;
	srv->shuttingdown = 0;
	srv->auth = NULL;

	srv->start = NULL;
	srv->shutdown = NULL;
	srv->destroy = NULL;
	srv->connopen = NULL;
	srv->connclose = NULL;
	srv->fiddestroy = NULL;

	srv->version = np_default_version;
	srv->attach = np_default_attach;
	srv->flush = np_default_flush;
	srv->clone = np_default_clone;
	srv->walk = np_default_walk;
	srv->open = np_default_open;
	srv->create = np_default_create;
	srv->read = np_default_read;
	srv->write = np_default_write;
	srv->clunk = np_default_clunk;
	srv->remove = np_default_remove;
	srv->stat = np_default_stat;
	srv->wstat = np_default_wstat;
	srv->upool = NULL;

	srv->aread = np_default_aread;
	srv->awrite = np_default_awrite;
	srv->statfs = np_default_statfs;
	srv->plock = np_default_lock;
	srv->flock = np_default_flock;
	srv->rename = np_default_rename;

	srv->conns = NULL;
	srv->reqs_first = NULL;
	srv->reqs_last = NULL;
	srv->workreqs = NULL;
	srv->wthreads = NULL;
	srv->debuglevel = 0;
	srv->debugprintf = NULL;
	srv->nwthread = nwthread;

	for(i = 0; i < nwthread; i++)
		np_wthread_create(srv);

	return srv;
}

void
np_srv_start(Npsrv *srv)
{
	if (srv->start)
		(*srv->start)(srv);
}

void
np_srv_shutdown(Npsrv *srv, int shutconns)
{
	Npconn *conn, *conn1;

	pthread_mutex_lock(&srv->lock);
	srv->shuttingdown = 1;
	(*srv->shutdown)(srv);
	if (shutconns) {
		conn = srv->conns;
		srv->conns = NULL;
	}
	pthread_mutex_unlock(&srv->lock);

	while (conn != NULL) {
		conn1 = conn->next;
		np_conn_shutdown(conn);
		conn = conn1;
	}
}

int
np_srv_add_conn(Npsrv *srv, Npconn *conn)
{
	int ret;

	ret = 0;
	pthread_mutex_lock(&srv->lock);
	np_conn_incref(conn);
	if (!srv->shuttingdown) {
		conn->srv = srv;
		conn->next = srv->conns;
		srv->conns = conn;
		ret = 1;
	}
	pthread_mutex_unlock(&srv->lock);

	if (srv->connopen)
		(*srv->connopen)(conn);

	return ret;
}

void
np_srv_remove_conn(Npsrv *srv, Npconn *conn)
{
	Npconn *c, **pc;

	pthread_mutex_lock(&srv->lock);
	pc = &srv->conns;
	c = *pc;
	while (c != NULL) {
		if (c == conn) {
			*pc = c->next;
			c->next = NULL;
			break;
		}

		pc = &c->next;
		c = *pc;
	}

	if (srv->connclose)
		(*srv->connclose)(conn);

	np_conn_decref(conn);
	if (srv->shuttingdown && !srv->conns)
		np_srv_destroy(srv);

	pthread_mutex_unlock(&srv->lock);
}

static void
np_srv_destroy(Npsrv *srv)
{
	Npwthread *wt;

	for(wt = srv->wthreads; wt != NULL; wt = wt->next) {
		wt->shutdown = 1;
	}
	pthread_cond_broadcast(&srv->reqcond);
	(*srv->destroy)(srv);
}

void
np_srv_add_req(Npsrv *srv, Npreq *req)
{
	req->prev = srv->reqs_last;
	if (srv->reqs_last)
		srv->reqs_last->next = req;
	srv->reqs_last = req;
	if (!srv->reqs_first)
		srv->reqs_first = req;
	pthread_cond_signal(&srv->reqcond);
}

void
np_srv_remove_req(Npsrv *srv, Npreq *req)
{
	if (req->prev)
		req->prev->next = req->next;

	if (req->next)
		req->next->prev = req->prev;

	if (req == srv->reqs_first)
		srv->reqs_first = req->next;

	if (req == srv->reqs_last)
		srv->reqs_last = req->prev;
}

void
np_srv_add_workreq(Npsrv *srv, Npreq *req)
{
	if (srv->workreqs)
		srv->workreqs->prev = req;

	req->next = srv->workreqs;
	srv->workreqs = req;
	req->prev = NULL;
}

void
np_srv_remove_workreq(Npsrv *srv, Npreq *req)
{
	if (req->prev)
		req->prev->next = req->next;
	else
		srv->workreqs = req->next;

	if (req->next)
		req->next->prev = req->prev;
}

static void
np_wthread_create(Npsrv *srv)
{
	int err;
	Npwthread *wt;

	wt = malloc(sizeof(*wt));
	wt->srv = srv;
	wt->shutdown = 0;
	err = pthread_create(&wt->thread, NULL, np_wthread_proc, wt);
	if (err) {
		fprintf(stderr, "can't create thread: %d\n", err);
		return;
	}

	pthread_mutex_lock(&srv->lock);
	wt->next = srv->wthreads;
	srv->wthreads = wt;
	pthread_mutex_unlock(&srv->lock);
}


static Npfcall*
np_process_request(Npreq *req)
{
	Npconn *conn;
	Npfcall *tc, *rc;
	char *ename, *op;
	int ecode;

	conn = req->conn;
	rc = NULL;
	tc = req->tcall;

	np_werror(NULL, 0);
	switch (tc->type) {
		case Tstatfs:
			rc = np_statfs(req, tc);
			op = "statfs";
			break;
		case Trename:
			rc = np_rename(req, tc);
			op = "rename";
			break;
		case Tlock:
			rc = np_lock(req, tc);
			op = "lock";
			break;
		case Tflock:
			rc = np_flock(req, tc);
			op = "flock";
			break;
		case Taread:
			rc = np_aread(req, tc);
			op = "aread";
			break;
		case Tawrite:
			rc = np_awrite(req, tc);
			op = "awrite";
			break;
		case Tversion:
			rc = np_version(req, tc);
			op = "version";
			break;
		case Tauth:
			rc = np_auth(req, tc);
			op = "auth";
			break;
		case Tattach:
			rc = np_attach(req, tc);
			op = "attach";
			break;
		case Tflush:
			rc = np_flush(req, tc);
			op = "flush";
			break;
		case Twalk:
			rc = np_walk(req, tc);
			op = "walk";
			break;
		case Topen:
			rc = np_open(req, tc);
			op = "open";
			break;
		case Tcreate:
			rc = np_create(req, tc);
			op = "create";
			break;
		case Tread:
			rc = np_read(req, tc);
			op = "read";
			break;
		case Twrite:
			rc = np_write(req, tc);
			op = "write";
			break;
		case Tclunk:
			rc = np_clunk(req, tc);
			op = "clunk";
			break;
		case Tremove:
			rc = np_remove(req, tc);
			op = "remove";
			break;
		case Tstat:
			rc = np_stat(req, tc);
			op = "stat";
			break;
		case Twstat:
			rc = np_wstat(req, tc);
			op = "wstat";
			break;
		default:
			np_werror("unknown message type", ENOSYS);
			op = "<unknown>";
			break;
	}
	np_rerror(&ename, &ecode);
	if (ename != NULL) {
		if (rc)
			free(rc);
		rc = np_create_rerror(ename, ecode, np_conn_extend(conn));
		if ((req->conn->srv->debuglevel & DEBUG_9P_ERRORS)
		  			&& req->conn->srv->debugprintf) {
			req->conn->srv->debugprintf ("%s error: %s\n", op,
					ecode > 0 ? strerror(ecode) : ename);
		}
	}

	return rc;
}

static void *
np_wthread_proc(void *a)
{
	Npwthread *wt;
	Npsrv *srv;
	Npreq *req;
	Npfcall *rc;

	wt = a;
	srv = wt->srv;
	req = NULL;

	pthread_mutex_lock(&srv->lock);
	while (!wt->shutdown) {
		req = srv->reqs_first;
		if (!req) {
			pthread_cond_wait(&srv->reqcond, &srv->lock);
			continue;
		}

		np_srv_remove_req(srv, req);
		np_srv_add_workreq(srv, req);
		pthread_mutex_unlock(&srv->lock);

		req->wthread = wt;
		rc = np_process_request(req);
		if (rc)
			np_respond(req, rc);

		pthread_mutex_lock(&srv->lock);
	}

	return NULL;
}

void
np_respond(Npreq *req, Npfcall *rc)
{
	Npsrv *srv;
	Npreq *freq;

	srv = req->conn->srv;
	pthread_mutex_lock(&req->lock);
	if (req->responded) {
		free(rc);
		pthread_mutex_unlock(&req->lock);
		np_req_unref(req);
		return;
	}
	req->responded = 1;
	pthread_mutex_unlock(&req->lock);

	pthread_mutex_lock(&srv->lock);
	np_srv_remove_workreq(srv, req);
	for(freq = req->flushreq; freq != NULL; freq = freq->flushreq)
		np_srv_remove_workreq(srv, freq);
	pthread_mutex_unlock(&srv->lock);

	pthread_mutex_lock(&req->lock);
	req->rcall = rc;
	if (req->rcall) {
		if (req->rcall->type==Rread && req->fid->type&Qtdir)
			req->fid->diroffset = req->tcall->offset + req->rcall->count;

		np_set_tag(req->rcall, req->tag);
		if (req->fid != NULL) {
			np_fid_decref(req->fid);
			req->fid = NULL;
		}
		np_conn_respond(req);		
	}

	for(freq = req->flushreq; freq != NULL; freq = freq->flushreq) {
		pthread_mutex_lock(&freq->lock);
		freq->rcall = np_create_rflush();
		np_set_tag(freq->rcall, freq->tag);
		np_conn_respond(freq);
		pthread_mutex_unlock(&freq->lock);
		np_req_unref(freq);
	}
	pthread_mutex_unlock(&req->lock);
	np_req_unref(req);
}

void
np_respond_error(Npreq *req, char *ename, int ecode)
{
	Npfcall *rc;

	rc = np_create_rerror(ename, ecode, np_conn_extend(req->conn));
	np_respond(req, rc);
}

static Npfcall*
np_default_version(Npconn *conn, u32 msize, Npstr *version) 
{
	int min_msize = IOHDRSZ;
	int proto_ver;
	char *ver = NULL;
	Npfcall *rc = NULL;

	if (msize > conn->srv->msize)
		msize = conn->srv->msize;

	if (!np_strcmp(version, "9P2000")) {
		ver = NPFS_PROTO_LEGACY;
		ver = "9P2000";
	} else if (!np_strcmp(version, "9P2000.u")) {
		if (conn->proto_version == NPFS_PROTO_2000U
		 || conn->proto_version == NPFS_PROTO_2000L) {
			proto_ver = NPFS_PROTO_2000U;
			ver = "9P2000.u";
		} else
			np_werror("unsupported 9P version", EIO);
	} else if (!np_strcmp(version, "9P2000.L")) {
		if (conn->proto_version == NPFS_PROTO_2000L) {
			proto_ver = NPFS_PROTO_2000L;
			ver = "9P2000.L";
		} else
			np_werror("unsupported 9P version", EIO);
	} else
		np_werror("unsupported 9P version", EIO);

	if (msize < min_msize)
		np_werror("msize too small", EIO);

	if (!np_haserror ()) {
		np_conn_reset(conn, msize, proto_ver);
		if (!(rc = np_create_rversion(msize, ver)))
			np_uerror(ENOMEM);
	}

	return rc;
}

static Npfcall*
np_default_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname)
{
	np_werror(Enotimpl, EIO);
	return NULL;
}

static void
np_default_flush(Npreq *req)
{
}

static int
np_default_clone(Npfid *fid, Npfid *newfid)
{
	return 0;
}

static int
np_default_walk(Npfid *fid, Npstr* wname, Npqid *wqid)
{
	np_werror(Enotimpl, ENOSYS);
	return 0;
}

static Npfcall*
np_default_open(Npfid *fid, u8 perm)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_create(Npfid *fid, Npstr *name, u32 mode, u8 perm, Npstr *extension)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_clunk(Npfid *fid)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_remove(Npfid *fid)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_stat(Npfid *fid)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_wstat(Npfid *fid, Npstat *stat)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_aread(Npfid *fid, u8 datacheck, u64 offset, u32 count, u32 rsize, Npreq *req)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_awrite(Npfid *fid, u64 offset, u32 count, u32 rsize, u8 *data, Npreq *req)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_statfs(Npfid *fid)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_lock(Npfid *fid, u8 cmd, Nplock *lck)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_flock(Npfid *fid, u8 cmd)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

static Npfcall*
np_default_rename(Npfid *fid, Npfid *newdirfid, Npstr *newname)
{
	np_werror(Enotimpl, ENOSYS);
	return NULL;
}

Npreq *np_req_alloc(Npconn *conn, Npfcall *tc) {
	Npreq *req;

	req = NULL;
	pthread_mutex_lock(&reqpool.lock);
	if (reqpool.reqlist) {
		req = reqpool.reqlist;
		reqpool.reqlist = req->next;
		reqpool.reqnum--;
	}
	pthread_mutex_unlock(&reqpool.lock);
	
	if (!req)
		req = malloc(sizeof(*req));

	np_conn_incref(conn);
	pthread_mutex_init(&req->lock, NULL);
	req->refcount = 1;
	req->conn = conn;
	req->tag = tc->tag;
	req->tcall = tc;
	req->rcall = NULL;
	req->responded = 0;
	req->flushreq = NULL;
	req->next = NULL;
	req->prev = NULL;
	req->wthread = NULL;
	req->fid = NULL;

	return req;
}

Npreq *
np_req_ref(Npreq *req)
{
	pthread_mutex_lock(&req->lock);
	req->refcount++;
	pthread_mutex_unlock(&req->lock);
	return req;
}

void
np_req_unref(Npreq *req)
{
	pthread_mutex_lock(&req->lock);
	assert(req->refcount > 0);
	req->refcount--;
	if (req->refcount) {
		pthread_mutex_unlock(&req->lock);
		return;
	}
	pthread_mutex_unlock(&req->lock);

	if (req->conn)
		np_conn_decref(req->conn);

	pthread_mutex_lock(&reqpool.lock);
	if (reqpool.reqnum < 64) {
		req->next = reqpool.reqlist;
		reqpool.reqlist = req;
		reqpool.reqnum++;
		req = NULL;
	}

	pthread_mutex_unlock(&reqpool.lock);
	if (req)
		free(req);
}

