/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBDIOD_DIOD_SOCK_H
#define LIBDIOD_DIOD_SOCK_H

#include <poll.h>
#include <sys/socket.h>
#include <netdb.h>
#include "src/libnpfs/npfs.h"
#include "src/liblsd/list.h"

struct pollfd;

void diod_sock_accept_one (Npsrv *srv, int fd, int lookup);

/* Return true if a forward lookup of 'name' includes the IP address of
 * 'addr'.  A name from a reverse lookup of a client address is chosen by
 * whoever runs DNS for that address block, so it is only trusted if it
 * maps back to the same address.
 */
int diod_sock_name_matches_addr (const char *name,
                                 const struct sockaddr *addr);

/* Return true if the IP address of 'addr' is in 'list'.  IPv4-mapped IPv6
 * addresses compare equal to the IPv4 address; ports and IPv6 scope IDs
 * are ignored.  This is the resolver-independent part of the above.
 */
int diod_sock_addr_in_list (const struct sockaddr *addr,
                            const struct addrinfo *list);

void diod_sock_startfd (Npsrv *srv, int fdin, int fdout, char *client_id,
                        int flags);

int  diod_sock_listen (List l, struct pollfd **fdsp, int *nfdsp);

#define DIOD_SOCK_QUIET     0x01
#define DIOD_SOCK_PRIVPORT  0x02

int diod_sock_connect (char *name, int flags);
int diod_sock_connect_inet (char *host, char *port, int flags);
int diod_sock_connect_unix (char *path, int flags);
int diod_sock_connect_vsock (char *addr, int flags);

/* Return true if 'name' is a vsock address, "vsock:[CID:]PORT".
 */
#define DIOD_SOCK_VSOCK_PREFIX "vsock:"
int diod_sock_is_vsock (const char *name);

/* Parse "vsock:[CID:]PORT" into cid and port.  If CID is omitted, use
 * 'default_cid'.  Return 0 on success, -1 with errno set on failure.
 */
int diod_sock_parse_vsock (const char *name, unsigned int default_cid,
                           unsigned int *cid, unsigned int *port);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
