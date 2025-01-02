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

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "npclient.h"
#include "npcimpl.h"

/* fid: parent directory on entry, new file on exit.
 */
int
npc_create (Npcfid *fid, char *name, u32 flags, u32 mode, gid_t gid)
{
	int maxio = fid->fsys->msize - P9_IOHDRSZ;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tlcreate(fid->fid, name, flags, mode, gid))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	fid->iounit = rc->u.rlcreate.iounit;
	if (fid->iounit == 0 || fid->iounit > maxio)
		fid->iounit = maxio;
	ret = 0;
done:
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return ret;
}

Npcfid *
npc_create_bypath (Npcfid *root, char *path, u32 flags, u32 mode, gid_t gid)
{
        Npcfid *fid;
        char *cpy, *dname, *fname;

        /* walk to the parent */
        if (!(cpy = strdup (path))) {
                np_uerror (ENOMEM);
                return NULL;
        }
        dname = dirname (cpy);
        fid = npc_walk (root, dname);
        free (cpy);
        if (!fid)
                return NULL;

        /* create the child */
        if (!(cpy = strdup (path))) {
                (void)npc_clunk (fid);
                np_uerror (ENOMEM);
                return NULL;
        }
        fname = basename (cpy);
        if (npc_create (fid, fname, flags, mode, gid) < 0) {
                int saved_err = np_rerror ();
                (void)npc_clunk (fid);
                free (cpy);
                np_uerror (saved_err);
                return NULL;
        }
        free (cpy);
        return fid;
}

int
npc_open (Npcfid *fid, u32 flags)
{
	int maxio = fid->fsys->msize - P9_IOHDRSZ;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tlopen(fid->fid, flags))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	fid->iounit = rc->u.rlopen.iounit;
	if (fid->iounit == 0 || fid->iounit > maxio)
		fid->iounit = maxio;
	fid->offset = 0;
	ret = 0;
done:
	if (tc)
		free(tc);
	if (rc)
		free(rc);
	return ret;
}

Npcfid *
npc_open_bypath (Npcfid *root, char *path, u32 flags)
{
	Npcfid *fid;

	if (!(fid = npc_walk (root, path)))
		return NULL;
	if (npc_open (fid, flags) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		np_uerror (saved_err);
		return NULL;
	}
	return fid;
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
