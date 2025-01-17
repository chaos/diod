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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <libgen.h>

#include "src/libnpfs/npfs.h"
#include "npclient.h"
#include "npcimpl.h"

int
npc_remove (Npcfid *fid)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tremove(fid->fid))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	ret = 0;
done:
	npc_fid_free (fid); /* fid is no longer valid after remove */
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return ret;
}

int
npc_remove_bypath (Npcfid *root, char *path)
{
	Npcfid *fid;

	if (!(fid = npc_walk (root, path)))
		return -1;
	if (npc_remove (fid) < 0)
		return -1;
	return 0;
}
