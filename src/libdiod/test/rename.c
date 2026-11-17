/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test Tremove and Trename
 *
 * The Linux v9fs client uses Tunlinkat and Trenameat, so these legacy
 * operations are not exercised by the v9fs tests.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "src/libtest/server.h"
#include "src/libnpclient/npclient.h"
#include "src/libtap/tap.h"

#include "diod_conf.h"

#define TEST_MSIZE 8192

static char tmpdir[] = "/tmp/test-rename.XXXXXX";

/* Return true if 'path' (relative to the export) exists on the server.
 */
static int exists (const char *path)
{
    char fullpath[PATH_MAX + 1];
    struct stat sb;

    snprintf (fullpath, sizeof (fullpath), "%s/%s", tmpdir, path);
    return lstat (fullpath, &sb) == 0;
}

static Npcfid *create_file (Npcfid *root, char *path, char *content)
{
    Npcfid *fid;

    if (!(fid = npc_create_bypath (root, path, O_RDWR, 0644, getgid ())))
        BAIL_OUT ("npc_create_bypath %s: %s", path, strerror (np_rerror ()));
    if (npc_puts (fid, content) < 0)
        BAIL_OUT ("npc_puts %s: %s", path, strerror (np_rerror ()));
    return fid;
}

static void check_content (Npcfid *root, char *path, char *expected)
{
    char *s = npc_aget (root, path);

    ok (s != NULL && !strcmp (s, expected),
        "%s contains \"%s\"", path, expected);
    if (!s)
        diag ("npc_aget %s: %s", path, strerror (np_rerror ()));
    free (s);
}

static void test_remove (Npcfid *root)
{
    Npcfid *fid;
    int rc;

    /* remove a file through a fid that is open */
    fid = create_file (root, "rmfile", "x");
    rc = npc_remove (fid);
    ok (rc == 0 && !exists ("rmfile"),
        "Tremove of an open file works");

    /* remove a file through a fid that was only walked to */
    if (npc_clunk (create_file (root, "rmfile2", "x")) < 0)
        BAIL_OUT ("npc_clunk: %s", strerror (np_rerror ()));
    rc = npc_remove_bypath (root, "rmfile2");
    ok (rc == 0 && !exists ("rmfile2"),
        "Tremove of a walked file works");

    /* remove an empty directory */
    if (npc_mkdir_bypath (root, "rmdir", 0755) < 0)
        BAIL_OUT ("npc_mkdir_bypath: %s", strerror (np_rerror ()));
    rc = npc_remove_bypath (root, "rmdir");
    ok (rc == 0 && !exists ("rmdir"),
        "Tremove of an empty directory works");

    /* remove a non-empty directory fails */
    if (npc_mkdir_bypath (root, "rmdir2", 0755) < 0)
        BAIL_OUT ("npc_mkdir_bypath: %s", strerror (np_rerror ()));
    if (npc_clunk (create_file (root, "rmdir2/file", "x")) < 0)
        BAIL_OUT ("npc_clunk: %s", strerror (np_rerror ()));
    np_uerror (0);
    rc = npc_remove_bypath (root, "rmdir2");
    ok (rc < 0 && (np_rerror () == ENOTEMPTY || np_rerror () == EEXIST)
        && exists ("rmdir2/file"),
        "Tremove of a non-empty directory fails with ENOTEMPTY");
    if (npc_remove_bypath (root, "rmdir2/file") < 0
        || npc_remove_bypath (root, "rmdir2") < 0)
        BAIL_OUT ("cleanup rmdir2: %s", strerror (np_rerror ()));

    /* remove a file that has already been removed fails */
    if (npc_clunk (create_file (root, "rmfile3", "x")) < 0)
        BAIL_OUT ("npc_clunk: %s", strerror (np_rerror ()));
    if (!(fid = npc_walk (root, "rmfile3")))
        BAIL_OUT ("npc_walk rmfile3: %s", strerror (np_rerror ()));
    if (npc_remove_bypath (root, "rmfile3") < 0)
        BAIL_OUT ("npc_remove_bypath rmfile3: %s", strerror (np_rerror ()));
    np_uerror (0);
    rc = npc_remove (fid);
    ok (rc < 0 && np_rerror () == ENOENT,
        "Tremove of an already removed file fails with ENOENT");
}

