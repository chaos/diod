/* ttrans.c - simple "loopback" transport module for testing */

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
#include <assert.h>
#include <sys/stat.h>
#include <sys/fcntl.h>

#include "9p.h"
#include "npfs.h"
#include "npfile.h"

#include "list.h"
#include "diod_log.h"

#include "ttrans.h"

struct Ttrans {
    Nptrans        *trans;
    int             tag;
    Npfcall        *tc;
    Npfcall        *rc;
    pthread_mutex_t lock; 
    pthread_cond_t  cond;
    enum { TT_NONE, TT_REQAVAIL, TT_BUSY, TT_RSPAVAIL, TT_EOF } state;
};
typedef struct Ttrans Ttrans;

static void
ttrans_destroy(void *a)
{
    Ttrans *tt = (Ttrans *)a;

    free(tt);
}

/* Read runs in the context of connection's reader thread.
 * Server reads requests with this.
 */
static int
ttrans_read (u8 *data, u32 count, void *a)
{
    Ttrans *tt = (Ttrans *)a;
    int ret = 0;

    pthread_mutex_lock (&tt->lock);
    while (tt->state != TT_REQAVAIL && tt->state != TT_EOF)
        pthread_cond_wait (&tt->cond, &tt->lock);
    if (tt->state == TT_REQAVAIL) {
        assert (tt->tc->size <= count);
        memcpy (data, tt->tc->pkt, tt->tc->size);
        ret = tt->tc->size;
        tt->state = TT_BUSY;
    }
    pthread_mutex_unlock (&tt->lock);

    return ret;
}

/* Write runs in the context of one of the server's worker threads.
 * Server writes replies with this.
 */
static int
ttrans_write (u8 *data, u32 count, void *a)
{
    Ttrans *tt = (Ttrans *)a;

    pthread_mutex_lock (&tt->lock);
    assert (tt->state == TT_BUSY);
    memcpy (tt->rc->pkt, data, count);
    if (np_deserialize (tt->rc, tt->rc->pkt) == 0)
        msg ("ttrans: deserialization error");
    tt->state = TT_RSPAVAIL;
    pthread_cond_broadcast (&tt->cond);
    pthread_mutex_unlock (&tt->lock);
    
    return count;
}    

int
ttrans_rpc (Nptrans *t, Npfcall *tc, Npfcall *rc)
{
    Ttrans *tt = (Ttrans *)t->aux;

    pthread_mutex_lock (&tt->lock);

    assert (tt->state == TT_NONE);
    if (tc && rc) {
        /* enqueue for ttrans_read */
        np_set_tag (tc, tt->tag++);
        tt->tc = tc;
        tt->rc = rc;
        tt->state = TT_REQAVAIL;
        pthread_cond_broadcast (&tt->cond);
        
        /* wait for ttrans_swrite to enqueue response */
        while (tt->state != TT_RSPAVAIL)
            pthread_cond_wait (&tt->cond, &tt->lock);
        tt->state = TT_NONE;
    } else {
        tt->state = TT_EOF;
        pthread_cond_broadcast (&tt->cond);
    }

    pthread_mutex_unlock (&tt->lock);

    return 0;
}

Nptrans *
ttrans_create(void)
{
    Ttrans *tt;

    if (!(tt = malloc (sizeof (*tt))))
        goto oom;
    pthread_mutex_init (&tt->lock, NULL);
    pthread_cond_init (&tt->cond, NULL);
    tt->tag = P9_NOTAG;
    tt->tc = NULL;
    tt->rc = NULL;
    tt->state = TT_NONE;
    tt->trans = np_trans_create(tt, ttrans_read, ttrans_write, ttrans_destroy);
    if (!tt->trans)
        goto oom;
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
