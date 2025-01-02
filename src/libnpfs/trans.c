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

