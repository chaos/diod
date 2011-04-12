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
#include <errno.h>
#include <pthread.h>
#include <assert.h>
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

static Npfcall *np_conn_new_incall(Npconn *conn);
static void np_conn_free_incall(Npconn *, Npfcall *, int);
static void *np_conn_read_proc(void *);
static void np_conn_reset(Npconn *conn, u32 msize);

Npconn*
np_conn_create(Npsrv *srv, Nptrans *trans)
{
	Npconn *conn;

	conn = malloc(sizeof(*conn));
	if (!conn)
		return NULL;

	//fprintf(stderr, "np_conn_create %p\n", conn);
	pthread_mutex_init(&conn->lock, NULL);
	pthread_mutex_init(&conn->wlock, NULL);
	pthread_cond_init(&conn->resetcond, NULL);
	pthread_cond_init(&conn->resetdonecond, NULL);
	conn->refcount = 0;
	conn->resetting = 0;
	conn->srv = srv;
	conn->msize = srv->msize;
	conn->shutdown = 0;
	conn->fidpool = np_fidpool_create();
	conn->trans = trans;
	conn->aux = NULL;
	conn->freercnum = 0;
	conn->freerclist = NULL;
	np_srv_add_conn(srv, conn);

	pthread_create(&conn->rthread, NULL, np_conn_read_proc, conn);
	return conn;
}

void
np_conn_incref(Npconn *conn)
{
	pthread_mutex_lock(&conn->lock);
	conn->refcount++;
	pthread_mutex_unlock(&conn->lock);
}

void
np_conn_decref(Npconn *conn)
{
	Npfcall *fc, *fc1;

	pthread_mutex_lock(&conn->lock);
	assert(conn->refcount > 0);
	conn->refcount--;
	if (conn->refcount) {
		pthread_mutex_unlock(&conn->lock);
		return;
	}

	if (conn->fidpool) {
		np_fidpool_destroy(conn->fidpool);
		conn->fidpool = NULL;
	}
	
	fc = conn->freerclist;
	conn->freerclist = NULL;
	while (fc != NULL) {
		fc1 = fc->next;
		free(fc);
		fc = fc1;
	}

	pthread_mutex_unlock(&conn->lock);
	pthread_mutex_destroy(&conn->lock);
	pthread_cond_destroy(&conn->resetcond);
	pthread_cond_destroy(&conn->resetdonecond);
	free(conn);
}

static void *
np_conn_read_proc(void *a)
{
	int i, n, size, msize;
	Npsrv *srv;
	Npconn *conn;
	Nptrans *trans;
	Npreq *req;
	Npfcall *fc, *fc1;

	pthread_detach(pthread_self());
	conn = a;
	np_conn_incref(conn);
	srv = conn->srv;
	msize = conn->msize;
	fc = np_conn_new_incall(conn);
	n = 0;
	while (fc && conn->trans && (i = np_trans_read(conn->trans, fc->pkt + n, msize - n)) > 0) {
		pthread_mutex_lock(&conn->lock);
		if (conn->resetting) {
			pthread_cond_wait(&conn->resetdonecond, &conn->lock);
			n = 0;	/* discard all input */
			i = 0;
		}
		pthread_mutex_unlock(&conn->lock);
		n += i;

again:
		if (n < 4)
			continue;

		size = fc->pkt[0] | (fc->pkt[1]<<8) | (fc->pkt[2]<<16) | (fc->pkt[3]<<24);
		if (n < size)
			continue;

		if (!np_deserialize(fc, fc->pkt))
			break;

		if ((conn->srv->debuglevel & DEBUG_9P_TRACE)
		 				&& conn->srv->debugprintf) {
			char s[1024];
			
			np_snprintfcall(s, sizeof (s), fc);
			conn->srv->debugprintf(s);
		}

		fc1 = np_conn_new_incall(conn);
		if (!fc1)
			break; /* FIXME */
		if (n > size)
			memmove(fc1->pkt, fc->pkt + size, n - size);
		n -= size;

		req = np_req_alloc(conn, fc);
		pthread_mutex_lock(&srv->lock);
		if (!conn->resetting)
			np_srv_add_req(srv, req);
		else 
			np_req_unref(req);
		pthread_mutex_unlock(&srv->lock);
		fc = fc1;
		if (n > 0)
			goto again;

	}

	pthread_mutex_lock(&conn->lock);
	trans = conn->trans;
	conn->trans = NULL;
	np_conn_free_incall(conn, fc, 0);
	pthread_mutex_unlock(&conn->lock);

	np_srv_remove_conn(conn->srv, conn);
	np_conn_reset(conn, 0);

	if (trans)
		np_trans_destroy(trans);

	np_conn_decref(conn);
	return NULL;
}