static void test_rename (Npcfid *root)
{
    Npcfid *fid, *dfid, *rootdir;
    int rc;

    if (!(rootdir = npc_walk (root, "")))
        BAIL_OUT ("npc_walk: %s", strerror (np_rerror ()));

    /* rename within the same directory */
    fid = create_file (root, "a", "a-content");
    rc = npc_rename (fid, rootdir, "b");
    ok (rc == 0 && !exists ("a") && exists ("b"),
        "Trename within a directory works");
    if (rc < 0)
        diag ("npc_rename: %s", strerror (np_rerror ()));
    check_content (root, "b", "a-content");

    /* the fid follows the file to its new name */
    rc = npc_puts (fid, "+more");
    ok (rc > 0, "write to renamed fid works");
    if (npc_clunk (fid) < 0)
        BAIL_OUT ("npc_clunk: %s", strerror (np_rerror ()));
    check_content (root, "b", "a-content+more");

    /* rename into another directory */
    if (npc_mkdir_bypath (root, "dir", 0755) < 0)
        BAIL_OUT ("npc_mkdir_bypath: %s", strerror (np_rerror ()));
    if (!(dfid = npc_walk (root, "dir")))
        BAIL_OUT ("npc_walk dir: %s", strerror (np_rerror ()));
    if (!(fid = npc_walk (root, "b")))
        BAIL_OUT ("npc_walk b: %s", strerror (np_rerror ()));
    rc = npc_rename (fid, dfid, "c");
    ok (rc == 0 && !exists ("b") && exists ("dir/c"),
        "Trename into another directory works");
    if (rc < 0)
        diag ("npc_rename: %s", strerror (np_rerror ()));
    check_content (root, "dir/c", "a-content+more");

    /* rename over an existing file replaces it */
    if (npc_clunk (create_file (root, "dir/d", "d-content")) < 0)
        BAIL_OUT ("npc_clunk: %s", strerror (np_rerror ()));
    rc = npc_rename (fid, dfid, "d");
    ok (rc == 0 && !exists ("dir/c") && exists ("dir/d"),
        "Trename over an existing file works");
    if (rc < 0)
        diag ("npc_rename: %s", strerror (np_rerror ()));
    check_content (root, "dir/d", "a-content+more");

    /* the renamed fid can still be removed under its new name */
    rc = npc_remove (fid);
    ok (rc == 0 && !exists ("dir/d"),
        "Tremove of a renamed fid removes the new name");

    /* rename a directory */
    if (npc_clunk (create_file (root, "dir/e", "e-content")) < 0)
        BAIL_OUT ("npc_clunk: %s", strerror (np_rerror ()));
    rc = npc_rename (dfid, rootdir, "dir2");
    ok (rc == 0 && !exists ("dir") && exists ("dir2/e"),
        "Trename of a directory works");
    if (rc < 0)
        diag ("npc_rename: %s", strerror (np_rerror ()));

    /* rename a directory over a non-empty directory fails */
    if (npc_mkdir_bypath (root, "dir3", 0755) < 0)
        BAIL_OUT ("npc_mkdir_bypath: %s", strerror (np_rerror ()));
    if (!(fid = npc_walk (root, "dir3")))
        BAIL_OUT ("npc_walk dir3: %s", strerror (np_rerror ()));
    np_uerror (0);
    rc = npc_rename (fid, rootdir, "dir2");
    ok (rc < 0 && (np_rerror () == ENOTEMPTY || np_rerror () == EEXIST)
        && exists ("dir3") && exists ("dir2/e"),
        "Trename over a non-empty directory fails with ENOTEMPTY");

    /* rename a file that no longer exists fails */
    if (npc_remove_bypath (root, "dir3") < 0)
        BAIL_OUT ("npc_remove_bypath dir3: %s", strerror (np_rerror ()));
    np_uerror (0);
    rc = npc_rename (fid, rootdir, "dir4");
    ok (rc < 0 && np_rerror () == ENOENT && !exists ("dir4"),
        "Trename of a removed file fails with ENOENT");
    npc_clunk (fid);

    /* clean up */
    if (npc_remove_bypath (root, "dir2/e") < 0)
        BAIL_OUT ("npc_remove_bypath dir2/e: %s", strerror (np_rerror ()));
    rc = npc_remove (dfid);
    ok (rc == 0 && !exists ("dir2"),
        "Tremove of a renamed directory removes the new name");
    npc_clunk (rootdir);
}

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int client_fd;
    Npcfid *root;

    plan (NO_PLAN);

    if (!mkdtemp (tmpdir))
        BAIL_OUT ("mkdtemp: %s", strerror (errno));

    srv = test_server_create (tmpdir, 0, &client_fd);

    root = npc_mount (client_fd, client_fd, TEST_MSIZE, tmpdir, NULL);
    ok (root != NULL, "npc_mount on socketpair works");
    if (!root)
        BAIL_OUT ("npc_mount: %s", strerror (np_rerror ()));

    test_remove (root);
    test_rename (root);

    npc_umount (root);

    test_server_destroy (srv);

    if (rmdir (tmpdir) < 0)
        diag ("rmdir %s: %s", tmpdir, strerror (errno));

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
