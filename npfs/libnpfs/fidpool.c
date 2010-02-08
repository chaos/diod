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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

Npfidpool *
np_fidpool_create(void)
{
	Npfidpool *fp;

	fp = np_malloc(sizeof(*fp) + FID_HTABLE_SIZE * sizeof(Npfid *));
	if (!fp)
		return NULL;

	pthread_mutex_init(&fp->lock, NULL);
	fp->size = FID_HTABLE_SIZE;
	fp->htable = (Npfid **)((char *) fp + sizeof(*fp));
	memset(fp->htable, 0, fp->size * sizeof(Npfid *));

	return fp;
}

void
np_fidpool_destroy(Npfidpool *pool)
{
	int i;
	Npfid *f, *ff;
	Npsrv *srv;

	for(i = 0; i < pool->size; i++) {
		f = pool->htable[i];
		while (f != NULL) {
			ff = f->next;
			srv = f->conn->srv;
			if (f->type&Qtauth && srv->auth && srv->auth->clunk)
				(*srv->auth->clunk)(f);
			else if (!(f->type&Qtauth) && srv->fiddestroy)
				(*srv->fiddestroy)(f);
			free(f);
			f = ff;
		}
	}

	free(pool);
}

Npfid *
np_fid_lookup(Npfidpool *fp, u32 fid, int hash)
{
	Npfid **htable, *f;

	htable = fp->htable;
	for(f = htable[hash]; f != NULL; f = f->next) {
		if (f->fid == fid) {
			if (f != htable[hash]) {
				if (f->next)
					f->next->prev = f->prev;

				f->prev->next = f->next;
				f->prev = NULL;
				f->next = htable[hash];
				htable[hash]->prev = f;
				htable[hash] = f;
			}

			break;
		}
	}

	return f;
}

Npfid*
np_fid_find(Npconn *conn, u32 fid)
{
	int hash;
	Npfidpool *fp;
	Npfid *ret;

	fp = conn->fidpool;
	pthread_mutex_lock(&fp->lock);
	hash = fid % fp->size;
	ret = np_fid_lookup(fp, fid, hash);
	pthread_mutex_unlock(&fp->lock);

	return ret;
}

Npfid*
np_fid_create(Npconn *conn, u32 fid, void *aux)
{
	int hash;
	Npfidpool *fp;
	Npfid **htable, *f;

	fp = conn->fidpool;
	pthread_mutex_lock(&fp->lock);
	htable = fp->htable;
	hash = fid % FID_HTABLE_SIZE;
	f = np_fid_lookup(fp, fid, hash);
	if (!f) {
		f = np_malloc(sizeof(*f));
		if (!f) {
			pthread_mutex_unlock(&fp->lock);
			return NULL;
		}

		pthread_mutex_init(&f->lock, NULL);
		f->fid = fid;
		f->conn = conn;
		f->refcount = 0;
		f->omode = ~0;
		f->type = 0;
		f->diroffset = 0;
		f->user = NULL;
		f->aux = aux;

		f->next = htable[hash];
		f->prev = NULL;
		if (htable[hash])
			htable[hash]->prev = f;

		htable[hash] = f;
	}

	pthread_mutex_unlock(&fp->lock);

	return f;
}

void
np_fid_destroy(Npfid *fid)
{
	int hash;
	Npconn *conn;
	Npsrv *srv;
	Npfidpool *fp;
	Npfid **htable;

	conn = fid->conn;
	srv = conn->srv;
	fp = conn->fidpool;
	if (!fp)
		return;

//	printf("destroy conn %p fid %d\n", conn, fid->fid);
	pthread_mutex_lock(&fp->lock);
	hash = fid->fid % fp->size;
	htable = fp->htable;
	if (fid->prev)
		fid->prev->next = fid->next;
	else
		htable[hash] = fid->next;

	if (fid->next)
		fid->next->prev = fid->prev;

	pthread_mutex_unlock(&fp->lock);

	if (fid->type & Qtauth && srv->auth && srv->auth->clunk)
		(*srv->auth->clunk)(fid);
	else if (!(fid->type&Qtauth) && srv->fiddestroy)
		(*srv->fiddestroy)(fid);

	if (fid->user)
		np_user_decref(fid->user);

	free(fid);

	return;
}

void
np_fid_incref(Npfid *fid)
{
	if (!fid)
		return;

	pthread_mutex_lock(&fid->lock);
	fid->refcount++;
	pthread_mutex_unlock(&fid->lock);
}

void
np_fid_decref(Npfid *fid)
{
	int n;

	if (!fid)
		return;

	pthread_mutex_lock(&fid->lock);
	n = --fid->refcount;
	pthread_mutex_unlock(&fid->lock);

	if (!n)
		np_fid_destroy(fid);
}
