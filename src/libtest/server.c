/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "server.h"

#include "src/liblsd/list.h"
#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_conf.h"
#include "src/libdiod/diod_ops.h"
#include "src/libdiod/diod_sock.h"
#include "src/libtap/tap.h"

Npsrv *test_server_create (const char *testdir, int flags, int *client_fd)
{
    int s[2];
    Npsrv *srv;

    diod_log_init ("#"); // add TAP compatible prefix on stderr logs
    diod_conf_init ();
    diod_conf_set_auth_required (0);

    if (testdir)
        diod_conf_add_exports ((char *)testdir);

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        BAIL_OUT ("socketpair: %s", strerror (errno));

    if (!(srv = np_srv_create (16, flags)))
        BAIL_OUT ("np_srv_create failed");

    if (diod_init (srv) < 0)
        BAIL_OUT ("diod_init: %s", strerror (np_rerror ()));

    diod_sock_startfd (srv, s[1], s[1], "simple-test-client", flags);

    diag ("serving %s on fd %d", testdir, s[0]);

    *client_fd = s[0];
    return srv;
}

void test_server_destroy (Npsrv *srv)
{
    diag ("waiting for client to finish");
    np_srv_wait_conncount (srv, 0);

    diod_fini (srv);
    np_srv_destroy (srv);

    diod_conf_fini ();
    diod_log_fini ();
}

// vi:ts=4 sw=4 expandtab
