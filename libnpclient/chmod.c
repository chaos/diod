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
#include <unistd.h>
#include <sys/types.h>
#include <utime.h>
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
npc_setattr (Npcfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
	     u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tsetattr (fid->fid, valid, mode, uid, gid, size,
				       atime_sec, atime_nsec,
				       mtime_sec, mtime_nsec))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	ret = 0;
done:
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return ret;
}

int
npc_fchmod (Npcfid *fid, mode_t mode)
{
	u32 valid = P9_ATTR_MODE;

	return npc_setattr (fid, valid, mode, 0, 0, 0, 0, 0, 0, 0);
}

int
npc_chmod (Npcfid *root, char *path, mode_t mode)
{
	Npcfid *fid;

	if (!(fid = npc_walk (root, path)))
		return -1;
	if (npc_fchmod (fid, mode) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		np_uerror (saved_err);
		return -1;
	}
	if (npc_clunk (fid) < 0)
		return -1;
	return 0;
}

int
npc_fchown (Npcfid *fid, uid_t owner, gid_t group)
{
	u32 valid = 0;

	if (owner != -1)
		valid |= P9_ATTR_UID;
	if (group != -1)
		valid |= P9_ATTR_GID;
	return npc_setattr (fid, valid, 0, owner, group, 0, 0, 0, 0, 0);
}

int
npc_chown (Npcfid *root, char *path, uid_t owner, gid_t group)
{
	Npcfid *fid;

	if (!(fid = npc_walk (root, path)))
		return -1;
	if (npc_fchown (fid, owner, group) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		np_uerror (saved_err);
		return -1;
	}
	if (npc_clunk (fid) < 0)
		return -1;
	return 0;
}

int
npc_ftruncate (Npcfid *fid, off_t length)
{
	u32 valid = P9_ATTR_SIZE;

	return npc_setattr (fid, valid, 0, 0, 0, length, 0, 0, 0, 0);
}

int
npc_truncate (Npcfid *root, char *path, off_t length)
{
	Npcfid *fid;

	if (!(fid = npc_walk (root, path)))
		return -1;
	if (npc_ftruncate (fid, length) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		np_uerror (saved_err);
		return -1;
	}
	if (npc_clunk (fid) < 0)
		return -1;
	return 0;
}

int
npc_futime (Npcfid *fid, const struct utimbuf *times)
{
	u32 valid = 0;
	u64 atime_sec = 0, atime_nsec = 0;
	u64 mtime_sec = 0, mtime_nsec = 0;

	valid |= P9_ATTR_ATIME;
	valid |= P9_ATTR_MTIME;
	if (times) {
		atime_sec = times->actime;
		valid |= P9_ATTR_ATIME_SET;
		mtime_sec = times->modtime;
		valid |= P9_ATTR_MTIME_SET;
	}

	return npc_setattr (fid, valid, 0, 0, 0, 0,
			    atime_sec, atime_nsec, mtime_sec, mtime_nsec);
}

int
npc_utime(Npcfid *root, char *path, const struct utimbuf *times)
{
	Npcfid *fid;

	if (!(fid = npc_walk (root, path)))
		return -1;
	if (npc_futime (fid, times) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		np_uerror (saved_err);
		return -1;
	}
	if (npc_clunk (fid) < 0)
		return -1;
	return 0;
}

