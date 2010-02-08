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
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include "npfs.h"

static Npfile* null_first(Npfile *);
static Npfile* null_next(Npfile *, Npfile *);
static int null_read(Npfilefid *, u64, u32, u8*, Npreq *);
static int null_write(Npfilefid *, u64, u32, u8*, Npreq *);
static int null_wstat(Npfile *, Npstat *);
static void null_connclose(Npconn *);

static Npsrv *srv;
static Npfile *root;
static Npfile * null;
static Npdirops rootops = {
	.first = null_first,
	.next = null_next,
};

static Npfileops nullops = {
	.read = null_read,
	.write = null_write,
	.wstat = null_wstat,
};

static void
usage()
{
	fprintf(stderr, "nullfs: -d -w nthreads mount-point\n");
	exit(-1);
}

int
main(int argc, char **argv)
{
	int c, debuglevel, nwthreads;
	pid_t pid;
	Npuser *user;
	char *opts, *s;

	debuglevel = 0;
	nwthreads = 16;
	user = np_default_users->uid2user(np_default_users, getuid());
	while ((c = getopt(argc, argv, "dw:")) != -1) {
		switch (c) {
		case 'd':
			debuglevel = 1;
			break;

		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		default:
			fprintf(stderr, "invalid option\n");
		}
	}

	if (optind >= argc)
		usage();

	if (!user) {
		fprintf(stderr, "invalid user\n");
		return -1;
	}

	close(0);
	close(1);
	close(2);
	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid != 0)
		return 0;

	setsid();
	chdir("/");

	root = npfile_alloc(NULL, strdup(""), 0755|Dmdir, 0, &rootops, NULL);
	root->parent = root;
	npfile_incref(root);
	root->atime = time(NULL);
	root->mtime = root->atime;
	root->uid = user;
	root->gid = user->dfltgroup;
	root->muid = user;

	null = npfile_alloc(root, strdup("null"), 0644, 1, &nullops, NULL);
	npfile_incref(null);
	root->dirfirst = null;
	root->dirlast = null;

	srv = np_pipesrv_create(nwthreads);
	if (!srv)
		return -1;

	srv->debuglevel = debuglevel;
	srv->connclose = null_connclose;
	npfile_init_srv(srv, root);

	np_pipesrv_mount(srv, argv[optind], user->uname, 0, opts);


	while (1) {
		sleep(100);
	}

	return 0;
}

static void
null_connclose(Npconn *conn)
{
	exit(0);
}

static Npfile*
null_first(Npfile *dir)
{
	if (dir->dirfirst)
		npfile_incref(dir->dirfirst);

	return dir->dirfirst;
}

static Npfile* 
null_next(Npfile *dir, Npfile *prevchild)
{
	if (prevchild->next)
		npfile_incref(prevchild->next);

	return prevchild->next;
}

static int
null_read(Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
	memset(data, 0, count);
	return count;
}

static int
null_write(Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
	return count;
}

static int
null_wstat(Npfile* file, Npstat* stat)
{
	return 1;
}
