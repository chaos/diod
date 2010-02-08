/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

Nptrans *
np_trans_create(void *aux, int (*read)(u8 *, u32, void *), 
	int (*write)(u8 *, u32, void *), void (*destroy)(void *))
{
	Nptrans *trans;

	trans = malloc(sizeof(*trans));

	if (!trans)
		return NULL;

	trans->aux = aux;
	trans->read = read;
	trans->write = write;
	trans->destroy = destroy;

	return trans;
}

void
np_trans_destroy(Nptrans *trans)
{
	if (trans->destroy)
		(*trans->destroy)(trans->aux);
	free(trans);
}

int
np_trans_write(Nptrans *trans, u8* data, u32 count)
{
	if (trans->write)
		return trans->write(data, count, trans->aux);
	else
		return -1;
}

int
np_trans_read(Nptrans *trans, u8* data, u32 count)
{
	if (trans->read)
		return trans->read(data, count, trans->aux);
	else
		return -1;
}

