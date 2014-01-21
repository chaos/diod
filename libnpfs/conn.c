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
#include <sys/time.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"

static void *np_conn_read_proc(void *);
static void np_conn_flush (Npconn *conn);
static void np_conn_destroy(Npconn *conn);

Npconn*
np_conn_create(Npsrv *srv, Nptrans *trans, char *client_id, int flags)
{
	Npconn *conn;
	int err;

	if (!(conn = malloc(sizeof(*conn)))) {
		np_uerror(ENOMEM);
		return NULL;
	}
	pthread_mutex_init(&conn->lock, NULL);
	pthread_mutex_init(&conn->wlock, NULL);
	pthread_cond_init(&conn->refcond, NULL);

	conn->refcount = 0;
	conn->srv = srv;
	conn->msize = srv->msize;
	conn->shutdown = 0;
	if (!(conn->fidpool = np_fidpool_create())) {
		free (conn);
		np_uerror(ENOMEM);
		return NULL;
	}
	snprintf(conn->client_id, sizeof(conn->client_id), "%s", client_id);
	conn->authuser = P9_NONUNAME;
	conn->flags = flags;

	conn->trans = trans;
	conn->aux = NULL;
	np_srv_add_conn(srv, conn);

	err = pthread_create(&conn->rthread, NULL, np_conn_read_proc, conn);
	if (err != 0) {
		np_conn_destroy (conn);
		np_uerror (err);
		return NULL;
	}

	return conn;
}

void
np_conn_incref(Npconn *conn)
{
	xpthread_mutex_lock(&conn->lock);
	conn->refcount++;
	xpthread_mutex_unlock(&conn->lock);
}

void
np_conn_decref(Npconn *conn)
{
	xpthread_mutex_lock(&conn->lock);
	NP_ASSERT(conn->refcount > 0);
	conn->refcount--;
	xpthread_mutex_unlock(&conn->lock);
	xpthread_cond_signal(&conn->refcond);
}

static void
np_conn_destroy(Npconn *conn)
{
	int n;

	NP_ASSERT(conn != NULL);
	NP_ASSERT(conn->refcount == 0);
	/* issue 83: remove from srv->conns before destroying fidpool
 	 */
	np_srv_remove_conn (conn->srv, conn);
	if (conn->fidpool) {
		if ((n = np_fidpool_destroy(conn->fidpool)) > 0) {
			np_logmsg (conn->srv, "%s: connection closed with "
					      "%d unclunked fids",
			                      np_conn_get_client_id (conn), n);
		}
		conn->fidpool = NULL;
	}
	if (conn->trans) {
		np_trans_destroy (conn->trans);
		conn->trans = NULL;
	}
	pthread_mutex_destroy(&conn->lock);
	pthread_mutex_destroy(&conn->wlock);
	pthread_cond_destroy(&conn->refcond);

	free(conn);
}

static void
_debug_trace (Npsrv *srv, Npfcall *fc)
{
	if ((srv->flags & SRV_FLAGS_DEBUG_9PTRACE)) {
		char s[512];
		static struct timeval b = { 0, 0 };
		struct timeval a, c;

		np_snprintfcall(s, sizeof (s), fc);
		if ((srv->flags & SRV_FLAGS_DEBUG_TIMES)) {
			if (b.tv_sec == 0)
				(void)gettimeofday(&b, NULL);
			(void)gettimeofday(&a, NULL);
			timersub(&a, &b, &c);
			np_logmsg(srv, "[%lu.%-3lu] %s",
				  c.tv_sec, c.tv_usec/1000, s);
		} else
			np_logmsg(srv, "%s", s);
	}
}

/* Per-connection read thread.
 */
static void *
np_conn_read_proc(void *a)
{
	Npconn *conn = (Npconn *)a;
	Npsrv *srv = conn->srv;
	Npreq *req;
	Npfcall *fc;

	pthread_detach(pthread_self());

	for (;;) {
		if (np_trans_recv(conn->trans, &fc, conn->msize) < 0) {
			np_logerr (srv, "recv error - "
				   "dropping connection to '%s'",
				   conn->client_id);
			break;
		}
		if (!fc) /* EOF */
			break;
		_debug_trace (srv, fc);

		/* Encapsulate fc in a request and hand to srv worker threads.
		 * In np_req_alloc, req->fid is looked up/initialized.
		 */
		req = np_req_alloc(conn, fc);
		if (!req) {
			np_logmsg (srv, "out of memory in receive path - "
				   "dropping connection to '%s'",
				   conn->client_id);
			free (fc);
			break;
		}

		/* Enqueue request for processing by next available worker
		 * thread, except P9_TFLUSH which is handled immediately.
		 */
		if (fc->type == P9_TFLUSH) {
			if (np_flush (req, fc)) {
				np_req_respond_flush (req);
				np_req_unref(req);
			}
			xpthread_mutex_lock (&srv->lock);
			srv->tpool->stats.nreqs[P9_TFLUSH]++;
			xpthread_mutex_unlock (&srv->lock);
		} else {
			xpthread_mutex_lock(&srv->lock);	
			np_srv_add_req(srv, req);
			xpthread_mutex_unlock(&srv->lock);	
		}
	}
	/* Just got EOF on read, or some other fatal error for the
	 * connection like out of memory.
	 */

	np_conn_flush (conn);

	xpthread_mutex_lock(&conn->lock);
	while (conn->refcount > 0)
		xpthread_cond_wait(&conn->refcond, &conn->lock);
	xpthread_mutex_unlock(&conn->lock);
	np_conn_destroy(conn);

	return NULL;
}

static void
np_conn_flush (Npconn *conn)
{
	Nptpool *tp;
	Npreq *creq, *nextreq;

	xpthread_mutex_lock(&conn->srv->lock);
	for (tp = conn->srv->tpool; tp != NULL; tp = tp->next) {
		for (creq = tp->reqs_first; creq != NULL; creq = nextreq) {
			nextreq = creq->next;
			if (creq->conn != conn)
				continue;
			np_srv_remove_req(tp, creq);
			np_req_unref(creq);
		}
		for (creq = tp->workreqs; creq != NULL; creq = creq->next) {
			if (creq->conn != conn)
				continue;
			creq->state = REQ_NOREPLY;
			if (conn->srv->flags & SRV_FLAGS_FLUSHSIG)
				pthread_kill (creq->wthread->thread, SIGUSR2);
		}
	}
	xpthread_mutex_unlock(&conn->srv->lock);
}

void
np_conn_respond(Npreq *req)
{
	int n;
	Npconn *conn = req->conn;
	Npsrv *srv = conn->srv;
	Npfcall *rc = req->rcall;

	_debug_trace (srv, rc);
	xpthread_mutex_lock(&conn->wlock);
	n = np_trans_send(conn->trans, rc);
	xpthread_mutex_unlock(&conn->wlock);
	if (n < 0)
		np_logerr (srv, "send to '%s'", conn->client_id);
}

char *
np_conn_get_client_id(Npconn *conn)
{
	return conn->client_id;
}

void
np_conn_set_authuser(Npconn *conn, u32 authuser)
{
	conn->authuser = authuser;
}

int
np_conn_get_authuser(Npconn *conn, u32 *uidp)
{
	int ret = -1;

	if (conn->authuser != P9_NONUNAME) {
		if (uidp)
			*uidp = conn->authuser;
		ret = 0;
	}

	return ret;
}
