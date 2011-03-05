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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <libgen.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

/* fid: parent directory on entry, new file on exit.
 */
static int
_fidcreate (Npcfid *fid, char *path, u32 flags, u32 mode)
{
	int maxio = fid->fsys->msize - P9_IOHDRSZ;
	char *cpy, *fname;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(cpy = strdup (path))) {
		errno = ENOMEM;
		return -1;
	}
	fname = basename (cpy);
	errno = 0;
	if (!(tc = np_create_tlcreate(fid->fid, fname, flags, mode, getegid())))
		goto done;
	if (npc_rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	fid->iounit = rc->u.rlcreate.iounit;
	if (!fid->iounit == 0 || fid->iounit > maxio)
		fid->iounit = maxio;
	ret = 0;
done:
	free (cpy);
	errno = np_rerror ();
	if (tc)
		free(tc);
	if (rc)
		free(rc);	
	return ret;
}

/* walk to parent
 */
static Npcfid*
_walkparent (Npcfsys *fs, char *path)
{
	char *cpy, *dname; 
	Npcfid *fid;

	if (!(cpy = strdup (path))) {
		errno = ENOMEM;
		return NULL;
	}
	dname = dirname (cpy);
	fid = npc_walk (fs, dname);
	free (cpy);

	return fid;
}

Npcfid*
npc_create (Npcfsys *fs, char *path, u32 flags, u32 mode)
{
	Npcfid *fid;
	int saved_errno;

	if (!(fid = _walkparent (fs, path)))
		return NULL;
	if (_fidcreate (fid, path, flags, mode) < 0) {
		saved_errno = errno;
		(void)npc_clunk (fid);
		errno = saved_errno;
		fid = NULL;
	}
	return fid;
}

int
npc_open_fid (Npcfid *fid, u32 mode)
{
	int maxio = fid->fsys->msize - P9_IOHDRSZ;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	errno = 0;
	if (!(tc = np_create_tlopen(fid->fid, mode)))
		goto done;
	if (npc_rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	fid->iounit = rc->u.rlopen.iounit;
	if (fid->iounit == 0 || fid->iounit > maxio)
		fid->iounit = maxio;
	fid->offset = 0;
	ret = 0;
done:
	errno = np_rerror ();
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return ret;
}

Npcfid*
npc_open (Npcfsys *fs, char *path, u32 mode)
{
	int saved_errno;
	Npcfid *fid;

	if (!(fid = npc_walk (fs, path)))
		return NULL;
	if (npc_open_fid (fid, mode) < 0) {
		saved_errno = errno;
		(void)npc_clunk (fid);
		errno = saved_errno;
		return NULL;
	}
	return fid;
}

int
npc_close(Npcfid *fid)
{
	return npc_clunk(fid);
}

u64
npc_lseek(Npcfid *fid, u64 offset, int whence)
{
	u64 file_size = 0;

	switch (whence) {
		case SEEK_SET:
			fid->offset = offset;
			break;
		case SEEK_CUR:
			fid->offset += offset;
			break;
		case SEEK_END:
			/* FIXME: get file_size */
			fid->offset = file_size + offset;
			break;
	}

	return fid->offset;
}
