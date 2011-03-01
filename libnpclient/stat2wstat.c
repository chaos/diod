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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

int
npc_wstatlen(Npstat *st)
{
	return sizeof(Npwstat) + st->name.len + st->uid.len + st->gid.len +
		st->muid.len + st->extension.len + 5;
}

static char *
npc_strcopy(Npstr *str, char **p)
{
	char *s;

	s = *p;
	memcpy(s, str->str, str->len);
	s[str->len] = '\0';
	*p += str->len + 1;

	return s;
}
 
void
npc_stat2wstat(Npstat *st, Npwstat *wst, char **sbuf)
{
	wst->size = st->size;
	wst->type = st->type;
	wst->dev = st->dev;
	wst->qid = st->qid;
	wst->mode = st->mode;
	wst->atime = st->atime;
	wst->mtime = st->mtime;
	wst->length = st->length;
	wst->name = npc_strcopy(&st->name, sbuf);
	wst->uid = npc_strcopy(&st->uid, sbuf);
	wst->gid = npc_strcopy(&st->gid, sbuf);
	wst->muid = npc_strcopy(&st->muid, sbuf);
	wst->extension = npc_strcopy(&st->extension, sbuf);
	wst->n_uid = st->n_uid;
	wst->n_gid = st->n_gid;
	wst->n_muid = st->n_muid;
}
