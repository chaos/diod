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

int
npc_getattr (Npcfid *fid, u64 request_mask, u64 *valid, struct p9_qid *qid,
	     u32 *mode, u32 *uid, u32 *gid, u64 *nlink, u64 *rdev, u64 *size,
	     u64 *blksize, u64 *blocks, u64 *atime_sec, u64 *atime_nsec,
	     u64 *mtime_sec, u64 *mtime_nsec, u64 *ctime_sec, u64 *ctime_nsec,
	     u64 *btime_sec, u64 *btime_nsec, u64 *gen, u64 *data_version)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tgetattr (fid->fid, request_mask))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	*valid = rc->u.rgetattr.valid;
	*qid = rc->u.rgetattr.qid;
	*mode = rc->u.rgetattr.mode;
	*uid = rc->u.rgetattr.uid;
	*gid = rc->u.rgetattr.gid;
	*nlink = rc->u.rgetattr.nlink;
	*rdev = rc->u.rgetattr.rdev;
	*size = rc->u.rgetattr.size;
	*blksize = rc->u.rgetattr.blksize;
	*blocks = rc->u.rgetattr.blocks;
	*atime_sec = rc->u.rgetattr.atime_sec;
	*atime_nsec = rc->u.rgetattr.atime_nsec;
	*mtime_sec = rc->u.rgetattr.mtime_sec;
	*mtime_nsec = rc->u.rgetattr.mtime_nsec;
	*ctime_sec = rc->u.rgetattr.ctime_sec;
	*ctime_nsec = rc->u.rgetattr.ctime_nsec;
	*btime_sec = rc->u.rgetattr.btime_sec;
	*btime_nsec = rc->u.rgetattr.btime_nsec;
	*gen = rc->u.rgetattr.gen;
	*data_version = rc->u.rgetattr.data_version;
	ret = 0;
done:
	if (tc)
		free(tc);
	if (rc)
		free(rc);	
	return ret;
}

int
npc_fstat (Npcfid *fid, struct stat *sb)
{
	int ret = -1;
	u64 valid;
	struct p9_qid qid;
	u32 mode, uid, gid;
	u64 nlink, rdev, size, blksize, blocks;
	u64 atime_sec, atime_nsec;
	u64 mtime_sec, mtime_nsec;
	u64 ctime_sec, ctime_nsec;
	u64 btime_sec, btime_nsec;
	u64 gen, data_version;

	ret = npc_getattr (fid, P9_STAT_BASIC, &valid, &qid,
			   &mode, &uid, &gid, &nlink, &rdev, &size,
			   &blksize, &blocks, &atime_sec, &atime_nsec,
			   &mtime_sec, &mtime_nsec, &ctime_sec, &ctime_nsec,
			   &btime_sec, &btime_nsec, &gen, &data_version);
#ifdef __MACH__
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#define st_ctim st_ctimespec
#endif
	if (ret == 0) {
		sb->st_dev = 0;
		sb->st_ino = qid.path;
		sb->st_mode = mode;
		sb->st_uid = uid;
		sb->st_gid = gid;
		sb->st_nlink = nlink;
		sb->st_rdev = rdev;
		sb->st_size = size;
		sb->st_blksize = blksize;
		sb->st_blocks = blocks;
		sb->st_atime = atime_sec;
		sb->st_mtime = mtime_sec;
		sb->st_ctime = ctime_sec;
		sb->st_atim.tv_nsec = atime_nsec;
		sb->st_mtim.tv_nsec = mtime_nsec;
		sb->st_ctim.tv_nsec = ctime_nsec;
	}
#ifdef __MACH__
#undef st_atim
#undef st_mtim
#undef st_ctim
#endif
	return ret;
}

int
npc_stat (Npcfid *root, char *path, struct stat *sb)
{
	Npcfid *fid;

	if (!(fid = npc_walk (root, path)))
		return -1;
	if (npc_fstat (fid, sb) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		np_uerror (saved_err);
		return -1;
	}
	if (npc_clunk (fid) < 0)
		return -1;
	return 0;
}
