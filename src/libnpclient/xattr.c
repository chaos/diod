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

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "npclient.h"
#include "npcimpl.h"

ssize_t
npc_xattrwalk (Npcfid *fid, Npcfid *attrfid, char *name)
{
	Npfcall *tc = NULL, *rc = NULL;
	ssize_t ret = -1;

	if (!(tc = np_create_txattrwalk (fid->fid, attrfid->fid, name))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	ret = rc->u.rxattrwalk.size;
done:
	if (tc)
		free (tc);
	if (rc)
		free (rc);
	return ret;
}

int
npc_xattrcreate (Npcfid *fid, char *name, u64 attr_size, u32 flags)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_txattrcreate (fid->fid, name, attr_size, flags))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	ret = 0;
done:
	if (tc)
		free (tc);
	if (rc)
		free (rc);
	return ret;
}

ssize_t
npc_listxattr (Npcfid *root, char *path, char *buf, size_t size)
{
	Npcfid *fid = NULL;
	Npcfid *attrfid = NULL;
	ssize_t ret = -1;
	size_t n, tot;

	if (!(fid = npc_walk (root, path)))
		goto done;
	if (!(attrfid = npc_fid_alloc(fid->fsys)))
		goto done;
        ret = npc_xattrwalk (fid, attrfid, NULL);
	if (ret < 0)
		goto done;
	if (buf == NULL || size == 0)
		goto done;
	if (ret > size) {
		np_uerror (ERANGE);
		goto done;
	}
	for (tot = 0; tot < size; ) {
		n = npc_read (attrfid, buf + tot, size - tot);
		if (n <= 0)
			break;
		tot += n;
	}
	ret = tot;
done:
	if (fid)
		(void)npc_clunk (fid);
	if (attrfid)
		(void)npc_clunk (attrfid);
        return ret;
}

ssize_t
npc_getxattr (Npcfid *root, char *path, char *attr, char *buf, size_t size)
{
	Npcfid *fid = NULL;
	Npcfid *attrfid = NULL;
	ssize_t ret = -1;
	size_t n, tot;

	if (!(fid = npc_walk (root, path)))
		goto done;
	if (!(attrfid = npc_fid_alloc(fid->fsys)))
		goto done;
        ret = npc_xattrwalk (fid, attrfid, attr);
	if (ret < 0)
		goto done;
	if (buf == NULL || size == 0)
		goto done;
	if (ret > size) {
		np_uerror (ERANGE);
		goto done;
	}
	for (tot = 0; tot < size; ) {
		n = npc_read (attrfid, buf + tot, size - tot);
		if (n <= 0)
			break;
		tot += n;
	}
	ret = tot;
done:
	if (fid)
		(void)npc_clunk (fid);
	if (attrfid)
		(void)npc_clunk (attrfid);
        return ret;
}

int
npc_setxattr (Npcfid *root, char *path, char *name, char *val, size_t size,
	      int flags)
{
	Npcfid *fid = NULL;
	int n, tot = 0;
	int ret = -1;

	if (!(fid = npc_walk (root, path)))
		goto done;
	if (npc_xattrcreate (fid, name, size, flags) < 0)
		goto done;
	for (tot = 0; tot < size; tot += n) {
		n = npc_write(fid, val + tot, size - tot);
		if (n < 0)
			goto done;
	}
	ret = 0;
done:
	if (fid)
		ret = npc_clunk(fid);
	return ret;
}

