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
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"

/* N.B. lock ordering:
 * 1) pool lock
 * 2) fid lock
 */

static Npfid *
_destroy_fid (Npfid *f)
{
	Npsrv *srv;
	Npfid *next;

	assert (f != NULL);
	assert (f->magic == FID_MAGIC);

	srv = f->conn->srv;
	next = f->next;
	if (f->refcount > 0 && f->history) {
		np_logmsg (srv, "_destroy_fid: fid %d has %d refs: %s",
			   f->fid, f->refcount, f->history);
	}
	if ((f->type & P9_QTAUTH)) {
		if (srv->auth && srv->auth->clunk)
			(*srv->auth->clunk)(f);
	} else if ((f->type & P9_QTTMP)) {
		np_ctl_fiddestroy (f);
	} else {
		if (srv->fiddestroy)
			(*srv->fiddestroy)(f);
	}	
	if (f->user)
		np_user_decref(f->user);
	if (f->tpool)
		np_tpool_decref(f->tpool);
	if (f->aname)
		free (f->aname);
	if (f->history)
		free (f->history);
	pthread_mutex_destroy (&f->lock);
	f->magic = FID_MAGIC_FREED;
	free(f);

	return next;
}

static Npfid *
_create_fid (Npconn *conn, u32 fid, void *aux)
{
	Npfid *f = malloc (sizeof (*f));

	if (f) {
		memset (f, 0, sizeof (*f));
		f->conn = conn;
		f->fid = fid;
		f->aux = aux;
		pthread_mutex_init (&f->lock, NULL);
		if ((conn->srv->flags & SRV_FLAGS_DEBUG_FIDPOOL)) 
			if ((f->history = malloc (FID_HISTORY_SIZE)))
				f->history[0] = '\0';
		f->magic = FID_MAGIC;
	} else
		np_uerror (ENOMEM);

	return f;
}

static void
_append_history (Npfid *f, char sign, enum p9_msg_t op)
{
	if (f->history) {
		int len = strlen (f->history);

		snprintf (f->history + len, FID_HISTORY_SIZE - len,
			  "%c%d", sign, op);
	}
}

Npfidpool *
np_fidpool_create (void)
{
	const size_t hsize = FID_HTABLE_SIZE * sizeof(Npfid *);
	Npfidpool *pool;

	if ((pool = malloc (sizeof (*pool) + hsize))) {
		pthread_mutex_init (&pool->lock, NULL);
		pool->size = FID_HTABLE_SIZE;
		pool->htable = (Npfid **)((char *) pool + sizeof (*pool));
		memset(pool->htable, 0, hsize);
	} else
		np_uerror (ENOMEM);

	return pool;
}

int
np_fidpool_destroy(Npfidpool *pool)
{
	int i;
	Npfid *f;
	int unclunked = 0;

	xpthread_mutex_lock(&pool->lock);
	for(i = 0; i < pool->size; i++) {
		f = pool->htable[i];
		while (f != NULL) {
			f = _destroy_fid (f);
			unclunked++;
		}
	}
	xpthread_mutex_unlock (&pool->lock);
	pthread_mutex_destroy (&pool->lock);
	free(pool);

	return unclunked;
}

int
np_fidpool_count(Npfidpool *pool)
{
	int i;
	Npfid *f;
	int count = 0;

	xpthread_mutex_lock(&pool->lock);
	for(i = 0; i < pool->size; i++) {
		for (f = pool->htable[i]; f != NULL; f = f->next) {
			assert (f->magic == FID_MAGIC);
			count++;
		}
	}
	xpthread_mutex_unlock(&pool->lock);

	return count;
}

static void
_optimize_fid (Npfid **htable, Npfid *f, int hash)
{
	/* assert (pool->lock held) */
	if (f->next)
		f->next->prev = f->prev;
	f->prev->next = f->next;
	f->prev = NULL;
	f->next = htable[hash];
	htable[hash]->prev = f;
	htable[hash] = f;
}

static Npfid *
_lookup_fid (Npfidpool *pool, u32 fid, int hash)
{
	Npfid **htable = pool->htable;
	Npfid *f;

	/* assert (pool->lock held) */
	for (f = htable[hash]; f != NULL; f = f->next) {
		if (f->fid == fid) {
			if (f != htable[hash]) /* move to head */
				_optimize_fid (htable, f, hash);
			break;
		}
	}

	return f;
}

/* Find a fid, then refcount++
 */
Npfid *
np_fid_find (Npconn *conn, u32 fid, enum p9_msg_t op)
{
	Npfidpool *pool = conn->fidpool;
	int hash = fid % pool->size;
	Npfid *f;

	xpthread_mutex_lock (&pool->lock);
	if ((f = _lookup_fid (pool, fid, hash)))
		np_fid_incref (f, op);
	xpthread_mutex_unlock (&pool->lock);
	
	return f;
}

/* Create a fid with initial refcount of 1.
 */
Npfid *
np_fid_create (Npconn *conn, u32 fid, void *aux, enum p9_msg_t op)
{
	Npfidpool *pool = conn->fidpool;
	int hash = fid % pool->size;
	Npfid **htable = pool->htable;
	Npfid *f;

	xpthread_mutex_lock(&pool->lock);
	if ((f = _lookup_fid (pool, fid, hash))) {
		if (f->history) {
			char *path = "<nil>";

			if (f->conn->srv->get_path)
				path = f->conn->srv->get_path (f);
			np_logmsg (f->conn->srv,
				   "np_fid_create: fid %d (%s) has %d refs: %s",
			           f->fid, path, f->refcount, f->history);
		}
		np_uerror (EEXIST);
		f = NULL;
		goto done;
	}
	if (!(f = _create_fid (conn, fid, aux)))
		goto done;	
	np_fid_incref (f, op);
	f->next = htable[hash];
	f->prev = NULL;
	if (htable[hash])
		htable[hash]->prev = f;
	htable[hash] = f;
done:
	xpthread_mutex_unlock(&pool->lock);

	return f;
}

/* refcount++
 */
Npfid *
np_fid_incref (Npfid *f, enum p9_msg_t op)
{
	assert (f->magic == FID_MAGIC);

	xpthread_mutex_lock (&f->lock);
	f->refcount++;
	_append_history (f, '+', op);
	xpthread_mutex_unlock (&f->lock);

	return f;
}

/* refcount--
 * Destroy when refcount reaches zero.
 */
void
np_fid_decref (Npfid *f, enum p9_msg_t op)
{
	int refcount;

	assert (f->magic == FID_MAGIC);

	xpthread_mutex_lock (&f->lock);
	refcount = --f->refcount;
	_append_history (f, '-', op);
	xpthread_mutex_unlock (&f->lock);

	if (refcount == 0) {		
		Npfidpool *pool = f->conn->fidpool;
		int hash = f->fid % pool->size;
		Npfid **htable = pool->htable;

		xpthread_mutex_lock (&pool->lock);
		if (f->prev)
			f->prev->next = f->next;
		else
			htable[hash] = f->next;
		if (f->next)
			f->next->prev = f->prev;
		xpthread_mutex_unlock (&pool->lock);

		(void) _destroy_fid (f);
	}
}
