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

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

struct Reqpool {
	pthread_mutex_t	lock;
	int		reqnum;
	Npreq*		reqlist;
} reqpool = { PTHREAD_MUTEX_INITIALIZER, 0, NULL };

static Nptpool *np_tpool_create(Npsrv *srv);
static void np_tpool_destroy(Nptpool *tp);
static void *np_wthread_proc(void *a);
static void np_respond(Nptpool *tp, Npreq *req, Npfcall *rc);
static void np_srv_remove_workreq(Nptpool *tp, Npreq *req);
static void np_srv_add_workreq(Nptpool *tp, Npreq *req);

static char *_get_version (void *a);
static char *_get_connections (void *a);
static char *_get_wthreads (void *a);
static char *_get_requests (void *a);

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
	if (!np_ctl_addfile (srv->ctlroot, "version", _get_version, NULL))
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "connections",_get_connections, srv))
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "wthreads", _get_wthreads, srv))
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "requests", _get_requests, srv))
		goto error;
	if (np_usercache_create (srv) < 0)
		goto error;
	srv->nwthread = nwthread;
	if (!(srv->tpool = np_tpool_create (srv)))
		goto error;
	return srv;
error:
	if (srv)
		np_srv_destroy (srv);
	return NULL;
}

void
np_srv_destroy(Npsrv *srv)
{
	if (srv->tpool)
		np_tpool_destroy (srv->tpool);
	np_usercache_destroy (srv);
	np_ctl_finalize (srv);
	free (srv);
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
	while (srv->conncount > 0 || srv->connhistory < count) {
		pthread_cond_wait(&srv->conncountcond, &srv->lock);
	}
	pthread_mutex_unlock(&srv->lock);
}

void
np_srv_add_req(Nptpool *tp, Npreq *req)
{
	/* assert: srv->lock held */
	req->prev = tp->reqs_last;
	if (tp->reqs_last)
		tp->reqs_last->next = req;
	tp->reqs_last = req;
	if (!tp->reqs_first)
		tp->reqs_first = req;
	pthread_cond_signal(&tp->reqcond);
}

void
np_srv_remove_req(Nptpool *tp, Npreq *req)
{
	/* assert: srv->lock held */
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
	/* assert: srv->lock held */
	if (tp->workreqs)
		tp->workreqs->prev = req;
	req->next = tp->workreqs;
	tp->workreqs = req;
	req->prev = NULL;
}

static void
np_srv_remove_workreq(Nptpool *tp, Npreq *req)
{
	/* assert: srv->lock held */

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
	wt->tpool = tp;
	wt->shutdown = 0;
	wt->state = WT_START;
	wt->fsuid = geteuid ();
	wt->sguid = P9_NONUNAME;
	wt->fsgid = getegid ();
	wt->reqs_total = 0;
	if ((err = pthread_create(&wt->thread, NULL, np_wthread_proc, wt))) {
		np_uerror (err);
		goto error;
	}
	pthread_mutex_lock(&tp->srv->lock);
	wt->next = tp->wthreads;
	tp->wthreads = wt;
	pthread_mutex_unlock(&tp->srv->lock);
	return 0;
error:
	return -1;
}

static void
np_tpool_destroy(Nptpool *tp)
{
	Npwthread *wt, *next;

	for(wt = tp->wthreads; wt != NULL; wt = wt->next) {
		wt->shutdown = 1;
	}
	pthread_cond_broadcast(&tp->reqcond);
	/* FIXME: will block here forever if wt is blocked in the kernel */
	for (wt = tp->wthreads; wt != NULL; wt = next) {
		next = wt->next;
		pthread_join (wt->thread, NULL);
		free (wt);
	}
	free (tp);
}

