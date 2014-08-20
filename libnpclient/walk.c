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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

Npcfid *
npc_clone(Npcfid *fid)
{
	Npcfsys *fsys = fid->fsys;
	Npcfid *nfid = NULL;
	Npfcall *tc = NULL, *rc = NULL;

	if (!(nfid = npc_fid_alloc(fsys)))
		goto error;
	if (!(tc = np_create_twalk(fid->fid, nfid->fid, 0, NULL))) {
		np_uerror(ENOMEM);
		goto error;
	}
	if (fsys->rpc(fsys, tc, &rc) < 0)
		goto error;
	free(tc);
	free(rc);

	return nfid;
error:
	if (rc)
		free(rc);
	if (tc)
		free(tc);
	if (nfid)
		npc_fid_free (nfid);
	return NULL;
}

Npcfid *
npc_walk(Npcfid *nfid, char *path)
{
	int n;
	char *fname, *s, *t = NULL;
	char *wnames[P9_MAXWELEM];
	Npfcall *tc = NULL, *rc = NULL;
	Npcfid *fid = NULL;

	if (path == NULL)
		return npc_clone(nfid);

	while (*path == '/')
		path++;

	fname = strdup(path);
	if (!fname) {
		np_uerror(ENOMEM);
		goto error;
	}
	fid = npc_fid_alloc(nfid->fsys);
	if (!fid)
		goto error;
	s = fname;
	while (1) {
		n = 0;
		while (n<P9_MAXWELEM && *s!='\0') {
			if (*s == '/') {
				s++;
				continue;
			}

			wnames[n++] = s;
			t = strchr(s, '/');
			if (!t)
				break;

			*t = '\0';
			s = t + 1;
		}

		if (!(tc = np_create_twalk(nfid->fid, fid->fid, n, wnames))) {
			np_uerror(ENOMEM);
			goto error;
		}
		if (nfid->fsys->rpc(nfid->fsys, tc, &rc) < 0)
			goto error;

		nfid = fid;
		if (rc->u.rwalk.nwqid != n) {
			np_uerror(ENOENT);
			goto error;
		}
		free(tc);
		free(rc);
		if (!t || *s=='\0')
			break;
	}

	free(fname);
	return fid;

error:
	if (rc)
		free(rc);
	if (tc)
		free(tc);
	if (fid && nfid->fid == fid->fid) {
		int saved_err = np_rerror ();
		(void)npc_clunk (fid);
		np_uerror (saved_err);
	} else if (fid)
		npc_fid_free (fid);
	if (fname)
		free(fname);
	return NULL;
}
