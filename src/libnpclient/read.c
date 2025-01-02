/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

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

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
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
	if (rc->u.rread.count > count) {
		np_uerror (EPROTO);
		goto done;
	}
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

static int
_buf_save (Npcfid *fid, char *buf, int len)
{
	if (fid->buf) {
		if (!(fid->buf = realloc (fid->buf, fid->buf_size + len)))
			goto nomem;
		memcpy (fid->buf + fid->buf_size, buf, len);
		fid->buf_size += len;
	} else {
		if (!(fid->buf = malloc (len)))
			goto nomem;
		memcpy (fid->buf, buf, len);
		fid->buf_size = len;
	}
	return 0;
nomem:
	np_uerror (ENOMEM);
	return -1;
}

static int
_buf_restore (Npcfid *fid, char *buf, int len)
{
	int ret = 0;

	if (fid->buf) {
		ret = len > fid->buf_size ? fid->buf_size : len;
		memcpy (buf, fid->buf, ret);
		if (fid->buf_size > ret) {
			memmove (fid->buf, fid->buf + ret, fid->buf_size - ret);
			fid->buf_size -= ret;
		} else {
			free (fid->buf);
			fid->buf = NULL;
			fid->buf_size = 0;
		}
	}
	return ret;
}

static char *
_strnchr (char *s, char c, int len)
{
	int i;

	for (i = 0; i < len; i++)
		if (s[i] == c)
			return &s[i];
	return NULL;
}

char *
npc_gets(Npcfid *fid, char *buf, u32 count)
{
	int n = 0, done = 0, extra = 0;
	char *nlp = NULL;

	n = _buf_restore (fid, buf, count - 1);
	while (done < count && !nlp) {
		if (done > 0 || n == 0) {
			n = npc_read (fid, buf + done, count - done - 1);
			if (n < 0)
				goto error;
			if (n == 0)
				break;
		}
		nlp = _strnchr (buf + done, '\n', n);
		done += n;
	}
	if (nlp) {
		*nlp = '\0';
		extra = done - (nlp - buf) - 1;
		if (extra > 0 && _buf_save (fid, nlp + 1, extra) < 0)
			goto error;
		done = nlp - buf;
	}
	buf[done] = '\0';
	return done > 0 ? buf : NULL;
error:
	return NULL;
}
