/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* vsockcheck.c - exit 0 if vsock loopback works, independent of diod
 *
 * Listen on an ephemeral vsock port, connect to it over the loopback
 * CID, and pass a byte each way.  An alarm bounds the whole check so
 * a misbehaving transport cannot hang the test suite.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#ifdef HAVE_LINUX_VM_SOCKETS_H
#include <linux/vm_sockets.h>
#endif

#ifndef VMADDR_CID_LOCAL
#define VMADDR_CID_LOCAL 1
#endif

static void
die (const char *what)
{
    fprintf (stderr, "vsockcheck: %s: %s\n", what, strerror (errno));
    exit (1);
}

int
main (int argc, char *argv[])
{
#ifdef HAVE_LINUX_VM_SOCKETS_H
    struct sockaddr_vm addr;
    socklen_t len = sizeof (addr);
    int lfd, cfd, afd;
    char c = 'x';

    alarm (5);

    if ((lfd = socket (AF_VSOCK, SOCK_STREAM, 0)) < 0)
        die ("socket");
    memset (&addr, 0, sizeof (addr));
    addr.svm_family = AF_VSOCK;
    addr.svm_cid = VMADDR_CID_ANY;
    addr.svm_port = VMADDR_PORT_ANY;
    if (bind (lfd, (struct sockaddr *)&addr, sizeof (addr)) < 0)
        die ("bind");
    if (listen (lfd, 1) < 0)
        die ("listen");
    if (getsockname (lfd, (struct sockaddr *)&addr, &len) < 0)
        die ("getsockname");

    if ((cfd = socket (AF_VSOCK, SOCK_STREAM, 0)) < 0)
        die ("socket");
    addr.svm_cid = VMADDR_CID_LOCAL;
    if (connect (cfd, (struct sockaddr *)&addr, sizeof (addr)) < 0)
        die ("connect");
    if ((afd = accept (lfd, NULL, NULL)) < 0)
        die ("accept");

    if (write (cfd, &c, 1) != 1 || read (afd, &c, 1) != 1)
        die ("client to server");
    if (write (afd, &c, 1) != 1 || read (cfd, &c, 1) != 1)
        die ("server to client");

    close (afd);
    close (cfd);
    close (lfd);
    return 0;
#else
    fprintf (stderr, "vsockcheck: vsock not supported by this build\n");
    return 1;
#endif
}
