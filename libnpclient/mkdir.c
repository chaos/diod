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

