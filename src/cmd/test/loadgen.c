/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* loadgen.c - load generator for diod */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <libgen.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/liblsd/list.h"
#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_sock.h"
#include "src/libdiod/diod_auth.h"

static const char *options = "s:m:n:r:g";

static const struct option longopts[] = {
    {"server",     required_argument,      0, 's'},
    {"msize",      required_argument,      0, 'm'},
    {"numthreads", required_argument,      0, 'n'},
    {"runtime",    required_argument,      0, 'r'},
    {"getattr",    no_argument,            0, 'g'},
    {0, 0, 0, 0},
};

typedef enum { LOAD_IO, LOAD_GETATTR } load_t;

typedef struct {
    pthread_t t;
    time_t stoptime;
    char *server;
    int msize;
    int fd;
    uint64_t readbytes;
    uint64_t writebytes;
    uint64_t opcount;
    char *buf;
    Npcfsys *fs;
    Npcfid *afid;
    Npcfid *root;
    Npcfid *infile;
    Npcfid *outfile;
    load_t loadtype;
} thd_t;


static void *loadgen (void *arg);

static void
usage (void)
{
    fprintf (stderr,
"Usage: loadgen [OPTIONS]\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -m,--msize            msize (default 65536)\n"
"   -n,--numthreads       specify thread count (default 16)\n"
"   -r,--runtime          specify runtime in seconds (default 10)\n"
"   -g,--getattr          issue getattrs instead of read/write\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *server = NULL;
    int msize = 65536;
    int numthreads = 16;
    int runtime = 10;
    int i, c, err;
    time_t now = time (NULL);
    thd_t *t;
    uint64_t readbytes = 0, writebytes = 0, opcount = 0;
    load_t loadtype = LOAD_IO;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
        switch (c) {
            case 's':   /* --server HOST[:PORT] or /path/to/socket */
                server = optarg;
                break;
            case 'm':   /* --msize SIZE */
                msize = strtoul (optarg, NULL, 10);
                break;
            case 'n':   /* --numthreads INT */
                numthreads = strtoul (optarg, NULL, 10);
                break;
            case 'r':   /* --runtime INT */
                runtime = strtoul (optarg, NULL, 10);
                break;
            case 'g':   /* --getattr */
                loadtype = LOAD_GETATTR;
                break;
            default:
                usage ();
        }
    }

    if (signal (SIGPIPE, SIG_IGN) == SIG_ERR)
        err_exit ("signal");

    if (!(t = malloc (sizeof (thd_t) * numthreads)))
        msg_exit ("out of memory");

    for (i = 0; i < numthreads; i++) {
        t[i].server = server;
        t[i].msize = msize;
        t[i].stoptime = now + runtime;
        t[i].fd = -1;
        t[i].fs = NULL;
        t[i].root = t[i].afid = t[i].infile = t[i].outfile = NULL;
        t[i].readbytes = t[i].writebytes = 0;
        t[i].loadtype = loadtype;
        if (!(t[i].buf = malloc (msize)))
            msg_exit ("out of memory");
        if ((err = pthread_create (&t[i].t, NULL, loadgen, &t[i])))
            errn_exit (err, "pthread_create");
    }

    for (i = 0; i < numthreads; i++) {
        if ((err = pthread_join (t[i].t, NULL)))
            errn_exit (err, "pthread_join");
        readbytes += t[i].readbytes;
        writebytes += t[i].writebytes;
        opcount += t[i].opcount;
        free (t[i].buf);
    }
    free (t);

    msg ("%"PRIu64" ops/s, %"PRIu64" rMB/s, %"PRIu64" wMB/s",
        opcount/runtime,
        readbytes/(1024*1024*runtime), writebytes/(1024*1024*runtime));

    diod_log_fini ();

    exit (0);
}

static void *
loadgen (void *arg)
{
    thd_t *t = (thd_t *)arg;
    uid_t uid = geteuid ();
    void *ret = NULL;
    int n, loops = 0;

    if ((t->fd = diod_sock_connect (t->server, 0)) < 0)
        goto done;
    if (!(t->fs = npc_start (t->fd, t->fd, t->msize, 0))) {
        errn (np_rerror (), "error negotiating protocol with server");
        goto done;
    }
    if (!(t->afid = npc_auth (t->fs, "ctl", uid, diod_auth))
                                                && np_rerror () != 0) {
        errn (np_rerror (), "error authenticating to server");
        goto done;
    }
    if (!(t->root = npc_attach (t->fs, t->afid, "ctl", uid))) {
        errn (np_rerror (), "error attaching to aname=ctl");
        goto done;
    }
    if (t->loadtype == LOAD_IO) {
        if (!(t->infile = npc_open_bypath (t->root, "zero", O_RDONLY))) {
            errn (np_rerror (), "open zero");
            goto done;
        }
        if (!(t->outfile = npc_open_bypath (t->root, "null", O_WRONLY))) {
            errn (np_rerror (), "open null");
            goto done;
        }
        do {
            if ((n = npc_pread (t->infile, t->buf, t->msize, 0)) <= 0) {
                errn (np_rerror (), "read zero");
                break;
            }
            t->readbytes += n;
            if ((n = npc_pwrite (t->outfile, t->buf, t->msize, 0)) < 0) {
                errn (np_rerror (), "write null");
                break;
            }
            t->writebytes += n;
            t->opcount++;
            loops++;
        } while ((loops % 100 != 0) || time (NULL) < t->stoptime);
    } else if (t->loadtype == LOAD_GETATTR) {
        if (!(t->infile = npc_walk (t->root, "null"))) {
            errn (np_rerror (), "walk null");
            goto done;
        }
        do {
            struct stat sb;

            if (npc_fstat (t->infile, &sb) < 0) {
                errn (np_rerror (), "walk null");
                break;
            }
            t->opcount++;
            loops++;
        } while ((loops % 100 != 0) || time (NULL) < t->stoptime);
    }
done:
    if (t->outfile && npc_clunk (t->outfile) < 0)
        errn (np_rerror (), "error clunking null");
    if (t->infile && npc_clunk (t->infile) < 0)
        errn (np_rerror (), "error clunking zero");
    if (t->root && npc_clunk (t->root) < 0)
        errn (np_rerror (), "error clunking ctl");
    if (t->afid && npc_clunk (t->afid) < 0)
        errn (np_rerror (), "error clunking afid");
    if (t->fs) {
        npc_finish (t->fs); /* closes fd */
        t->fd = -1;
    }
    if (t->fd != -1)
        close (t->fd);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
