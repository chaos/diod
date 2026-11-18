/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test Tread and Twrite on files that do not support pread/pwrite
 *
 * A FIFO stands in for files such as tracefs trace_pipe or relayfs
 * buffers, which fail pread(2) and pwrite(2) with ESPIPE.  Opened O_RDWR,
 * a FIFO does not block, so one fid can both write and read it.
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
#include "src/libnpclient/npcimpl.h"
#include "src/libtap/tap.h"

#include "diod_conf.h"

#define TEST_MSIZE 8192

/* Send a request without waiting for the reply.  Return its tag.
 */
static u16 send_request (Npcfid *fid, Npfcall *tc)
{
    Npcfsys *fs = fid->fsys;
    u16 tag;

    if (!tc)
        BAIL_OUT ("out of memory");
    tag = npc_get_id (fs->tagpool);
    np_set_tag (tc, tag);
    if (np_trans_send (fs->trans, tc) < 0)
        BAIL_OUT ("np_trans_send: %s", strerror (np_rerror ()));
    free (tc);
    return tag;
}

/* Receive one reply.  The caller must free it.
 */
static Npfcall *recv_reply (Npcfid *fid)
{
    Npcfsys *fs = fid->fsys;
    Npfcall *rc = NULL;

    if (np_trans_recv (fs->trans, &rc, fs->msize) < 0 || !rc)
        BAIL_OUT ("np_trans_recv failed");
    npc_put_id (fs->tagpool, rc->tag);
    return rc;
}

/* With a Tread blocked on an empty FIFO, a second request on the same fid
 * must not wait behind it.  A second Tread fails with ESPIPE, since the first
 * has not yet advanced the offset.  A Twrite at that same offset also
 * fails, but a client must not deadlock, so check that the reply comes
 * back, then unblock the read by writing on a second fid.
 */
