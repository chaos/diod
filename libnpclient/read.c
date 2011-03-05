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
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

int
npc_pread(Npcfid *fid, void *buf, u32 count, u64 offset)
{
	int maxio = fid->fsys->msize - P9_IOHDRSZ;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (count > maxio)
		count = maxio;
	if (!(tc = np_create_tread(fid->fid, offset, count)))
		goto done;
	if (npc_rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	memmove(buf, rc->u.rread.data, rc->u.rread.count);
	ret = rc->u.rread.count;
done:
	if (rc)
		free(rc);
	if (tc)
		free(tc);
	errno = np_rerror ();

	return ret;
}

int
npc_pread_all(Npcfid *fid, void *buf, u32 count, u64 offset)
{
	int n = 0, done = 0;

	while (done < count && n > 0) {
		n = npc_pread(fid, buf + done, count - done, offset + done);
		if (n < 0)
			return -1;
		done += n;
	}
	return done;
}

int
npc_read(Npcfid *fid, void *buf, u32 count)
{
	int ret;

	ret = npc_pread (fid, buf, count, fid->offset);
	if (ret > 0)
		fid->offset += ret;
	return ret;
}

int
npc_read_all(Npcfid *fid, void *buf, u32 count)
{
	int n = 0, done = 0;

	while (done < count && n > 0) {
		n = npc_read(fid, buf + done, count - done);
		if (n < 0)
			return -1;
		done += n;
	}
	return done;
}

char *
npc_gets (Npcfid *fid, char *buf, u32 count)
{
	int n, skipchars = 0;
	char *p;

	n = npc_pread_all (fid, (void *)buf, count - 1, fid->offset);
	if (n <= 0)
		return NULL;
	buf[n] = '\0';
	if ((p = strchr (buf, '\n'))) {
		*p = '\0';
		skipchars++;
	}
	fid->offset += strlen (buf) + skipchars;
	return buf;
}
