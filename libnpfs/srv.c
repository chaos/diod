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

static int np_wthread_create(Npsrv *srv);
static void *np_wthread_proc(void *a);

static char *_syn_version_get (void *a);
static char *_syn_connections_get (void *a);

Npsrv*
np_srv_create(int nwthread, int flags)
{
	int i;
	Npsrv *srv = NULL;

	np_uerror (0);
	if (!(srv = malloc(sizeof(*srv)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	memset (srv, 0, sizeof (*srv));
	pthread_mutex_init(&srv->lock, NULL);
	pthread_cond_init(&srv->reqcond, NULL);
	pthread_cond_init(&srv->conncountcond, NULL);

	srv->msize = 8216;
	srv->flags = flags;

	if (np_syn_initialize (srv) < 0)
		goto error;
	if (np_syn_addfile (srv->synroot, "version", P9_QTFILE,
				_syn_version_get, NULL) < 0)
		goto error;
	if (np_syn_addfile (srv->synroot, "connections", P9_QTFILE,
				_syn_connections_get, srv) < 0)
		goto error;

	srv->nwthread = nwthread;
	for(i = 0; i < nwthread; i++) {
		if (np_wthread_create(srv) < 0) {
			goto error;
		}
	}

	return srv;
error:
	if (srv)
		np_srv_destroy (srv);
	return NULL;
}

void
np_srv_destroy(Npsrv *srv)
{
	Npwthread *wt, *next;

	for(wt = srv->wthreads; wt != NULL; wt = wt->next) {
		wt->shutdown = 1;
	}
	pthread_cond_broadcast(&srv->reqcond);

	/* FIXME: will block here forever if wt is blocked in the kernel.
	 */
	for (wt = srv->wthreads; wt != NULL; wt = next) {
		next = wt->next;
		pthread_join (wt->thread, NULL);
		free (wt);
	}
	np_usercache_flush (srv);
	np_syn_finalize (srv);
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
	wt->fsuid = geteuid ();
	wt->sguid = P9_NONUNAME;
	wt->fsgid = getegid ();
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
_syn_version_get (void *a)
{
	char *s = strdup (META_ALIAS "\n");
        if (!s)
                np_uerror (ENOMEM);
        return s;
}

static char *
_syn_connections_get (void *a)
{
	Npsrv *srv = (Npsrv *)a;
	Npconn *cc;
	int err, len, n;
	char *s = NULL;

	if ((err = pthread_mutex_lock(&srv->lock))) {
		np_uerror (err);
		goto done;
	}
	len = srv->conncount * (sizeof (cc->client_id) + 1) + 1;
	if (!(s = malloc (len))) {
		np_uerror (ENOMEM);
		goto done_unlock;
	}
	s[0] = '\0';
	for (cc = srv->conns; cc != NULL; cc = cc->next) {
		n = strlen (s);
		(void)snprintf (s + n, len - n, "%s\n",
				np_conn_get_client_id (cc));
	}
done_unlock:
	if ((err = pthread_mutex_unlock(&srv->lock))) {
		np_uerror (err);
		free (s);
		s = NULL;
		goto done;
	}
done:
	return s;
}
