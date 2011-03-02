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

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

static int npc_clunk(Npcfid *fid);

Npcfid*
npc_create(Npcfsys *fs, char *path, u32 flags, u32 mode)
{
	char *fname, *pname;
	Npfcall *tc = NULL, *rc = NULL;
	Npcfid *fid;

	pname = strdup(path);
	if (!pname)
		return NULL;

	fname = strrchr(pname, '/');
	if (fname) {
		*fname = '\0';
		fname++;
	} else {
		fname = pname;
		pname = NULL;
	}

//	fprintf(stderr, "path %s dir %s fname %s\n", path, pname, fname);

	tc = NULL;
	fid = npc_walk(fs, pname);
	if (!fid) 
		goto error;

	tc = np_create_tlcreate(fid->fid, fname, flags, mode, getegid());
	if (!tc)
		goto error;
	if (npc_rpc(fs, tc, &rc) < 0)
		goto error;

	fid->iounit = rc->u.rlcreate.iounit;
	if (!fid->iounit || fid->iounit > fid->fsys->msize - P9_IOHDRSZ)
		fid->iounit = fid->fsys->msize - P9_IOHDRSZ;

	free(tc);
	free(rc);
	if (pname)
		free(pname);
	else
		free(fname);
	return fid;

error:
	npc_clunk(fid);
	if (tc)
		free(tc);
	if (pname)
		free(pname);
	else
		free(fname);

	return NULL;
}

Npcfid*
npc_open(Npcfsys *fs, char *path, u32 mode)
{
	Npfcall *tc = NULL;
	Npfcall *rc = NULL;
	Npcfid *fid;

	if (!(fid = npc_walk(fs, path)))
		goto error;
	if (!(tc = np_create_tlopen(fid->fid, mode)))
		goto error;
	if (npc_rpc(fs, tc, &rc) < 0)
		goto error;

	fid->iounit = rc->u.rlopen.iounit;
	if (!fid->iounit || fid->iounit > fid->fsys->msize - P9_IOHDRSZ)
		fid->iounit = fid->fsys->msize - P9_IOHDRSZ;

	free(tc);
	free(rc);

	return fid;
error:
	if (fid)
		npc_clunk(fid);
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return fid;
}

static int
npc_clunk(Npcfid *fid)
{
	Npfcall *tc = NULL;
	Npfcall *rc = NULL;

	if (!(tc = np_create_tclunk(fid->fid)))
		goto error;
	if (npc_rpc(fid->fsys, tc, &rc) < 0)
		goto error;
	npc_fid_free(fid);
	free(tc);
	free(rc);

	return 0;
error:
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return -1;
}

int
npc_close(Npcfid *fid)
{
	//npc_cancel_fid_requests(fid);
	return npc_clunk(fid);
}

