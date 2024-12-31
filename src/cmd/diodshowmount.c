/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* diodshowmount.c - cat ctl:connections */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <libgen.h>
#include <signal.h>

#include "src/libnpfs/9p.h"
#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/liblsd/list.h"
#include "src/liblsd/hostlist.h"
#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_sock.h"
#include "src/libdiod/diod_auth.h"

#define OPTIONS "s:m:u:t:l"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"server",  required_argument,      0, 's'},
    {"msize",   required_argument,      0, 'm'},
    {"uid",     required_argument,      0, 'u'},
    {"timeout", required_argument,      0, 't'},
    {"long",    no_argument,            0, 'l'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void sigalarm (int arg);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodshowmount [OPTIONS] [-s HOST[:PORT]]\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -m,--msize            msize (default 65536)\n"
"   -u,--uid              authenticate as uid (default is your euid)\n"
"   -t,--timeout SECS     give up after specified seconds\n"
"   -l,--long             list clients one per line and don't strip domain\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *server = NULL;
    int msize = 65536;
    uid_t uid = geteuid ();
    int topt = 0;
    Npcfsys *fs = NULL;
    Npcfid *fid, *afid = NULL, *root;
    int c, fd;
    char buf[80], *host, *p;
    hostlist_t hl;
    hostlist_iterator_t itr;
    int lopt = 0;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 's':   /* --server HOST[:PORT] or /path/to/socket */
                server = optarg;
                break;
            case 'm':   /* --msize SIZE */
                msize = strtoul (optarg, NULL, 10);
                break;
            case 'u':   /* --uid UID */
                uid = strtoul (optarg, NULL, 10);
                break;
            case 't':   /* --timeout SECS */
                topt = strtoul (optarg, NULL, 10);
                break;
            case 'l':   /* --long */
                lopt = 1;
                break;
            default:
                usage ();
        }
    }

    if (signal (SIGPIPE, SIG_IGN) == SIG_ERR)
        err_exit ("signal");
    if (signal (SIGALRM, sigalarm) == SIG_ERR)
        err_exit ("signal");

    if (topt > 0)
        alarm (topt);

    if ((fd = diod_sock_connect (server, 0)) < 0)
        exit (1);

    if (!(fs = npc_start (fd, fd, msize, 0)))
        errn_exit (np_rerror (), "error negotiating protocol with server");
    if (!(afid = npc_auth (fs, "ctl", uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "error authenticating to server");
    if (!(root = npc_attach (fs, afid, "ctl", uid)))
        errn_exit (np_rerror (), "error attaching to aname=ctl");
    if (!(fid = npc_open_bypath (root, "connections", O_RDONLY)))
        errn_exit (np_rerror (), "open connections");

    if (!(hl = hostlist_create (NULL)))
        err_exit ("hostlist_create");
    while (npc_gets (fid, buf, sizeof(buf))) {
        if ((p = strchr (buf, ' ')))
            *p = '\0';
        if (!lopt && (p = strchr (buf, '.')))
            *p = '\0';
        if (!hostlist_push_host (hl, buf))
            err_exit ("hostlist_push_host");
    }
    hostlist_uniq (hl);
    if (lopt) {
        if (!(itr = hostlist_iterator_create (hl)))
            err_exit ("hostlist_iterator_create");
        while ((host = hostlist_next (itr)))
            printf ("%s\n", host);
        hostlist_iterator_destroy (itr);
    } else {
        char s[1024];

        if (hostlist_ranged_string (hl, sizeof (s), s) < 0)
            msg_exit ("hostlist output would be too long (use -l)");
        printf ("%s\n", s);
    }
    hostlist_destroy (hl);

    if (npc_clunk (fid) < 0)
        errn_exit (np_rerror (), "clunk connections");
    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "error clunking ctl");
    if (afid && npc_clunk (afid) < 0)
        errn_exit (np_rerror (), "error clunking afid");
    npc_finish (fs);

    exit(0);
}

static void sigalarm (int arg)
{
    msg_exit ("timed out");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
