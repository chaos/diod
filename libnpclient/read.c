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
#include <stdarg.h>
#include <unistd.h>
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
npc_pread(Npcfid *fid, void *buf, u32 count, u64 offset)
{
	int maxio = fid->fsys->msize - P9_IOHDRSZ;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (count > maxio)
		count = maxio;
	if (!(tc = np_create_tread(fid->fid, offset, count))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	memmove(buf, rc->u.rread.data, rc->u.rread.count);
	ret = rc->u.rread.count;
done:
	if (rc)
		free(rc);
	if (tc)
		free(tc);

	return ret;
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
npc_get(Npcfid *root, char *path, void *buf, u32 count)
{
	int n, len = 0;
	Npcfid *fid;

	if (!(fid = npc_open_bypath(root, path, O_RDONLY)))
		return -1;
	while (len < count) {
		n = npc_read(fid, buf + len, count - len);
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		len += n;
		if ((fid->fsys->flags & NPC_SHORTREAD_EOF)
					&& (len - n < count - len))
			break;
	}
	if (npc_clunk (fid) < 0)
		return -1;
	return len;
}

#define AGET_CHUNK 4096
char *
npc_aget(Npcfid *root, char *path)
{
	int n, len;
	Npcfid *fid = NULL;
	char *s = NULL;;
	int ssize = 0;

	if (!(fid = npc_open_bypath(root, path, O_RDONLY)))
		goto error;
	len = 0;
	do {
		if (!s) {
			ssize = AGET_CHUNK;
			s = malloc (ssize);
		} else if (ssize - len == 1) {
			ssize += AGET_CHUNK;
			s = realloc (s, ssize);
		}
		if (!s) {
			np_uerror (ENOMEM);
			goto error;
		}
		n = npc_read(fid, s + len, ssize - len - 1);
		if (n > 0) {
			len += n;
			if ((fid->fsys->flags & NPC_SHORTREAD_EOF)
						&& (len - n < ssize - len - 1))
				break;
		}
	} while (n > 0);
	if (n < 0)
		goto error;
	if (npc_clunk (fid) < 0)
		goto error;
	s[len] = '\0';
	return s;
error:
	if (s)
		free (s);
	if (fid)
		(void)npc_clunk (fid);
	return NULL;
}


/* FIXME: embed a buffer in Npcfid like stdio.
 */
char *
npc_gets(Npcfid *fid, char *buf, u32 count)
{
	int n, done = 0;
	char *p;

	while (done < count) {
		n = npc_pread (fid, buf + done,
			       count - done - 1, fid->offset + done);
		if (n < 0)
			return NULL;
		if (n == 0)
			break;
		done += n;
		buf[done] = '\0';
		if ((p = strchr (buf, '\n'))) {
			*p = '\0';
			done = strlen (buf) + 1;
			break;
		}
	}
	fid->offset += done;
	return (done > 0 ? buf : NULL);
}
