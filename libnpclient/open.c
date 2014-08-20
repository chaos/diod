/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010-2014 by Lawrence Livermore National Security, LLC
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
#include <stdarg.h>
#include <string.h>
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
	if (!fid->iounit == 0 || fid->iounit > maxio)
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
