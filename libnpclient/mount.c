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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <stdint.h>
#include <inttypes.h>
#include <pwd.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

#define PASSWD_BUFSIZE  4096 /* FIXME: sysconf(_SC_GETPW_R_SIZE_MAX) ? */

void
npc_finish (Npcfsys *fs)
{
	npc_disconnect_fsys (fs);
	npc_decref_fsys (fs);
}

Npcfsys*
npc_start (int fd, int msize)
{
	Npcfsys *fs;
	Npfcall *tc = NULL, *rc = NULL;

	errno = 0;
	if (!(fs = npc_create_fsys (fd, msize)))
		goto done;
	if (!(tc = np_create_tversion (msize, "9P2000.L")))
		goto done;
	if (npc_rpc (fs, tc, &rc) < 0)
		goto done;
	if (rc->u.rversion.msize < msize)
		fs->msize = rc->u.rversion.msize;
	if (np_strcmp (&rc->u.rversion.version, "9P2000.L") != 0) {
		np_uerror(EIO);
		goto done;
	}
done:
	if (tc)
		free (tc);
	if (rc)
		free (rc);
	errno = np_rerror ();
	if (errno && fs) {
		npc_finish (fs);
		fs = NULL;
	}			
	return fs;
}

int
npc_attach (Npcfsys *fs, char *aname, uid_t uid)
{
	Npfcall *tc = NULL, *rc = NULL;
	Npcfid *fid = NULL;
	struct passwd pw, *pwd = NULL;
	char buf[PASSWD_BUFSIZE];
	int ret = -1;

	errno = 0;
	errno = getpwuid_r (uid, &pw, buf, sizeof(buf), &pwd);
	if (errno) {
		np_uerror (errno);
		goto done;
	}
	if (!pwd) {
		np_uerror (ESRCH);
		goto done;
	}

	if (!(fid = npc_fid_alloc (fs)))
		goto done;
	if (!(tc = np_create_tattach (fid->fid, P9_NOFID,
				      pwd->pw_name, aname, pwd->pw_uid)))
		goto done;
	if (npc_rpc (fs, tc, &rc) < 0)
		goto done;
	fs->root = fid;
	ret = 0;
done:
	if (tc)
		free (tc);
	if (rc)
		free (rc);
	errno = np_rerror ();
	if (errno && fid)
		npc_fid_free (fid);

	return ret;	
}

int
npc_clunk (Npcfid *fid)
{
        Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	errno = 0;
        if (!(tc = np_create_tclunk (fid->fid)))
                goto done;
        if (npc_rpc (fid->fsys, tc, &rc) < 0)
                goto done;
        npc_fid_free(fid);
	ret = 0;
done:
	if (tc)
        	free (tc);
	if (rc)
        	free (rc);
	errno = np_rerror ();

        return ret;
}

Npcfsys*
npc_mount (int fd, int msize, char *aname, uid_t uid)
{
	Npcfsys *fs;

	if (!(fs = npc_start (fd, msize)))
		return NULL;
	if (npc_attach (fs, aname, uid) < 0) {
		npc_finish (fs);
		fs = NULL;
	}

	return fs;
}

int
npc_umount (Npcfsys *fs)
{
	int ret = 0;

	if (fs->root) {
		if (npc_clunk (fs->root) < 0)
			ret = -1;
		fs->root = NULL;
	}
	npc_finish (fs);

	return ret;
}
