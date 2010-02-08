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
#include <time.h>
#ifdef _WIN32
  #include "winhelp.c"
#else
  #include <unistd.h>
  #define __cdecl
#endif
#include "npfs.h"
#include "npclient.h" // ugh.. irritating dep from npauth
#include "npauth.h"

typedef struct Req Req;
struct Req {
	Npreq*	req;
	Req*	next;
};

static Npfile* echo_first(Npfile *);
static Npfile* echo_next(Npfile *, Npfile *);
static int echo_read(Npfilefid *, u64, u32, u8*, Npreq *);
static int echo_write(Npfilefid *, u64, u32, u8*, Npreq *);
static int echo_wstat(Npfile *, Npstat *);
static void echo_connclose(Npconn *);

static Npsrv *srv;
static Npfile *root;
static Npfile *echo;
static pthread_mutex_t reqslock = PTHREAD_MUTEX_INITIALIZER;
static Req *reqs;

static Npdirops rootops = {
	0, 0, 0, 
	echo_first, echo_next,
	0, 0, 0, 0, 0
};

static Npfileops nullops = {
	0, 0,
	echo_read, echo_write, echo_wstat,
	0, 0, 0
};

static void
usage()
{
	fprintf(stderr, "echofs: [-d] [-w nthreads] [-p port] id domain password\n");
	exit(-1);
}

int __cdecl
main(int argc, char **argv)
{
	int c, debuglevel, nwthreads, port;
	Npuser *user;
	char *s;
#ifdef _WIN32
	WSADATA wsData;

	WSAStartup(MAKEWORD(2,2), &wsData);
#endif

	debuglevel = 0;
	nwthreads = 16;
	port = 564;
	while ((c = getopt(argc, argv, "dp:w:")) != -1) {
		switch (c) {
		case 'd':
			debuglevel = 1;
			break;

		case 'p':
			port = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		default:
			fprintf(stderr, "invalid option\n");
			usage();
			exit(1);
		}
	}

	argc -= optind;
	argv += optind;
	if (argc != 3)
		usage();

	srvid = argv[0];
	srvdom = argv[1];
	makeKey(argv[2], srvkey);
	user = np_default_users->uname2user(np_default_users, srvid);

	if (!user) {
		fprintf(stderr, "invalid user\n");
		return -1;
	}

	root = npfile_alloc(NULL, strdup(""), 0755|Dmdir, 0, &rootops, NULL);
	root->parent = root;
	npfile_incref(root);
	root->atime = time(NULL);
	root->mtime = root->atime;
	root->uid = user;
	root->gid = user->dfltgroup;
	root->muid = user;

	echo = npfile_alloc(root, strdup("echo"), 0644, 1, &nullops, NULL);
	npfile_incref(echo);
	root->dirfirst = echo;
	root->dirlast = echo;

	srv = np_socksrv_create_tcp(nwthreads, &port);
	if (!srv) {
		fprintf(stderr, "cant create server\n");
		return -1;
	}

	srv->debuglevel = debuglevel;
	srv->connclose = echo_connclose;
	srv->auth = &srvauthp9any;
	npfile_init_srv(srv, root);
	np_srv_start(srv);

	while (1) {
		sleep(100);
	}

	return 0;
}

static void
echo_connclose(Npconn *conn)
{
	exit(0);
}

static Npfile*
echo_first(Npfile *dir)
{
	if (dir->dirfirst)
		npfile_incref(dir->dirfirst);

	return dir->dirfirst;
}

static Npfile* 
echo_next(Npfile *dir, Npfile *prevchild)
{
	if (prevchild->next)
		npfile_incref(prevchild->next);

	return prevchild->next;
}

static int
echo_read(Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
	Req *r;

	pthread_mutex_lock(&reqslock);
	r = malloc(sizeof(*r));
	r->req = req;
	r->next = reqs;
	reqs = r;
	pthread_mutex_unlock(&reqslock);

	return -1;
}

static int
echo_write(Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
	Npfcall *rc;
	Req *r, *r1;

	pthread_mutex_lock(&reqslock);
	r = reqs;
	reqs = NULL;
	pthread_mutex_unlock(&reqslock);

	while (r) {
		rc = np_create_rread(count, data);
		np_respond(r->req, rc);
		r1 = r->next;
		free(r);
		r = r1;
	}

	return count;
}

static int
echo_wstat(Npfile* file, Npstat* stat)
{
	return 1;
}
