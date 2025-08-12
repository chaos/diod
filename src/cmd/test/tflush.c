/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tflush.c - issue 100 requests, flush one, read the responses */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <getopt.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"
#include "src/libnpclient/npcimpl.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_auth.h"
#include "src/libdiod/diod_sock.h"
#include "src/libdiod/diod_auth.h"

static void flush_series (Npcfsys *fs, Npcfid *root);

static void usage (void)
{
    fprintf (stderr, "Usage: tflush [OPTIONS]\n"
"   -a,--aname NAME       file system (default ctl)\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -m,--msize            msize (default 65536)\n"
"   -u,--uid              authenticate as uid (default is your euid)\n"
"   -t,--timeout SECS     give up after specified seconds\n"
"   -p,--privport         connect from a privileged port (root user only)\n"
);
    exit (1);
}

static const char *options = "a:s:m:u:p";

static const struct option longopts[] = {
    {"aname",   required_argument,      0, 'a'},
    {"server",  required_argument,      0, 's'},
    {"msize",   required_argument,      0, 'm'},
    {"uid",     required_argument,      0, 'u'},
    {"privport",no_argument,            0, 'p'},
    {0, 0, 0, 0},
};

int main (int argc, char *argv[])
{
    char *aname = NULL;
    char *server = NULL;
    int msize = 65536;
    uid_t uid = geteuid ();
    int flags = 0;
    int server_fd;
    bool not_my_serverfd = false;
    Npcfsys *fs;
    Npcfid *afid, *root;
    int c;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
        switch (c) {
            case 'a':   /* --aname NAME */
                aname = optarg;
                break;
            case 's':   /* --server HOST[:PORT] or /path/to/socket */
                server = optarg;
                break;
            case 'm':   /* --msize SIZE */
                msize = strtoul (optarg, NULL, 10);
                break;
            case 'u':   /* --uid UID */
                uid = strtoul (optarg, NULL, 10);
                break;
            case 'p':   /* --privport */
                flags |= DIOD_SOCK_PRIVPORT;
                break;
            default:
                usage ();
        }
    }
    if (optind != argc)
        usage ();

    const char *s = getenv ("DIOD_SERVER_FD");
    if (server || !s) {
        server_fd = diod_sock_connect (server, flags);
        if (server_fd < 0)
            exit (1);
    }
    else {
        server_fd = strtoul (s, NULL, 10);
        not_my_serverfd = true;
    }
    if (!aname)
        aname = getenv ("DIOD_SERVER_ANAME");

    if (!(fs = npc_start (server_fd, server_fd, msize, 0)))
        errn_exit (np_rerror (), "start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn_exit (np_rerror (), "npc_clunk afid");

    flush_series (fs, root);

    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    if (!not_my_serverfd)
        close (server_fd);

    diod_log_fini ();

    exit (0);
}

static void flush_series (Npcfsys *fs, Npcfid *root)
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
        // alternate with datasync set to 1 or 0
        if (!(tc = np_create_tfsync (f->fid, i % 2 == 0 ? 0 : 1)))
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
        if (!(tc = np_create_tfsync (f->fid, i % 2 == 0 ? 0 : 1)))
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
            case Rfsync:
                if (rc->tag == flushtag) {
                    assert (orig_reply_received == 0);
                    if (rflush_received)
                        msg_exit ("received Rfsync after Rflush");
                    orig_reply_received = 1;
                }
                break;
            case Rflush:
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
