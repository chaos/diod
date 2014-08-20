/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010-2014 by Lawrence Livermore National Security, LLC
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* mtfsys.c - pretty much Lucho's original fsys.c */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "xpthread.h"
#include "npcimpl.h"

typedef struct Npcrpc Npcrpc;
struct Npcrpc {
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	u32		ecode;
	Npfcall*	tc;
	Npfcall*	rc;
};

static Npcreq *npc_reqalloc();
static void npc_reqfree(Npcreq *req);
static void *npc_read_proc(void *a);
static void *npc_write_proc(void *a);
static void npc_incref_fsys(Npcfsys *fs);
static void npc_decref_fsys(Npcfsys *fs);
static int npc_rpc(Npcfsys *fs, Npfcall *tc, Npfcall **rc);
static void npc_disconnect_fsys(Npcfsys *fs);

Npcfsys *
npc_create_mtfsys(int rfd, int wfd, int msize, int flags)
{
	Npcfsys *fs;
	int err;

	fs = malloc(sizeof(*fs));
	if (!fs) {
		np_uerror(ENOMEM);
		return NULL;
	}

	np_uerror (0);
	pthread_mutex_init(&fs->lock, NULL);
	pthread_cond_init(&fs->cond, NULL);
	fs->msize = msize;
	fs->trans = NULL;
	fs->tagpool = NULL;
	fs->fidpool = NULL;
	fs->unsent_first = NULL;
	fs->unsent_last = NULL;
	fs->pend_first = NULL;
	fs->readproc = 0;
	fs->writeproc = 0;
	fs->refcount = 1;
	fs->rfd = rfd;
	fs->wfd = wfd;
	fs->rpc = npc_rpc;
	fs->incref = npc_incref_fsys;
	fs->decref = npc_decref_fsys;
	fs->disconnect = npc_disconnect_fsys;
	fs->flags = flags;

	fs->trans = np_fdtrans_create(rfd, wfd);
	if (!fs->trans)
		goto error;
	fs->tagpool = npc_create_pool(P9_NOTAG);
	if (!fs->tagpool)
		goto error;
	fs->fidpool = npc_create_pool(P9_NOFID);
	if (!fs->fidpool)
		goto error;

	err = pthread_create(&fs->readproc, NULL, npc_read_proc, fs);
	if (err) {
		np_uerror (err);
		goto error;
	}
	err = pthread_create(&fs->writeproc, NULL, npc_write_proc, fs);
	if (err) {
		np_uerror (err);
		goto error;
	}

	return fs;

error:
	npc_disconnect_fsys(fs);
	npc_decref_fsys(fs);
	return NULL;
}

static void
npc_disconnect_fsys(Npcfsys *fs)
{
	void *v;

	xpthread_mutex_lock(&fs->lock);
	if (fs->wfd >= 0) {
		shutdown(fs->wfd, 2);
		close(fs->wfd);
		fs->wfd = -1;
	}
	xpthread_mutex_unlock(&fs->lock);

	if (fs->readproc)
		pthread_join(fs->readproc, &v);

	pthread_cond_broadcast(&fs->cond);
	if (fs->writeproc)
		pthread_join(fs->writeproc, &v);

	xpthread_mutex_lock(&fs->lock);
	if (fs->trans) {
		np_trans_destroy(fs->trans);
		fs->trans = NULL;
	}
	xpthread_mutex_unlock(&fs->lock);
}

static void
npc_incref_fsys(Npcfsys *fs)
{
	xpthread_mutex_lock(&fs->lock);
	fs->refcount++;
	xpthread_mutex_unlock(&fs->lock);
}

static void
npc_decref_fsys(Npcfsys *fs)
{
	xpthread_mutex_lock(&fs->lock);
	fs->refcount--;
	if (fs->refcount) {
		xpthread_mutex_unlock(&fs->lock);
		return;
	}

	NP_ASSERT(fs->wfd<0 && fs->trans==NULL);
	if (fs->tagpool) {
		npc_destroy_pool(fs->tagpool);
		fs->tagpool = NULL;
	}

	if (fs->fidpool) {
		npc_destroy_pool(fs->fidpool);
		fs->fidpool = NULL;
	}
	xpthread_mutex_unlock(&fs->lock);

	pthread_mutex_destroy(&fs->lock);
	pthread_cond_destroy(&fs->cond);

	free(fs);
}

