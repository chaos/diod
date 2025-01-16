/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2025 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include "src/libnpfs/npfs.h"
#include "npclient.h"
#include "npcimpl.h"

int
npc_lock (Npcfid *fid, u32 flags, Npclockinfo *info, u8 *status)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tlock (fid->fid,
				    info->type,
				    flags,
				    info->start,
				    info->length,
				    info->proc_id,
				    info->client_id))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	if (status)
		*status = rc->u.rlock.status;
	ret = 0;
done:
	free(tc);
	free(rc);
	return ret;
}

int npc_getlock (Npcfid *fid, Npclockinfo *info_in, Npclockinfo *info_out)
{
	Npfcall *tc = NULL, *rc = NULL;
	int ret = -1;

	if (!(tc = np_create_tgetlock (fid->fid,
				       info_in->type,
				       info_in->start,
				       info_in->length,
				       info_in->proc_id,
				       info_in->client_id))) {
		np_uerror (ENOMEM);
		goto done;
	}
	if (fid->fsys->rpc(fid->fsys, tc, &rc) < 0)
		goto done;
	if (info_out) {
		info_out->type = rc->u.rgetlock.type;
		info_out->start = rc->u.rgetlock.start;
		info_out->length = rc->u.rgetlock.length;
		info_out->proc_id= rc->u.rgetlock.proc_id;
		if (!(info_out->client_id = np_strdup (&rc->u.rgetlock.client_id)))
			goto done;
	}
	ret = 0;
done:
	free(tc);
	free(rc);
	return ret;
}

