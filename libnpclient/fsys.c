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

/* fsys.c - simpler, single-threaded version of Lucho's original */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "xpthread.h"
#include "npcimpl.h"

static int npc_rpc(Npcfsys *fs, Npfcall *tc, Npfcall **rcp);
static void npc_incref_fsys(Npcfsys *fs);
static void npc_decref_fsys(Npcfsys *fs);

Npcfsys *
npc_create_fsys(int rfd, int wfd, int msize, int flags)
{
	Npcfsys *fs;

	fs = malloc(sizeof(*fs));
	if (!fs) {
		np_uerror(ENOMEM);
		return NULL;
	}

	np_uerror (0);
	pthread_mutex_init(&fs->lock, NULL);
	fs->msize = msize;
	fs->trans = NULL;
	fs->tagpool = NULL;
	fs->fidpool = NULL;
	fs->refcount = 1;
	fs->rpc = npc_rpc;
	fs->incref = npc_incref_fsys;
	fs->decref = npc_decref_fsys;
	fs->disconnect = NULL;
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
	return fs;

error:
	npc_decref_fsys(fs); /* will close fds if trans successfully created */
	(void)close (rfd);   /* close here anyway for consistancy */
	if (rfd != wfd)
		(void)close (wfd);
	return NULL;
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
	if (fs->trans) {
		np_trans_destroy(fs->trans); /* closes fds */
		fs->trans = NULL;
	}
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
	free(fs);
}

static int
npc_rpc(Npcfsys *fs, Npfcall *tc, Npfcall **rcp)
{
	Npfcall *rc = NULL;
	u16 tag = P9_NOTAG;
	int n, ret = -1;

	if (!fs->trans) {
		np_uerror(ECONNABORTED);
		goto done;
	}
	if (tc->type != P9_TVERSION)
		tag = npc_get_id(fs->tagpool);
	np_set_tag(tc, tag);

	xpthread_mutex_lock(&fs->lock);
	n = np_trans_send (fs->trans, tc);
	if (n >= 0)
		n = np_trans_recv(fs->trans, &rc, fs->msize);
	xpthread_mutex_unlock(&fs->lock);
	if (n < 0)
		goto done;
	if (rc == NULL) {
		np_uerror (EPROTO); /* premature EOF */
		goto done;
	}
	if (tc->tag != rc->tag) {
		np_uerror (EPROTO); /* unmatched response */
		goto done;
	}
	if (rc->type == P9_RLERROR) {
		np_uerror (rc->u.rlerror.ecode);
		goto done;
	}
	*rcp = rc;
	ret = 0;
done:
	if (tag != P9_NOTAG)
		npc_put_id(fs->tagpool, tag);
	if (ret < 0 && rc != NULL)
		free (rc);
	return ret;
}
