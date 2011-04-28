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

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

static int
_fidstat (Npcfid *fid, struct stat *sb)
{
	u64 request_mask = P9_GETATTR_BASIC;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	errno = 0;
	if (!(tc = np_create_tgetattr (fid->fid, request_mask)))
		goto done;
	if (npc_rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	sb->st_dev = 0;
	sb->st_ino = rc->u.rgetattr.qid.path;
	sb->st_mode = rc->u.rgetattr.mode;
	sb->st_uid = rc->u.rgetattr.uid;
	sb->st_gid = rc->u.rgetattr.gid;
	sb->st_nlink = rc->u.rgetattr.nlink;
	sb->st_rdev = rc->u.rgetattr.rdev;
	sb->st_size = rc->u.rgetattr.size;
	sb->st_blksize = rc->u.rgetattr.blksize;
	sb->st_blocks = rc->u.rgetattr.blocks;
	sb->st_atime = rc->u.rgetattr.atime_sec;
	sb->st_atim.tv_nsec = rc->u.rgetattr.atime_nsec;
	sb->st_mtime = rc->u.rgetattr.mtime_sec;
	sb->st_mtim.tv_nsec = rc->u.rgetattr.mtime_nsec;
	sb->st_ctime = rc->u.rgetattr.ctime_sec;
	sb->st_ctim.tv_nsec = rc->u.rgetattr.ctime_nsec;
	ret = 0;
done:
	errno = np_rerror ();
	if (tc)
		free(tc);
	if (rc)
		free(rc);	
	return ret;
}

int
npc_stat (Npcfsys *fs, char *path, struct stat *sb)
{
	Npcfid *fid;
	int saved_errno;
	int ret = -1;

	if (!(fid = npc_walk (fs, path)))
		return -1;
	ret = _fidstat (fid, sb);
	saved_errno = errno;
	(void)npc_clunk (fid);
	errno = saved_errno;

	return ret;
}
