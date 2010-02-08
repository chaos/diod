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
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/mount.h>
#include "npfs.h"
#include "npfsimpl.h"

typedef struct Pipesrv Pipesrv;

struct Pipesrv {
	int	fdin;
	int	fdout;
	int	pipin[2];
	int	pipout[2];
};

static void np_pipesrv_start(Npsrv *srv);
static void np_pipesrv_shutdown(Npsrv *srv);
static void np_pipesrv_destroy(Npsrv *srv);

Npsrv*
np_pipesrv_create(int nwthreads)
{
	Npsrv *srv;
	Pipesrv *ps;

	ps = malloc(sizeof(*ps));
	if (pipe(ps->pipin) < 0) {
		fprintf(stderr, "cannot create a pipe: %d\n", errno);
		free(ps);
		return NULL;
	}

	if (pipe(ps->pipout) < 0) {
		fprintf(stderr, "cannot create a pipe: %d\n", errno);
		close(ps->pipin[0]);
		close(ps->pipin[1]);
		free(ps);
		return NULL;
	}

	ps->fdin = ps->pipin[0];
	ps->fdout = ps->pipout[1];

	srv = np_srv_create(nwthreads);
	srv->srvaux = ps;
	srv->start = np_pipesrv_start;
	srv->shutdown = np_pipesrv_shutdown;
	srv->destroy = np_pipesrv_destroy;

	return srv;
}

int
np_pipesrv_mount(Npsrv *srv, char *mntpt, char *user, int mntflags, char *opts)
{
	int n, ret;
	Pipesrv *ps;
	char options[256];

	np_pipesrv_start(srv);

	ps = srv->srvaux;
	snprintf(options, sizeof(options), 
		"msize=%d,name=%s,%s,proto=fd,rfdno=%d,wfdno=%d,%s",
		srv->msize, user, srv->dotu?"":"noextend",
		ps->pipout[0], ps->pipin[1], opts);
 
	n = mount("none", mntpt, "9p", mntflags, options);
	if (n < 0) {
		ret = errno;
		fprintf(stderr, "cannot mount: %d %s\n", ret, strerror(ret));
	} else
		ret = 0;

	close(ps->pipout[0]);
	close(ps->pipin[1]);

	return ret;
}

static void
np_pipesrv_start(Npsrv *srv)
{
	Pipesrv *ps;
	Nptrans *trans;
	Npconn *conn;

	ps = srv->srvaux;
	trans = np_fdtrans_create(ps->fdin, ps->fdout);
	conn = np_conn_create(srv, trans);
	np_srv_add_conn(srv, conn);
}

static void
np_pipesrv_shutdown(Npsrv *srv)
{
}

static void
np_pipesrv_destroy(Npsrv *srv)
{
	Pipesrv *ps;
	ps = srv->srvaux;
	free(ps);
	srv->srvaux = NULL;
}

