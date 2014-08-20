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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "xpthread.h"
#include "npcimpl.h"

u8 m2id[] = {
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 5, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 6, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 5, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 7, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 5, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 6, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 5, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 4, 
0, 1, 0, 2, 0, 1, 0, 3, 
0, 1, 0, 2, 0, 1, 0, 0,
};

Npcpool *
npc_create_pool(u32 maxid)
{
	Npcpool *p;

	p = malloc(sizeof(*p));
	if (!p) {
		np_uerror(ENOMEM);
		return NULL;
	}

	p->maxid = maxid;
	pthread_mutex_init(&p->lock, NULL);
	pthread_cond_init(&p->cond, NULL);
	p->msize = 32;	/* 256 ids */
	p->map = malloc(p->msize);
	if (!p->map) {
		np_uerror(ENOMEM);
		free(p);
		return NULL;
	}

	memset(p->map, 0, p->msize);
	return p;
}

void
npc_destroy_pool(Npcpool *p)
{
	if (p) {
		free(p->map);
		free(p);
	}
}

u32
npc_get_id(Npcpool *p)
{
	int i, n;
	u32 ret;
	u8 *pt;

	xpthread_mutex_lock(&p->lock);

again:
	for(i = 0; i < p->msize; i++)
		if (p->map[i] != 0xFF)
			break;

	if (i>=p->msize && p->msize*8<p->maxid) {
		n = p->msize + 32;
		if (n*8 > p->maxid)
			n = p->maxid/8 + 1;

		pt = realloc(p->map, n);
		if (pt) {
			memset(pt + p->msize, 0, n - p->msize);
			p->map = pt;
			i = p->msize;
			p->msize = n;
		}
	}

	if (i >= p->msize) {
		pthread_cond_wait(&p->cond, &p->lock);
		goto again;
	}

	ret = m2id[p->map[i]];
	p->map[i] |= 1 << ret;
	ret += i * 8;

	xpthread_mutex_unlock(&p->lock);
	return ret;
}

void
npc_put_id(Npcpool *p, u32 id)
{
	xpthread_mutex_lock(&p->lock);
	p->map[id / 8] &= ~(1 << (id % 8));
	xpthread_mutex_unlock(&p->lock);
	pthread_cond_broadcast(&p->cond);
}
