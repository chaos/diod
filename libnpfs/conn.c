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

/* Server should:
 * 1) accept a connection from client
 * 2) create a 'trans' instance for the connection
 * 3) call np_conn_create () to create 'conn' and thread to service requests
 */

/* Conn reference counting:
 * . np_conn_create () ref=0
 * . np_conn_read_proc () start ref++, finish ref--
 * . np_srv_add_conn () ref++,         np_srv_remove_conn () ref--
 * . np_req_alloc () ref++	       np_req_unref () ref--
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <pthread.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

static Npfcall *_alloc_npfcall(int msize);
static void _free_npfcall(Npfcall *rc);
static void *np_conn_read_proc(void *);
static void np_conn_reset(Npconn *conn);

Npconn*
np_conn_create(Npsrv *srv, Nptrans *trans, char *client_id)
{
	Npconn *conn;
	int err;

	if (!(conn = malloc(sizeof(*conn)))) {
		errno = ENOMEM;
		return NULL;
	}
	pthread_mutex_init(&conn->lock, NULL);
	pthread_mutex_init(&conn->wlock, NULL);
	pthread_cond_init(&conn->resetcond, NULL);

	conn->refcount = 0;
	conn->resetting = 0;
	conn->srv = srv;
	conn->msize = srv->msize;
	conn->shutdown = 0;
	conn->reqs_in = 0;
	conn->reqs_out = 0;
	if (!(conn->fidpool = np_fidpool_create())) {
		free (conn);
		errno = ENOMEM;
		return NULL;
	}
	snprintf(conn->client_id, sizeof(conn->client_id), "%s", client_id);
	conn->authuser = P9_NONUNAME;

	conn->trans = trans;
	conn->aux = NULL;
	np_srv_add_conn(srv, conn);

	err = pthread_create(&conn->rthread, NULL, np_conn_read_proc, conn);
	if (err != 0) {
		np_srv_remove_conn (srv, conn);
		np_fidpool_destroy(conn->fidpool);
		free (conn);
		errno = err;
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
	assert(conn->refcount > 0);
	conn->refcount--;
	if (conn->refcount) {
		xpthread_mutex_unlock(&conn->lock);
		return;
	}

	if (conn->fidpool) {
		np_fidpool_destroy(conn->fidpool);
		conn->fidpool = NULL;
	}
	
	xpthread_mutex_unlock(&conn->lock);
	pthread_mutex_destroy(&conn->lock);
	pthread_cond_destroy(&conn->resetcond);
	free(conn);
}

static void
_debug_trace (Npsrv *srv, Npfcall *fc)
{
	char s[512];

	np_snprintfcall(s, sizeof (s), fc);
	np_logmsg(srv, "%s", s);
}

/* Per-connection read thread.
 */
