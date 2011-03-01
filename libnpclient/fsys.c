/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <assert.h>
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

typedef struct Npcrpc Npcrpc;
struct Npcrpc {
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	char*		ename;
	u32		ecode;
	Npfcall*	tc;
	Npfcall*	rc;
};

int npc_chatty;

char *Econn = "connection closed";
static char *Emismatch = "response mismatch";

static Npcreq *npc_reqalloc();
static void npc_reqfree(Npcreq *req);
static Npfcall *npc_fcall_alloc(u32 msize);
static void npc_fcall_free(Npfcall *fc);
static void *npc_read_proc(void *a);
static void *npc_write_proc(void *a);

Npcfsys *
npc_create_fsys(int fd, int msize)
{
	Npcfsys *fs;

	fs = malloc(sizeof(*fs));
	if (!fs) {
		np_werror(Ennomem, ENOMEM);
		return NULL;
	}

	pthread_mutex_init(&fs->lock, NULL);
	pthread_cond_init(&fs->cond, NULL);
	fs->fd = fd;
	fs->dotu = 0;
	fs->msize = msize;
	fs->trans = NULL;
	fs->root = NULL;
	fs->tagpool = NULL;
	fs->fidpool = NULL;
	fs->unsent_first = NULL;
	fs->unsent_last = NULL;
	fs->pend_first = NULL;
	fs->readproc = 0;
	fs->writeproc = 0;
	fs->refcount = 1;

	fs->trans = np_fdtrans_create(fd, fd);
	if (!fs->trans) {
		np_werror("cannot create transport", EIO);
		goto error;
	}

	fs->tagpool = npc_create_pool(NOTAG);
	if (!fs->tagpool) {
		np_werror("cannot create tag pool", EIO);
		goto error;
	}
		
	fs->fidpool = npc_create_pool(NOFID);
	if (!fs->fidpool) {
		np_werror("cannot create fid pool", EIO);
		goto error;
	}

	pthread_create(&fs->readproc, NULL, npc_read_proc, fs);
	pthread_create(&fs->writeproc, NULL, npc_write_proc, fs);

	return fs;

error:
	npc_disconnect_fsys(fs);
	npc_decref_fsys(fs);
	return NULL;
}

void
npc_disconnect_fsys(Npcfsys *fs)
{
	void *v;

	pthread_mutex_lock(&fs->lock);
	if (fs->fd >= 0) {
		shutdown(fs->fd, 2);
		close(fs->fd);
		fs->fd = -1;
	}
	pthread_mutex_unlock(&fs->lock);

	if (fs->readproc)
		pthread_join(fs->readproc, &v);

	pthread_cond_broadcast(&fs->cond);
	if (fs->writeproc)
		pthread_join(fs->writeproc, &v);

	pthread_mutex_lock(&fs->lock);
	if (fs->trans) {
		np_trans_destroy(fs->trans);
		fs->trans = NULL;
	}
	pthread_mutex_unlock(&fs->lock);
}

void
npc_incref_fsys(Npcfsys *fs)
{
	pthread_mutex_lock(&fs->lock);
	fs->refcount++;
	pthread_mutex_unlock(&fs->lock);
}

void
npc_decref_fsys(Npcfsys *fs)
{
	pthread_mutex_lock(&fs->lock);
	fs->refcount--;
	if (fs->refcount) {
		pthread_mutex_unlock(&fs->lock);
		return;
	}

	assert(fs->fd<0 && fs->trans==NULL);
	if (fs->tagpool) {
		npc_destroy_pool(fs->tagpool);
		fs->tagpool = NULL;
	}

	if (fs->fidpool) {
		npc_destroy_pool(fs->fidpool);
		fs->fidpool = NULL;
	}
	pthread_mutex_unlock(&fs->lock);

	pthread_mutex_destroy(&fs->lock);
	pthread_cond_destroy(&fs->cond);

	free(fs);
}

