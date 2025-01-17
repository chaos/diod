/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test lock/getlock */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "src/libtest/server.h"
#include "src/libnpclient/npclient.h"
#include "src/libtap/tap.h"

#include "src/liblsd/list.h"
#include "diod_conf.h"

#define TEST_MSIZE 8192
#define TEST_FILE_SIZE 16384

static const char *statstr[] = { "Lsuccess", "Lblocked", "Lerror", "Lgrace" };
static const char *typestr[] = { "Lrdlck", "Lwrlck", "Lunlck" };

const char *rerrstr (void)
{
    return strerror (np_rerror ());
}

static void make_test_file (Npcfid *root, char *name, size_t size)
{
    Npcfid *fid;
    char *buf;
    int n;

    if (!(buf = malloc (size)))
        BAIL_OUT ("out of memory");
    memset (buf, 'a', size);
    buf[size - 1] = '\0';

    if (!(fid = npc_create_bypath (root, name, 0, 0644, getgid ())))
        BAIL_OUT ("npc_create_bypath %s: %s", name, rerrstr ());
    if (npc_clunk (fid) < 0)
        BAIL_OUT ("npc_clunk failed: %s", rerrstr ());
    if ((n = npc_put (root, name, buf, size)) != size)
        BAIL_OUT ("npc_put failed: %s", n < 0 ? rerrstr () : "short write");
    free (buf);
}

static void check_lock_range (Npcfid *fid, u8 type, int hi, int lo, u8 xstatus)
{
    Npclockinfo info = {
        .type = type,
        .start = lo,
        .length = hi - lo + 1,
        .proc_id = fid->fid,
        .client_id = "locktest",
    };
    u8 status;
    int rc;

    if ((rc = npc_lock (fid, 0, &info, &status)) < 0)
        diag ("npc_lock failed: %s", rerrstr ());
    if (rc == 0 && status != xstatus)
        diag ("npc_lock status is %s", statstr[status % 4]);
    ok (rc == 0 && status == xstatus,
        "fid %d %s [%d:%d] => %s",
        fid->fid, typestr[type % 3], hi, lo, statstr[xstatus % 4]);
}

static void check_lock (Npcfid *fid, u8 type, u8 xstatus)
{
    return check_lock_range (fid, type, 0, -1, xstatus);
}

static void check_getlock (Npcfid *fid, u8 type, u8 xtype)
{
    Npclockinfo in = {
        .type = type,
        .start = 0,
        .length = 0,
        .proc_id = fid->fid,
        .client_id = "locktest",
    };
    Npclockinfo out;
    int rc;

    memset (&out, 0, sizeof (out));
    if ((rc = npc_getlock (fid, &in, &out)) < 0)
        diag ("npc_lock failed: %s", rerrstr ());
    if (rc == 0 && out.type != xtype)
        diag ("npc_lock status is %s", typestr[out.type % 3]);
    ok (rc == 0 && out.type == xtype,
        "fid %d try %s => %s",
        fid->fid, typestr[type % 3], typestr[xtype % 3]);
}

