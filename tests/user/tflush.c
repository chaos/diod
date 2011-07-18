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
#include <signal.h>

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

    if (!(fs = npc_start (fd, 8192+24, 0)))
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
_alarm_clock (int sig)
{
    //msg ("alarm clock!");
}

static void
_flush_series (Npcfsys *fs, Npcfid *root)
{
    Npfcall *rc = NULL, *tc = NULL, *ac = NULL;
    Npcfid *f;
    u16 tag, flushtag;
    int n, i;
    struct sigaction sa;

    assert (fs->trans != NULL);

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = _alarm_clock;
    if (sigaction (SIGALRM, &sa, NULL) < 0)
        err_exit ("sigaction");

    if (!(f = npc_open_bypath (root, "/", 0644)))
        err_exit ("open)");

    /* write 100 fsyncs */
    for (i = 0; i < 100; i++) {
        if (!(tc = np_create_tfsync (f->fid)))
            msg_exit ("out of memory");
        flushtag = tag = npc_get_id(fs->tagpool);
        np_set_tag(tc, tag);
        n = np_trans_send(fs->trans, tc);
        if (n < 0)
            errn_exit (np_rerror (), "np_trans_write");
        //msg ("sent tversion tag %d", tc->tag);
        free(tc);
    }
    msg ("sent 100 tfsyncs");

    /* flush the most recent */
    if (!(ac = np_create_tflush (flushtag)))
        msg_exit ("out of memory");
    tag = npc_get_id(fs->tagpool);
    np_set_tag(ac, tag);
    if (np_trans_send(fs->trans, ac) < 0)
        errn_exit (np_rerror (), "np_trans_write");
    //msg ("sent tflush tag %d (flushing tag %d)", ac->tag, flushtag);
    free (ac);
    msg ("sent 1 tflush");
        
    /* receive up to 101 responses with 1s timeout */
    for (i = 0; i < 101; i++) {
        /* Trick: both rfsync and rflush are the same size (empty).
         * Read exactly that much.  Code bloats if we can't assume that.
         */
        int size = sizeof(rc->size)+sizeof(rc->type)+sizeof(rc->tag);
        assert (size <= fs->msize);
        alarm (1);
        if (np_trans_recv (fs->trans, &rc, size) < 0) {
            if (errno == EINTR)
                break;
            errn_exit (np_rerror (), "np_trans_read");
        }
        alarm (0);
        if (rc == NULL)
            msg_exit ("np_trans_read: unexpected EOF");
        if (!np_deserialize (rc))
            msg_exit ("failed to deserialize response in one go");
        if (rc->type != P9_RFSYNC && rc->type != P9_RFLUSH)
            msg_exit ("received unexpected reply type (%d)", rc->type);
        //msg ("received tag %d", rc->tag);
        free(rc);
        //npc_put_id(fs->tagpool, rc->tag);
    }
    if (i == 100 || i == 101)
        msg ("received 100/101 respones");
    else 
        msg ("received %d responses", i);

    (void)npc_clunk (f);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