static void *
np_conn_read_proc(void *a)
{
	int i, n, size;
	Npsrv *srv;
	Npconn *conn = (Npconn *)a;
	Nptrans *trans;
	Npreq *req;
	Npfcall *fc, *fc1;

	pthread_detach(pthread_self());
	np_conn_incref(conn);
	srv = conn->srv;
	fc = _alloc_npfcall(conn->msize);
	n = 0;
	while (fc && conn->trans && (i = np_trans_read(conn->trans, fc->pkt + n, conn->msize - n)) > 0) {
		n += i;
again:
		size = np_peek_size (fc->pkt, n);
		if (size == 0 || n < size)
			continue;

		/* Corruption on the transport, unhandled op, etc.
		 * is fatal to the connection.  We could consider returning
		 * an error to the client here.   However, various kernels
		 * may not handle that well, depending on where it happens.
		 */
		if (!np_deserialize(fc, fc->pkt)) {
			_debug_trace (srv, fc);
			np_logerr (srv, "protocol error - "
				   "dropping connection to '%s'",
				   conn->client_id);
			break;
		}
		if ((srv->flags & SRV_FLAGS_DEBUG_9PTRACE))
			_debug_trace (srv, fc);

		/* Replace fc, and copy any data past the current packet
		 * to the replacement.
		 */
		fc1 = _alloc_npfcall(conn->msize);
		if (!fc1) {
			np_logerr (srv, "out of memory in receive path - "
				   "dropping connection to '%s'",
				   conn->client_id);
			break;
		}
		if (n > size)
			memmove(fc1->pkt, fc->pkt + size, n - size);
		n -= size;

		/* Encapsulate fc in a request and hand to srv worker threads.
		 * In np_req_alloc, req->fid is looked up/initialized.
		 */
		req = np_req_alloc(conn, fc);
		if (!req) {
			np_logerr (srv, "out of memory in receive path - "
				   "dropping connection to '%s'",
				   conn->client_id);
			break;
		}
		np_srv_add_req(srv, req);
		xpthread_mutex_lock(&conn->lock);
		conn->reqs_in++;
		xpthread_mutex_unlock(&conn->lock);
		fc = fc1;
		if (n > 0)
			goto again;

	}
	/* Just got EOF on read, or some other fatal error for the
	 * connection like out of memory.
	 */

	xpthread_mutex_lock(&conn->lock);
	trans = conn->trans;
	conn->trans = NULL;
	if (fc)
		_free_npfcall(fc);
	xpthread_mutex_unlock(&conn->lock);

	np_srv_remove_conn(conn->srv, conn);
	np_conn_reset(conn);

	if (trans)
		np_trans_destroy(trans);

	np_conn_decref(conn);
	return NULL;
}

static Npreq *
_get_waiting_reqs (Npconn *conn)
{
	Npsrv *srv = conn->srv;
	Npreq *req, *req1, *preqs;
	Nptpool *tp;

	/* assert: srv->lock held */
	preqs = NULL;
	for (tp = srv->tpool; tp != NULL; tp = tp->next) {
		xpthread_mutex_lock (&tp->lock);
		req = tp->reqs_first;
		while (req != NULL) {
			req1 = req->next;
			if (req->conn == conn) {
				np_srv_remove_req(tp, req);
				req->next = preqs;
				preqs = req;
			}
			req = req1;
		}
		xpthread_mutex_unlock (&tp->lock);
	}
	return preqs;
}

static void
_flush_waiting_reqs (Npreq *reqs)
{
	Npreq *req = reqs;
	Npreq *req1;

	while (req != NULL) {
		req1 = req->next;
		np_conn_respond(req);
		np_req_unref(req);
		req = req1;
	}
}

static int
_count_working_reqs (Npconn *conn, int boolonly)
{
	Npsrv *srv = conn->srv;
	Nptpool *tp;
	Npreq *req;
	int n;

	/* assert: srv->lock held */
	for (n = 0, tp = srv->tpool; tp != NULL; tp = tp->next) {
		xpthread_mutex_lock (&tp->lock);
		for (req = tp->workreqs; req != NULL; req = req->next) {
			if (req->conn != conn)
				continue;
			if (req->tcall->type != P9_TVERSION)
				n++;
			if (boolonly && n > 0)
				break;		
		}
		xpthread_mutex_unlock (&tp->lock);
		if (boolonly && n > 0)
			break;
	}
	return n;
}

static int
_get_working_reqs (Npconn *conn, Npreq ***rp, int *lp)
{
	Npsrv *srv = conn->srv;
	Npreq *req, **reqs = NULL;
	Nptpool *tp;
	int n;

	/* assert: srv->lock held */
	n = _count_working_reqs (conn, 0);
	if ((reqs = malloc(n * sizeof(Npreq *))))
		goto error;
	for (n = 0, tp = srv->tpool; tp != NULL; tp = tp->next) {
		xpthread_mutex_lock (&tp->lock);
		for (req = tp->workreqs; req != NULL; req = req->next) {
			if (req->conn != conn)
				continue;
			if (req->tcall->type != P9_TVERSION)
				reqs[n++] = np_req_ref (req);
		}
		xpthread_mutex_unlock (&tp->lock);
	}
	*lp = n;
	*rp = reqs;
	return 0;
error:
	if (reqs)
		free (reqs);
	return -1;
}

