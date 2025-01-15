/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test diod client/server with root/mult users */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <dirent.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"
#include "src/liblsd/list.h"
#include "src/libtap/tap.h"

#include "diod_log.h"
#include "diod_conf.h"
#include "diod_sock.h"
#include "diod_ops.h"

#define TEST_MSIZE 8192

int
file_test (Npcfid *root)
{
    int n, len = 4096*100;
    char *buf = malloc (len);
    char *buf2 = malloc (len);
    Npcfid *f;
    int rc = -1;

    if (!buf || !buf2)
        BAIL_OUT ("out of memory");

    /* create */
    if (!(f = npc_create_bypath (root, "foo", 0, 0644, getgid()))) {
        diag ("npc_create_bypath: %s", strerror (np_rerror ()));
        goto done;
    }
    if (npc_clunk (f) < 0) {
        diag ("npc_clunk: %s", strerror (np_rerror ()));
        goto done;
    }

    /* put */
    memset (buf, 9, len);
    n = npc_put (root, "foo", buf, len);
    if (n < 0) {
        diag ("npc_put: %s", strerror (np_rerror ()));
        goto done;
    }
    if (n < len) {
        diag ("npc_put: short write: %d", n);
        goto done;
    }

    /*  walk */
    f = npc_walk (root, "foo");
    if (!f) {
        diag ("npc_walk: %s", strerror (np_rerror ()));
        goto done;
    }

    /* open */
    if (npc_open (f, O_RDONLY) < 0) {
        diag ("npc_open: %s", strerror (np_rerror ()));
        goto done;
    }

    /* read */
    memset (buf2, 0, len);
    n = npc_get (root, "foo", buf2, len);
    if (n < 0) {
        diag ("npc_get: %s", strerror (np_rerror ()));
        goto done;
    }
    if (n < len) {
        diag ("npc_get: short read: %d bytes", n);
        goto done;
    }
    if (memcmp (buf, buf2, n) != 0) {
        diag ("memcmp failure");
        goto done;
    }

    /* clunk */
    if (npc_clunk (f) < 0) {
        diag ("npc_clunk: %s", strerror (np_rerror ()));
        goto done;
    }

    /* remove */
    if (npc_remove_bypath (root, "foo") < 0)
        diag ("npc_remove_bypath: %s", strerror (np_rerror ()));
    rc = 0;
done:
    free (buf);
    free (buf2);
    return rc;
}

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int s[2];
    int flags = 0;
    int rc;
    Npcfsys *fs;
    Npcfid *root, *user, *user2;
    char tmpdir[] = "/tmp/tnpsrv2.XXXXXX";

    if (geteuid () != 0 || getenv ("FAKEROOTKEY") != NULL)
        plan (SKIP_ALL, "this test must run as root");

    diag ("initialize diod logging and configuration");
    diod_log_init (argv[0]);
    diod_conf_init ();

    /* Allow attach with afid==-1.
     */
    diod_conf_set_auth_required (0);

    /* create export directory: will be owned by root, mode=0700 */
    if (!mkdtemp (tmpdir))
        BAIL_OUT ("mkdtemp: %s", strerror (errno));
    diod_conf_add_exports (tmpdir);
    diod_conf_set_exportopts ("sharefd");

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        BAIL_OUT ("socketpair: %s", strerror (errno));

    /* Note: supplementary groups do not work in this mode, however
     * regular uid:gid switching of fsid works.  Enabling DAC_BYPASS
     * assumes v9fs is enforcing permissions, not the case with npclient.
     */
    flags |= SRV_FLAGS_SETFSID;
    srv = np_srv_create (16, flags);
    ok (srv != NULL, "np_srv_create works");
    if (!srv)
        BAIL_OUT ("need server to continue");
    rc = diod_init (srv);
    ok (rc == 0, "diod_init works");
    if (rc < 0)
        BAIL_OUT ("diod_init: %s", strerror (np_rerror ()));

    diag ("connecting server to socketpair");
    diod_sock_startfd (srv, s[1], s[1], "loopback", 0);

    fs = npc_start (s[0], s[0], TEST_MSIZE, 0);
    ok (fs != NULL, "npc_start works");
    if (!fs)
        BAIL_OUT ("npc_start: %s", strerror (np_rerror ()));

    root = npc_attach (fs, NULL, tmpdir, 0); /* attach as uid=0 */
    ok (root != NULL, "npc_attach %s uid=0 works", tmpdir);
    if (!root)
        BAIL_OUT ("npc_attach: %s", strerror (np_rerror ()));

    user = npc_attach (fs, NULL, tmpdir, 1); /* attach as uid=1 */
    ok (user != NULL, "npc_attach %s uid=1 works", tmpdir);
    if (!user)
        BAIL_OUT ("npc_attach: %s", strerror (np_rerror ()));

    /* attach one more time as uid=1 to exercise user cache under valgrind */
    user2 = npc_attach (fs, NULL, tmpdir, 1);
    ok (user2 != NULL, "npc_attach %s uid=1 again works", tmpdir);
    if (!user2)
        diag ("npc_attach: %s", strerror (np_rerror ()));

    ok (file_test (root) == 0, "a file can be manipulated as root");
    ok (file_test (user) < 0, "a file cannot be manipulated as uid=1");

    rc = npc_chmod (root, ".", 0777);
    ok (rc == 0, "npc_chmod . 0777 works as root");
    if (rc < 0)
        diag ("npc_chmod: %s", strerror (np_rerror ()));

    ok (file_test (user) == 0, "a file can now be manipulated as uid=1");

    rc = npc_chown (root, ".", 1, 1);
    ok (rc == 0, "npc_chown . 1:1 works as root");
    if (rc < 0)
        diag ("npc_chown: %s", strerror (np_rerror ()));

    rc = npc_chmod (root, ".", 0700);
    ok (rc == 0, "npc_chmod . 0700 works as root");
    if (rc < 0)
        diag ("npc_chmod: %s", strerror (np_rerror ()));

    ok (file_test (user) == 0, "a file can still be manipulated as uid=1");

    ok (npc_clunk (user) == 0, "npc_clunk user works");
    ok (npc_clunk (user2) == 0, "npc_clunk user2 works");
    ok (npc_clunk (root) == 0, "npc_clunk root works");

    diag ("npc_finish");
    npc_finish (fs);

    diag ("npc_srv_wait_conncount");
    np_srv_wait_conncount (srv, 0);

    diag ("finalizing server");
    diod_fini (srv);
    np_srv_destroy (srv);

    rmdir (tmpdir);

    diod_conf_fini ();
    diod_log_fini ();

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
