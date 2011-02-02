/* Copyright ©2007-2010 Kris Maglione <maglione.k at Gmail>
 * Copyright ©2004-2006 Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "ixp_local.h"

/* Note: These functions modify the strings that they are passed.
 *   The lookup function duplicates the original string, so it is
 *   not modified.
 */

/* From FreeBSD's sys/su.h */
#ifndef SUN_LEN
#define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif

typedef struct addrinfo addrinfo;
typedef struct sockaddr sockaddr;
typedef struct sockaddr_un sockaddr_un;
typedef struct sockaddr_in sockaddr_in;

static char*
get_port(char *addr) {
	char *s;

	s = strchr(addr, '!');
	if(s == nil) {
		werrstr("no port provided");
		return nil;
	}

	*s++ = '\0';
	if(*s == '\0') {
		werrstr("invalid port number");
		return nil;
	}
	return s;
}

static int
sock_unix(char *address, sockaddr_un *sa, socklen_t *salen) {
	int fd;

	memset(sa, 0, sizeof *sa);

	sa->sun_family = AF_UNIX;
	strncpy(sa->sun_path, address, sizeof sa->sun_path);
	*salen = SUN_LEN(sa);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if(fd < 0)
		return -1;
	return fd;
}

static int
dial_unix(char *address) {
	sockaddr_un sa;
	socklen_t salen;
	int fd;

	fd = sock_unix(address, &sa, &salen);
	if(fd == -1)
		return fd;

	if(connect(fd, (sockaddr*) &sa, salen)) {
		close(fd);
		return -1;
	}
	return fd;
}

static int
announce_unix(char *file) {
	const int yes = 1;
	sockaddr_un sa;
	socklen_t salen;
	int fd;

	signal(SIGPIPE, SIG_IGN);

	fd = sock_unix(file, &sa, &salen);
	if(fd == -1)
		return fd;

	if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&yes, sizeof yes) < 0)
		goto fail;

	unlink(file);
	if(bind(fd, (sockaddr*)&sa, salen) < 0)
		goto fail;

	chmod(file, S_IRWXU);
	if(listen(fd, IXP_MAX_CACHE) < 0)
		goto fail;

	return fd;

fail:
	close(fd);
	return -1;
}

static addrinfo*
alookup(char *host, int announce) {
	addrinfo hints, *ret;
	char *port;
	int err;

	/* Truncates host at '!' */
	port = get_port(host);
	if(port == nil)
		return nil;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if(announce) {
		hints.ai_flags = AI_PASSIVE;
		if(!strcmp(host, "*"))
			host = nil;
	}

	err = getaddrinfo(host, port, &hints, &ret);
	if(err) {
		werrstr("getaddrinfo: %s", gai_strerror(err));
		return nil;
	}
	return ret;
}

static int
ai_socket(addrinfo *ai) {
	return socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
}

static int
dial_tcp(char *host) {
	addrinfo *ai, *aip;
	int fd;

	aip = alookup(host, 0);
	if(aip == nil)
		return -1;

	SET(fd);
	for(ai = aip; ai; ai = ai->ai_next) {
		fd = ai_socket(ai);
		if(fd == -1) {
			werrstr("socket: %s", strerror(errno));
			continue;
		}

		if(connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			break;

		werrstr("connect: %s", strerror(errno));
		close(fd);
		fd = -1;
	}

	freeaddrinfo(aip);
	return fd;
}

static int
announce_tcp(char *host) {
	addrinfo *ai, *aip;
	int fd;

	aip = alookup(host, 1);
	if(aip == nil)
		return -1;

	/* Probably don't need to loop */
	SET(fd);
	for(ai = aip; ai; ai = ai->ai_next) {
		fd = ai_socket(ai);
		if(fd == -1)
			continue;

		if(bind(fd, ai->ai_addr, ai->ai_addrlen) < 0)
			goto fail;

		if(listen(fd, IXP_MAX_CACHE) < 0)
			goto fail;
		break;
	fail:
		close(fd);
		fd = -1;
	}

	freeaddrinfo(aip);
	return fd;
}

typedef struct addrtab addrtab;
static
struct addrtab {
	char *type;
	int (*fn)(char*);
} dtab[] = {
	{"tcp", dial_tcp},
	{"unix", dial_unix},
	{0, 0}
}, atab[] = {
	{"tcp", announce_tcp},
	{"unix", announce_unix},
	{0, 0}
};

static int
lookup(const char *address, addrtab *tab) {
	char *addr, *type;
	int ret;

	ret = -1;
	type = estrdup(address);

	addr = strchr(type, '!');
	if(addr == nil)
		werrstr("no address type defined");
	else {
		*addr++ = '\0';
		for(; tab->type; tab++)
			if(strcmp(tab->type, type) == 0) break;
		if(tab->type == nil)
			werrstr("unsupported address type");
		else
			ret = tab->fn(addr);
	}

	free(type);
	return ret;
}

/**
 * Function: ixp_dial
 * Function: ixp_announce
 *
 * Params:
 *	address: An address on which to connect or listen,
 *		 specified in the Plan 9 resources
 *		 specification format
 *		 (<protocol>!address[!<port>])
 *
 * These functions hide some of the ugliness of Berkely
 * Sockets. ixp_dial connects to the resource at P<address>,
 * while ixp_announce begins listening on P<address>.
 *
 * Returns:
 *	These functions return file descriptors on success, and -1
 *	on failure. ixp_errbuf(3) may be inspected on failure.
 * See also:
 *	socket(2)
 */

int
ixp_dial(const char *address) {
	return lookup(address, dtab);
}

int
ixp_announce(const char *address) {
	return lookup(address, atab);
}

