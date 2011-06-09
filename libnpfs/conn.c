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
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

static Npfcall *_alloc_npfcall(int msize);
static void *np_conn_read_proc(void *);
static void np_conn_flush (Npconn *conn);

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

	conn->refcount = 0;
	conn->srv = srv;
	conn->msize = srv->msize;
	conn->shutdown = 0;
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
	int n;

	xpthread_mutex_lock(&conn->lock);
	assert(conn->refcount > 0);
	n = --conn->refcount;
	xpthread_mutex_unlock(&conn->lock);
	if (n > 0)
		return;

	if (conn->fidpool) {
		np_fidpool_destroy(conn->fidpool);
		conn->fidpool = NULL;
	}
	if (conn->trans) {
		np_trans_destroy (conn->trans);
		conn->trans = NULL;
	}
	pthread_mutex_destroy(&conn->lock);
	pthread_mutex_destroy(&conn->wlock);
	np_srv_remove_conn (conn->srv, conn);
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
	Npreq *req;
	Npfcall *fc, *fc1;

	pthread_detach(pthread_self());
	np_conn_incref(conn);
	srv = conn->srv;
	fc = _alloc_npfcall(conn->msize);
	if (!fc)
		np_logmsg (srv, "out of memory in receive path - "
				"dropping connection to '%s'", conn->client_id);
	n = 0;
	while (fc && conn->trans) {
		i = np_trans_read(conn->trans, fc->pkt + n, conn->msize - n);
		if (i < 0) {
			np_uerror (errno);
			np_logerr (srv, "read error - "
				   "dropping connection to '%s'",
				   conn->client_id);
			break;
		}
		/* This is the normal exit path for umount.
		 */
		if (i == 0)
			break;
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
			np_logmsg (srv, "protocol error - "
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
			np_logmsg (srv, "out of memory in receive path - "
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
			np_logmsg (srv, "out of memory in receive path - "
				   "dropping connection to '%s'",
				   conn->client_id);
			break;
		}

		/* Enqueue request for processing by next available worker
		 * thread, except P9_TFLUSH which is handled immediately.
		 */
		if (fc->type == P9_TFLUSH) {
			Npfcall *rc;

			rc = np_flush (req, fc);
			np_req_respond (req, rc);
		} else {
			xpthread_mutex_lock(&srv->lock);	
			np_srv_add_req(srv, req);
			xpthread_mutex_unlock(&srv->lock);	
		}
		
		fc = fc1;
		if (n > 0)
			goto again;

	}
	/* Just got EOF on read, or some other fatal error for the
	 * connection like out of memory.
	 */
	if (fc)
		free (fc);

	np_conn_flush (conn);
	np_conn_decref(conn);
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
			creq->flushed = 1;
			if (conn->srv->flags & SRV_FLAGS_FLUSHSIG)
				pthread_kill (creq->wthread->thread, SIGUSR2);
		}
	}
	xpthread_mutex_unlock(&conn->srv->lock);
}

void
np_conn_respond(Npreq *req)
{
	int n, len;
	Npconn *conn = req->conn;
	Npsrv *srv = conn->srv;
	Npfcall *rc = req->rcall;

	if ((srv->flags & SRV_FLAGS_DEBUG_9PTRACE))
		_debug_trace (srv, rc);
	xpthread_mutex_lock(&conn->wlock);
	len = 0;
	do {
		n = np_trans_write(conn->trans, rc->pkt + len, rc->size - len);
		if (n > 0)
			len += n;
	} while (n > 0 && len < rc->size);
	xpthread_mutex_unlock(&conn->wlock);
	if (n <= 0) {
		np_uerror (errno);
		np_logerr (srv, "write to '%s'", conn->client_id);
	}
}

static Npfcall *
_alloc_npfcall(int msize)
{
	Npfcall *fc;

	if ((fc = malloc(sizeof(*fc) + msize)))
		fc->pkt = (u8*) fc + sizeof(*fc);

	return fc;
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
