/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tattachmt.c - simulate multiple simultaneous mount requests
 *
 * When run with numthreads > 1 this selects multithreaded mtfsys
 * in libnpclient, which prevents multiple threads from accessing the
 * server fd simultaneously.  Unfortunately it has a bug that causes
 * the next thing run in test_under_diod() mode to fail with a
 * protocol error.  That needs to be run down.
 */

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
#include <sys/stat.h>
#include <pwd.h>

#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_auth.h"
#include "src/libdiod/diod_sock.h"
#include "src/libdiod/diod_auth.h"


typedef struct {
    Npcfsys *fs;
    uid_t uid;
    char *aname;
    int numgetattrs;
    int iterations;
    pthread_t t;
} thd_t;

void usage (void)
{
    fprintf (stderr, "Usage: tattach [OPTIONS] numusers numthreads numgetattrs iterations\n"
"   -a,--aname NAME       file system (default ctl)\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -m,--msize            msize (default 65536)\n"
"   -t,--timeout SECS     give up after specified seconds\n"
"   -p,--privport         connect from a privileged port (root user only)\n"
);
    exit (1);
}

static const char *options = "a:s:m:p";

static const struct option longopts[] = {
    {"aname",   required_argument,      0, 'a'},
    {"server",  required_argument,      0, 's'},
    {"msize",   required_argument,      0, 'm'},
    {"privport",no_argument,            0, 'p'},
    {0, 0, 0, 0},
};

/* attach to aname and issue a getattr,
 * similar to what a v9fs mount looks like to the server.
 */
void *
client (void *arg)
{
    thd_t *t = (thd_t *)arg;
    Npcfid *afid = NULL, *root;
    struct stat sb;
    int i, j;

    for (j = 0; j < t->iterations; j++) {
        if (!(afid = npc_auth (t->fs, t->aname, t->uid, diod_auth))
                            && np_rerror () != 0) {
            errn_exit (np_rerror (), "npc_auth");
        }
        if (!(root = npc_attach (t->fs, afid, t->aname, t->uid))) {
            errn_exit (np_rerror (), "npc_attach");
        }
        if (afid && npc_clunk (afid) < 0)
            errn_exit (np_rerror (), "npc_clunk afid");
        for (i = 0; i < t->numgetattrs; i++) {
            if (npc_fstat (root, &sb) < 0) {
                errn_exit (np_rerror (), "npc_getattr");
            }
        }
        if (npc_clunk (root) < 0) {
            errn_exit (np_rerror (), "npc_clunk root");
            goto done;
        }
    }
done:
    return NULL;
}

int
main (int argc, char *argv[])
{
    char *aname = NULL;
    char *server = NULL;
    int msize = 65536;
    int flags = 0;
    int server_fd;
    bool not_my_serverfd = false;
    Npcfsys *fs;
    int npcflags = 0;
    thd_t *t;
    int i, err, numthreads, numusers, numgetattrs, iterations;
    uid_t *uids;
    struct passwd *pw;
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
            case 'p':   /* --privport */
                flags |= DIOD_SOCK_PRIVPORT;
                break;
            default:
                usage ();
        }
    }
    if (argc - optind != 4)
        usage ();

    numusers = strtoul (argv[optind++], NULL, 10);
    numthreads = strtoul (argv[optind++], NULL, 10);
    numgetattrs = strtoul (argv[optind++], NULL, 10);
    iterations = strtoul (argv[optind++], NULL, 10);

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

    t = calloc (numthreads, sizeof (t[0]));
    uids = calloc (numusers, sizeof (uids[0]));
    if (!t || !uids)
        msg_exit ("out of memory");

    if (numusers > 1) {
        setpwent ();
        for (i = 0; i < numusers; i++) {
            if (!(pw = getpwent ()))
                msg_exit ("could not look up %d users", numusers);
            uids[i] = pw->pw_uid;
        }
        endpwent ();
    } else if (numusers == 1) {
        uids[0] = geteuid();
    } else {
        msg_exit ("numusers must be >= 1");
    }

    if (numthreads > 1)
        npcflags |= NPC_MULTI_RPC;
    if (!(fs = npc_start (server_fd, server_fd, msize, npcflags)))
        errn_exit (np_rerror (), "npc_start");

    for (i = 0; i < numthreads; i++) {
        t[i].fs = fs;
        t[i].uid = uids[i % numusers];
        t[i].aname = aname;
        t[i].numgetattrs = numgetattrs;
        t[i].iterations = iterations;
        err = pthread_create (&t[i].t, NULL, client, &t[i]);
        if (err)
            errn_exit (err, "pthread_create");
    }

    for (i = 0; i < numthreads; i++) {
        pthread_join (t[i].t, NULL);
    }

    npc_finish (fs);

    if (!not_my_serverfd)
        close (server_fd);

    free (t);
    free (uids);

    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
