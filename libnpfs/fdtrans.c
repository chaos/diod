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
	int		fc_msize;
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
	fdt->fc_msize = 0;
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

static int
np_fdtrans_recv(Npfcall **fcp, u32 msize, void *a)
{
	Fdtrans *fdt = (Fdtrans *)a;
	Npfcall *fc = NULL;
	int n, size, len;

	if (fdt->fc) {
		NP_ASSERT (fdt->fc_msize >= msize);
		fc = fdt->fc;
		len = fdt->fc->size;
		fdt->fc = NULL;
		size = np_peek_size (fc->pkt, len);
	} else {
		if (!(fc = np_alloc_fcall (msize))) {
			np_uerror (ENOMEM);
			goto error;
		}
		len = 0;
		size = 0;
	}
	while (len < size || len < 4) {
		n = read(fdt->fdin, fc->pkt + len, msize - len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			np_uerror (errno);
			goto error;
		}	
		if (n == 0) {	/* EOF */
			free (fc);
			fc = NULL;
			goto done;
		}
		len += n;
		if (size == 0)
			size = np_peek_size(fc->pkt, len);
		if (size > msize) {
			np_uerror(EPROTO);
			goto error;
		}
	}
	if (len > size) {
		if (!(fdt->fc = np_alloc_fcall (msize))) {
			np_uerror(ENOMEM);
			goto error;
		}
		fdt->fc_msize = msize;
		memcpy (fdt->fc->pkt, fc->pkt + size, len - size);
		fdt->fc->size = len - size;
	}
	fc->size = size;
done:
	*fcp = fc;
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
	int n, len = 0, size = fc->size;

	/* N.B. Caching fc->size avoids a race with mtfsys.c where fc
  	 * is replaced under us before the do conditional - see issue 72.
	 */
	do {
		n = write(fdt->fdout, fc->pkt + len, size - len);
		if (n < 0 && errno == EINTR)
			continue;
		if (n < 0) {
			np_uerror(errno);
			return -1;
		}
		len += n;
	} while (len < size);

	return len;		
}	
