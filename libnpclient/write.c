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
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

int
npc_pwrite(Npcfid *fid, void *buf, u32 count, u64 offset)
{
	int maxio = fid->fsys->msize - P9_IOHDRSZ;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (count > maxio)
		count = maxio;	
	if (!(tc = np_create_twrite(fid->fid, offset, count, buf))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	ret = rc->u.rwrite.count;
done:
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return ret;
}

int
npc_write(Npcfid *fid, void *buf, u32 count)
{
	int ret;

	ret = npc_pwrite (fid, buf, count, fid->offset);
	if (ret > 0)
		fid->offset += ret;
	return ret;
}

int
npc_put(Npcfid *root, char *path, void *buf, u32 count)
{
	int n, done = 0;
	Npcfid *fid;

	if (!(fid = npc_open_bypath (root, path, O_WRONLY)))
		return -1;
	while (done < count) {
		n = npc_write(fid, buf + done, count - done);
		if (n < 0) {
			done = -1;
			break;
		}
		done += n;
	}
	if (npc_clunk (fid) < 0)
		done = -1;
	return done;
}

int
npc_puts (Npcfid *fid, char *buf)
{
	int n, count = strlen (buf), done = 0;

	while (done < count) {
		n = npc_write(fid, buf + done, count - done);
		if (n < 0) {
			done = -1;
			break;
		}
		done += n;
	}
	return done;
}
