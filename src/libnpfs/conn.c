/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>

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
	conn->authuser = NONUNAME;
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
	xpthread_cond_signal(&conn->refcond);
	xpthread_mutex_unlock(&conn->lock);
}

static void
np_conn_destroy(Npconn *conn)
{
	int n;

	NP_ASSERT(conn != NULL);
	NP_ASSERT(conn->refcount == 0);
	/* issue 83: remove from srv->conns before destroying fidpool
 	 */
	np_srv_remove_conn_pre(conn->srv, conn);
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

	np_srv_remove_conn_post(conn->srv);
	free(conn);
}

static void
_debug_trace (Npsrv *srv, Npfcall *fc)
{
	if ((srv->flags & SRV_FLAGS_DEBUG_9PTRACE) && srv->logmsg) {
		xpthread_mutex_lock (&srv->tracebuf_lock);

		char *buf = srv->tracebuf;
		size_t size = srv->tracebuf_size;

		if ((srv->flags & SRV_FLAGS_DEBUG_TIMES)) {
			static struct timeval b = { 0, 0 };
			struct timeval a, c;
			int n;

			if (b.tv_sec == 0)
				(void)gettimeofday(&b, NULL);
			(void)gettimeofday (&a, NULL);
			timersub (&a, &b, &c);
			n = snprintf (buf, size, "[%"PRIdMAX".%-3"PRIdMAX"] ",
					(intmax_t)c.tv_sec,
					(intmax_t)c.tv_usec/1000);
			if (n < size) {
				buf += n;
				size -= n;
			}
		}
		np_snprintfcall (buf, size, fc);

		srv->logmsg (srv->tracebuf);

		xpthread_mutex_unlock (&srv->tracebuf_lock);
	}
}

static void
np_conn_cleanup(void *a)
{
	Npconn *conn = (Npconn *)a;

	np_conn_flush (conn);

	xpthread_mutex_lock(&conn->lock);
	while (conn->refcount > 0)
		xpthread_cond_wait(&conn->refcond, &conn->lock);
	xpthread_mutex_unlock(&conn->lock);
	np_conn_destroy(conn);
}

/* Check if the message type is allowed or not.
 */
static int
np_check_allowed (Npfcall *fc)
{
	switch (fc->type)
	{
	case Tlerror:
	case Tstatfs:
	case Tlopen:
	case Tlcreate:
	case Tsymlink:
	case Tmknod:
	case Trename:
	case Treadlink:
	case Tgetattr:
	case Tsetattr:
	case Txattrwalk:
	case Txattrcreate:
	case Treaddir:
	case Tfsync:
	case Tlock:
	case Tgetlock:
	case Tlink:
	case Tmkdir:
	case Trenameat:
	case Tunlinkat:
	case Tversion:
	case Tauth:
	case Tattach:
	case Terror:
	case Tflush:
	case Twalk:
	case Topen:
	case Tcreate:
	case Tread:
	case Twrite:
	case Tclunk:
	case Tremove:
	case Tstat:
	case Twstat:
		np_uerror(EPROTO);
		return 1;
	default:
		return 0;
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
	pthread_cleanup_push(np_conn_cleanup, a);

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

		/* Reject invalid message types */
		if (np_check_allowed (fc) != 1) {
			np_logerr (srv, "unexpected request - "
				   "dropping connection to '%s'",
				   conn->client_id);
			break;
		}

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
		 * thread, except Tflush which is handled immediately.
		 */
		if (fc->type == Tflush) {
			if (np_flush (req, fc)) {
				np_req_respond_flush (req);
				np_req_unref(req);
			}
			xpthread_mutex_lock (&srv->lock);
			srv->tpool->stats.nreqs[Tflush]++;
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
	pthread_cleanup_pop(1);
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

	if (conn->authuser != NONUNAME) {
		if (uidp)
			*uidp = conn->authuser;
		ret = 0;
	}

	return ret;
}
