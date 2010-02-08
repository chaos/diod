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
#include <errno.h>
#ifndef _WIN32
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <arpa/inet.h>
#else
  #include <ws2tcpip.h>
#endif
#include <assert.h>
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

struct addrinfo *
npc_netaddr(char *address, int dfltport)
{
	struct addrinfo hints;
	int r;
	char *addr, *name, *p, port[8];
	struct addrinfo *addrlist;

	addrlist = NULL;
	addr = strdup(address);
	if (strncmp(addr, "tcp!", 4) == 0)
		name = addr + 4;
	else
		name = addr;

	p = strrchr(name, '!');
	if (p) {
		*p = '\0';
		p++;
		snprintf(port, sizeof(port), "%s", p);
	}
	else 
		snprintf(port, sizeof(port), "%d", dfltport);
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	r = getaddrinfo(name, port, &hints, &addrlist);
	
	if (r)
		np_werror("cannot resolve name", EIO);

	free(addr);
	return addrlist;
}


Npcfsys *
npc_netmount(struct addrinfo *addrlist, int dotu, Npuser *user, int dfltport, 
	int (*auth)(Npcfid *afid, Npuser *user, void *aux), void *aux)
{
	char *s;
	int fd;
	char ename[32];

        if (!addrlist)
                goto error;

	fd = socket(addrlist->ai_family, addrlist->ai_socktype, 0);
	if (fd < 0) {
		np_uerror(errno);
		goto error;
	}

	if (connect(fd, addrlist->ai_addr, sizeof(*addrlist->ai_addr)) < 0) {
		/* real computers have errstr */
#ifdef _WIN32
		snprintf(ename, sizeof ename, "connect error"); // XXX
#else // !_WIN32
		strerror_r(errno, ename, sizeof(ename));
#endif
		s = inet_ntoa(*(struct in_addr*) addrlist->ai_addr);
		np_werror("%s:%s", errno, s, ename);
		close(fd);
		goto error;
	}

	return npc_mount(fd, NULL, dotu, user, auth, aux);

error:
	return NULL;
}

