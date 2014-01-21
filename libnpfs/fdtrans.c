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
#include <unistd.h>
#include <errno.h>
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

typedef struct Fdtrans Fdtrans;

struct Fdtrans {
	Nptrans*	trans;
	int 		fdin;
	int		fdout;
	Npfcall		*fc;
	int		fc_len;  /* used bytes in fc */
};

static int np_fdtrans_recv(Npfcall **fcp, u32 msize, void *a);
static int np_fdtrans_send(Npfcall *fc, void *a);
static void np_fdtrans_destroy(void *a);

Nptrans *
np_fdtrans_create(int fdin, int fdout)
{
	Nptrans *npt;
	Fdtrans *fdt;

	fdt = malloc(sizeof(*fdt));
	if (!fdt) {
		np_uerror(ENOMEM);
		return NULL;
	}

	fdt->fdin = fdin;
	fdt->fdout = fdout;
	fdt->fc = NULL;
	fdt->fc_len = 0;
	npt = np_trans_create(fdt, np_fdtrans_recv,
				   np_fdtrans_send,
				   np_fdtrans_destroy);
	if (!npt) {
		free(fdt);
		return NULL;
	}

	fdt->trans = npt;
	return npt;
}

static void
np_fdtrans_destroy(void *a)
{
	Fdtrans *fdt = (Fdtrans *)a;

	fdt = a;
	if (fdt->fdin >= 0)
		(void)close(fdt->fdin);
	if (fdt->fdout >= 0 && fdt->fdout != fdt->fdin)
		(void)close(fdt->fdout);
	if (fdt->fc)
		free(fdt->fc);

	free(fdt);
}

/* This function must perform request framing, and return with one request
 * or an EOF/error.  If we read some extra bytes after a full request,
 * store the extra in fdt->fc and start reading into that next time instead
 * of allocating a fresh buffer.
 * N.B. msize starts out at max for the server and can shrink if client
 * negotiates a smaller one with Tversion.  It cannot grow, therefore
 * the allocated size of cached 'fc' from a preveious call will always be
 * >= the msize of the current call.  See fcall.c::np_version().
 */
static int
np_fdtrans_recv(Npfcall **fcp, u32 msize, void *a)
{
	Fdtrans *fdt = (Fdtrans *)a;
	Npfcall *fc;
	u32 size;
	int n, len;

	if (fdt->fc) {
		fc = fdt->fc;
		fdt->fc = NULL;
		len = fdt->fc_len;
		size = np_peek_size(fc->pkt, len); /* 0 if len < 4 */
		if (size > msize) {
			np_uerror(EPROTO);
			goto error;
		}
	} else {
		if (!(fc = np_alloc_fcall(msize))) {
			np_uerror (ENOMEM);
			goto error;
		}
		len = 0;
		size = 0;
	}
	while (size == 0 || len < size) {
		n = read(fdt->fdin, fc->pkt + len, msize - len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			np_uerror(errno);
			goto error;
		}	
		if (n == 0)
			goto eof;
		len += n;
		if (size == 0) {
			size = np_peek_size(fc->pkt, len);
			if (size > msize) {
				np_uerror(EPROTO);
				goto error;
			}
		}
	}
	if (len > size) {
		if (!(fdt->fc = np_alloc_fcall (msize))) {
			np_uerror(ENOMEM);
			goto error;
		}
		fdt->fc_len = len - size;
		memcpy (fdt->fc->pkt, fc->pkt + size, fdt->fc_len);
	}
	fc->size = size;
	*fcp = fc;
	return 0;
eof:
	free(fc);
	*fcp = NULL;
	return 0;
error:
	if (fc)
		free (fc);
	return -1;
}

static int
np_fdtrans_send(Npfcall *fc, void *a)
{
	Fdtrans *fdt = (Fdtrans *)a;
	u8 *data = fc->pkt;
	u32 size = fc->size;
	int len = 0;
	int n;

	while (len < size) {
		n = write(fdt->fdout, data + len, size - len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			np_uerror(errno);
			goto error;
		}
		len += n;
	}
	return len;		
error:
	return -1;
}	
