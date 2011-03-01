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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

static int npc_clunk(Npcfid *fid);

Npcfid*
npc_create(Npcfsys *fs, char *path, u32 perm, int mode)
{
	char *fname, *pname;
	Npfcall *tc, *rc;
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

	tc = np_create_tcreate(fid->fid, fname, perm, mode);
	if (npc_rpc(fs, tc, &rc) < 0)
		goto error;

	fid->iounit = rc->iounit;
	if (!fid->iounit || fid->iounit>fid->fsys->msize-IOHDRSZ)
		fid->iounit = fid->fsys->msize-IOHDRSZ;

	free(tc);
	free(rc);
	if (pname)
		free(pname);
	else
		free(fname);

	return fid;

error:
	npc_clunk(fid);

	free(tc);
	if (pname)
		free(pname);
	else
		free(fname);

	return NULL;
}

Npcfid*
npc_open(Npcfsys *fs, char *path, int mode)
{
	Npfcall *tc, *rc;
	Npcfid *fid;

	fid = npc_walk(fs, path);
	if (!fid)
		return NULL;

	tc = np_create_topen(fid->fid, mode);
	if (npc_rpc(fs, tc, &rc) < 0) {
		npc_clunk(fid);
		free(tc);
		return NULL;
	}

	fid->iounit = rc->iounit;
	if (!fid->iounit || fid->iounit>fid->fsys->msize-IOHDRSZ)
		fid->iounit = fid->fsys->msize-IOHDRSZ;

	free(tc);
	free(rc);

	return fid;
}

static int
npc_clunk(Npcfid *fid)
{
	Npfcall *tc, *rc;
	Npcfsys *fs;

	fs = fid->fsys;
	tc = np_create_tclunk(fid->fid);
	if (npc_rpc(fid->fsys, tc, &rc) < 0) {
		free(tc);
		return -1;
	}

	npc_fid_free(fid);
	free(tc);
	free(rc);

	return 0;
}

int
npc_close(Npcfid *fid)
{
	npc_cancel_fid_requests(fid);
	return npc_clunk(fid);
}