#if 0
int
npc_cancel_fid_requests(Npcfid *fid)
{
	int i, n;
	u16 *ftags;
	Npfcall *tc, *rc;
	Npcfsys *fs;
	Npcreq *ureqs, *req, *req1, *preq;

	fs = fid->fsys;
	xpthread_mutex_lock(&fs->lock);
	ureqs = NULL;
	req = fs->unsent_first;
	while (req != NULL) {
		req1 = req->next;
		if (req->tc->fid == fid->fid) {
			if (req->prev)
				req->prev->next = req->next;
			if (req->next)
				req->next->prev = req->prev;
			if (req == fs->unsent_last)
				fs->unsent_last = req->prev;
			if (req == fs->unsent_first);
				fs->unsent_first = req->next;

			req->next = ureqs;
			ureqs = req;
		} 
		req = req1;
	}

	for(n = 0, req = fs->pend_first; req != NULL; req = req->next)
		if (req->tc->fid == fid->fid) {
			req->flushed = 1;
			n++;
		}

	ftags = malloc(n * sizeof(u16));
	if (!ftags) {
		np_uerror(ENOMEM);
		xpthread_mutex_unlock(&fs->lock);
		return -1;
	}

	for(n = 0, req = fs->pend_first; req != NULL; req = req->next)
		if (req->tc->fid == fid->fid)
			ftags[n++] = req->tag;
	xpthread_mutex_unlock(&fs->lock);

	/* report error on the unsent requests */
	req = ureqs;
	while (req != NULL) {
		req1 = req->next;
		req->ecode = EIO;
		(*req->cb)(req, req->cba);
		npc_put_id(fs->tagpool, req->tag);
		npc_reqfree(req);
		req = req1;
	}

	/* flush the pending ones */
	for(i = 0; i < n; i++) {
		tc = np_create_tflush(ftags[i]);
		if (npc_rpc(fs, tc, &rc) > 0) {
			/* if the request didn't receive a response, reply */
			xpthread_mutex_lock(&fs->lock);
			for(preq = NULL, req = fs->pend_first; req != NULL; preq = req, req = req->next) {
				if (req->tag == ftags[i]) {
					if (preq)
						preq->next = req->next;
					else
						fs->pend_first = req->next;
					break;
				}
			}
			xpthread_mutex_unlock(&fs->lock);

			if (req) {
				req->ecode = EIO;
				if (req->cb) 
					(*req->cb)(req, req->cba);
				npc_reqfree(req);
			}

			npc_put_id(fs->tagpool, ftags[i]);
		}

		free(tc);
		free(rc);
	}
	free(ftags);

	return 0;
}
#endif

static void*
npc_read_proc(void *a)
{
	Npcfsys *fs = (Npcfsys *)a;
	Npfcall *fc = NULL;
	Npcreq *req, *req1, *unsent, *pend, *preq;

	while (fs->trans) {
		if (np_trans_recv (fs->trans, &fc, fs->msize) < 0)
			break;
		if (fc == NULL)
			break;

		xpthread_mutex_lock(&fs->lock);
		for(preq = NULL, req = fs->pend_first;
				req != NULL; preq = req, req = req->next) {
			if (req->tag == fc->tag) {
				if (preq)
					preq->next = req->next;
				else
					fs->pend_first = req->next;

				xpthread_mutex_unlock(&fs->lock);
				req->rc = fc;
				if (fc->type == P9_RLERROR) {
					req->ecode = fc->u.rlerror.ecode;
				} else if (fc->type != req->tc->type+1) {
					req->ecode = EIO;
				}
				fc = NULL;
				if (req->cb) 
					(*req->cb)(req, req->cba);

				if (!req->flushed)
					npc_put_id(fs->tagpool, req->tag);
				npc_reqfree(req);
				break;
			}
		}

		if (!req) {
			xpthread_mutex_unlock(&fs->lock);
			free(fc);
			fc = NULL;
		}
	}
	if (fc)
		free(fc);
	xpthread_mutex_lock(&fs->lock);
	unsent = fs->unsent_first;
	fs->unsent_first = NULL;
	pend = fs->pend_first;
	fs->pend_first = NULL;
	if (fs->trans)
		np_trans_destroy(fs->trans);
	fs->trans = NULL;
	xpthread_mutex_unlock(&fs->lock);

	req = unsent;
	while (req != NULL) {
		req->ecode = ECONNABORTED;
		req1 = req->next;

		if (req->cb)
			(*req->cb)(req, req->cba);

		npc_reqfree(req);
		req = req1;
	}

	req = pend;
	while (req != NULL) {
		req->ecode = ECONNABORTED;
		req1 = req->next;

		if (req->cb)
			(*req->cb)(req, req->cba);

		npc_reqfree(req);
		req = req1;
	}

	return NULL;
}

