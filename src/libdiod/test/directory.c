/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test client/server with diod ops */

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

#define TEST_ITER 64

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int s[2];
    int flags = 0;
    int rc;
    Npcfid *root, *dir, *f[TEST_ITER];
    char tmpdir[] = "/tmp/test-ops.XXXXXX";
    int i;
    char tmpstr[PATH_MAX + 1];
    int n;
    char dbuf[TEST_MSIZE - P9_IOHDRSZ];
    struct dirent d, *dp;

    plan (NO_PLAN);

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

    /* create a directory with TEST_ITER files, and exercise various ops
     * on the files and directory.
    */
    rc = npc_mkdir_bypath (root, "foo", 0755);
    ok (rc == 0, "npc_mkdir_bypath foo works");
    if (rc < 0)
        BAIL_OUT ("npc_mkdir_bypath: %s", strerror (np_rerror ()));

    /* create files */
    int errors = 0;
    for (i = 0; i < TEST_ITER; i++) {
        snprintf (tmpstr, sizeof (tmpstr), "foo/%-.200i", i);
        f[i] = npc_create_bypath (root, tmpstr, 0, 0644, getgid());
        if (!f[i]) {
            diag ("npc_create_bypath %s: %s", tmpstr, strerror (np_rerror ()));
            errors++;
        }
    }
    ok (errors == 0, "npc_create_bypath works on %d files under foo", TEST_ITER);

    /* open the directory */
    dir = npc_opendir (root, "foo");
    ok (dir != NULL, "npc_opendir foo works");
    if (!dir)
        BAIL_OUT ("npc_opendir foo: %s", strerror (np_rerror ()));

    /* read one chunk of directory (redundant with below) */
    rc = npc_readdir (dir, 0, dbuf, sizeof (dbuf));
    ok (rc >= 0, "npc_readdir works");
    if (rc < 0)
        diag ("npc_readdir: %s", strerror (np_rerror ()));

    /* list the files in the directory */
    i = 0;
    errors = 0;
    do {
        if ((n = npc_readdir_r (dir, &d, &dp)) > 0) {
            diag ("npc_readdir_r: %s", strerror (n));
            errors++;
            break;
        }
        if (dp)
            i++;
    } while (n == 0 && dp != NULL);
    ok (errors == 0 && i == TEST_ITER + 2, /* . and .. will appear */
        "npc_readdir_r loop found correct number of files");

    /* close the directory */
    rc = npc_clunk (dir);
    ok (rc == 0, "npc_clunk works on directory");
    if (rc < 0)
        diag ("npc_clunk: %s", strerror (n));

    /* remove files (implicit clunk) */
    errors = 0;
    for (i = 0; i < TEST_ITER; i++) {
        if (npc_remove (f[i]) < 0) {
            diag ("npc_remove: %s", strerror (n));
            errors++;
        }
    }
    ok (errors == 0, "npc_remove removed %d files", TEST_ITER);

    /* remove directory */
    rc = npc_remove_bypath (root, "foo");
    ok (rc == 0, "npc_remove_bypath removed foo directory");
    if (rc < 0)
        diag ("npc_remove foo: %s", strerror (np_rerror ()));

    diag ("npc_umount");
    npc_umount (root);

    diag ("npc_srv_wait_conncount");
    np_srv_wait_conncount (srv, 0);

    diag ("finalzing server");
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
