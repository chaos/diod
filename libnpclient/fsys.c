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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

static Npfcall *npc_fcall_alloc(u32 msize);
static void npc_fcall_free(Npfcall *fc);

Npcfsys *
npc_create_fsys(int fd, int msize)
{
	Npcfsys *fs;

	fs = malloc(sizeof(*fs));
	if (!fs) {
		np_uerror(ENOMEM);
		return NULL;
	}

	np_uerror (0);
	pthread_mutex_init(&fs->lock, NULL);
	fs->fd = fd;
	fs->msize = msize;
	fs->trans = NULL;
	fs->root = NULL;
	fs->tagpool = NULL;
	fs->fidpool = NULL;
	fs->refcount = 1;

	fs->trans = np_fdtrans_create(fd, fd);
	if (!fs->trans) {
		np_uerror(EIO);
		goto error;
	}

	fs->tagpool = npc_create_pool(P9_NOTAG);
	if (!fs->tagpool) {
		np_uerror(EIO);
		goto error;
	}
		
	fs->fidpool = npc_create_pool(P9_NOFID);
	if (!fs->fidpool) {
		np_uerror(EIO);
		goto error;
	}

	return fs;

error:
	npc_disconnect_fsys(fs);
	npc_decref_fsys(fs);
	return NULL;
}

void
npc_disconnect_fsys(Npcfsys *fs)
{
	pthread_mutex_lock(&fs->lock);
	if (fs->fd >= 0) {
		//shutdown(fs->fd, 2);
		close(fs->fd);
		fs->fd = -1;
	}
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

	//assert(fs->fd<0 && fs->trans==NULL);
	assert(fs->trans==NULL);
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

	free(fs);
}

static int
_write_request (Npcfsys *fs, Npfcall *tc)
{
	int i, n = 0;
	int ret = -1;

	do {
		i = np_trans_write(fs->trans, tc->pkt + n, tc->size - n);
		if (i < 0) {
			np_uerror (errno);
			goto done;
		}
		n += i;
	} while (n < tc->size);
	ret = 0;
done:
	return ret;	
}

static int
_read_response (Npcfsys *fs, Npfcall *rc)
{
	int i, n = 0;
	int size;
	int ret = -1;

	while ((i = np_trans_read(fs->trans, rc->pkt + n, fs->msize - n)) > 0) {
		n += i;
		if (n < 4)
			continue;

		size = rc->pkt[0] | (rc->pkt[1]<<8)
				  | (rc->pkt[2]<<16) 
				  | (rc->pkt[3]<<24);
		if (n < size)
			continue;

		if (!np_deserialize(rc, rc->pkt)) {
			np_uerror (EIO); /* failed to parse */
			break;
		}
		ret = 0;
		break;
	}
	if (i < 0) {
		np_uerror (errno);
		goto done;
	}
	if (i == 0) {
		np_uerror (EIO);
		goto done;
	}
done:
	return ret;
}

int
npc_rpc(Npcfsys *fs, Npfcall *tc, Npfcall **rcp)
{
	Npfcall *rc = NULL;
	u16 tag = P9_NOTAG;
	int saved_error = 0;
	int ret = -1;

	if (!fs->trans) {
		np_uerror(ECONNABORTED);
		goto done;
	}
	if (tc->type != P9_TVERSION)
		tag = npc_get_id(fs->tagpool);
	np_set_tag(tc, tag);

	if (_write_request (fs, tc) < 0)
		goto done;

	if (!(rc = npc_fcall_alloc(fs->msize))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (_read_response (fs, rc) < 0)
		goto done;

	if (tc->tag != rc->tag) {
		np_uerror (EIO); /* unmatched response */
		goto done;
	}
	if (rc->type == P9_RLERROR) {
		np_uerror (rc->u.rlerror.ecode);
		goto done;
	}
	*rcp = rc;
	ret = 0;
done:
	saved_error = np_rerror ();
	if (tag != P9_NOTAG)
		npc_put_id(fs->tagpool, tag);
	if (ret < 0 && rc != NULL)
		npc_fcall_free (rc);
	np_uerror (saved_error);
	return ret;
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
