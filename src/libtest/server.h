/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBTEST_SERVER_H
#define LIBTEST_SERVER_H

#include "src/libnpfs/npfs.h"

/* Embed a diod server in a test program that uses libtap,
 * initializing diod logging and configuration.
 *
 * 'testdir' is a directory to exported by the server (may be NULL)
 * 'client_fd' is set to the client side file descriptor (for libnpfs)
 * 'flags' is a mask of SRV_FLAGS_* from libnpfs/npfs.h.
 */
Npsrv *test_server_create (const char *testdir, int flags, int *client_fd);

/* Wait for clients to finalize, then destroy the server and finalize
 * diod logging and configuration.
 */
void test_server_destroy (Npsrv *srv);

#endif

// vi:ts=4 sw=4 expandtab