static void test_unlock (Npcfid *root, char *path)
{
    Npcfid *fid1, *fid2;

    diag ("Lwrlcks contend and Lunlck works");

    ok ((fid1 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");
    ok ((fid2 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");

    check_lock (fid1, Lwrlck, Lsuccess);
    check_lock (fid2, Lwrlck, Lblocked);
    check_lock (fid1, Lunlck, Lsuccess);
    check_lock (fid2, Lwrlck, Lsuccess);

    ok (npc_clunk (fid2) == 0, "clunked fid");
    ok (npc_clunk (fid1) == 0, "clunked fid");
}

static void test_clunk (Npcfid *root, char *path)
{
    Npcfid *fid1, *fid2;

    diag ("Clunk == Lunlck");

    ok ((fid1 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");
    ok ((fid2 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");

    check_lock (fid1, Lwrlck, Lsuccess);
    check_lock (fid2, Lwrlck, Lblocked);
    int fidno = fid1->fid;
    ok (npc_clunk (fid1) == 0, "clunked fid %d", fidno);
    check_lock (fid2, Lwrlck, Lsuccess);

    ok (npc_clunk (fid2) == 0, "clunked fid");
}

static void test_readwrite (Npcfid *root, char *path)
{
    Npcfid *fid1, *fid2, *fid3;

    diag ("reader/writer semantics work");

    ok ((fid1 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");
    ok ((fid2 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");
    ok ((fid3 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");

    check_lock (fid1, Lrdlck, Lsuccess);
    check_lock (fid2, Lrdlck, Lsuccess);
    check_lock (fid3, Lwrlck, Lblocked);
    check_lock (fid2, Lunlck, Lsuccess);
    check_lock (fid1, Lunlck, Lsuccess);
    check_lock (fid3, Lwrlck, Lsuccess);
    check_lock (fid1, Lrdlck, Lblocked);
    check_lock (fid1, Lwrlck, Lblocked);

    ok (npc_clunk (fid3) == 0, "clunked fid");
    ok (npc_clunk (fid2) == 0, "clunked fid");
    ok (npc_clunk (fid1) == 0, "clunked fid");
}

static void test_ranges (Npcfid *root, char *path)
{
    Npcfid *fid1, *fid2;

    diag ("posix record locking is not implemented");

    ok ((fid1 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");
    ok ((fid2 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");

    check_lock_range (fid1, Lwrlck, 0, 31, Lsuccess);
    check_lock_range (fid2, Lwrlck, 32, 63, Lblock);

    ok (npc_clunk (fid2) == 0, "clunked fid");
    ok (npc_clunk (fid1) == 0, "clunked fid");
}

static void test_getlock (Npcfid *root, char *path)
{
    Npcfid *fid1, *fid2;

    diag ("check getlock");

    ok ((fid1 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");
    ok ((fid2 = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");

    check_getlock (fid1, Lrdlck, Lunlck);
    check_getlock (fid1, Lwrlck, Lunlck);
    check_lock (fid2, Lrdlck, Lsuccess);
    check_getlock (fid1, Lrdlck, Lunlck);
    check_getlock (fid1, Lwrlck, Lwrlck);

    ok (npc_clunk (fid2) == 0, "clunked fid");
    ok (npc_clunk (fid1) == 0, "clunked fid");
}

static void test_badparam (Npcfid *root, char *path)
{
    Npclockinfo info = {
        .proc_id = 42,
        .client_id = "locktest",
    };
    Npcfid *fid;
    u8 status;

    diag ("Confirm that the server handles some error cases");

    ok ((fid = npc_open_bypath (root, path, Ordwr)) != NULL, "opened fid");

    info.type = Lwrlck;
    info.start = info.length = 0;
    ok (npc_lock (fid, 42, &info, &status) < 0 && np_rerror () == EINVAL,
        "npc_lock with illegal flags value fails with EINVAL");

    info.type = 42;
    info.start = info.length = 0;
    ok (npc_lock (fid, 0, &info, &status) < 0 && np_rerror () == EINVAL,
        "npc_lock with illegal type value fails with EINVAL");

    ok (npc_clunk (fid) == 0, "clunked fid");

    ok ((fid = npc_walk (root, path)) != NULL, "walked to fid");
    info.type = Lwrlck;
    info.start = info.length = 0;
    ok (npc_lock (fid, 0, &info, &status) < 0 && np_rerror () == EBADF,
        "npc_lock with on fid that has not been opened type value fails with EBADF");

    ok (npc_clunk (fid) == 0, "clunked fid");
}

int main (int argc, char *argv[])
{
    Npsrv *srv;
    int client_fd;
    int flags = 0;//SRV_FLAGS_DEBUG_9PTRACE;
    char tmpdir[] = "/tmp/test-lock.XXXXXX";
    Npcfid *root;

    plan (NO_PLAN);

    if (!mkdtemp (tmpdir))
        BAIL_OUT ("mkdtemp: %s", strerror (errno));
    srv = test_server_create (tmpdir, flags, &client_fd);

    root = npc_mount (client_fd, client_fd, TEST_MSIZE, tmpdir, NULL);
    if (!root)
        BAIL_OUT ("npc_mount: %s", strerror (np_rerror ()));

    make_test_file (root, "foo", TEST_FILE_SIZE);

    test_unlock (root, "foo");
    test_clunk (root, "foo");
    test_readwrite (root, "foo");
    test_ranges (root, "foo");
    test_getlock (root, "foo");
    test_badparam (root, "foo");

    if (npc_remove_bypath (root, "foo") < 0)
        BAIL_OUT ("npc_remove_bypath: %s", strerror (np_rerror ()));

    npc_umount (root);

    test_server_destroy (srv);

    rmdir (tmpdir);

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
