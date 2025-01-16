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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/libnpfs/npfs.h"
#include "npclient.h"
#include "npcimpl.h"

void
npc_finish (Npcfsys *fs)
{
	if (fs->disconnect)
		fs->disconnect (fs);
	fs->decref (fs);
}

Npcfsys*
npc_start (int rfd, int wfd, int msize, int flags)
{
	Npcfsys *fs;
	Npfcall *tc = NULL, *rc = NULL;

	if ((flags & NPC_MULTI_RPC))
		fs = npc_create_mtfsys (rfd, wfd, msize, flags);
	else
		fs = npc_create_fsys (rfd, wfd, msize, flags);
	if (!fs)
		goto done;
	if (!(tc = np_create_tversion (msize, "9P2000.L"))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fs->rpc (fs, tc, &rc) < 0)
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
	if (np_rerror () && fs) {
		npc_finish (fs);
		fs = NULL;
	}
	return fs;
}

Npcfid*
npc_auth (Npcfsys *fs, char *aname, u32 uid, AuthFun auth)
{
        Npcfid *afid = NULL;
        Npfcall *tc = NULL, *rc = NULL;

        if (!(afid = npc_fid_alloc (fs)))
                goto done;
        if (!(tc = np_create_tauth (afid->fid, NULL, aname, uid))) {
		np_uerror (ENOMEM);
		npc_fid_free (afid);
		afid = NULL;
                goto done;
	}
        if (afid->fsys->rpc (afid->fsys, tc, &rc) < 0) {
		npc_fid_free (afid);
		afid = NULL;
		if (np_rerror() == 0 || np_rerror() == ENOENT)
			np_uerror(0); /* auth not required */
		goto done;
	}
	if (auth && auth (afid, uid) < 0) {
		int saved_err = np_rerror ();
		(void)npc_clunk (afid);
		afid = NULL;
		np_uerror (saved_err);
		goto done;
	}
done:
        if (tc)
                free(tc);
        if (rc)
                free(rc);
        return afid;
}

Npcfid *
npc_attach (Npcfsys *fs, Npcfid *afid, char *aname, uid_t uid)
{
	Npfcall *tc = NULL, *rc = NULL;
	Npcfid *fid = NULL;

	if (!(fid = npc_fid_alloc (fs)))
		goto done;
	if (!(tc = np_create_tattach (fid->fid, afid ? afid->fid : NOFID,
				      NULL, aname, uid))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fs->rpc (fs, tc, &rc) < 0)
		goto done;
done:
	if (tc)
		free (tc);
	if (rc)
		free (rc);
	if (np_rerror () && fid) {
		npc_fid_free (fid);
		fid = NULL;
	}
	return fid;	
}

int
npc_clunk (Npcfid *fid)
{
        Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

        if (!(tc = np_create_tclunk (fid->fid))) {
		np_uerror (ENOMEM);
                goto done;
	}
        if (fid->fsys->rpc (fid->fsys, tc, &rc) < 0)
                goto done;
        npc_fid_free(fid);
	ret = 0;
done:
	if (tc)
        	free (tc);
	if (rc)
        	free (rc);
        return ret;
}

Npcfid *
npc_mount (int rfd, int wfd, int msize, char *aname, AuthFun auth)
{
	Npcfsys *fs;
	Npcfid *afid, *fid;
	int flags = 0;

	//flags |= NPC_SHORTREAD_EOF;
	if (!(fs = npc_start (rfd, wfd, msize, flags)))
		return NULL;
	if (!(afid = npc_auth (fs, aname, geteuid (), auth)) && np_rerror ()) {
		npc_finish (fs);
		return NULL;
	}
	if (!(fid = npc_attach (fs, afid, aname, geteuid ()))) {
		int saved_err = np_rerror ();
		if (afid)
			(void)npc_clunk (afid);
		npc_finish (fs);
		np_uerror (saved_err);
		return NULL;
	}
	if (afid)
		(void)npc_clunk (afid);
	return fid;
};

void
npc_umount (Npcfid *fid)
{
	Npcfsys *fs = fid->fsys;

	(void)npc_clunk (fid);
	npc_finish (fs);
}
