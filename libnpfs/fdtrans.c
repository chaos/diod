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
#include <unistd.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

typedef struct Fdtrans Fdtrans;

struct Fdtrans {
	Nptrans*	trans;
	int 		fdin;
	int		fdout;
};

static int np_fdtrans_read(u8 *data, u32 count, void *a);
static int np_fdtrans_write(u8 *data, u32 count, void *a);
static void np_fdtrans_destroy(void *);

Nptrans *
np_fdtrans_create(int fdin, int fdout)
{
	Nptrans *npt;
	Fdtrans *fdt;

	fdt = malloc(sizeof(*fdt));
	if (!fdt)
		return NULL;

	//fprintf(stderr, "np_fdtrans_create trans %p fdtrans %p\n", npt, fdt);
	fdt->fdin = fdin;
	fdt->fdout = fdout;
	npt = np_trans_create(fdt, np_fdtrans_read, np_fdtrans_write, np_fdtrans_destroy);
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
	Fdtrans *fdt;

	fdt = a;
	if (fdt->fdin >= 0)
		close(fdt->fdin);

	if (fdt->fdout >= 0)
		close(fdt->fdout);

	free(fdt);
}

static int
np_fdtrans_read(u8 *data, u32 count, void *a)
{
	Fdtrans *fdt;

	fdt = a;
	return read(fdt->fdin, data, count);
}

static int
np_fdtrans_write(u8 *data, u32 count, void *a)
{
	Fdtrans *fdt;
	int ret;

	fdt = a;
	ret = write(fdt->fdout, data, count);
//	fprintf(stderr, "np_fdtrans_write fd %d datalen %d count %d\n", fdt->fdout, count, ret);
	return ret;
}	