static Nptpool *
np_tpool_create(Npsrv *srv)
{
	Nptpool *tp;

	if (!(tp = malloc (sizeof (*tp)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	memset (tp, 0, sizeof (*tp));
	tp->srv = srv;
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

/* Look up operation's fid and assign it to req->fid.
 * This is done before the request is handed off to a worker, with
 * the plan of using fid data in scheduling work.
 * The fid refcount is incremented here, then decremented in np_respond ().
 */
static void
np_preprocess_request(Npreq *req)
{
	Npfcall *tc = req->tcall;

	switch (tc->type) {
		case P9_TSTATFS:
			req->fid = np_fid_find (req->conn, tc->u.tstatfs.fid);
			break;
		case P9_TLOPEN:
			req->fid = np_fid_find (req->conn, tc->u.tlopen.fid);
			break;
		case P9_TLCREATE:
			req->fid = np_fid_find (req->conn, tc->u.tlcreate.fid);
			break;
		case P9_TSYMLINK:
			req->fid = np_fid_find (req->conn, tc->u.tsymlink.fid);
			break;
		case P9_TMKNOD:
			req->fid = np_fid_find (req->conn, tc->u.tmknod.fid);
			break;
		case P9_TRENAME:
			req->fid = np_fid_find (req->conn, tc->u.trename.fid);
			break;
		case P9_TREADLINK:
			req->fid = np_fid_find (req->conn, tc->u.treadlink.fid);
			break;
		case P9_TGETATTR:
			req->fid = np_fid_find (req->conn, tc->u.tgetattr.fid);
			break;
		case P9_TSETATTR:
			req->fid = np_fid_find (req->conn, tc->u.tsetattr.fid);
			break;
		case P9_TXATTRWALK:
			req->fid = np_fid_find (req->conn,
						tc->u.txattrwalk.fid);
			break;
		case P9_TXATTRCREATE:
			req->fid = np_fid_find (req->conn,
						tc->u.txattrcreate.fid);
			break;
		case P9_TREADDIR:
			req->fid = np_fid_find (req->conn, tc->u.treaddir.fid);
			break;
		case P9_TFSYNC:
			req->fid = np_fid_find (req->conn, tc->u.tfsync.fid);
			break;
		case P9_TLOCK:
			req->fid = np_fid_find (req->conn, tc->u.tlock.fid);
			break;
		case P9_TGETLOCK:
			req->fid = np_fid_find (req->conn, tc->u.tgetlock.fid);
			break;
		case P9_TLINK:
			req->fid = np_fid_find (req->conn, tc->u.tlink.dfid);
			break;
		case P9_TMKDIR:
			req->fid = np_fid_find (req->conn, tc->u.tmkdir.fid);
			break;
		case P9_TVERSION:
			break;
		case P9_TAUTH:
			if (np_fid_find (req->conn, tc->u.tauth.afid))
				break;
			req->fid = np_fid_create (req->conn, tc->u.tauth.afid,
						  NULL);
			if (!req->fid)
				break;
			req->fid->aname = np_strdup (&tc->u.tauth.aname);
			if (!req->fid->aname) {
				np_fid_destroy(req->fid);
				req->fid = NULL;
			}
			break;
		case P9_TATTACH:
			if (np_fid_find (req->conn, tc->u.tattach.fid))
				break;
			req->fid = np_fid_create (req->conn, tc->u.tattach.fid,
						  NULL);
			if (!req->fid)
				break;
			req->fid->aname = np_strdup (&tc->u.tattach.aname);
			if (!req->fid->aname) {
				np_fid_destroy(req->fid);
				req->fid = NULL;
			}
			break;
		case P9_TFLUSH:
			break;
		case P9_TWALK:
			req->fid = np_fid_find (req->conn, tc->u.twalk.fid);
			break;
		case P9_TREAD:
			req->fid = np_fid_find (req->conn, tc->u.tread.fid);
			break;
		case P9_TWRITE:
			req->fid = np_fid_find (req->conn, tc->u.twrite.fid);
			break;
		case P9_TCLUNK:
			req->fid = np_fid_find (req->conn, tc->u.tclunk.fid);
			break;
		case P9_TREMOVE:
			req->fid = np_fid_find (req->conn, tc->u.tremove.fid);
			break;
		default:
			break;
	}
	if (req->fid)
		np_fid_incref (req->fid);
}

static Npfcall*
np_process_request(Npreq *req)
{
	Npfcall *rc = NULL;
	Npfcall *tc = req->tcall;
	int ecode;

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
			break;
		case P9_TWRITE:
			rc = np_write(req, tc);
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
			break;
	}
	if ((ecode = np_rerror())) {
		if (rc)
			free(rc);
		rc = np_create_rlerror(ecode);
	}

	return rc;
}

static void *
np_wthread_proc(void *a)
{
	Npwthread *wt = (Npwthread *)a;
	Nptpool *tp = wt->tpool;
	Npsrv *srv = tp->srv;
	Npreq *req = NULL;
	Npfcall *rc;

	pthread_mutex_lock(&srv->lock);
	while (!wt->shutdown) {
		wt->state = WT_IDLE;
		req = tp->reqs_first;
		if (!req) {
			pthread_cond_wait(&tp->reqcond, &srv->lock);
			continue;
		}

		np_srv_remove_req(tp, req);
		np_srv_add_workreq(tp, req);
		pthread_mutex_unlock(&srv->lock);

		req->wthread = wt;
		wt->state = WT_WORK;
		rc = np_process_request(req);
		if (rc) {
			wt->state = WT_REPLY;
			np_respond(tp, req, rc);
		}
		wt->reqs_total++;
		pthread_mutex_lock(&srv->lock);
	}
	pthread_mutex_unlock (&srv->lock);
	wt->state = WT_SHUT;

	return NULL;
}

static void
np_respond(Nptpool *tp, Npreq *req, Npfcall *rc)
{
	Npsrv *srv = req->conn->srv;
	Npreq *freq;

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
	np_srv_remove_workreq(tp, req);
	for(freq = req->flushreq; freq != NULL; freq = freq->flushreq)
		np_srv_remove_workreq(tp, freq);
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

	np_preprocess_request (req); /* assigns req->fid */

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
		int ecode = np_rerror ();

		va_start (ap, fmt);
		vsnprintf (buf, sizeof(buf), fmt, ap);
		va_end (ap);

		if (strerror_r (ecode, ebuf, sizeof (ebuf)) == -1)
			snprintf (ebuf, sizeof (ebuf), "error %d", ecode);

		np_logmsg (srv, "%s: %s", buf, ebuf);
	}
}

static char *
_get_version (void *a)
{
	char *s = NULL;
	int len = 0;

	if (aspf (&s, &len, "%s\n", META_ALIAS) < 0)
                np_uerror (ENOMEM);
        return s;
}

static char *
_get_connections (void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Npconn *cc;
	int err;
	char *s = NULL;
	int len = 0;

	if ((err = pthread_mutex_lock(&srv->lock))) {
		np_uerror (err);
		goto error;
	}
	for (cc = srv->conns; cc != NULL; cc = cc->next) {
		
		if (aspf (&s, &len, "%s %d %d %d\n",
				np_conn_get_client_id(cc),
				cc->reqs_in, cc->reqs_out,
				np_fidpool_count (cc->fidpool)) < 0) {
			np_uerror (ENOMEM);
			goto error_unlock;
		}
	}
	if ((err = pthread_mutex_unlock(&srv->lock))) {
		np_uerror (err);
		goto error;
	}
	return s;
error_unlock:
	(void)pthread_mutex_unlock(&srv->lock);
error:
	if (s)
		free(s);
	return NULL;
}

static const char *
_wtstatestr (Npwthread *wt)
{
	char *res = NULL;

	switch (wt->state) {
		case WT_START:
			res = "START";
			break;
		case WT_IDLE:
			res = "IDLE";
			break;
		case WT_WORK:
			res = "WORK";
			break;
		case WT_REPLY:
			res = "REPLY";
			break;
		case WT_SHUT:
			res = "SHUT";
			break;
	}
	return res;
}

static char *
_get_wthreads (void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Nptpool *tp;
	Npwthread *wt;
	int err;
	char *s = NULL;
	int i = 0, len = 0;

	if ((err = pthread_mutex_lock(&srv->lock))) {
		np_uerror (err);
		goto error;
	}
	for (tp = srv->tpool; tp != NULL; tp = tp->next) {
		for (wt = tp->wthreads; wt != NULL; wt = wt->next) {
			if (aspf (&s, &len, "%d: %s (%d:%d) %d\n", i++,
					_wtstatestr (wt), wt->fsuid,
					wt->fsgid, wt->reqs_total) < 0) {
				np_uerror (ENOMEM);
				goto error_unlock;
			}
		}
	}
	if ((err = pthread_mutex_unlock(&srv->lock))) {
		np_uerror (err);
		goto error;
	}
	return s;
error_unlock:
	(void)pthread_mutex_unlock(&srv->lock);
error:
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
_get_requests(void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Nptpool *tp;
	int err;
	char *s = NULL;
	int len = 0;
	Npreq *req;

	if ((err = pthread_mutex_lock(&srv->lock))) {
		np_uerror (err);
		goto error;
	}
	for (tp = srv->tpool; tp != NULL; tp = tp->next) {
		for (req = tp->workreqs; req != NULL; req = req->next)
			if (!(_get_one_request (&s, &len, req)))
				goto error_unlock;
		for (req = tp->reqs_first; req != NULL; req = req->next)
			if (!(_get_one_request (&s, &len, req)))
				goto error_unlock;
	}
	if ((err = pthread_mutex_unlock(&srv->lock))) {
		np_uerror (err);
		goto error;
	}
	return s;
error_unlock:
	(void)pthread_mutex_unlock(&srv->lock);
error:
	if (s)
		free(s);
	return NULL;
}
