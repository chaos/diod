/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* testopenfid.c - test operations on open fids */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: topenfid aname test\n");
    exit (1);
}

static int
create_file (Npcfid *root, char *name, u32 mode, char *content, size_t len) {
    Npcfid *fid;

    fid = npc_create_bypath (root, name, O_TRUNC|O_RDWR, mode, getgid ());
    if (fid == NULL) {
        return -1;
    }
    if (npc_write (fid, content, len) != len) {
        return -1;
    }
    if (npc_clunk (fid) == -1) {
        return -1;
    }

    return 0;
}

// Twalk should fail on opened fids
static void
open_walk (Npcfid *root) {
    Npcfid *fid;

    if (npc_mkdir_bypath (root, "subdir", 0755) == -1) {
        errn_exit (np_rerror (), "npc_mkdir_bypath subdir");
    }

    fid = npc_open_bypath (root, "subdir", O_RDONLY);
    if (fid == NULL) {
        errn_exit (np_rerror (), "npc_open subdir");
    }

    if (npc_walk (fid, "..") != NULL) {
        msg_exit ("walk succeeded on opened fid");
    }

    if (npc_clunk (fid) == -1) {
        errn_exit (np_rerror (), "npc_clunk fid");
    }
}

// Tread should work on opened fids even if the file has been removed
void
open_remove_read(Npcfid *root) {
    Npcfid *fid;
    char *testfile = "open_remove_read.test";
    char buf[3];

    if (create_file(root, testfile, 0644, "foo", 3) == -1) {
        errn_exit (np_rerror (), "create_file %s", testfile);
    }

    fid = npc_open_bypath(root, testfile, O_RDONLY);
    if (fid == NULL) {
        errn_exit (np_rerror (), "npc_open %s", testfile);
    }

    if (npc_remove_bypath(root, testfile) == -1) {
        errn_exit (np_rerror (), "npc_remove_bypath %s", testfile);
    }

    if (npc_read(fid, buf, sizeof(buf)) != sizeof(buf)) {
        errn_exit (np_rerror (), "npc_read fid");
    }

    if (npc_clunk(fid) == -1) {
        errn_exit (np_rerror (), "npc_clunk fid");
    }
}

// Tgetattr should work on opened fids even if the file has been removed
void
open_remove_getattr (Npcfid *root) {
    Npcfid *fid;
    char *testfile = "open_remove_getattr.test";
    struct stat sb, osb;

    if (create_file (root, testfile, 0644, "foo", 3) == -1) {
        errn_exit (np_rerror (), "create_file %s", testfile);
    }

    if (npc_stat (root, testfile, &sb) == -1) {
        errn_exit (np_rerror (), "npc_stat %s", testfile);
    }

    fid = npc_open_bypath (root, testfile, O_RDONLY);
    if (fid == NULL) {
        errn_exit (np_rerror (), "npc_open %s", testfile);
    }

    if (npc_remove_bypath (root, testfile) == -1) {
        errn_exit (np_rerror (), "npc_remove %s", testfile);
    }

    if (npc_fstat (fid, &osb) == -1) {
        errn_exit (np_rerror (), "npc_dstat fid");
    }

    if (npc_clunk (fid) == -1) {
        errn_exit (np_rerror (), "npc_clunk fid");
    }
}

// Tsetattr should work on opened fids even if the file has been removed
void
open_remove_setattr (Npcfid *root) {
    Npcfid *fid;
    char *testfile = "open_remove_setattr.test";
    struct stat sb;

    if (create_file (root, testfile, 0644, "foo", 3) == -1) {
        errn_exit (np_rerror (), "create_file %s", testfile);
    }

    if (npc_stat (root, testfile, &sb) == -1) {
        errn_exit (np_rerror (), "npc_stat %s", testfile);
    }

    fid = npc_open_bypath (root, testfile, O_RDONLY);
    if (fid == NULL) {
        errn_exit (np_rerror (), "npc_open %s", testfile);
    }

    if (npc_remove_bypath (root, testfile) == -1) {
        errn_exit (np_rerror (), "npc_remove %s", testfile);
    }

    if (npc_fchmod (fid, 0444) == -1) {
        errn_exit (np_rerror (), "npc_fchmod fid");
    }

    if (npc_clunk (fid) == -1) {
        errn_exit (np_rerror (), "npc_clunk fid");
    }
}

// Tsetattr on a fid opened from path should not affect path when path is no longer the same file
void
open_remove_create_setattr (Npcfid *root) {
    Npcfid *fid;
    char *testfile = "open_remove_create_setattr.test";
    struct stat sb;

    if (create_file (root, testfile, 0644, "foo", 3) == -1) {
        errn_exit (np_rerror (), "create_file %s", testfile);
    }

    fid = npc_open_bypath (root, testfile, O_RDONLY);
    if (fid == NULL) {
        errn_exit (np_rerror (), "npc_open %s", testfile);
    }

    if (npc_remove_bypath (root, testfile) == -1) {
        errn_exit (np_rerror (), "npc_remove %s", testfile);
    }
    if (create_file (root, testfile, 0644, "foo", 3) == -1) {
        errn_exit (np_rerror (), "create_file %s", testfile);
    }

    if (npc_fchmod (fid, 0444) == -1) {
        errn_exit (np_rerror (), "npc_fchmod fid");
    }
    if (npc_fstat (fid, &sb) == -1) {
        errn_exit (np_rerror (), "npc_fstat fid");
    }
    if ((sb.st_mode & 0x777) != 0444) {
        msg_exit ("fchmod didn't change sb.st_mode");
    }

    if (npc_stat (root, testfile, &sb) == -1) {
        errn_exit (np_rerror (), "npc_stat %s", testfile);
    }
    if ((sb.st_mode & 0777) != 0644) {
        msg_exit ("stat changed %o", sb.st_mode);
    }

    if (npc_clunk (fid) == -1) {
        errn_exit (np_rerror (), "npc_clunk fid");
    }
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    uid_t uid = geteuid ();
    char *aname, *test;
    int fd = 0; /* stdin */

    diod_log_init (argv[0]);

    if (argc < 3)
        usage ();
    aname = argv[1];
    test = argv[2];

    if (!(fs = npc_start (fd, fd, 65536+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn (np_rerror (), "npc_clunk afid");

    msg("testing '%s'", test);
    if (strcmp(test, "open_walk") == 0) {
        open_walk (root);
    } else if (strcmp (test, "open_remove_read") == 0) {
        open_remove_read (root);
    } else if (strcmp (test, "open_remove_getattr") == 0) {
        open_remove_getattr (root);
    } else if (strcmp (test, "open_remove_setattr") == 0) {
        open_remove_setattr (root);
    } else if (strcmp (test, "open_remove_create_setattr") == 0) {
        open_remove_create_setattr (root);
    } else {
        fprintf (stderr, "unknown test %s\n", test);
        exit (1);
    }
    npc_finish (fs);

    exit(0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
