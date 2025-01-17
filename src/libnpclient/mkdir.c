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
#include <string.h>
#include <stdarg.h>
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
npc_mkdir (Npcfid *fid, char *name, u32 mode)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tmkdir(fid->fid, name, mode, getegid()))) {
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
npc_mkdir_bypath (Npcfid *root, char *path, u32 mode)
{
        Npcfid *fid;
	char *cpy, *dname, *fname;

	/* walk to the parent */
	if (!(cpy = strdup (path))) {
		np_uerror (ENOMEM);
		return -1;
	}
	dname = dirname (cpy);
        fid = npc_walk (root, dname);
	free (cpy);
	if (!fid)
		return -1;

	/* create the child */
	if (!(cpy = strdup (path))) {
		(void)npc_clunk (fid);
		np_uerror (ENOMEM);
		return -1;
	}
	fname = basename (cpy);
	if (npc_mkdir (fid, fname, mode) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		free (cpy);
		np_uerror (saved_err);
		return -1;
	}
	free (cpy);
        if (npc_clunk (fid) < 0)
		return -1;
        return 0;
}

