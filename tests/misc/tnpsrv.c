/* tnpsrv.c - test skeleton libnpfs server (valgrind me) */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"

#include "diod_log.h"

#include "opt.h"

int
main (int argc, char *argv[])
{
#if 0
    Npsrv *srv;
    Npconn *conn;
    Nptrans *trans;

    diod_log_init (argv[0]);
    if (!(srv = np_srv_create (16)))
        msg_exit ("out of memory");
    if (!(trans = np_fdtrans_create (0, 0)))
        msg_exit ("np_fdtrans_create");
    if (!(conn = np_conn_create (srv, trans)))
        msg_exit  ("np_conn_create failure");

    np_conn_shutdown (conn);
    //np_conn_incref (conn);

    //np_conn_decref (conn);

    free (srv);

    diod_log_fini ();

#endif
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
