/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* simple npfs client/server */

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

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"
#include "src/libtap/tap.h"

#define TEST_MSIZE 8192

static void diag_logger (const char *fmt, va_list ap)
{
    char buf[1024]; /* make it large enough for protocol debug output */
    vsnprintf (buf, sizeof (buf), fmt, ap);  /* ignore overflow */
    fprintf (stderr, "# %s\n", buf);
}

static void check_data_trimmed (const char *name, const char *s, const char *expect)
{
    size_t len = s ? strlen (s) : 0;
    if (len > 0 && s[len - 1] == '\n')
        len--;
    ok (s && !strncmp (s, expect, len), "%s has expected content", name);
}

int main (int argc, char *argv[])
{
    Npsrv *srv;
    int s[2];
    int flags = SRV_FLAGS_DEBUG_9PTRACE | SRV_FLAGS_DEBUG_USER;
    Npconn *conn;
    Nptrans *trans;
    Npcfsys *fs;
    Npcfid *root0, *root1, *root2;
    char *str;

    plan (NO_PLAN);

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        BAIL_OUT ("socketpair: %s", strerror (errno));

    srv = np_srv_create (16, flags);
    ok (srv != NULL, "np_srv_create works");
    if (!srv)
        BAIL_OUT ("need server to continue");
    srv->logmsg = diag_logger;

    trans = np_fdtrans_create (s[1], s[1]);
    ok (trans != NULL, "np_fdtrans_create works");
    if (!trans)
        BAIL_OUT ("need transport to continue");

    /* N.B. trans is destroyed in np_conn_create on failure */
    conn = np_conn_create (srv, trans, "simple-test-client", flags);
    ok (conn != NULL, "np_conn_create works");
    if (!conn) {
        diag ("np_conn_create: %s", strerror (np_rerror ()));
        BAIL_OUT ("need server connection to continue");
    }

    fs = npc_start (s[0], s[0], TEST_MSIZE, 0);
    ok (fs != NULL, "npc_start works");
    if (!fs) {
        diag ("npc_start: %s", strerror (np_rerror ()));
        BAIL_OUT ("need client connection to continue");
    }
    root0 = npc_attach (fs, NULL, "ctl", 0);
    ok (root0 != NULL, "npc_attach aname=ctl uid=0 works");
    if (!root0) {
        diag ("npc_attach: %s", strerror (np_rerror ()));
        BAIL_OUT ("need ctl:0 fid to continue");
    }
    str = npc_aget (root0, "connections");
    ok (str != NULL, "npc_aget connections on ctl:0 fid works");
    check_data_trimmed ("connections", str, "simple-test-client 2");

    free (str);

    root1 = npc_attach (fs, NULL, "ctl", 1);
    ok (root1 != NULL, "npc_attach aname=ctl uid=1 works");
    if (!root1) {
        diag ("npc_attach: %s", strerror (np_rerror ()));
        BAIL_OUT ("need ctl:1 fid to continue");
    }
    str = npc_aget (root1, "connections");
    ok (str != NULL, "npc_aget connections on ctl:1 fid works");
    check_data_trimmed ("connections", str, "simple-test-client 3");
    free (str);

    /* Same user (1) - user cache should be valid, so we won't see a message
     * for this user lookup in the output.
     */
    root2 = npc_attach (fs, NULL, "ctl", 1);
    ok (root2 != NULL, "second npc_attach aname=ctl uid=1 works");
    if (!root1) {
        diag ("npc_attach: %s", strerror (np_rerror ()));
        BAIL_OUT ("need second ctl:1 fid to continue");
    }
    str = npc_aget (root2, "null");
    ok (str != NULL, "npc_aget connections on second ctl:1 fid works");
    check_data_trimmed ("connections", str, "");
    free (str);

    ok (npc_clunk (root0) == 0, "npc_clunk uid:0 fid works");
    ok (npc_clunk (root1) == 0, "npc_clunk uid:1 fid works");
    ok (npc_clunk (root2) == 0, "npc_clunk second uid:1 fid works");

    diag ("npc_finish");
    npc_finish (fs);

    diag ("np_srv_wait_conncount 0");
    np_srv_wait_conncount (srv, 0);
    diag ("np_srv_wait_conncount returned");

    diag ("np_srv_destroy");
    np_srv_destroy (srv);

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
