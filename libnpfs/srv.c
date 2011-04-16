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
#include <sys/time.h>
#include <assert.h>
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

struct Reqpool {
	pthread_mutex_t	lock;
	int		reqnum;
	Npreq*		reqlist;
} reqpool = { PTHREAD_MUTEX_INITIALIZER, 0, NULL };

static int np_wthread_create(Npsrv *srv);
static void *np_wthread_proc(void *a);

static Npfcall* np_default_version(Npconn *, u32, Npstr *);
static Npfcall* np_default_attach(Npfid *, Npfid *, Npstr *);
static void np_default_flush(Npreq *);
static int np_default_clone(Npfid *, Npfid *);
static int np_default_walk(Npfid *, Npstr*, Npqid *);
static Npfcall* np_default_read(Npfid *, u64, u32, Npreq *);
static Npfcall* np_default_write(Npfid *, u64, u32, u8*, Npreq *);
static Npfcall* np_default_clunk(Npfid *);
static Npfcall* np_default_remove(Npfid *);
#if HAVE_LARGEIO
static Npfcall* np_default_aread(Npfid *, u8, u64, u32, u32, Npreq *);
static Npfcall* np_default_awrite(Npfid *, u64, u32, u32, u8*, Npreq *);
#endif
static Npfcall* np_default_statfs(Npfid *);
static Npfcall* np_default_lopen(Npfid *, u32);
static Npfcall* np_default_lcreate(Npfid *, Npstr *, u32, u32, u32);
static Npfcall* np_default_symlink(Npfid *, Npstr *, Npstr *, u32);
static Npfcall* np_default_mknod(Npfid *, Npstr *, u32, u32, u32, u32);
static Npfcall* np_default_rename(Npfid *, Npfid *, Npstr *);
static Npfcall* np_default_readlink(Npfid *);
static Npfcall* np_default_getattr(Npfid *, u64);
static Npfcall* np_default_setattr(Npfid *, u32, u32, u32, u32, u64,
                                   u64, u64, u64, u64);
static Npfcall* np_default_xattrwalk(void); /* FIXME */
static Npfcall* np_default_xattrcreate(void); /* FIXME */
static Npfcall* np_default_readdir(Npfid *, u64, u32, Npreq *);
static Npfcall* np_default_fsync(Npfid *);
static Npfcall* np_default_lock(Npfid *, u8, u32, u64, u64, u32, Npstr *);
static Npfcall* np_default_getlock(Npfid *, u8, u64, u64, u32, Npstr *);
static Npfcall* np_default_link(Npfid *, Npfid *, Npstr *);
static Npfcall* np_default_mkdir(Npfid *, Npstr *, u32, u32);

Npsrv*
np_srv_create(int nwthread)
{
	int i;
	Npsrv *srv;

	np_uerror (0);
	if (!(srv = malloc(sizeof(*srv)))) {
		errno = ENOMEM;
		return NULL;
	}
	pthread_mutex_init(&srv->lock, NULL);
	pthread_cond_init(&srv->reqcond, NULL);
	pthread_cond_init(&srv->conncountcond, NULL);
	srv->conncount = 0;
	srv->connhistory = 0;
	srv->msize = 8216;
	srv->srvaux = NULL;
	srv->treeaux = NULL;
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
	srv->read = np_default_read;
	srv->write = np_default_write;
	srv->clunk = np_default_clunk;
	srv->remove = np_default_remove;
	srv->upool = NULL;
#if HAVE_LARGEIO
	srv->aread = np_default_aread;
	srv->awrite = np_default_awrite;
#endif
	srv->statfs = np_default_statfs;
	srv->lopen = np_default_lopen;
	srv->lcreate = np_default_lcreate;
	srv->symlink = np_default_symlink;
	srv->mknod = np_default_mknod;
	srv->rename = np_default_rename;
	srv->readlink= np_default_readlink;
	srv->getattr = np_default_getattr;
	srv->setattr = np_default_setattr;
	srv->xattrwalk = np_default_xattrwalk;
	srv->xattrcreate = np_default_xattrcreate;
	srv->readdir = np_default_readdir;
	srv->fsync = np_default_fsync;
	srv->llock = np_default_lock;
	srv->getlock = np_default_getlock;
	srv->link = np_default_link;
	srv->mkdir = np_default_mkdir;

	srv->conns = NULL;
	srv->reqs_first = NULL;
	srv->reqs_last = NULL;
	srv->workreqs = NULL;
	srv->wthreads = NULL;
	srv->debuglevel = 0;
	srv->debugprintf = NULL;
	srv->nwthread = nwthread;
	for(i = 0; i < nwthread; i++) {
		if (np_wthread_create(srv) < 0) {
			free (srv);
			return NULL;
		}
	}

	return srv;
}

