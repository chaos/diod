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
#include <errno.h>
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

Npwstat *
npc_stat(Npcfsys *fs, char *path)
{
	Npfcall *tc, *rc;
	Npcfid *fid;
	Npwstat *st;
	char *sbuf;

	st = NULL;
	fid = npc_walk(fs, path);
	if (!fid)
		return NULL;

	tc = np_create_tstat(fid->fid);
	if (npc_rpc(fs, tc, &rc) < 0) {
		free(tc);
		npc_close(fid);
		return NULL;
	}

	free(tc);
	st = malloc(npc_wstatlen(&rc->stat));
	if (!st) {
		np_werror(Ennomem, ENOMEM);
		npc_close(fid);
		return NULL;
	}

	sbuf = ((char *) st) + sizeof(*st);
	npc_stat2wstat(&rc->stat, st, &sbuf);
	free(rc);

	return st;
}

void
npc_emptystat(Npwstat *st)
{
	u64 i64 = 0xffffffffffffffffL;
	u32 i32 = 0xffffffff;
	u16 i16 = 0xffff;
	u8 i8 = 0xff;

	st->size = i16;
	st->type = i16;
	st->dev = i32;
	st->qid.type = i8;
	st->qid.version = i32;
	st->qid.path = i64;
	st->mode = i32;
	st->atime = i32;
	st->mtime = i32;
	st->length = i64;
	st->name = "";
	st->uid = "";
	st->muid = "";
	st->n_uid = i32;
	st->n_gid = i32;
	st->n_muid = i32;
}

int
npc_wstat(Npcfsys *fs, char *path, Npwstat *st)
{
	Npfcall *tc, *rc;
	Npcfid *fid;

	fid = npc_walk(fs, path);
	if (!fid)
		return 0;

	tc = np_create_twstat(fid->fid, st, fs->dotu);
	if (npc_rpc(fs, tc, &rc) < 0) {
		free(tc);
		npc_close(fid);
		return -1;
	}
	free(rc);
	free(tc);
	return 0;
}

