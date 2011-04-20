/* tnpfile.c - test skeleton libnpfs npfile server (valgrind me) */

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
#include "diod_upool.h"
#include "diod_conf.h"
#include "diod_auth.h"

#define TEST_MSIZE 8192

static Nptrans *ttrans_create(void);
static Npfile *_file_root_create (void);

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
    npfile_init_srv (srv, _file_root_create ());

    /* Diod_auth module presumes diod_trans transport.
     * Disable auth_required to avoid triggering an assertion.
     */
    diod_conf_init ();
    srv->upool = diod_upool;
    diod_conf_set_auth_required (0);

    /* create one connection */
    if (!(trans = ttrans_create ()))
        err_exit ("ttrans_create");
    if (!(conn = np_conn_create (srv, trans)))
        msg_exit  ("np_conn_create failure");

    /* wait for exactly one connect/disconnect */
    np_srv_wait_conncount (srv, 1);
    np_srv_destroy (srv);

    diod_conf_fini ();
    diod_log_fini ();
    exit (0);
}

static Npfile *
_root_first (Npfile *dir)
{
    if (dir->dirfirst)
        npfile_incref(dir->dirfirst);

    return dir->dirfirst;
}

static Npfile *
_root_next (Npfile *dir, Npfile *prevchild)
{
    if (prevchild->next)
        npfile_incref (prevchild->next);

    return prevchild->next;
}

static int
_foo_read (Npfilefid *file, u64 offset, u32 count, u8 *data, Npreq *req)
{
    return 0;
}

static void
_foo_closefid (Npfilefid *file)
{
}

static int
_foo_openfid (Npfilefid *file)
{
    return 1;
}

static int
_bar_read (Npfilefid *file, u64 offset, u32 count, u8 *data, Npreq *req)
{
    return 0;
}

static int
_bar_write (Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *req)
{
    return 0;
}

static void
_bar_closefid (Npfilefid *file)
{
}

static int
_bar_openfid (Npfilefid *file)
{
    return 1;
}

static Npdirops root_ops = {
        .first = _root_first,
        .next =  _root_next,
};
static Npfileops foo_ops = {
        .read  = _foo_read,
        .closefid = _foo_closefid,
        .openfid = _foo_openfid,
};
static Npfileops bar_ops = {
        .read = _bar_read,
        .write = _bar_write,
        .closefid = _bar_closefid,
        .openfid = _bar_openfid,
};

static Npfile *
_file_root_create (void)
{
    Npfile *root, *foo, *bar;

    if (!(root = npfile_alloc (NULL, "", 0555|S_IFDIR, 0, &root_ops, NULL)))
        msg_exit ("out of memory");
    root->parent = root;
    npfile_incref(root);

    if (!(foo = npfile_alloc(root, "foo", 0444|S_IFREG, 1, &foo_ops, NULL)))
        msg_exit ("out of memory");
    npfile_incref(foo);

    if (!(bar = npfile_alloc(root, "bar", 0666|S_IFREG, 3, &bar_ops, NULL)))
        msg_exit ("out of memory");
    npfile_incref(bar);

    root->dirfirst = bar;
    bar->next = foo;
    root->dirlast = foo;

    return root;

}

/**
 ** Special transport for testing.
 **/

struct Ttrans {
    Nptrans *trans;
    Npfcall *rc;            /* receive buffer */
    List reqs;              /* request queue, established at creation */
    int outstanding;        /* count of outstanding requests (will be 0 or 1) */
    int tag;                /* next tag to use */
    pthread_mutex_t lock; 
    pthread_cond_t reqcond;
};
typedef struct Ttrans Ttrans;

static void
ttrans_destroy(void *a)
{
    Ttrans *tt = (Ttrans *)a;

    if (tt->reqs)
        list_destroy (tt->reqs);
    if (tt->rc)
        free (tt->rc);
    free(tt);
}

/* Read runs in the context of connection's reader thread.
 */
static int
ttrans_read (u8 *data, u32 count, void *a)
{
    Ttrans *tt = (Ttrans *)a;
    Npfcall *fc;
    int ret = 0;

    pthread_mutex_lock (&tt->lock);
    while (tt->outstanding > 0)
            pthread_cond_wait (&tt->reqcond, &tt->lock);
    if ((fc = list_dequeue (tt->reqs))) {
        assert (fc->size <= count);
        memcpy (data, fc->pkt, fc->size);
        ret = fc->size;
        tt->outstanding++;
        free (fc);
    }
    pthread_mutex_unlock (&tt->lock);

    return ret;
}

/* Write runs in the context of one of the server's worker threads.
 */
static int
ttrans_write(u8 *data, u32 count, void *a)
{
    Ttrans *tt = (Ttrans *)a;

    pthread_mutex_lock (&tt->lock);
    assert (count <= TEST_MSIZE);
    memcpy (tt->rc->pkt, data, count);
    assert (np_peek_size (tt->rc->pkt, count) == count);
    if (np_deserialize (tt->rc, tt->rc->pkt) == 0)
        msg ("deserialization error");
    tt->outstanding--;
    pthread_cond_broadcast (&tt->reqcond);
    pthread_mutex_unlock (&tt->lock);
    
    return count;
}    

static int
_add_tclunk (Ttrans *tt)
{
    Npfcall *fc;

    if (!(fc = np_create_tclunk (0)))
        goto oom;
    np_set_tag (fc, tt->tag++);
    if (!list_append (tt->reqs, fc))
        goto oom; 
    return 0;
oom:
    if (fc)
        free (fc);
    return -1;
}

static int
_add_tattach (Ttrans *tt)
{
    Npfcall *fc;

    if (!(fc = np_create_tattach (0, P9_NOFID, NULL, "/foo", 0)))
        goto oom;
    np_set_tag (fc, tt->tag++);
    if (!list_append (tt->reqs, fc))
        goto oom; 
    return 0;
oom:
    if (fc)
        free (fc);
    return -1;
}


static int
_add_tauth (Ttrans *tt)
{
    Npfcall *fc;

    if (!(fc = np_create_tauth (0, NULL, "/foo", 0)))
        goto oom;
    np_set_tag (fc, tt->tag++);
    if (!list_append (tt->reqs, fc))
        goto oom; 
    return 0;
oom:
    if (fc)
        free (fc);
    return -1;
}

static int
_add_tversion (Ttrans *tt)
{
    Npfcall *fc;

    if (!(fc = np_create_tversion (TEST_MSIZE, "9P2000.L")))
        goto oom;
    np_set_tag (fc, P9_NOTAG);
    if (!list_append (tt->reqs, fc))
        goto oom;
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
    pthread_mutex_init (&tt->lock, NULL);
    pthread_cond_init (&tt->reqcond, NULL);
    if (!(tt->reqs = list_create ((ListDelF)free)))
        goto oom;
    /* receive buffer */
    if (!(tt->rc = malloc(sizeof (tt->rc) + TEST_MSIZE)))
        goto oom;
    tt->rc->pkt = (u8*) tt->rc + sizeof(*tt->rc);
    if (_add_tversion (tt) < 0)
        goto oom;
    if (_add_tauth (tt) < 0)
        goto oom;
    if (_add_tattach (tt) < 0)
        goto oom;
    if (_add_tclunk (tt) < 0)
        goto oom;
    tt->outstanding = 0;
    
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
