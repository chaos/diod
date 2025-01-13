/*************************************************************\
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

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

#include "src/liblsd/list.h"
#include "src/libtap/tap.h"


void diag_logger (const char *fmt, va_list ap)
{
    char buf[1024]; /* make it large enough for protocol debug output */
    vsnprintf (buf, sizeof (buf), fmt, ap);  /* ignore overflow */
    fprintf (stderr, "# %s\n", buf);
}


int main (int argc, char *argv[])
{
    Npsrv srv;
    Npconn conn;
    Npfid **fid;
    unsigned long nfids = 1000;
    int i, n;

    plan (NO_PLAN);

    if (argc > 2)
        BAIL_OUT ("Usage: test_fidpool.t [nfids]");
    if (argc == 2) {
        nfids = strtoul (argv[1], NULL, 10);
        if (nfids == 0)
            BAIL_OUT ("invalid nfids value\n");
    }
    fid = (Npfid **)malloc (nfids * sizeof (Npfid *));
    if (!fid)
        BAIL_OUT ("out of memory");
    memset (&srv, 0, sizeof (srv));
    srv.logmsg = diag_logger;
    srv.flags |= SRV_FLAGS_DEBUG_FIDPOOL;
    conn.fidpool = np_fidpool_create ();
    conn.srv = &srv;

    ok (np_fidpool_count (conn.fidpool) == 0,
        "np_fidpool_count returns 0") ;

    int errors = 0;
    for (i = 0; i < nfids; i++) {
        fid[i] = np_fid_find (&conn, i);
        if (fid[i] != NULL) {
            diag ("np_fid_find (i=%d) failed", i);
            errors++;
        }
    }
    diag ("np_find_fid called on %d fids", nfids);
    ok (errors == 0, "there were no errors");
    ok (np_fidpool_count (conn.fidpool) == 0,
        "np_fidpool_count returns 0");

    for (i = 0; i < nfids; i++) {
        fid[i] = np_fid_create (&conn, i);
        np_fid_incref (fid[i]);
        np_fid_incref (fid[i]);
    }
    diag ("np_fid_create called %d times (with 2 increfs each)");
    ok (np_fidpool_count (conn.fidpool) == nfids,
        "np_fidpool_count returns %d", nfids);

    for (i = 0; i < nfids; i++) {
        np_fid_decref (&fid[i]);
        np_fid_decref (&fid[i]);
        np_fid_decref (&fid[i]);
    }
    diag ("np_fid_decref called %d times (with 2 more decrefs each)", nfids);
    ok (np_fidpool_count (conn.fidpool) == 0,
        "np_fidpool_count returns 0");

    n = np_fidpool_destroy (conn.fidpool);
    ok (n == 0,
        "np_fidpool_destroy returned 0 (meaning 0 unclunked)", n);

    free (fid);

    done_testing ();
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
