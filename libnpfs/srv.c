/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010-2014 by Lawrence Livermore National Security, LLC.
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
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"

static Nptpool *np_tpool_create(Npsrv *srv, char *name);
static void np_tpool_cleanup (Npsrv *srv);
static void *np_wthread_proc(void *a);
static void np_srv_remove_workreq(Nptpool *tp, Npreq *req);
static void np_srv_add_workreq(Nptpool *tp, Npreq *req);

static char *_ctl_get_conns (char *name, void *a);
static char *_ctl_get_tpools (char *name, void *a);

/* Ugly hack so NP_ASSERT can get to registsered srv->logmsg */
static Npsrv *np_assert_srv = NULL;

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
	if (!np_ctl_addfile (srv->ctlroot, "connections", _ctl_get_conns,srv,0))
		goto error;
	if (!np_ctl_addfile (srv->ctlroot, "tpools", _ctl_get_tpools, srv, 0))
		goto error;
	if (np_usercache_create (srv) < 0)
		goto error;
	srv->nwthread = nwthread;
	if (!(srv->tpool = np_tpool_create (srv, "default")))
		goto error;
	np_tpool_incref (srv->tpool);
	np_assert_srv = srv;
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
	np_assert_srv = NULL;
	free (srv);
}

int
np_srv_add_conn(Npsrv *srv, Npconn *conn)
{
	xpthread_mutex_lock(&srv->lock);
	conn->srv = srv;
	conn->next = srv->conns;
	srv->conns = conn;
	srv->conncount++;
	srv->connhistory++;
	xpthread_cond_signal(&srv->conncountcond);
	xpthread_mutex_unlock(&srv->lock);

	return 1;
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

	srv->conncount--;
	xpthread_cond_signal(&srv->conncountcond);
	xpthread_mutex_unlock(&srv->lock);

	np_tpool_cleanup (srv);
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

	/* assert: srv->lock held */
	if (req->fid)
		tp = req->fid->tpool;
	if (!tp)
		tp = srv->tpool;
	req->prev = tp->reqs_last;
	if (tp->reqs_last)
		tp->reqs_last->next = req;
	tp->reqs_last = req;
	if (!tp->reqs_first)
		tp->reqs_first = req;
	xpthread_cond_signal(&tp->reqcond);
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

	/* assert srv->lock held */
	if (!(wt = malloc(sizeof(*wt)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	memset (wt, 0, sizeof (*wt));
	wt->tpool = tp;
	wt->shutdown = 0;
	wt->fsuid = geteuid ();
	wt->fsgid = getegid ();
	wt->privcap = (wt->fsuid == 0 ? 1 : 0);
	if ((err = pthread_create(&wt->thread, NULL, np_wthread_proc, wt))) {
		np_uerror (err);
		goto error;
	}
	wt->next = tp->wthreads;
	tp->wthreads = wt;
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
	if (tp->name)
		free (tp->name);
	free (tp);
}

static Nptpool *
np_tpool_create(Npsrv *srv, char *name)
{
	Nptpool *tp;

	/* assert srv->lock held */
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
	Nptpool *tp = NULL;

	NP_ASSERT (srv->tpool != NULL);
	if (!req->fid || req->fid->tpool)
		return;

	xpthread_mutex_lock (&srv->lock);
	if ((srv->flags & SRV_FLAGS_TPOOL_SINGLE) || !req->fid->aname
						  || *req->fid->aname != '/') {
		tp = srv->tpool;
	}
	if (!tp) {
		for (tp = srv->tpool; tp != NULL; tp = tp->next) {
			if (!strcmp (req->fid->aname, tp->name))
				break;
		}
	}
	if (!tp) {
		tp = np_tpool_create(srv, req->fid->aname);
		if (tp) {
			NP_ASSERT (srv->tpool); /* default tpool */
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
	Nptpool *tp, *next, *dead = NULL, *prev = NULL;

	xpthread_mutex_lock (&srv->lock);
	for (tp = srv->tpool; tp != NULL; tp = next) {
		next = tp->next;
		xpthread_mutex_lock (&tp->lock);
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
 * The fid refcount is incremented here, then decremented in
 * np_process_request ().
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
			req->fid = np_fid_create (conn, tc->u.tauth.afid);
			if (!req->fid)
				break;
			req->fid->aname = np_strdup (&tc->u.tauth.aname);
			if (!req->fid->aname)
				np_fid_decref (&req->fid);
			/* XXX leave fid->tpool NULL for now as auth
			 * can be handled in the default thread pool
			 * without risk of deadlock.
			 */
			break;
		case P9_TATTACH:
			req->fid = np_fid_create (conn, tc->u.tattach.fid);
			if (!req->fid)
				break;
			req->fid->aname = np_strdup (&tc->u.tattach.aname);
			if (!req->fid->aname)
				np_fid_decref (&req->fid);
			/* Here we select the tpool that will handle this
			 * request and requests on fids walked from this fid.
			 */
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
		case P9_TRENAMEAT:
			req->fid = np_fid_find (conn,
						tc->u.trenameat.olddirfid);
			break;
		case P9_TUNLINKAT:
			req->fid = np_fid_find (conn, tc->u.tunlinkat.dirfid);
			break;
		default:
			break;
	}
}

static u32
_floorlog2 (u32 i)
{
	u32 j = 0;

	if (i >= (1 <<16)) { i >>= 16; j += 16; }
	if (i >= (1 << 8)) { i >>=  8; j +=  8; }
	if (i >= (1 << 4)) { i >>=  4; j +=  4; }
	if (i >= (1 << 2)) { i >>=  2; j +=  2; }
	if (i >= (1 << 1)) {           j +=  1; }

	return j;
}

static int
_hbin (u64 val)
{
	u32 i = val / 1024;
	u32 j = i < 4 ? 0 : _floorlog2 (i) - 1;

	return j < NPSTATS_RWCOUNT_BINS ? j : NPSTATS_RWCOUNT_BINS - 1;
}

static Npfcall*
np_process_request(Npreq *req, Nptpool *tp)
{
	Npfcall *rc = NULL;
	Npfcall *tc = req->tcall;
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
		case P9_TRENAMEAT:
			rc = np_renameat (req, tc);
			break;
		case P9_TUNLINKAT:
			rc = np_unlinkat (req, tc);
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
			NP_ASSERT (0); /* handled in receive path */
			break;
		case P9_TWALK:
			rc = np_walk(req, tc);
			break;
		case P9_TREAD:
			rc = np_read(req, tc);
			if (rc)
				rbytes = rc->u.rread.count;
			break;
		case P9_TWRITE:
			rc = np_write(req, tc);
			if (rc)
				wbytes = rc->u.rwrite.count;
			break;
		case P9_TCLUNK:
			rc = np_clunk(req, tc);
			break;
		case P9_TREMOVE:
			rc = np_remove(req, tc);
			break;
		default:
			NP_ASSERT (0); /* handled in np_deserialize */
			break;
	}

	/* update stats */
	xpthread_mutex_lock (&tp->srv->lock);
	if (rbytes > 0) {
		tp->stats.rcount[_hbin(rbytes)]++;
		tp->stats.rbytes += rbytes;
	}
	if (wbytes > 0) {
		tp->stats.wcount[_hbin(wbytes)]++;
		tp->stats.wbytes += wbytes;
	}
	tp->stats.nreqs[tc->type]++;
	xpthread_mutex_unlock (&tp->srv->lock);

	return rc;
}

static void
np_postprocess_request(Npreq *req, Npfcall *rc)
{
	Npfcall *tc = req->tcall;
	int ecode = np_rerror();

	NP_ASSERT (tc != NULL);

	/* If an in-progress op was interrupted with a signal due to a flush,
	 * fix up the fid accounting and suppress reply.
	 */
	if (ecode == EINTR) {
		switch (tc->type) {
			case P9_TCLUNK:
			case P9_TREMOVE:
				req->fid = NULL; /* avoid final decrement */
				break;
			case P9_TWALK: {
				u32 ofid = tc->u.twalk.fid;
				u32 nfid = tc->u.twalk.newfid;

				if (ofid != nfid)
					np_fid_decref_bynum (req->conn, nfid);
				break;
			}
		}
		req->state = REQ_NOREPLY;
	}
	/* In case this was Tclunk or Tremove, fid must be discarded
	 * prior to reply, or we could find it reused before we're done.
	 */ 
	if (req->fid) {
		np_fid_decref (&req->fid);
		req->fid = NULL;
	}
	/* Send the response.
	 */
	if (ecode) {
		if (rc)
			free(rc);
		np_req_respond_error(req, ecode);
	} else
		np_req_respond(req, rc);
}

/* If a Tflush was received for this request, send the Rflush now.
 * This must come after the original response, if there is one.
 * Since req->flushreq is set while request is in the work queue,
 * Rflush is deferred until after it is no longer in the work queue to
 * avoid a race.
 */
static void
np_postprocess_flush (Npreq *req)
{
	if (req->flushreq) {
		np_req_respond_flush(req->flushreq);
		np_req_unref(req->flushreq);
		req->flushreq = NULL;
	}
}

static void *
np_wthread_proc(void *a)
{
	Npwthread *wt = (Npwthread *)a;
	Nptpool *tp = wt->tpool;
	Npreq *req = NULL;
	Npfcall *rc;

	xpthread_mutex_lock(&tp->srv->lock);
	while (!wt->shutdown) {
		req = tp->reqs_first;
		if (!req) {
			xpthread_cond_wait(&tp->reqcond, &tp->srv->lock);
			continue;
		}
		np_srv_remove_req(tp, req);
		np_srv_add_workreq(tp, req);
		req->wthread = wt;
		xpthread_mutex_unlock(&tp->srv->lock);

		rc = np_process_request(req, tp);
		np_postprocess_request (req, rc);
			
		xpthread_mutex_lock(&tp->srv->lock);
		np_srv_remove_workreq(tp, req);
		xpthread_mutex_unlock(&tp->srv->lock);

		np_postprocess_flush (req);
		np_req_unref(req);

		xpthread_mutex_lock(&tp->srv->lock);
	}
	xpthread_mutex_unlock (&tp->srv->lock);

	return NULL;
}

void
np_req_respond(Npreq *req, Npfcall *rc)
{
	NP_ASSERT (rc != NULL);

	xpthread_mutex_lock(&req->lock);
	req->rcall = rc;
	if (req->state == REQ_NORMAL) {
		np_set_tag(req->rcall, req->tag);
		np_conn_respond(req);
	}
	xpthread_mutex_unlock(&req->lock);
}

void
np_req_respond_error(Npreq *req, int ecode)
{
	char buf[STATIC_RLERROR_SIZE];
	Npfcall *rc = np_create_rlerror_static(ecode, buf, sizeof(buf));

	np_req_respond (req, rc);
	req->rcall = NULL;
}

void
np_req_respond_flush(Npreq *req)
{
	char buf[STATIC_RFLUSH_SIZE];
	Npfcall *rc = np_create_rflush_static(buf, sizeof(buf));

	np_req_respond (req, rc);
	req->rcall = NULL;
}

Npreq *
np_req_alloc(Npconn *conn, Npfcall *tc) {
	Npreq *req;

	req = malloc(sizeof(*req));
	if (!req)
		return NULL;

	np_conn_incref(conn);
	pthread_mutex_init(&req->lock, NULL);
	req->refcount = 1;
	req->conn = conn;
	req->tag = tc->tag;
	req->state = REQ_NORMAL;
	req->flushreq = NULL;
	req->tcall = tc;
	req->rcall = NULL;
	req->next = NULL;
	req->prev = NULL;
	req->wthread = NULL;
	req->fid = NULL;
	req->birth = time (NULL);

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
	NP_ASSERT (req->refcount > 0);
	req->refcount--;
	if (req->refcount) {
		xpthread_mutex_unlock(&req->lock);
		return;
	}
	xpthread_mutex_unlock(&req->lock);

	if (req->fid) {
		np_fid_decref (&req->fid);
		req->fid = NULL;
	}
	if (req->flushreq)
		np_req_unref(req->flushreq);
	if (req->conn) {
		np_conn_decref(req->conn);
		req->conn = NULL;
	}
	if (req->tcall) {
		free (req->tcall);
		req->tcall = NULL;
	}
	if (req->rcall) {
		free (req->rcall);
		req->rcall = NULL;
	}
	pthread_mutex_destroy (&req->lock);

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

void
np_assfail (char *ass, char *file, int line)
{
	Npsrv *srv = np_assert_srv;

	if (srv && srv->logmsg)
		np_logmsg (srv, "assertion failure: %s:%d: %s",
			   file, line, ass);
	else
		fprintf (stderr, "assertion failure: %s:%d: %s",
			   file, line, ass);
	if (raise (SIGABRT) < 0)
		exit (1);
}

static char *
_ctl_get_conns (char *name, void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Npconn *cc;
	char *s = NULL;
	int len = 0;

	xpthread_mutex_lock(&srv->lock);
	for (cc = srv->conns; cc != NULL; cc = cc->next) {
		xpthread_mutex_lock(&cc->lock);
		if (aspf (&s, &len, "%s %d\n",
				np_conn_get_client_id(cc),
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
_ctl_get_tpools (char *name, void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Nptpool *tp;
	Npreq *req;
	char *s = NULL;
	int n, len = 0;

	xpthread_mutex_lock(&srv->lock);
	for (tp = srv->tpool; tp != NULL; tp = tp->next) {
		tp->stats.name = tp->name;
		xpthread_mutex_lock (&tp->lock);
		tp->stats.numfids = tp->refcount;
		xpthread_mutex_unlock (&tp->lock);
		tp->stats.numreqs = 0;
		for (req = tp->reqs_first; req != NULL; req = req->next)
			tp->stats.numreqs++;
		for (req = tp->workreqs; req != NULL; req = req->next)
			tp->stats.numreqs++;
		n = np_encode_tpools_str (&s, &len, &tp->stats);
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
