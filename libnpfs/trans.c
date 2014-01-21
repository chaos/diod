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
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

Nptrans *
np_trans_create(void *aux,
		int (*recv)(Npfcall **, u32, void *), 
		int (*send)(Npfcall *, void *),
		void (*destroy)(void *))
{
	Nptrans *trans;

	trans = malloc(sizeof(*trans));
	if (!trans) {
		np_uerror(ENOMEM);
		return NULL;
	}

	trans->aux = aux;
	trans->recv = recv;
	trans->send = send;
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
np_trans_send (Nptrans *trans, Npfcall *fc)
{
	return trans->send(fc, trans->aux);
}

int
np_trans_recv (Nptrans *trans, Npfcall **fcp, u32 msize)
{
	Npfcall *fc;

	if (trans->recv (&fc, msize, trans->aux) < 0)
		return -1;
	if (fc && !np_deserialize(fc)) {
		free (fc);
		np_uerror (EPROTO);
		return -1;
	}
	*fcp = fc;
	return 0;
}

