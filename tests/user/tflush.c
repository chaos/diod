/* tflush.c - issue 100 requests, flush one, read the responses */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

#include "diod_log.h"
#include "diod_auth.h"

static void
_flush_series (Npcfsys *fs, Npcfid *root);

static void
usage (void)
{
    fprintf (stderr, "Usage: tflush aname\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    char *aname;
    int fd = 0; /* stdin */
    uid_t uid = geteuid ();

    diod_log_init (argv[0]);

    if (argc != 2)
        usage ();
    aname = argv[1];

    if (!(fs = npc_start (fd, fd, 8192+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn_exit (np_rerror (), "npc_clunk afid");

    _flush_series (fs, root);

    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    diod_log_fini ();

    exit (0);
}

static void
_flush_series (Npcfsys *fs, Npcfid *root)
{
    Npfcall *rc = NULL, *tc = NULL, *ac = NULL;
    Npcfid *f;
    u16 tag, flushtag;
    int n, i;
    int orig_reply_received = 0;
    int rflush_received = 0;

    assert (fs->trans != NULL);

    if (!(f = npc_open_bypath (root, "/", 0644)))
        err_exit ("open)");

    for (i = 0; i < 100; i++) {
        if (!(tc = np_create_tfsync (f->fid)))
            msg_exit ("out of memory");
        flushtag = tag = npc_get_id(fs->tagpool);
        np_set_tag(tc, tag);
        n = np_trans_send(fs->trans, tc);
        if (n < 0)
            errn_exit (np_rerror (), "np_trans_write");
        free(tc);
    }
    msg ("sent 100 Tfsyncs");

    /* flush one fsync */
    if (!(ac = np_create_tflush (flushtag)))
        msg_exit ("out of memory");
    tag = npc_get_id(fs->tagpool);
    np_set_tag(ac, tag);
    if (np_trans_send(fs->trans, ac) < 0)
        errn_exit (np_rerror (), "np_trans_write");
    free (ac);
    msg ("sent 1 Tflush");

    for (i = 0; i < 100; i++) {
        if (!(tc = np_create_tfsync (f->fid)))
            msg_exit ("out of memory");
        tag = npc_get_id(fs->tagpool);
        np_set_tag(tc, tag);
        n = np_trans_send(fs->trans, tc);
        if (n < 0)
            errn_exit (np_rerror (), "np_trans_write");
        free(tc);
    }
    msg ("sent 100 Tfsyncs");
        
    for (i = 0; i < 200 + orig_reply_received; i++) {
        const int size = sizeof(rc->size)+sizeof(rc->type)+sizeof(rc->tag);

        assert (size <= fs->msize);

        /* Trick: both rfsync and rflush are the same size (empty).
         * Read exactly that much.  Code bloats if we can't assume that.
         */
        if (np_trans_recv (fs->trans, &rc, size) < 0)
            errn_exit (np_rerror (), "np_trans_read");
        if (rc == NULL)
            msg_exit ("np_trans_read: unexpected EOF");
        if (!np_deserialize (rc))
            msg_exit ("failed to deserialize response");
        switch (rc->type) {
            case P9_RFSYNC:
                if (rc->tag == flushtag) {
                    assert (orig_reply_received == 0);
                    if (rflush_received)
                        msg_exit ("received Rfsync after Rflush");
                    orig_reply_received = 1;
                }
                break;
            case P9_RFLUSH:
                rflush_received = 1;
                break;
            default:
                msg_exit ("received unexpected reply type (%d)", rc->type);
                break;
        }
        free(rc);
    }

    msg ("received 1 Rflush");
    msg ("received either 199 or 200 Rfsyncs");

    (void)npc_clunk (f);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
