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
_fidmkdir(Npcfid *fid, char *path, u32 mode)
{
	char *cpy, *fname;
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(cpy = strdup (path))) {
		errno = ENOMEM;
		return -1;
	}
	fname = basename (cpy);
	errno = 0;
	if (!(tc = np_create_tmkdir(fid->fid, fname, mode, getegid())))
		goto done;
	if (npc_rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	ret = 0;
done:
	free (cpy);
	errno = np_rerror ();
	if (tc)
		free(tc);
	if (rc)
		free(rc);	
	return ret;
}

static Npcfid*
_walkparent (Npcfsys *fs, char *path)
{
	char *cpy, *dname; 
	Npcfid *fid;

	if (!(cpy = strdup (path))) {
		errno = ENOMEM;
		return NULL;
	}
	dname = dirname (cpy);
	fid = npc_walk (fs, dname);
	free (cpy);

	return fid;
}

int
npc_mkdir (Npcfsys *fs, char *path, u32 mode)
{
	Npcfid *fid;
	int saved_errno;
	int ret = -1;

	if (!(fid = _walkparent (fs, path)))
		return -1;
	ret = _fidmkdir(fid, path, mode);
	saved_errno = errno;
	(void)npc_clunk (fid);
	errno = saved_errno;

	return ret;
}