static void
_flush_working_reqs (Npreq **reqs, int len)
{
	int i;

	for(i = 0; i < len; i++) {
		Npreq *req = reqs[i];
		if (req->conn->srv->flush)
			(*req->conn->srv->flush)(req);
	}
}

static void
_free_working_reqs (Npreq **reqs, int len)
{
	int i;

	for(i = 0; i < len; i++) 
		np_req_unref(reqs[i]);
	free(reqs);
}

/* Clear all state associated with conn out of the srv.
 * No more I/O is possible; we have disassociated the trans from the conn.
 */
static void
np_conn_reset(Npconn *conn)
{
	int reqslen;
	Npsrv *srv;
	Npreq *preqs, **reqs;

	xpthread_mutex_lock(&conn->lock);
	conn->resetting = 1;
	xpthread_mutex_unlock(&conn->lock);
	
	xpthread_mutex_lock(&conn->srv->lock);
	srv = conn->srv;
	preqs = _get_waiting_reqs (conn);
	if (_get_working_reqs (conn, &reqs, &reqslen) < 0) {
		xpthread_mutex_unlock(&conn->srv->lock);
		goto error;
	}
	xpthread_mutex_unlock(&conn->srv->lock);

	_flush_waiting_reqs (preqs);
	_flush_working_reqs (reqs, reqslen);

	xpthread_mutex_lock(&srv->lock);
	while (_count_working_reqs (conn, 1) > 0)
		xpthread_cond_wait(&conn->resetcond, &srv->lock);
	xpthread_mutex_unlock(&srv->lock);

	xpthread_mutex_lock(&conn->lock);
	if (conn->fidpool) {
		np_fidpool_destroy(conn->fidpool);
		conn->fidpool = NULL;
	}
	conn->resetting = 0;
	xpthread_mutex_unlock(&conn->lock);

	_free_working_reqs (reqs, reqslen);
	return;
error:
	return;
}

/* Called by srv workers to transmit req->rcall->pkt.
 */
void
np_conn_respond(Npreq *req)
{
	int n, send;
	Npconn *conn = req->conn;
	Npsrv *srv = conn->srv;
	Npfcall *rc = req->rcall;
	Nptrans *trans = NULL;

	if (!rc)
		goto done;

	xpthread_mutex_lock(&conn->lock);
	send = conn->trans && !conn->resetting;
	xpthread_mutex_unlock(&conn->lock);

	if (send) {
		if ((srv->flags & SRV_FLAGS_DEBUG_9PTRACE))
			_debug_trace (srv, rc);
		xpthread_mutex_lock(&conn->wlock);
		n = np_trans_write(conn->trans, rc->pkt, rc->size);
		conn->reqs_out++;
		xpthread_mutex_unlock(&conn->wlock);
		if (n <= 0) { /* write error */
			xpthread_mutex_lock(&conn->lock);
			trans = conn->trans;
			conn->trans = NULL;
			xpthread_mutex_unlock(&conn->lock);
		}
	}

done:
	_free_npfcall(req->tcall);
	free(req->rcall);
	req->tcall = NULL;
	req->rcall = NULL;

	if (conn->resetting) {
		xpthread_mutex_lock(&conn->srv->lock);
		xpthread_cond_broadcast(&conn->resetcond);
		xpthread_mutex_unlock(&conn->srv->lock);
	}

	if (trans) /* np_conn_read_proc will take care of resetting */
		np_trans_destroy(trans); 
}

static Npfcall *
_alloc_npfcall(int msize)
{
	Npfcall *fc;

	if ((fc = malloc(sizeof(*fc) + msize)))
		fc->pkt = (u8*) fc + sizeof(*fc);

	return fc;
}

static void
_free_npfcall(Npfcall *rc)
{
	if (rc)
		free (rc);
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