static void test_concurrent (Npcfid *root)
{
    Npcfid *fid, *wfid;
    Npfcall *rc;
    u16 rtag, tag;

    fid = npc_open_bypath (root, "fifo", O_RDWR);
    wfid = npc_open_bypath (root, "fifo", O_RDWR);
    if (!fid || !wfid)
        BAIL_OUT ("npc_open_bypath fifo: %s", strerror (np_rerror ()));

    /* Send two Treads on fid at the same offset.  They may run in either
     * order on the server, but whichever starts second must fail with
     * ESPIPE while the other blocks, since there is nothing to read.
     */
    u16 tag1 = send_request (fid, np_create_tread (fid->fid, 0, 5));
    u16 tag2 = send_request (fid, np_create_tread (fid->fid, 0, 5));
    rc = recv_reply (fid);
    ok ((rc->tag == tag1 || rc->tag == tag2) && rc->type == Rlerror
        && rc->u.rlerror.ecode == ESPIPE,
        "Tread while another is blocked on the same fid fails with ESPIPE");
    rtag = (rc->tag == tag1) ? tag2 : tag1;
    free (rc);

    /* Twrite on fid at the same offset also fails without blocking */
    tag = send_request (fid, np_create_twrite (fid->fid, 0, 3, (u8 *)"abc"));
    rc = recv_reply (fid);
    ok (rc->tag == tag && rc->type == Rlerror
        && rc->u.rlerror.ecode == ESPIPE,
        "Twrite while a Tread is blocked on the same fid fails with ESPIPE");
    free (rc);

    /* Twrite on another fid unblocks the first Tread */
    tag = send_request (wfid, np_create_twrite (wfid->fid, 0, 5,
                                                (u8 *)"later"));
    int got_write = 0, got_read = 0;
    for (int i = 0; i < 2; i++) {
        rc = recv_reply (fid);
        if (rc->tag == tag && rc->type == Rwrite && rc->u.rwrite.count == 5)
            got_write = 1;
        else if (rc->tag == rtag && rc->type == Rread
                 && rc->u.rread.count == 5
                 && !memcmp (rc->u.rread.data, "later", 5))
            got_read = 1;
        free (rc);
    }
    ok (got_write && got_read,
        "Twrite on another fid unblocks the Tread");

    /* the completed Tread advanced fid's offset */
    char buf[8];
    int n;
    if (npc_pwrite (wfid, "more!", 5, 5) != 5)
        BAIL_OUT ("npc_pwrite: %s", strerror (np_rerror ()));
    n = npc_pread (fid, buf, 5, 5);
    ok (n == 5 && !memcmp (buf, "more!", 5),
        "Tread continues at the offset after the unblocked Tread");

    npc_clunk (wfid);
    npc_clunk (fid);
}

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int client_fd;
    Npcfid *root, *fid;
    char tmpdir[] = "/tmp/test-stream.XXXXXX";
    char path[PATH_MAX + 1];
    char buf[64];
    int rc;

    plan (NO_PLAN);

    if (!mkdtemp (tmpdir))
        BAIL_OUT ("mkdtemp: %s", strerror (errno));
    snprintf (path, sizeof (path), "%s/fifo", tmpdir);
    if (mkfifo (path, 0644) < 0)
        BAIL_OUT ("mkfifo: %s", strerror (errno));

    srv = test_server_create (tmpdir, 0, &client_fd);

    root = npc_mount (client_fd, client_fd, TEST_MSIZE, tmpdir, NULL);
    ok (root != NULL, "npc_mount on socketpair works");
    if (!root)
        BAIL_OUT ("npc_mount: %s", strerror (np_rerror ()));

    fid = npc_open_bypath (root, "fifo", O_RDWR);
    ok (fid != NULL, "opened FIFO O_RDWR");
    if (!fid)
        BAIL_OUT ("npc_open_bypath fifo: %s", strerror (np_rerror ()));

    /* sequential writes starting at offset 0 */
    rc = npc_pwrite (fid, "hello", 5, 0);
    ok (rc == 5, "Twrite at offset 0 works");
    if (rc < 0)
        diag ("npc_pwrite: %s", strerror (np_rerror ()));
    rc = npc_pwrite (fid, "world", 5, 5);
    ok (rc == 5, "Twrite at the next offset works");
    if (rc < 0)
        diag ("npc_pwrite: %s", strerror (np_rerror ()));

    /* a write at any other offset fails as it would locally */
    np_uerror (0);
    rc = npc_pwrite (fid, "oops", 4, 0);
    ok (rc < 0 && np_rerror () == ESPIPE,
        "Twrite at an earlier offset fails with ESPIPE");
    np_uerror (0);
    rc = npc_pwrite (fid, "oops", 4, 100);
    ok (rc < 0 && np_rerror () == ESPIPE,
        "Twrite at a later offset fails with ESPIPE");

    /* The offset is not a position in the data, which comes out of the
     * FIFO in the order it was written.  It only has to match where the
     * fid's last read or write left off, as the client keeps one file
     * position for both.  So after writing 10 bytes, the first read is
     * at offset 10 and returns "hello".
     */
    memset (buf, 0, sizeof (buf));
    rc = npc_pread (fid, buf, 5, 10);
    ok (rc == 5 && !memcmp (buf, "hello", 5),
        "Tread at the next offset returns the first write");
    if (rc < 0)
        diag ("npc_pread: %s", strerror (np_rerror ()));
    np_uerror (0);
    rc = npc_pread (fid, buf, 5, 0);
    ok (rc < 0 && np_rerror () == ESPIPE,
        "Tread at an earlier offset fails with ESPIPE");
    memset (buf, 0, sizeof (buf));
    rc = npc_pread (fid, buf, sizeof (buf), 15);
    ok (rc == 5 && !memcmp (buf, "world", 5),
        "Tread at the next offset returns the second write");
    if (rc < 0)
        diag ("npc_pread: %s", strerror (np_rerror ()));

    rc = npc_clunk (fid);
    ok (rc == 0, "clunked FIFO");

    test_concurrent (root);

    npc_umount (root);

    test_server_destroy (srv);

    (void)unlink (path);
    if (rmdir (tmpdir) < 0)
        diag ("rmdir %s: %s", tmpdir, strerror (errno));

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
