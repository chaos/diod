/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tversion.c - send Tversion with msize and print the negotiated msize */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"
#include "src/libnpclient/npcimpl.h"

#include "src/libdiod/diod_log.h"

int main (int argc, char *argv[])
{
    const char *s;
    int server_fd;
    unsigned long msize;
    Npcfsys *fs;
    Npfcall *tc, *rc = NULL;

    diod_log_init (argv[0]);

    if (argc != 2) {
        fprintf (stderr, "Usage: tversion MSIZE\n");
        exit (1);
    }
    msize = strtoul (argv[1], NULL, 0);
    if (msize > UINT32_MAX)
        msg_exit ("msize must fit in 32 bits");
    if (!(s = getenv ("DIOD_SERVER_FD")))
        msg_exit ("DIOD_SERVER_FD is not set");
    server_fd = strtoul (s, NULL, 10);

    /* The receive buffer only needs to hold the Rversion.
     */
    if (!(fs = npc_create_fsys (server_fd, server_fd, 8192, 0)))
        errn_exit (np_rerror (), "npc_create_fsys");
    if (!(tc = np_create_tversion (msize, "9P2000.L")))
        msg_exit ("out of memory");
    if (fs->rpc (fs, tc, &rc) < 0)
        errn_exit (np_rerror (), "Tversion");
    printf ("%u\n", rc->u.rversion.msize);

    free (tc);
    free (rc);
    npc_finish (fs);
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
