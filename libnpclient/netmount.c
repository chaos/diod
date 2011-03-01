/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

struct addrinfo *
npc_netaddr(char *address, int dfltport)
{
	int fd, r;
	char *addr, *name, *p, *port;
	/* stupid library doesn't set it to NULL if error! */
	struct addrinfo *addrlist = NULL;

	addr = strdup(address);
	port = (char *) malloc(sizeof(char) * 6);
	if (strncmp(addr, "tcp!", 4) == 0)
		name = addr + 4;
	else
		name = addr;

	p = strrchr(name, '!');
	if (p) {
		*p = '\0';
		p++;
		sprintf(port, "%s", p);
	}
	else 
		sprintf(port, "%d", dfltport);
	
	/* they have this cute 'hints' thing you can put in. 
	 * it would be really great if it worked, but it fails in some 
	 * places, so just don't use it.
	 */
	r = getaddrinfo(name, port, NULL, &addrlist);
	
	if (r) {
		np_werror("cannot resolve name", EIO);
		close(fd);
	}
	return addrlist;
}


Npcfsys *
npc_netmount(struct addrinfo *addrlist, Npuser *user, int dfltport, 
	int (*auth)(Npcfid *afid, Npuser *user, void *aux), void *aux)
{
	char *s;
	int fd;
	char ename[32];

	fd = socket(addrlist->ai_family, addrlist->ai_socktype, 0);
	if (fd < 0) {
		np_uerror(errno);
		goto error;
	}

	if (connect(fd, addrlist->ai_addr, sizeof(*addrlist->ai_addr)) < 0) {
		/* real computers have errstr */
		strerror_r(errno, ename, sizeof(ename));
		s = inet_ntoa(*(struct in_addr*) addrlist->ai_addr);
		np_werror("%s:%s", errno, s, ename);
		close(fd);
		goto error;
	}

	return npc_mount(fd, NULL, user, auth, aux);

error:
	return NULL;
}

