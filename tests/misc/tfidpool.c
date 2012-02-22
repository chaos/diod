/* tfidpool.c - exercise fidpool.c (valgrind me) */

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

#include "9p.h"
#include "npfs.h"

#include "list.h"
#include "diod_log.h"

int
main (int argc, char *argv[])
{
    Npsrv srv;
    Npconn conn;
    Npfid **fid;
    unsigned long nfids;
    int i, n;

    diod_log_init (argv[0]);

    if (argc != 2) {
        fprintf (stderr, "Usage: tfidpool nfids\n");
        exit (1);
    }
    nfids = strtoul (argv[1], NULL, 10);
    if (nfids == 0)
        msg_exit ("invalid nfids value");
    fid = (Npfid **)malloc (nfids * sizeof (Npfid *));
    if (!fid)
        msg_exit ("out of memory");

    memset (&srv, 0, sizeof (srv));
    srv.logmsg = diod_log_msg;
    srv.flags |= SRV_FLAGS_DEBUG_FIDPOOL;
    conn.fidpool = np_fidpool_create ();
    conn.srv = &srv;

    msg ("initial count: %d", np_fidpool_count (conn.fidpool));

    for (i = 0; i < nfids; i++) {
        fid[i] = np_fid_find (&conn, i);
        assert (fid[i] == NULL);
    }

    for (i = 0; i < nfids; i++) {
        fid[i] = np_fid_create (&conn, i);
        np_fid_incref (fid[i]);
        np_fid_incref (fid[i]);
    }

    msg ("count after fid create: %d", np_fidpool_count (conn.fidpool));

    for (i = 0; i < nfids; i++) {
        np_fid_decref (&fid[i]);
        np_fid_decref (&fid[i]);
        np_fid_decref (&fid[i]);
    }

    msg ("count after fid destroy: %d", np_fidpool_count (conn.fidpool));

    n = np_fidpool_destroy (conn.fidpool);

    msg ("unclunked: %d", n);

    free (fid);

    diod_log_fini ();
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