static void
np_conn_reset(Npconn *conn, u32 msize)
{
	int i, n;
	Npsrv *srv;
	Npreq *req, *req1, *preqs, **reqs;
	Npfcall *fc, *fc1;

	pthread_mutex_lock(&conn->lock);
	conn->resetting = 1;
	pthread_mutex_unlock(&conn->lock);
	
	pthread_mutex_lock(&conn->srv->lock);
	srv = conn->srv;
	// first flush all outstanding requests
	preqs = NULL;
	req = srv->reqs_first;
	while (req != NULL) {
		req1 = req->next;
		if (req->conn == conn) {
			np_srv_remove_req(srv, req);
			req->next = preqs;
			preqs = req;
		}
		req = req1;
	}

	// then flush all working requests
	n = 0;
	req = conn->srv->workreqs;
	while (req != NULL) {
		if (req->conn == conn && (msize==0 || req->tcall->type != P9_TVERSION)) 
			n++;

		req = req->next;
	}

	reqs = malloc(n * sizeof(Npreq *));
	if (!reqs) /* FIXME: bailing out here is probably wrong */
		return;
	n = 0;
	req = conn->srv->workreqs;
	while (req != NULL) {
		if (req->conn == conn && (msize==0 || req->tcall->type != P9_TVERSION))
			reqs[n++] = np_req_ref(req);
		req = req->next;
	}
	pthread_mutex_unlock(&conn->srv->lock);

	req = preqs;
	while (req != NULL) {
		req1 = req->next;
		np_conn_respond(req);
		np_req_unref(req);
		req = req1;
	}

	for(i = 0; i < n; i++) {
		req = reqs[i];
		if (req->conn->srv->flush)
			(*req->conn->srv->flush)(req);
	}

	/* wait until all working requests finish */
/*
	pthread_mutex_lock(&conn->lock);
	while (1) {
		for(i = 0; i < n; i++) 
			if (!reqs[i]->responded)
				break;

		if (i >= n)
			break;

		pthread_cond_wait(&conn->resetcond, &conn->lock);
	}
*/
	pthread_mutex_lock(&srv->lock);
	while (1) {
		for(req = srv->workreqs; req != NULL; req = req->next)
			if (req->conn == conn && (msize==0 || req->tcall->type != P9_TVERSION))
				break;

		if (req == NULL)
			break;

		pthread_cond_wait(&conn->resetcond, &srv->lock);
	}
	pthread_mutex_unlock(&srv->lock);

	/* free old pool of fcalls */	
	fc = conn->freerclist;
	conn->freerclist = NULL;
	while (fc != NULL) {
		fc1 = fc->next;
		free(fc);
		fc = fc1;
	}

	if (conn->fidpool) {
		np_fidpool_destroy(conn->fidpool);
		conn->fidpool = NULL;
	}

	if (msize) {
		conn->resetting = 0;
		conn->fidpool = np_fidpool_create();
		pthread_cond_broadcast(&conn->resetdonecond);
	}
	conn->resetting = 0;
	pthread_mutex_unlock(&conn->lock);

	/* free the working requests */
	for(i = 0; i < n; i++) 
		np_req_unref(reqs[i]);
	free(reqs);
}

void
np_conn_shutdown(Npconn *conn)
{
	Nptrans *trans;

	pthread_mutex_lock(&conn->lock);
	trans = conn->trans;
	conn->trans = NULL;
	pthread_mutex_unlock(&conn->lock);

	if (trans)
		np_trans_destroy(trans);
}

void
np_conn_respond(Npreq *req)
{
	int n, send;
	Npconn *conn;
	Nptrans *trans;
	Npfcall *rc;

	trans = NULL;
	conn = req->conn;
	rc = req->rcall;
	if (!rc)
		goto done;

	pthread_mutex_lock(&conn->lock);
	send = conn->trans && !conn->resetting;
	pthread_mutex_unlock(&conn->lock);

	if (send) {
		pthread_mutex_lock(&conn->wlock);
		if ((conn->srv->debuglevel & DEBUG_9P_TRACE)
		 				&& conn->srv->debugprintf) {
			char s[1024]; /* FIXME: user/t09 segfaults if =256 */
			np_snprintfcall(s, sizeof (s), rc);
			conn->srv->debugprintf(s);
		}
		n = np_trans_write(conn->trans, rc->pkt, rc->size);
		pthread_mutex_unlock(&conn->wlock);

		if (n <= 0) {
			pthread_mutex_lock(&conn->lock);
			trans = conn->trans;
			conn->trans = NULL;
			pthread_mutex_unlock(&conn->lock);
		}
	}

done:
	np_conn_free_incall(req->conn, req->tcall, 1);
	free(req->rcall);
	req->tcall = NULL;
	req->rcall = NULL;

	if (conn->resetting) {
		pthread_mutex_lock(&conn->srv->lock);
		pthread_cond_broadcast(&conn->resetcond);
		pthread_mutex_unlock(&conn->srv->lock);
	}

	if (trans)
		np_trans_destroy(trans); /* np_conn_read_proc will take care of resetting */
}

static Npfcall *
np_conn_new_incall(Npconn *conn)
{
	Npfcall *fc;

	pthread_mutex_lock(&conn->lock);
//	if (!conn->trans) {
//		pthread_mutex_unlock(&conn->lock);
//		return NULL;
//	}

	if (conn->freerclist) {
		fc = conn->freerclist;
		conn->freerclist = fc->next;
		conn->freercnum--;
	} else {
		fc = malloc(sizeof(*fc) + conn->msize);
	}

	if (!fc) {
		pthread_mutex_unlock(&conn->lock);
		return NULL;
	}

	fc->pkt = (u8*) fc + sizeof(*fc);
	pthread_mutex_unlock(&conn->lock);

	return fc;
}

static void
np_conn_free_incall(Npconn* conn, Npfcall *rc, int lock)
{
	if (!rc)
		return;

	if (lock)
		pthread_mutex_lock(&conn->lock);

	if (conn->freercnum < 64) {
		rc->next = conn->freerclist;
		conn->freerclist = rc;
		rc = NULL;
	}

	if (lock)
		pthread_mutex_unlock(&conn->lock);

	if (rc)
		free(rc);
}