void
np_srv_destroy(Npsrv *srv)
{
	Npwthread *wt, *next;

	for(wt = srv->wthreads; wt != NULL; wt = wt->next) {
		wt->shutdown = 1;
	}
	pthread_cond_broadcast(&srv->reqcond);

	/* FIXME: will block here forever if wt is blocked in the
	 * kernel on some op.  Consider signaling?
	 */
	for (wt = srv->wthreads; wt != NULL; wt = next) {
		next = wt->next;
		pthread_join (wt->thread, NULL);
		free (wt);
	}
	if (srv->destroy)
		(*srv->destroy)(srv);
	free (srv);
}

void
np_srv_start(Npsrv *srv)
{
	if (srv->start)
		(*srv->start)(srv);
}

int
np_srv_add_conn(Npsrv *srv, Npconn *conn)
{
	int ret;

	ret = 0;
	pthread_mutex_lock(&srv->lock);
	np_conn_incref(conn);
	conn->srv = srv;
	conn->next = srv->conns;
	srv->conns = conn;
	ret = 1;
	srv->conncount++;
	srv->connhistory++;
	pthread_cond_signal(&srv->conncountcond);
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
	srv->conncount--;
	pthread_cond_signal(&srv->conncountcond);
	pthread_mutex_unlock(&srv->lock);
}

/* Block the caller until the server has no active connections,
 * and there have been at least 'count' connections historically.
 */
void
np_srv_wait_conncount(Npsrv *srv, int count)
{
	pthread_mutex_lock(&srv->lock);
	while (srv->conncount > 0 || srv->connhistory < count)
		pthread_cond_wait(&srv->conncountcond, &srv->lock);
	pthread_mutex_unlock(&srv->lock);
}

/* Block the caller until the server has no active connections for
 * 'inactivity_secs'.
 */
