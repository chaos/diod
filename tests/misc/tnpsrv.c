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

#define TEST_MSIZE 8192

static Nptrans *ttrans_create(void);

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    Npconn *conn;
    Nptrans *trans;

    diod_log_init (argv[0]);

    if (!(srv = np_srv_create (16)))
        msg_exit ("out of memory");
    srv->debuglevel |= DEBUG_9P_TRACE;
    srv->debugprintf = msg;

    /* create a connection */
    if (!(trans = ttrans_create ()))
        err_exit ("ttrans_create");
    if (!(conn = np_conn_create (srv, trans)))
        msg_exit  ("np_conn_create failure");

    np_srv_wait_conncount (srv, 1);
    np_srv_destroy (srv);

    diod_log_fini ();
    exit (0);
}

/* Special transport for testing.
 */
struct Ttrans {
    Nptrans *trans;
    char *inbuf;
    int inbuf_size;
    int inbuf_done;
};
typedef struct Ttrans Ttrans;

static void
ttrans_destroy(void *a)
{
    Ttrans *tt = (Ttrans *)a;

    if (tt->inbuf)
        free (tt->inbuf);
    free(tt);
}

static int
ttrans_read (u8 *data, u32 count, void *a)
{
    Ttrans *tt = (Ttrans *)a;
    int ret = count;
    int left = tt->inbuf_size - tt->inbuf_done;

    if (ret > left)
        ret = left;
    memcpy (data, tt->inbuf + tt->inbuf_done, ret);
    tt->inbuf_done += ret;

    if (ret == 0)
        sleep(1); /* FIXME: this avoid premature teardown of transmit */

    return ret;
}

static int
ttrans_write(u8 *data, u32 count, void *a)
{
    //Ttrans *tt = (Ttrans *)a;

    return count;
}    

/* Add a Tversion message to the transport's input buffer.
 */
static int
_add_tversion (Ttrans *tt)
{
    Npfcall *fc;

    if (!(fc = np_create_tversion (TEST_MSIZE, "9P2000.L")))
        goto oom;
    if (tt->inbuf)
        tt->inbuf = realloc (tt->inbuf, tt->inbuf_size + fc->size);
    else
        tt->inbuf = malloc (fc->size);
    if (!tt->inbuf)
        goto oom;
    memcpy (tt->inbuf + tt->inbuf_size, fc->pkt, fc->size);
    tt->inbuf_size += fc->size;
    free (fc);
    return 0;
oom:
    if (fc)
        free (fc);
    return -1;
}

static Nptrans *
ttrans_create(void)
{
    Ttrans *tt;

    if (!(tt = malloc (sizeof (*tt))))
        goto oom;
    memset (tt, 0, sizeof (*tt));
    if (_add_tversion (tt) < 0)
        goto oom;
    
    tt->trans = np_trans_create(tt, ttrans_read, ttrans_write, ttrans_destroy);
    if (!tt->trans) {
        free(tt);
        return NULL;
    }
    return tt->trans;
oom:
    if (tt)
        ttrans_destroy (tt);
    np_uerror (ENOMEM);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
