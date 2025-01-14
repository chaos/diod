/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test client/server reads with diod ops */

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
#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_conf.h"
#include "src/libdiod/diod_sock.h"

#include "ops.h"
#include "src/libtap/tap.h"

#define TEST_MSIZE 8192

#define TEST_ITER 64

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int s[2];
    int flags = 0;
    int rc;
    Npcfid *root, *f[TEST_ITER];
    char tmpdir[] = "/tmp/test-ops.XXXXXX";
    int i;
    int n, len = 4096*100;
    char *buf = malloc (len);
    char *buf2 = malloc (len);

    plan (NO_PLAN);

    if (!buf || !buf2)
        BAIL_OUT ("out of memory");

    diag ("initialize diod logging and configuration");
    diod_log_init (argv[0]);
    diod_conf_init ();
    diod_conf_set_auth_required (0);

    /* export */
    if (!mkdtemp (tmpdir))
        BAIL_OUT ("mkdtemp: %s", strerror (errno));
    diag ("exporting %s", tmpdir);
    diod_conf_add_exports (tmpdir);
    diod_conf_set_exportopts ("sharefd");

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        BAIL_OUT ("socketpair: %s", strerror (errno));

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

    root = npc_mount (s[0], s[0], TEST_MSIZE, tmpdir, NULL);
    ok (root != NULL, "npc_mount on socketpair works");
    if (!root)
        BAIL_OUT ("npc_mount: %s", strerror (np_rerror ()));

    /* create a file, write some data into it, then read it TEST_ITER
     * times, each on a unique fid.  The path and ioctx will be shared due
     * to "sharefd" export option set above.  We want to be sure no memory
     * leaks result from the management of shared paths and io contexts.
     */
    f[0] = npc_create_bypath (root, "foo", 0, 0644, getgid());
    ok (f[0] != NULL, "npc_create_bypath foo works");
    if (!f[0])
        BAIL_OUT ("npc_create_bypath foo: %s", strerror (np_rerror ()));
    ok (npc_clunk (f[0]) == 0, "npc_clunk on foo works");

    /* fill it with some data (npc_put does open + write + clunk) */
    memset (buf, 9, len);
    n = npc_put (root, "foo", buf, len);
    ok (n == len, "npc_put %d bytes works", len);
    if (n < 0)
        diag ("ncp_put: %s", strerror (np_rerror ()));
    if (n < len)
        diag ("npc_put: short write: %d", n);

    /*  walk */
    int errors = 0;
    for (i = 0; i < TEST_ITER; i++) {
        f[i] = npc_walk (root, "foo");
        if (!f[i]) {
            diag ("ncp_walk: %s", strerror (np_rerror ()));
            errors++;
        }
    }
    ok (errors == 0, "npc_walk created %d fids for foo", TEST_ITER);

    /* open */
    errors = 0;
    for (i = 0; i < TEST_ITER; i++) {
        if (npc_open (f[i], O_RDONLY) < 0) {
            diag ("ncp_open: %s", strerror (np_rerror ()));
            errors++;
        }
    }
    ok (errors == 0, "npc_open works on each fid");

    /* read (using new fids) */
    errors = 0;
    for (i = 0; i < TEST_ITER; i++) {
        if (f[i]) {
            memset (buf2, 0, len);
            int count = 0;
            while (count < len) {
                n = npc_read (f[i], buf2 + count, len - count);
                if (n < 0) {
                    diag ("ncp_read: %s", strerror (np_rerror ()));
                    break;
                }
                if (n == 0) {
                    diag ("npc_read: short read (%d bytes)", count);
                    break;
                }
                count += n;
            } while (count < len);
            if (count != len || memcmp (buf, buf2, n) != 0)
                errors++;
        }
    }
    ok (errors == 0, "npc_read works on each fid (full content verified)");

    /* clunk */
    errors = 0;
    for (i = 0; i < TEST_ITER; i++) {
        if (f[i]) {
            if (npc_clunk (f[i]) < 0) {
                diag ("npc_clunk: %s", strerror (np_rerror ()));
                errors++;
            }
        }
    }
    ok (errors == 0, "npc_clunk works on each fid");

    /* remove */
    rc = npc_remove_bypath (root, "foo");
    ok (rc == 0, "npc_remove_bypath foo works");
    if (rc < 0)
        diag ("npc_remove_bypath: %s", strerror (np_rerror ()));

    free (buf);
    free (buf2);

    diag ("npc_umount");
    npc_umount (root);

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
