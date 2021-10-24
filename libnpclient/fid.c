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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

Npcfid *
npc_fid_alloc(Npcfsys *fs)
{
	Npcfid *ret;

	ret = malloc(sizeof(*ret));
	if (!ret) {
		np_uerror(ENOMEM);
		return NULL;
	}

	ret->fsys = fs;
	ret->fid = npc_get_id(fs->fidpool);
	ret->offset = 0;
	ret->iounit = 0;
	ret->buf = NULL;

	fs->incref(fs);
	return ret;
}

void
npc_fid_free(Npcfid *fid)
{
	if (fid) {
		if (fid->buf)
			free (fid->buf);
		npc_put_id(fid->fsys->fidpool, fid->fid);
		fid->fsys->decref(fid->fsys);
		free(fid);
	}
}
