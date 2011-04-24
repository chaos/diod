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
#include "diod_conf.h"
#include "diod_auth.h"

#include "ttrans.h"

static Npfile *_file_root_create (void);

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    Npconn *conn;
    Nptrans *trans;

    diod_log_init (argv[0]);
    diod_conf_init ();

    if (!(srv = np_srv_create (16)))
        msg_exit ("out of memory");
    srv->debuglevel |= DEBUG_9P_TRACE;
    srv->msg = msg;
    srv->auth = diod_auth;
    diod_conf_set_auth_required (0); /* diod_auth presumes diod_trans */
    npfile_init_srv (srv, _file_root_create ());

    /* create one connection */
    if (!(trans = ttrans_create ()))
        err_exit ("ttrans_create");
    if (!(conn = np_conn_create (srv, trans)))
        msg_exit  ("np_conn_create failure");

    /* do stuff */
    ttrans_rpc (trans, NULL, NULL); /* signifies EOF to reader */

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