int
npc_cancel_fid_requests(Npcfid *fid)
{
	int i, n;
	u16 *ftags;
	Npfcall *tc, *rc;
	Npcfsys *fs;
	Npcreq *ureqs, *req, *req1, *preq;

	fs = fid->fsys;
	pthread_mutex_lock(&fs->lock);
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
		np_werror(Ennomem, ENOMEM);
		pthread_mutex_unlock(&fs->lock);
		return -1;
	}

	for(n = 0, req = fs->pend_first; req != NULL; req = req->next)
		if (req->tc->fid == fid->fid)
			ftags[n++] = req->tag;
	pthread_mutex_unlock(&fs->lock);

	/* report error on the unsent requests */
	req = ureqs;
	while (req != NULL) {
		req1 = req->next;
		req->ename = strdup("file closed");
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
			pthread_mutex_lock(&fs->lock);
			for(preq = NULL, req = fs->pend_first; req != NULL; preq = req, req = req->next) {
				if (req->tag == ftags[i]) {
					if (preq)
						preq->next = req->next;
					else
						fs->pend_first = req->next;
					break;
				}
			}
			pthread_mutex_unlock(&fs->lock);

			if (req) {
				req->ename = strdup("file closed");
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

static void*
npc_read_proc(void *a)
{
	int i, n, size, msize;
	char *ename;
	Npfcall *fc, *fc1;
	Npcreq *req, *req1, *unsent, *pend, *preq;
	Npcfsys *fs;

	fs = a;
	msize = fs->msize;
	fc = npc_fcall_alloc(msize);
	n = 0;
	while (fs->trans && (i = np_trans_read(fs->trans, fc->pkt + n, msize - n)) > 0) {
		n += i;

again:
		if (n < 4)
			continue;

		size = fc->pkt[0] | (fc->pkt[1]<<8) | (fc->pkt[2]<<16) | (fc->pkt[3]<<24);
		if (n < size)
			continue;

		if (!np_deserialize(fc, fc->pkt, fs->dotu))
			break;

		if (npc_chatty) {
			fprintf(stderr, ">>> ");
			np_printfcall(stderr, fc, fs->dotu);
			fprintf(stderr, "\n");
		}

		fc1 = npc_fcall_alloc(msize);
		if (n > size)
			memmove(fc1->pkt, fc->pkt + size, n - size);
		n -= size;

		pthread_mutex_lock(&fs->lock);
		for(preq = NULL, req = fs->pend_first; req != NULL; preq = req, req = req->next) {
			if (req->tag == fc->tag) {
				if (preq)
					preq->next = req->next;
				else
					fs->pend_first = req->next;

				pthread_mutex_unlock(&fs->lock);
				ename = NULL;
				req->rc = fc;
				if (fc->type == Rerror) {
					ename = np_strdup(&fc->ename);
					req->ename = ename;
					req->ecode = fc->ecode;
				} else if (fc->type != req->tc->type+1) {
					req->ename = Emismatch;
					req->ecode = EIO;
				}

				if (req->cb) 
					(*req->cb)(req, req->cba);

//				free(ename);
				if (!req->flushed)
					npc_put_id(fs->tagpool, req->tag);
				npc_reqfree(req);
				break;
			}
		}

		if (!req) {
			pthread_mutex_unlock(&fs->lock);
			fprintf(stderr, "unmatched response: ");
			np_printfcall(stderr, fc, fs->dotu);
			fprintf(stderr, "\n");
			free(fc);
		}

		fc = fc1;
		msize = fs->msize;
		if (n > 0)
			goto again;
	}

	npc_fcall_free(fc);
	pthread_mutex_lock(&fs->lock);
	unsent = fs->unsent_first;
	fs->unsent_first = NULL;
	pend = fs->pend_first;
	fs->pend_first = NULL;
	if (fs->trans)
		np_trans_destroy(fs->trans);
	fs->trans = NULL;
	pthread_mutex_unlock(&fs->lock);

	req = unsent;
	while (req != NULL) {
		req->ecode = ECONNABORTED;
		req->ename = strdup(Econn);
		req1 = req->next;

		if (req->cb)
			(*req->cb)(req, req->cba);

		npc_reqfree(req);
		req = req1;
	}

	req = pend;
	while (req != NULL) {
		req->ecode = ECONNABORTED;
		req->ename = strdup(Econn);
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
	pthread_mutex_lock(&fs->lock);
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
		pthread_mutex_unlock(&fs->lock);

		if (fs->trans) {
			if (npc_chatty) {
				fprintf(stderr, "<<< ");
				np_printfcall(stderr, req->tc, fs->dotu);
				fprintf(stderr, "\n");
			}
			n = np_trans_write(fs->trans, req->tc->pkt, req->tc->size);
			if (n < 0) {
				pthread_mutex_lock(&fs->lock);
				if (fs->trans)
					np_trans_destroy(fs->trans);
				fs->trans = NULL;
				pthread_mutex_unlock(&fs->lock);
			}
		}

		pthread_mutex_lock(&fs->lock);
	}

	pthread_mutex_unlock(&fs->lock);
	return NULL;
}

int
npc_rpcnb(Npcfsys *fs, Npfcall *tc, void (*cb)(Npcreq *, void *), void *cba)
{
	Npcreq *req;

	if (!fs->trans) {
		np_werror(Econn, ECONNABORTED);
		return -1;
	}

	req = npc_reqalloc();
	if (!req) 
		return -1;

	if (tc->type != Tversion) {
		tc->tag = npc_get_id(fs->tagpool);
		np_set_tag(tc, tc->tag);
	}

	req->tag = tc->tag;
	req->tc = tc;
	req->cb = cb;
	req->cba = cba;

	pthread_mutex_lock(&fs->lock);
	if (fs->unsent_last)
		fs->unsent_last->next = req;
	req->prev = fs->unsent_last;
	fs->unsent_last = req;

	if (!fs->unsent_first)
		fs->unsent_first = req;

	pthread_cond_broadcast(&fs->cond);
	pthread_mutex_unlock(&fs->lock);

	return 0;
}

void
npc_rpc_cb(Npcreq *req, void *cba)
{
	Npcrpc *r;

	r = cba;
	pthread_mutex_lock(&r->lock);
	r->ename = req->ename;
	r->ecode = req->ecode;
	r->rc = req->rc;
	pthread_cond_broadcast(&r->cond);
	pthread_mutex_unlock(&r->lock);
}

int
npc_rpc(Npcfsys *fs, Npfcall *tc, Npfcall **rc)
{
	int n;
	Npcrpc r;

	if (rc)
		*rc = NULL;

	r.ecode = 0;
	r.ename = NULL;
	r.tc = tc;
	r.rc = NULL;
	pthread_mutex_init(&r.lock, NULL);
	pthread_cond_init(&r.cond, NULL);

	n = npc_rpcnb(fs, tc, npc_rpc_cb, &r);
	if (n < 0) 
		return n;

	pthread_mutex_lock(&r.lock);
	while (!r.ename && !r.rc)
		pthread_cond_wait(&r.cond, &r.lock);
	pthread_mutex_unlock(&r.lock);

	if (r.ename) {
		np_werror(r.ename, r.ecode);
		free(r.ename);
		free(r.rc);
		return -1;
	}
/*
	if (r.rc->type == Rerror) {
		ename = np_strdup(&r.rc->ename);
		np_werror(ename, r.rc->ecode);
		free(ename);
		free(r.rc);
		return -1;
	} else if (r.tc->type+1 != r.rc->type) {
		np_werror(Emismatch, EIO);
		free(r.rc);
		return -1;
	}
*/

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
	if (!req)
		np_werror(Ennomem, ENOMEM);

	req->fsys = NULL;
	req->tag = NOTAG;
	req->tc = NULL;
	req->rc = NULL;
	req->ecode = 0;
	req->ename = NULL;
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

static Npfcall *
npc_fcall_alloc(u32 msize)
{
	Npfcall *fc;

	fc = malloc(sizeof(*fc) + msize);
	if (!fc)
		return NULL;

	fc->pkt = (u8*) fc + sizeof(*fc);
	fc->size = msize;

	return fc;
}

static void
npc_fcall_free(Npfcall *fc)
{
	free(fc);
}
