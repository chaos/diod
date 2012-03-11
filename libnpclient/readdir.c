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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <libgen.h>
#include <sys/param.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

int
npc_readdir (Npcfid *fid, u64 offset, char *data, u32 count)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_treaddir(fid->fid, offset, count))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	NP_ASSERT(rc->u.rreaddir.count <= count);
	memcpy (data, rc->u.rreaddir.data, rc->u.rreaddir.count);
	ret = rc->u.rreaddir.count;
done:
	if (tc)
		free(tc);
	if (rc)
		free(rc);	
	return ret;
}

int
npc_closedir (Npcfid *fid)
{
	if (fid->dbuf) {
		free (fid->dbuf);
		fid->dbuf = NULL;
	}

	return npc_clunk (fid);
}

Npcfid *
npc_opendir (Npcfid *root, char *path)
{
	Npcfid *fid;

	if ((fid = npc_open_bypath (root, path, O_RDONLY))) {
		fid->dbuf_size = root->fsys->msize - P9_IOHDRSZ;
		if (!(fid->dbuf = malloc (fid->dbuf_size))) {
			(void)npc_closedir (fid);
			np_uerror (ENOMEM);
			fid = NULL;
		}
		fid->offset = 0;
		fid->dbuf_len = 0;
		fid->dbuf_used = 0;
	}
	return fid;
}

/* Returns error code > 0 on error.
 * Returns 0 on success: result set to NULL at EOF, o/w set to entry
 */
int
npc_readdir_r (Npcfid *fid, struct dirent *entry, struct dirent **result)
{
	Npqid qid;
	int dname_size = PATH_MAX + 1;
	u64 offset;
	u8 type;
	int res;

	if (fid->dbuf_used >= fid->dbuf_len) {
		fid->dbuf_len = npc_readdir (fid, fid->offset, fid->dbuf,
					      fid->dbuf_size);
		if (fid->dbuf_len < 0)
			return np_rerror ();
		if (fid->dbuf_len == 0) {	/* EOF */
			*result = NULL;
			return 0;
		}
		fid->dbuf_used = 0;
	}
	res = np_deserialize_p9dirent (&qid, &offset, &type,
				       entry->d_name, dname_size,
				       (u8 *)fid->dbuf + fid->dbuf_used,
				       fid->dbuf_len   - fid->dbuf_used);
	if (res == 0)
		return EIO;
	entry->d_off = offset;
	entry->d_type = type;
	entry->d_ino = qid.path;
	//entry->d_reclen
	fid->offset = offset;
	fid->dbuf_used += res;
	*result = entry;
	return 0;
}

void
npc_seekdir (Npcfid *fid, long offset)
{
	fid->offset = offset;
	fid->dbuf_used = fid->dbuf_len; /* force a 9p readdir call */
}

long
npc_telldir (Npcfid *fid)
{
	return fid->offset;
}
