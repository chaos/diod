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
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

typedef struct Npcrcall Npcrcall;
typedef struct Npcread Npcread;

struct Npcrcall {
	Npcread*	r;
	char*		ename;
	u32		ecode;
	u32		count;
};
	
struct Npcread
{
	pthread_mutex_t	lock;
	int		err;
	u32		iounit;
	int		nreq;
	int		nresp;
	Npcrcall*	calls;
	void		(*cb)(void *, u32);
	void*		cba;
};

int
npc_read(Npcfid *fid, u8 *buf, u32 count, u64 offset)
{
	int i, n, l;
	Npfcall *tc, *rc;

	n = 0;
	while (n < count) {
		i = count - n;
		if (i > (fid->fsys->msize - IOHDRSZ))
			i = fid->fsys->msize - IOHDRSZ;

		tc = np_create_tread(fid->fid, offset + n, i);
		if (npc_rpc(fid->fsys, tc, &rc) < 0) {
			free(tc);
			return -1;
		}

		l = rc->count;
		if (l > count)
			l = count;
		memmove(buf, rc->data, l);
		free(rc);
		free(tc);

		if (l == 0)
			break;

		n += l;
	}

	return n;
}

static void 
npc_read_cb(Npcreq *req, void *cba)
{
	int i, nocount, count;
	Npcrcall *rc;
	Npcread *r;

	rc = cba;
	r = rc->r;

	rc->ename = req->ename;
	rc->ecode = req->ecode;
	if (req->rc)
		rc->count = req->rc->count;
	free(req->rc);

	pthread_mutex_lock(&r->lock);
	r->nresp++;
	free(req->tc);

	/* if there are more read requests, return */
	if (r->nreq-r->nresp > 0) {
		pthread_mutex_unlock(&r->lock);
		return;
	}

	/* if there was an error while sending the requests, don't call the callback */
	if (r->err) 
		goto done;

	nocount = 0;
	for(i = 0; i < r->nreq; i++) {
		if (r->calls[i].ename) {
			np_werror(r->calls[i].ename, r->calls[i].ecode);
			(*r->cb)(r->cba, -1);
			goto done;
		}

		if (nocount) {
			if (r->calls[i].count != 0) {
				np_werror("invalid read", EIO);
				(*r->cb)(r->cba, -1);
				goto done;
			}
		} else {
			count += r->calls[i].count;
			if (r->calls[i].count < r->iounit)
				nocount = 1;
		}
	}

	(*r->cb)(r->cba, count);

done:
	for(i = 0; i < r->nreq; i++)
		if (r->calls[i].ename)
			free(r->calls[i].ename);

	pthread_mutex_unlock(&r->lock);
	pthread_mutex_destroy(&r->lock);
	free(r);
}

int
npc_readnb(Npcfid *fid, u8 *buf, u32 count, u64 offset, 
	void (*cb)(void *cba, u32 count), void *cba)
{
	int i, n, nreq, iounit;
	Npcread *r;
	Npfcall *tc;

	iounit = fid->iounit;
	nreq = count/iounit + (count%iounit?1:0);
	r = malloc(sizeof(*r) + nreq*sizeof(Npcrcall));
	if (!r) {
		np_werror(Ennomem, ENOMEM);
		return -1;
	}

	pthread_mutex_init(&r->lock, NULL);
	r->nreq = nreq;
	r->nresp = 0;
	r->iounit = iounit;
	r->err = 0;
	r->calls = (Npcrcall *) ((char *)r + sizeof(*r));
	r->cb = cb;
	r->cba = cba;

	for(i = 0; i < nreq; i++) {
		r->calls[i].r = r;
		r->calls[i].ename = NULL;
		r->calls[i].ecode = 0;
		r->calls[i].count = 0;
	}

	for(i = 0; i < nreq; i++) {
		n = iounit;
		if ((i+1)*iounit > count)
			n = count - i*iounit;

		tc = np_create_tread(fid->fid, offset + i*iounit, n);
		if (npc_rpcnb(fid->fsys, tc, npc_read_cb, &r->calls[i]) < 0) {
			pthread_mutex_lock(&r->lock);
			r->nreq = i;
			r->err = 1;
			pthread_mutex_lock(&r->lock);
			return -1;
		}
	}

	return 0;
}
