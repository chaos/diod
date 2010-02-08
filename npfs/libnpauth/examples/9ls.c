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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "npfs.h"
#include "npclient.h"
#ifdef _WIN32
  #include "winhelp.c"
#else
  #include <unistd.h>
  #define __cdecl
#endif
#include "npauth.h"


extern int npc_chatty;

static void
usage()
{
	fprintf(stderr, "9ls [-dU] [-a authsrv] [-p port] [-P passwd] [-u user] addr path\n");
	exit(1);
}

int __cdecl
main(int argc, char **argv)
{
	struct npcauth auth;
	int i, n;
	int c, port, dotu;
	char *addr, *authsrv, *s;
	char *path, *passwd;
	Npuser *user;
	Npcfsys *fs;
	Npcfid *fid;
	Npwstat *stat;

	port = 564;
	dotu = 2;
	passwd = NULL;
	authsrv = NULL;
//	npc_chatty = 1;

#ifdef _WIN32
	init();
	user = np_default_users->uname2user(np_default_users, "nobody");
#else
	user = np_default_users->uid2user(np_default_users, geteuid());
	if (!user) {
		fprintf(stderr, "cannot retrieve user %d\n", geteuid());
		exit(1);
	}
#endif
	while ((c = getopt(argc, argv, "a:dp:P:u:U")) != -1) {
		switch (c) {
		case 'a':
			authsrv = optarg;
			break;
			
		case 'd':
			npc_chatty = 1;
			break;

		case 'p':
			port = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'P':
			passwd = optarg;
			break;

		case 'u':
			user = np_default_users->uname2user(np_default_users, optarg);
			break;


		case 'U':
			dotu = 0;
			break;

		default:
			usage();
		}
	}

	

	if (argc - optind < 2)
		usage();

	addr = argv[optind];
	path = argv[optind+1];

	if(passwd) {
		if(!authsrv)
			authsrv = addr;
		memset(&auth, 0, sizeof auth);
		makeKey(passwd, auth.key);
		auth.srv = npc_netaddr(authsrv, 567);
		fs = npc_netmount(npc_netaddr(addr, port), dotu, user, port, authp9any, &auth);
	} else {
		fs = npc_netmount(npc_netaddr(addr, port), dotu, user, port, NULL, NULL);
	}

	if(!fs) {
		char *estr;
		int eno;

		np_rerror(&estr, &eno);
		fprintf(stderr, "error mounting: (%d) %s\n", eno, estr);
		exit(1);
	}

	fid = npc_open(fs, path, Oread);
	if (!fid) {
		fprintf(stderr, "error\n");
	}

	while (1) {
		n = npc_dirread(fid, &stat);
		if (n <= 0)
			break;

		for(i = 0; i < n; i++)
			printf("%s\n", stat[i].name);
		free(stat);
	}

	npc_close(fid);
	npc_umount(fs);

	exit(0);
}
