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
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

struct Reqpool {
	pthread_mutex_t	lock;
	int		reqnum;
	Npreq*		reqlist;
} reqpool = { PTHREAD_MUTEX_INITIALIZER, 0, NULL };

static Nptpool *np_tpool_create(Npsrv *srv, char *name);
static void np_tpool_cleanup (Npsrv *srv);
static void *np_wthread_proc(void *a);
static void np_respond(Nptpool *tp, Npreq *req, Npfcall *rc);
static void np_srv_remove_workreq(Nptpool *tp, Npreq *req);
static void np_srv_add_workreq(Nptpool *tp, Npreq *req);

static char *_ctl_get_version (void *a);
static char *_ctl_get_connections (void *a);
static char *_ctl_get_tpools (void *a);
static char *_ctl_get_requests (void *a);

Npsrv*
np_srv_create(int nwthread, int flags)
{
	Npsrv *srv = NULL;

	np_uerror (0);
	if (!(srv = malloc(sizeof(*srv)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	memset (srv, 0, sizeof (*srv));
	pthread_mutex_init(&srv->lock, NULL);
	pthread_cond_init(&srv->conncountcond, NULL);

	srv->msize = 8216;
	srv->flags = flags;

	if (np_ctl_initialize (srv) < 0)
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "version", _ctl_get_version, NULL))
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "connections",
			     _ctl_get_connections, srv))
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "tpools", _ctl_get_tpools, srv))
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "requests", _ctl_get_requests, srv))
		goto error;
	if (np_usercache_create (srv) < 0)
		goto error;
	srv->nwthread = nwthread;
	if (!(srv->tpool = np_tpool_create (srv, "default")))
		goto error;
	np_tpool_incref (srv->tpool);
	return srv;
error:
	if (srv)
		np_srv_destroy (srv);
	return NULL;
}

void
np_srv_destroy(Npsrv *srv)
{
	np_tpool_decref (srv->tpool);
	np_tpool_cleanup (srv);
	np_usercache_destroy (srv);
	np_ctl_finalize (srv);
	free (srv);
}

int
np_srv_add_conn(Npsrv *srv, Npconn *conn)
{
	int ret;

	ret = 0;
	xpthread_mutex_lock(&srv->lock);
	np_conn_incref(conn);
	conn->srv = srv;
	conn->next = srv->conns;
	srv->conns = conn;
	ret = 1;
	srv->conncount++;
	srv->connhistory++;
	xpthread_cond_signal(&srv->conncountcond);
	xpthread_mutex_unlock(&srv->lock);

	return ret;
}

void
np_srv_remove_conn(Npsrv *srv, Npconn *conn)
{
	Npconn *c, **pc;

	xpthread_mutex_lock(&srv->lock);
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

	np_conn_decref(conn);
	srv->conncount--;
	xpthread_cond_signal(&srv->conncountcond);
	xpthread_mutex_unlock(&srv->lock);

	// XXX temporarily skip this while working on issue 47
	// np_tpool_cleanup (srv);
}

/* Block the caller until the server has no active connections,
 * and there have been at least 'count' connections historically.
 */
void
np_srv_wait_conncount(Npsrv *srv, int count)
{
	xpthread_mutex_lock(&srv->lock);
	while (srv->conncount > 0 || srv->connhistory < count) {
		xpthread_cond_wait(&srv->conncountcond, &srv->lock);
	}
	xpthread_mutex_unlock(&srv->lock);
}

void
np_srv_add_req(Npsrv *srv, Npreq *req)
{
	Nptpool *tp = NULL;

	if (req->fid)
		tp = req->fid->tpool;
	if (!tp)
		tp = srv->tpool;
	xpthread_mutex_lock(&tp->lock);
	req->prev = tp->reqs_last;
	if (tp->reqs_last)
		tp->reqs_last->next = req;
	tp->reqs_last = req;
	if (!tp->reqs_first)
		tp->reqs_first = req;
	xpthread_mutex_unlock(&tp->lock);
	xpthread_cond_signal(&tp->reqcond);
}

void
np_srv_remove_req(Nptpool *tp, Npreq *req)
{
	/* assert: tp->lock held */
	if (req->prev)
		req->prev->next = req->next;
	if (req->next)
		req->next->prev = req->prev;
	if (req == tp->reqs_first)
		tp->reqs_first = req->next;
	if (req == tp->reqs_last)
		tp->reqs_last = req->prev;
}