static void*
npc_write_proc(void *a)
{
	int n;
	Npcreq *req;
	Npcfsys *fs;

	fs = a;
	xpthread_mutex_lock(&fs->lock);
	while (fs->trans) {
		if (!fs->unsent_first) {
			pthread_cond_wait(&fs->cond, &fs->lock);
			continue;
		}

		req = fs->unsent_first;
		fs->unsent_first = req->next;
		req->prev = NULL;
		if (fs->unsent_last == req)
			fs->unsent_last = NULL;

		req->prev = NULL;
		req->next = fs->pend_first;
		if (fs->pend_first)
			fs->pend_first->prev = req;

		fs->pend_first = req;
		xpthread_mutex_unlock(&fs->lock);

		if (fs->trans) {
			n = np_trans_send(fs->trans, req->tc);
			if (n < 0) {
				xpthread_mutex_lock(&fs->lock);
				if (fs->trans)
					np_trans_destroy(fs->trans);
				fs->trans = NULL;
				xpthread_mutex_unlock(&fs->lock);
			}
		}

		xpthread_mutex_lock(&fs->lock);
	}

	xpthread_mutex_unlock(&fs->lock);
	return NULL;
}

static int
npc_rpcnb(Npcfsys *fs, Npfcall *tc, void (*cb)(Npcreq *, void *), void *cba)
{
	Npcreq *req;

	if (!fs->trans) {
		np_uerror(ECONNABORTED);
		return -1;
	}

	req = npc_reqalloc();
	if (!req) 
		return -1;

	if (tc->type != P9_TVERSION) {
		tc->tag = npc_get_id(fs->tagpool);
		np_set_tag(tc, tc->tag);
	}

	req->tag = tc->tag;
	req->tc = tc;
	req->cb = cb;
	req->cba = cba;

	xpthread_mutex_lock(&fs->lock);
	if (fs->unsent_last)
		fs->unsent_last->next = req;
	req->prev = fs->unsent_last;
	fs->unsent_last = req;

	if (!fs->unsent_first)
		fs->unsent_first = req;

	pthread_cond_broadcast(&fs->cond);
	xpthread_mutex_unlock(&fs->lock);

	return 0;
}

static void
npc_rpc_cb(Npcreq *req, void *cba)
{
	Npcrpc *r;

	r = cba;
	xpthread_mutex_lock(&r->lock);
	r->ecode = req->ecode;
	r->rc = req->rc;
	pthread_cond_broadcast(&r->cond);
	xpthread_mutex_unlock(&r->lock);
}

static int
npc_rpc(Npcfsys *fs, Npfcall *tc, Npfcall **rc)
{
	int n;
	Npcrpc r;

	if (rc)
		*rc = NULL;

	r.ecode = 0;
	r.tc = tc;
	r.rc = NULL;
	pthread_mutex_init(&r.lock, NULL);
	pthread_cond_init(&r.cond, NULL);

	n = npc_rpcnb(fs, tc, npc_rpc_cb, &r);
	if (n < 0) 
		return n;

	xpthread_mutex_lock(&r.lock);
	while (!r.ecode && !r.rc)
		pthread_cond_wait(&r.cond, &r.lock);
	xpthread_mutex_unlock(&r.lock);

	if (r.rc == NULL) {
		np_uerror (EPROTO); /* premature EOF */
		return -1;
	}

	/* N.B. allow for auth returning error with ecode == 0 */
	if (r.ecode || r.rc->type == P9_RLERROR) {
		np_uerror(r.ecode);
		free(r.rc);
		return -1;
	}

	if (rc)
		*rc = r.rc;
	else
		free(r.rc);

	return 0;
}

static Npcreq *
npc_reqalloc()
{
	Npcreq *req;

	req = malloc(sizeof(*req));
	if (!req) {
		np_uerror(ENOMEM);
		return NULL;
	}

	req->fsys = NULL;
	req->tag = P9_NOTAG;
	req->tc = NULL;
	req->rc = NULL;
	req->ecode = 0;
	req->cb = NULL;
	req->cba = NULL;
	req->next = NULL;
	req->prev = NULL;
	req->flushed = 0;

	return req;
}

static void
npc_reqfree(Npcreq *req)
{
	free(req);
}
