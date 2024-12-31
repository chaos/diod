/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tremovexattr.c - remove extended attributes */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tremovexattr aname path attr\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    uid_t uid = geteuid ();
    char *aname, *path, *attr;
    int fd = 0; /* stdin */

    diod_log_init (argv[0]);

    if (argc < 3)
        usage ();
    aname = argv[1];
    path = argv[2];
    attr = argv[3];

    if (!(fs = npc_start (fd, fd, 65536+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn (np_rerror (), "npc_clunk afid");

    if (npc_setxattr (root, path, attr, NULL, 0, 0) < 0)
        errn_exit (np_rerror (), "npc_setxattr");

    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
