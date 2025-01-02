/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tattach.c - attach and clunk a file server on fd=0 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tattach [uid] aname\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    char *aname;
    int fd = 0; /* stdin */
    uid_t uid = geteuid ();

    diod_log_init (argv[0]);

    if (argc != 2 && argc != 3)
        usage ();
    if (argc == 3) {
        uid = strtoul (argv[1], NULL, 10);
        aname = argv[2];
    } else {
        aname = argv[1];
    }

    if (!(fs = npc_start (fd, fd, 8192+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn_exit (np_rerror (), "npc_clunk afid");
    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