static void
np_srv_add_workreq(Nptpool *tp, Npreq *req)
{
	/* assert: tp->lock held */
	if (tp->workreqs)
		tp->workreqs->prev = req;
	req->next = tp->workreqs;
	tp->workreqs = req;
	req->prev = NULL;
}

static void
np_srv_remove_workreq(Nptpool *tp, Npreq *req)
{
	/* assert: tp->lock held */
	if (req->prev)
		req->prev->next = req->next;
	else
		tp->workreqs = req->next;
	if (req->next)
		req->next->prev = req->prev;
}

static int
np_wthread_create(Nptpool *tp)
{
	int err;
	Npwthread *wt;

	if (!(wt = malloc(sizeof(*wt)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	memset (wt, 0, sizeof (*wt));
	wt->tpool = tp;
	wt->shutdown = 0;
	wt->state = WT_START;
	wt->fsuid = geteuid ();
	wt->sguid = P9_NONUNAME;
	wt->fsgid = getegid ();
	if ((err = pthread_create(&wt->thread, NULL, np_wthread_proc, wt))) {
		np_uerror (err);
		goto error;
	}
	xpthread_mutex_lock(&tp->lock);
	wt->next = tp->wthreads;
	tp->wthreads = wt;
	xpthread_mutex_unlock(&tp->lock);
	return 0;
error:
	return -1;
}

static void
np_tpool_destroy(Nptpool *tp)
{
	Npsrv *srv = tp->srv;
	Npwthread *wt, *next;
	void *retval;
	int err, i;

	for(wt = tp->wthreads; wt != NULL; wt = wt->next) {
		wt->shutdown = 1;
	}
	xpthread_cond_broadcast(&tp->reqcond);
	for (i = 0, wt = tp->wthreads; wt != NULL; wt = next, i++) {
		next = wt->next;
		if ((err = pthread_join (wt->thread, &retval))) {
			np_uerror (err);
			np_logerr(srv, "%s: join thread %d", tp->name, i);
		} else if (retval == PTHREAD_CANCELED) {
			np_logmsg(srv, "%s: join thread %d: cancelled",
					tp->name, i);
		} else if (retval != NULL) {
			np_logmsg(srv, "%s: join thread %d: non-NULL return",
					tp->name, i);
		}
		free (wt);
	}
	pthread_cond_destroy (&tp->reqcond);
	pthread_mutex_destroy (&tp->lock);
	pthread_mutex_destroy (&tp->stats.lock);
	if (tp->name)
		free (tp->name);
	free (tp);
}

static Nptpool *
np_tpool_create(Npsrv *srv, char *name)
{
	Nptpool *tp;

	if (!(tp = malloc (sizeof (*tp)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	memset (tp, 0, sizeof (*tp));
	if (!(tp->name = strdup (name))) {
		np_uerror (ENOMEM);
		goto error;
	}
	tp->srv = srv;
	tp->refcount = 0;
	pthread_mutex_init(&tp->stats.lock, NULL);
	pthread_mutex_init(&tp->lock, NULL);
	pthread_cond_init(&tp->reqcond, NULL);
	for(tp->nwthread = 0; tp->nwthread < srv->nwthread; tp->nwthread++) {
		if (np_wthread_create(tp) < 0)
			goto error;
	}
	return tp;
error:
	if (tp)
		np_tpool_destroy (tp);
	return NULL;
}

void
np_tpool_incref (Nptpool *tp)
{
	if (!tp)
		return;
	xpthread_mutex_lock (&tp->lock);
	tp->refcount++;
	xpthread_mutex_unlock (&tp->lock);
}

void
np_tpool_select (Npreq *req)
{
	Npsrv *srv = req->conn->srv;
	Nptpool *tp;

	if ((srv->flags & SRV_FLAGS_TPOOL_SINGLE))
		return;
	if (!req->fid || !req->fid->aname || *req->fid->aname != '/')
		return;
	if (req->fid->tpool)
		return;

	xpthread_mutex_lock (&srv->lock);
	for (tp = srv->tpool; tp != NULL; tp = tp->next) {
		if (!strcmp (req->fid->aname, tp->name))
			break;
	}
	if (!tp) {
		tp = np_tpool_create(srv, req->fid->aname);
		if (tp) {
			assert (srv->tpool); /* default tpool */
			tp->next = srv->tpool->next;
			srv->tpool->next = tp;
		} else
			np_logerr (srv, "np_tpool_create %s", req->fid->aname);
	}
	if (tp) {
		np_tpool_incref (tp);
		req->fid->tpool = tp;
	}
	xpthread_mutex_unlock (&srv->lock);
}

/* Tpool cleanup occurs when conns are destroyed.  This serves two purposes:
 * 1) avoids gratuitous create/destroy/create in user/kernel auth handoff
 * 2) avoids cleanup in context of thread handling tclunk (join EDEADLK)
 */
void
np_tpool_decref (Nptpool *tp)
{
	if (!tp)
		return;
	xpthread_mutex_lock (&tp->lock);
	tp->refcount--;
	xpthread_mutex_unlock (&tp->lock);
}

static void
np_tpool_cleanup (Npsrv *srv)
{
	Nptpool *tp, *next, *dead , *prev = NULL;

	xpthread_mutex_lock (&srv->lock);
	prev = NULL;
	dead = NULL;
	for (tp = srv->tpool; tp != NULL; tp = next) {
		next = tp->next;
		xpthread_mutex_lock (&tp->lock);
		assert (tp->refcount >= 0);
		if (tp->refcount == 0) {
			tp->next = dead;
			dead = tp;
			if (prev)
				prev->next = next;
			else
				srv->tpool = next;
		} else
			prev = tp;
		xpthread_mutex_unlock (&tp->lock);
	}
	xpthread_mutex_unlock (&srv->lock);
	for (tp = dead; tp != NULL; tp = next) {
		next = tp->next;
		np_tpool_destroy (tp);	
	}
}

/* Look up operation's fid and assign it to req->fid.
 * This is done before the request is handed off to a worker, with
 * the plan of using fid data in scheduling work.
 * The fid refcount is incremented here, then decremented in np_respond ().
 */
static void
np_preprocess_request(Npreq *req)
{
	Npfcall *tc = req->tcall;
	Npconn *conn = req->conn;

	switch (tc->type) {
		case P9_TSTATFS:
			req->fid = np_fid_find (conn, tc->u.tstatfs.fid);
			break;
		case P9_TLOPEN:
			req->fid = np_fid_find (conn, tc->u.tlopen.fid);
			break;
		case P9_TLCREATE:
			req->fid = np_fid_find (conn, tc->u.tlcreate.fid);
			break;
		case P9_TSYMLINK:
			req->fid = np_fid_find (conn, tc->u.tsymlink.fid);
			break;
		case P9_TMKNOD:
			req->fid = np_fid_find (conn, tc->u.tmknod.fid);
			break;
		case P9_TRENAME:
			req->fid = np_fid_find (conn, tc->u.trename.fid);
			break;
		case P9_TREADLINK:
			req->fid = np_fid_find (conn, tc->u.treadlink.fid);
			break;
		case P9_TGETATTR:
			req->fid = np_fid_find (conn, tc->u.tgetattr.fid);
			break;
		case P9_TSETATTR:
			req->fid = np_fid_find (conn, tc->u.tsetattr.fid);
			break;
		case P9_TXATTRWALK:
			req->fid = np_fid_find (conn, tc->u.txattrwalk.fid);
			break;
		case P9_TXATTRCREATE:
			req->fid = np_fid_find (conn, tc->u.txattrcreate.fid);
			break;
		case P9_TREADDIR:
			req->fid = np_fid_find (conn, tc->u.treaddir.fid);
			break;
		case P9_TFSYNC:
			req->fid = np_fid_find (conn, tc->u.tfsync.fid);
			break;
		case P9_TLOCK:
			req->fid = np_fid_find (conn, tc->u.tlock.fid);
			break;
		case P9_TGETLOCK:
			req->fid = np_fid_find (conn, tc->u.tgetlock.fid);
			break;
		case P9_TLINK:
			req->fid = np_fid_find (conn, tc->u.tlink.dfid);
			break;
		case P9_TMKDIR:
			req->fid = np_fid_find (conn, tc->u.tmkdir.fid);
			break;
		case P9_TVERSION:
			break;
		case P9_TAUTH:
			if (np_fid_find (conn, tc->u.tauth.afid))
				break;
			req->fid = np_fid_create (conn, tc->u.tauth.afid, NULL);
			if (!req->fid)
				break;
			req->fid->aname = np_strdup (&tc->u.tauth.aname);
			if (!req->fid->aname) {
				np_fid_destroy(req->fid);
				req->fid = NULL;
			}
			/* XXX leave fid->tpool NULL for now as auth
			 * can be handled in the default thread pool
			 * without risk of deadlock.
			 */
			break;
		case P9_TATTACH:
			if (np_fid_find (conn, tc->u.tattach.fid))
				break;
			req->fid = np_fid_create (conn, tc->u.tattach.fid,NULL);
			if (!req->fid)
				break;
			req->fid->aname = np_strdup (&tc->u.tattach.aname);
			if (!req->fid->aname) {
				np_fid_destroy(req->fid);
				req->fid = NULL;
			}
			np_tpool_select (req);
			break;
		case P9_TFLUSH:
			break;
		case P9_TWALK:
			req->fid = np_fid_find (conn, tc->u.twalk.fid);
			break;
		case P9_TREAD:
			req->fid = np_fid_find (conn, tc->u.tread.fid);
			break;
		case P9_TWRITE:
			req->fid = np_fid_find (conn, tc->u.twrite.fid);
			break;
		case P9_TCLUNK:
			req->fid = np_fid_find (conn, tc->u.tclunk.fid);
			break;
		case P9_TREMOVE:
			req->fid = np_fid_find (conn, tc->u.tremove.fid);
			break;
		default:
			break;
	}
	if (req->fid)
		np_fid_incref (req->fid);
}

static Npfcall*
np_process_request(Npreq *req, Npstats *stats)
{
	Npfcall *rc = NULL;
	Npfcall *tc = req->tcall;
	int ecode, valid_op = 1;
	u64 rbytes = 0, wbytes = 0;

	np_uerror(0);
	switch (tc->type) {
		case P9_TSTATFS:
			rc = np_statfs(req, tc);
			break;
		case P9_TLOPEN:
			rc = np_lopen(req, tc);
			break;
		case P9_TLCREATE:
			rc = np_lcreate(req, tc);
			break;
		case P9_TSYMLINK:
			rc = np_symlink(req, tc);
			break;
		case P9_TMKNOD:
			rc = np_mknod(req, tc);
			break;
		case P9_TRENAME:
			rc = np_rename(req, tc);
			break;
		case P9_TREADLINK:
			rc = np_readlink(req, tc);
			break;
		case P9_TGETATTR:
			rc = np_getattr(req, tc);
			break;
		case P9_TSETATTR:
			rc = np_setattr(req, tc);
			break;
		case P9_TXATTRWALK:
			rc = np_xattrwalk(req, tc);
			break;
		case P9_TXATTRCREATE:
			rc = np_xattrcreate(req, tc);
			break;
		case P9_TREADDIR:
			rc = np_readdir(req, tc);
			break;
		case P9_TFSYNC:
			rc = np_fsync(req, tc);
			break;
		case P9_TLOCK:
			rc = np_lock(req, tc);
			break;
		case P9_TGETLOCK:
			rc = np_getlock(req, tc);
			break;
		case P9_TLINK:
			rc = np_link(req, tc);
			break;
		case P9_TMKDIR:
			rc = np_mkdir(req, tc);
			break;
		case P9_TVERSION:
			rc = np_version(req, tc);
			break;
		case P9_TAUTH:
			rc = np_auth(req, tc);
			break;
		case P9_TATTACH:
			rc = np_attach(req, tc);
			break;
		case P9_TFLUSH:
			rc = np_flush(req, tc);
			break;
		case P9_TWALK:
			rc = np_walk(req, tc);
			break;
		case P9_TREAD:
			rc = np_read(req, tc);
			rbytes = rc->u.rread.count;
			break;
		case P9_TWRITE:
			rc = np_write(req, tc);
			wbytes = rc->u.rwrite.count;
			break;
		case P9_TCLUNK:
			rc = np_clunk(req, tc);
			break;
		case P9_TREMOVE:
			rc = np_remove(req, tc);
			break;
		default: /* N.B. shouldn't get here - unhandled ops are
			  * caught in np_deserialize ().
			  */
			np_uerror(ENOSYS);
			valid_op = 0;
			break;
	}
	if ((ecode = np_rerror())) {
		if (rc)
			free(rc);
		rc = np_create_rlerror(ecode);
	}
	if (valid_op) {
		xpthread_mutex_lock (&stats->lock);
		stats->rbytes += rbytes;
		stats->wbytes += wbytes;
		stats->nreqs[tc->type]++;
		xpthread_mutex_unlock (&stats->lock);
	}

	return rc;
}

static void *
np_wthread_proc(void *a)
{
	Npwthread *wt = (Npwthread *)a;
	Nptpool *tp = wt->tpool;
	Npreq *req = NULL;
	Npfcall *rc;

	xpthread_mutex_lock(&tp->lock);
	while (!wt->shutdown) {
		wt->state = WT_IDLE;
		req = tp->reqs_first;
		if (!req) {
			xpthread_cond_wait(&tp->reqcond, &tp->lock);
			continue;
		}

		np_srv_remove_req(tp, req);
		np_srv_add_workreq(tp, req);
		xpthread_mutex_unlock(&tp->lock);

		req->wthread = wt;
		wt->state = WT_WORK;
		rc = np_process_request(req, &tp->stats);
		if (rc) {
			wt->state = WT_REPLY;
			np_respond(tp, req, rc);
		}
		xpthread_mutex_lock(&tp->lock);
	}
	xpthread_mutex_unlock (&tp->lock);
	wt->state = WT_SHUT;

	return NULL;
}

static void
np_respond(Nptpool *tp, Npreq *req, Npfcall *rc)
{
	Npreq *freq;

	xpthread_mutex_lock(&req->lock);
	if (req->responded) {
		free(rc);
		xpthread_mutex_unlock(&req->lock);
		np_req_unref(req);
		return;
	}
	req->responded = 1;
	xpthread_mutex_unlock(&req->lock);

	xpthread_mutex_lock(&tp->lock);
	np_srv_remove_workreq(tp, req);
	for(freq = req->flushreq; freq != NULL; freq = freq->flushreq)
		np_srv_remove_workreq(tp, freq);
	xpthread_mutex_unlock(&tp->lock);

	xpthread_mutex_lock(&req->lock);
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
		xpthread_mutex_lock(&freq->lock);
		freq->rcall = np_create_rflush();
		np_set_tag(freq->rcall, freq->tag);
		np_conn_respond(freq);
		xpthread_mutex_unlock(&freq->lock);
		np_req_unref(freq);
	}
	xpthread_mutex_unlock(&req->lock);
	np_req_unref(req);
}

Npreq *np_req_alloc(Npconn *conn, Npfcall *tc) {
	Npreq *req;

	req = NULL;
	xpthread_mutex_lock(&reqpool.lock);
	if (reqpool.reqlist) {
		req = reqpool.reqlist;
		reqpool.reqlist = req->next;
		reqpool.reqnum--;
	}
	xpthread_mutex_unlock(&reqpool.lock);

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

	np_preprocess_request (req); /* assigns req->fid */

	return req;
}

Npreq *
np_req_ref(Npreq *req)
{
	xpthread_mutex_lock(&req->lock);
	req->refcount++;
	xpthread_mutex_unlock(&req->lock);
	return req;
}

void
np_req_unref(Npreq *req)
{
	xpthread_mutex_lock(&req->lock);
	assert(req->refcount > 0);
	req->refcount--;
	if (req->refcount) {
		xpthread_mutex_unlock(&req->lock);
		return;
	}
	xpthread_mutex_unlock(&req->lock);

	if (req->conn)
		np_conn_decref(req->conn);

	xpthread_mutex_lock(&reqpool.lock);
	if (reqpool.reqnum < 64) {
		req->next = reqpool.reqlist;
		reqpool.reqlist = req;
		reqpool.reqnum++;
		req = NULL;
	}
	xpthread_mutex_unlock(&reqpool.lock);
	if (req)
		free(req);
}


void
np_logmsg(Npsrv *srv, const char *fmt, ...)
{
	va_list ap;

	va_start (ap, fmt);
	if (srv->logmsg)
		srv->logmsg (fmt, ap);
	va_end (ap);
}

void
np_logerr(Npsrv *srv, const char *fmt, ...)
{
	va_list ap;

	if (srv->logmsg) {
		char buf[128];
		char ebuf[64];
		char *s = strerror_r (np_rerror (), ebuf, sizeof (ebuf));

		va_start (ap, fmt);
		vsnprintf (buf, sizeof(buf), fmt, ap);
		va_end (ap);

		np_logmsg (srv, "%s: %s", buf, s);
	}
}

static char *
_ctl_get_version (void *a)
{
	char *s = NULL;
	int len = 0;

	if (aspf (&s, &len, "%s\n", META_ALIAS) < 0)
                np_uerror (ENOMEM);
        return s;
}

static char *
_ctl_get_connections (void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Npconn *cc;
	char *s = NULL;
	int len = 0;

	xpthread_mutex_lock(&srv->lock);
	for (cc = srv->conns; cc != NULL; cc = cc->next) {
		xpthread_mutex_lock(&cc->lock);
		if (aspf (&s, &len, "%s %"PRIu64" %"PRIu64" %d\n",
				np_conn_get_client_id(cc),
				cc->reqs_in, cc->reqs_out,
				np_fidpool_count (cc->fidpool)) < 0) {
			np_uerror (ENOMEM);
			goto error_unlock;
		}
		xpthread_mutex_unlock(&cc->lock);
	}
	xpthread_mutex_unlock(&srv->lock);
	return s;
error_unlock:
	xpthread_mutex_unlock(&cc->lock);
	xpthread_mutex_unlock(&srv->lock);
	if (s)
		free(s);
	return NULL;
}

static char *
_ctl_get_tpools (void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Nptpool *tp;
	Npreq *req;
	char *s = NULL;
	int n, len = 0;

	xpthread_mutex_lock(&srv->lock);
	for (tp = srv->tpool; tp != NULL; tp = tp->next) {
		xpthread_mutex_lock(&tp->lock);
		xpthread_mutex_lock(&tp->stats.lock);
		tp->stats.name = tp->name;
		tp->stats.numfids = tp->refcount;
		tp->stats.numreqs = 0;
		for (req = tp->reqs_first; req != NULL; req = req->next)
			tp->stats.numreqs++;
		for (req = tp->workreqs; req != NULL; req = req->next)
			tp->stats.numreqs++;
		n = np_encode_tpools_str (&s, &len, &tp->stats);
		xpthread_mutex_unlock(&tp->stats.lock);
		xpthread_mutex_unlock(&tp->lock);
		if (n < 0) {
			np_uerror (ENOMEM);
			goto error_unlock;
		}
	}
	xpthread_mutex_unlock(&srv->lock);
	return s;
error_unlock:
	xpthread_mutex_unlock(&srv->lock);
	if (s)
		free(s);
	return NULL;
}

static char *
_get_one_request (char **sp, int *lp, Npreq *req)
{
	char *uname = req->fid ? req->fid->user->uname : "-";
	char *aname = req->fid && req->fid->aname ? req->fid->aname : "-";
	char reqstr[40];

	np_snprintfcall (reqstr, sizeof (reqstr), req->tcall);
	if (aspf (sp, lp, "%-10.10s %-10.10s %-10.10s %s...\n",
		 			np_conn_get_client_id (req->conn),
					aname, uname, reqstr) < 0) {
		np_uerror (ENOMEM);
		return NULL;
	}
	return *sp;
}

static char *
_ctl_get_requests(void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Nptpool *tp;
	char *s = NULL;
	int len = 0;
	Npreq *req;

	xpthread_mutex_lock(&srv->lock);
	for (tp = srv->tpool; tp != NULL; tp = tp->next) {
		xpthread_mutex_lock(&tp->lock);
		for (req = tp->workreqs; req != NULL; req = req->next)
			if (!(_get_one_request (&s, &len, req)))
				goto error_unlock;
		for (req = tp->reqs_first; req != NULL; req = req->next)
			if (!(_get_one_request (&s, &len, req)))
				goto error_unlock;
		xpthread_mutex_unlock(&tp->lock);
	}
	xpthread_mutex_unlock(&srv->lock);
	return s;
error_unlock:
	xpthread_mutex_unlock(&tp->lock);
	xpthread_mutex_unlock(&srv->lock);
	if (s)
		free(s);
	return NULL;
}
