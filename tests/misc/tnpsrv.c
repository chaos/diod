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
#include <pthread.h>

#include "9p.h"
#include "npfs.h"

#include "list.h"
#include "diod_log.h"
#include "diod_conf.h"

#include "ttrans.h"

#define TEST_MSIZE 8192

static Npfcall* myattach (Npfid *fid, Npfid *afid, Npstr *aname);
static Npfcall *myclunk (Npfid *fid);

static void _send_tversion (Nptrans *t);
static void _send_tauth (Nptrans *t);
static void _send_tattach (Nptrans *t);
static void _send_tclunk (Nptrans *t);

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    Npconn *conn;
    Nptrans *trans;
    int flags = SRV_FLAGS_DEBUG_9PTRACE;

    diod_log_init (argv[0]);
    diod_conf_init ();

    if (!(srv = np_srv_create (16, flags)))
        msg_exit ("out of memory");
    srv->logmsg = diod_log_msg;

    srv->attach = myattach;
    srv->clunk = myclunk;

    /* create one connection */
    if (!(trans = ttrans_create ()))
        err_exit ("ttrans_create");
    if (!(conn = np_conn_create (srv, trans, "loopback")))
        msg_exit  ("np_conn_create failure");

    _send_tversion (trans);
    _send_tauth (trans);
    _send_tattach (trans);
    _send_tclunk (trans);
    ttrans_rpc (trans, NULL, NULL); /* signifies EOF to reader */

    /* wait for exactly one connect/disconnect */
    np_srv_wait_conncount (srv, 1);
    np_srv_destroy (srv);

    diod_conf_fini ();
    diod_log_fini ();
    exit (0);
}

static Npfcall *
myattach (Npfid *fid, Npfid *afid, Npstr *aname)
{
    Npqid qid = { 1, 2, 3};
    Npfcall *ret = NULL;

    if (!(ret = np_create_rattach(&qid))) {
        np_uerror (ENOMEM);
        return NULL;
    }
    np_fid_incref (fid);
    return ret;
}

static Npfcall *
myclunk (Npfid *fid)
{
    Npfcall *ret;

    if (!(ret = np_create_rclunk ())) {
        np_uerror (ENOMEM);
        return NULL;
    }
    return ret;
}

static Npfcall *
_alloc_rc (void)
{
    Npfcall *rc;

    rc = malloc(sizeof (*rc) + TEST_MSIZE);
    if (!rc)
        msg_exit ("oom");
    rc->pkt = (u8*)rc + sizeof(*rc);
    return rc;
}

static void
_send_tclunk (Nptrans *t)
{
    Npfcall *tc, *rc = _alloc_rc ();

    tc = np_create_tclunk (0);
    if (!tc)
        msg_exit ("oom");
    ttrans_rpc (t, tc, rc);
    free (tc);
    free (rc);
}

static void
_send_tattach (Nptrans *t)
{
    Npfcall *tc, *rc = _alloc_rc ();

    tc = np_create_tattach (0, P9_NOFID, NULL, "/foo", 0);
    if (!tc)
        msg_exit ("oom");
    ttrans_rpc (t, tc, rc);
    free (tc);
    free (rc);
}


static void
_send_tauth (Nptrans *t)
{
    Npfcall *tc, *rc = _alloc_rc ();

    tc = np_create_tauth (0, NULL, "/foo", 0);
    if (!tc)
        msg_exit ("oom");
    ttrans_rpc (t, tc, rc);
    free (tc);
    free (rc);
}

static void 
_send_tversion (Nptrans *t)
{
    Npfcall *tc, *rc = _alloc_rc ();

    tc = np_create_tversion (TEST_MSIZE, "9P2000.L");
    if (!tc)
        msg_exit ("oom");
    ttrans_rpc (t, tc, rc);
    free (tc);
    free (rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
