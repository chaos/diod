/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tsetxattr_wildoffset.c - pass a crazy Twrite offset to an xattr */

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
#if HAVE_SYS_XATTR_H
    #include <sys/xattr.h>
#else
    #include <attr/xattr.h>
#endif


#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"
#include "diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tsetxattr_wildoffset aname path attr value\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root, *fid;
    uid_t uid = geteuid ();
    char *aname, *path, *attr, *value;
    u64 offset = 2684469248;
    int fd = 0; /* stdin */
    int n;

    diod_log_init (argv[0]);

    if (argc != 5)
        usage ();
    aname = argv[1];
    path = argv[2];
    attr = argv[3];
    value = argv[4];

    if (!(fs = npc_start (fd, fd, 65536+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn (np_rerror (), "npc_clunk afid");
    if (!(fid = npc_walk (root, path)))
        errn (np_rerror (), "npc_walk %s", path);
    if (npc_xattrcreate (fid, attr, strlen (value), XATTR_CREATE) < 0)
        errn_exit (np_rerror (), "npc_xattrcreate");
    n = npc_pwrite (fid, value, strlen (value), offset);
    if (n >= 0)
        msg_exit ("FAIL: Twrite with crazy offset to xattr succeeded");
    else {
        msg ("OK: Twrite with crazy offset failed with %s",
             strerror (np_rerror ()));
    }
    if (npc_clunk (fid) < 0)
        errn_exit (np_rerror (), "npc_clunk %s", path);
    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