void
np_srv_wait_timeout(Npsrv *srv, int inactivity_secs)
{
	struct timeval tv;
	struct timespec ts;
	int rc = 0;

	do {
		pthread_mutex_lock(&srv->lock);
		while (srv->conncount == 0 && rc != ETIMEDOUT) {
			gettimeofday (&tv, NULL);
			ts.tv_sec = tv.tv_sec + inactivity_secs;
			ts.tv_nsec = tv.tv_usec * 1000;
			rc = pthread_cond_timedwait(&srv->conncountcond,
						    &srv->lock, &ts);
		}
		while (srv->conncount > 0)
			pthread_cond_wait(&srv->conncountcond, &srv->lock);
		pthread_mutex_unlock(&srv->lock);
	} while (rc != ETIMEDOUT);
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

static int
np_wthread_create(Npsrv *srv)
{
	int err;
	Npwthread *wt;

	if (!(wt = malloc(sizeof(*wt)))) {
		errno = ENOMEM;
		return -1;
	}
	wt->srv = srv;
	wt->shutdown = 0;
	wt->state = WT_START;
	err = pthread_create(&wt->thread, NULL, np_wthread_proc, wt);
	if (err) {
		errno = err;
		return -1;
	}

	pthread_mutex_lock(&srv->lock);
	wt->next = srv->wthreads;
	srv->wthreads = wt;
	pthread_mutex_unlock(&srv->lock);
	return 0;
}


static Npfcall*
np_process_request(Npreq *req)
{
	Npconn *conn;
	Npfcall *tc, *rc;
	char *op;
	int ecode;

	conn = req->conn;
	rc = NULL;
	tc = req->tcall;

	np_uerror(0);
	switch (tc->type) {
		case P9_TSTATFS:
			rc = np_statfs(req, tc);
			op = "statfs";
			break;
		case P9_TLOPEN:
			rc = np_lopen(req, tc);
			op = "lopen";
			break;
		case P9_TLCREATE:
			rc = np_lcreate(req, tc);
			op = "lcreate";
			break;
		case P9_TSYMLINK:
			rc = np_symlink(req, tc);
			op = "symlink";
			break;
		case P9_TMKNOD:
			rc = np_mknod(req, tc);
			op = "mknod";
			break;
		case P9_TRENAME:
			rc = np_rename(req, tc);
			op = "rename";
			break;
		case P9_TREADLINK:
			rc = np_readlink(req, tc);
			op = "readlink";
			break;
		case P9_TGETATTR:
			rc = np_getattr(req, tc);
			op = "getattr";
			break;
		case P9_TSETATTR:
			rc = np_setattr(req, tc);
			op = "setattr";
			break;
		case P9_TXATTRWALK:
			rc = np_xattrwalk(req, tc);
			op = "xattrwalk";
			break;
		case P9_TXATTRCREATE:
			rc = np_xattrcreate(req, tc);
			op = "xattrcreate";
			break;
		case P9_TREADDIR:
			rc = np_readdir(req, tc);
			op = "readdir";
			break;
		case P9_TFSYNC:
			rc = np_fsync(req, tc);
			op = "fsync";
			break;
		case P9_TLOCK:
			rc = np_lock(req, tc);
			op = "lock";
			break;
		case P9_TGETLOCK:
			rc = np_getlock(req, tc);
			op = "getlock";
			break;
		case P9_TLINK:
			rc = np_link(req, tc);
			op = "link";
			break;
		case P9_TMKDIR:
			rc = np_mkdir(req, tc);
			op = "mkdir";
			break;
#if HAVE_LARGEIO
		case P9_TAREAD:
			rc = np_aread(req, tc);
			op = "aread";
			break;
		case P9_TAWRITE:
			rc = np_awrite(req, tc);
			op = "awrite";
			break;
#endif
		case P9_TVERSION:
			rc = np_version(req, tc);
			op = "version";
			break;
		case P9_TAUTH:
			rc = np_auth(req, tc);
			op = "auth";
			break;
		case P9_TATTACH:
			rc = np_attach(req, tc);
			op = "attach";
			break;
		case P9_TFLUSH:
			rc = np_flush(req, tc);
			op = "flush";
			break;
		case P9_TWALK:
			rc = np_walk(req, tc);
			op = "walk";
			break;
		case P9_TREAD:
			rc = np_read(req, tc);
			op = "read";
			break;
		case P9_TWRITE:
			rc = np_write(req, tc);
			op = "write";
			break;
		case P9_TCLUNK:
			rc = np_clunk(req, tc);
			op = "clunk";
			break;
		case P9_TREMOVE:
			rc = np_remove(req, tc);
			op = "remove";
			break;
		default:
			np_uerror(ENOSYS);
			op = "<unknown>";
			break;
	}
	if ((ecode = np_rerror())) {
		if (rc)
			free(rc);
		rc = np_create_rlerror(ecode);
		if ((req->conn->srv->debuglevel & DEBUG_9P_ERRORS)
		  			&& req->conn->srv->debugprintf) {
			req->conn->srv->debugprintf ("%s error: %s",
					op, strerror(ecode));
		}
	}

	return rc;
}

static void *
np_wthread_proc(void *a)
{
	Npwthread *wt = (Npwthread *)a;
	Npsrv *srv = wt->srv;
	Npreq *req = NULL;
	Npfcall *rc;

	pthread_mutex_lock(&srv->lock);
	while (!wt->shutdown) {
		req = srv->reqs_first;
		if (!req) {
			wt->state = WT_IDLE;
			pthread_cond_wait(&srv->reqcond, &srv->lock);
			continue;
		}

		np_srv_remove_req(srv, req);
		np_srv_add_workreq(srv, req);
		pthread_mutex_unlock(&srv->lock);

		req->wthread = wt;
		wt->state = WT_WORK;
		rc = np_process_request(req);
		if (rc) {
			wt->state = WT_REPLY;
			np_respond(req, rc);
		}

		pthread_mutex_lock(&srv->lock);
	}
	pthread_mutex_unlock (&srv->lock);
	wt->state = WT_SHUT;

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

static Npfcall*
np_default_version(Npconn *conn, u32 msize, Npstr *version) 
{
	Npfcall *rc = NULL;

	/* msize already checked for > P9_IOHDRSZ */
	if (msize > conn->msize)
		msize = conn->msize;
	if (msize < conn->msize)
		conn->msize = msize; /* conn->msize can only be reduced */
	if (np_strcmp(version, "9P2000.L") == 0) {
		if (!(rc = np_create_rversion(msize, "9P2000.L")))
			np_uerror(ENOMEM);
	} else
		np_uerror(EIO);
	return rc;
}

static Npfcall*
np_default_attach(Npfid *fid, Npfid *afid, Npstr *aname)
{
	np_uerror(EIO);
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
	np_uerror(ENOSYS);
	return 0;
}

static Npfcall*
np_default_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	np_uerror(ENOSYS);
	return NULL;
}

static Npfcall*
np_default_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	np_uerror(ENOSYS);
	return NULL;
}

static Npfcall*
np_default_clunk(Npfid *fid)
{
	np_uerror(ENOSYS);
	return NULL;
}

static Npfcall*
np_default_remove(Npfid *fid)
{
	np_uerror(ENOSYS);
	return NULL;
}

#if HAVE_LARGEIO
static Npfcall*
np_default_aread(Npfid *fid, u8 datacheck, u64 offset, u32 count, u32 rsize, Npreq *req)
{
	np_uerror(ENOSYS);
	return NULL;
}

static Npfcall*
np_default_awrite(Npfid *fid, u64 offset, u32 count, u32 rsize, u8 *data, Npreq *req)
{
	np_uerror(ENOSYS);
	return NULL;
}
#endif
static Npfcall*
np_default_statfs(Npfid *fid)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_lopen(Npfid *fid, u32 mode)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_lcreate(Npfid *fid, Npstr *name, u32 flags, u32 mode, u32 gid)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_symlink(Npfid *fid, Npstr *name, Npstr *symtgt, u32 gid)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_mknod(Npfid *fid, Npstr *name, u32 mode, u32 major, u32 minor,
		 u32 gid)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_rename(Npfid *fid, Npfid *dfid, Npstr *name)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_readlink(Npfid *fid)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_getattr(Npfid *fid, u64 request_mask)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_setattr(Npfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
                   u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_xattrwalk(void) 
{
	/* FIXME: args */
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_xattrcreate(void) 
{
	/* FIXME: args */
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_fsync(Npfid *fid)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_lock(Npfid *fid, u8 type, u32 flags, u64 start, u64 length,
		u32 proc_id, Npstr *client_id)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_getlock(Npfid *fid, u8 type, u64 start, u64 length, u32 proc_id,
		   Npstr *client_id)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_link(Npfid *dfid, Npfid *oldfid, Npstr *newpath)
{
	np_uerror(ENOSYS);
	return NULL;
}
static Npfcall*
np_default_mkdir(Npfid *fid, Npstr *name, u32 mode, u32 gid)
{
	np_uerror(ENOSYS);
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

	if (!req) {
		req = malloc(sizeof(*req));
		if (!req)
			return NULL;
	}

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

