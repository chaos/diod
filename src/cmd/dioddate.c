/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* dioddate.c - sync time with server */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
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

#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/liblsd/list.h"
#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_sock.h"
#include "src/libdiod/diod_auth.h"

static const char *options = "s:m:u:t:S";

static const struct option longopts[] = {
    {"server",  required_argument,      0, 's'},
    {"msize",   required_argument,      0, 'm'},
    {"uid",     required_argument,      0, 'u'},
    {"timeout", required_argument,      0, 't'},
    {"set-time",no_argument,            0, 'S'},
    {0, 0, 0, 0},
};

static void sigalarm (int arg);

static void
usage (void)
{
    fprintf (stderr,
"Usage: dioddate [OPTIONS] [-s HOST[:PORT]]\n"
"   -S,--set-time         set local time to match server's\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -m,--msize            msize (default 65536)\n"
"   -u,--uid              authenticate as uid (default is your euid)\n"
"   -t,--timeout SECS     give up after specified seconds\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *aname = "ctl";
    char *server = NULL;
    int msize = 65536;
    uid_t uid = geteuid ();
    int topt = 0;
    int Sopt = 0;
    int flags = 0;
    int fd, c;
    Npcfsys *fs = NULL;
    Npcfid *afid = NULL, *root = NULL;
    char *buf = NULL;
    struct timeval tv;
    struct timezone tz;

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
            case 'u':   /* --uid UID */
                uid = strtoul (optarg, NULL, 10);
                break;
            case 't':   /* --timeout SECS */
                topt = strtoul (optarg, NULL, 10);
                break;
            case 'S':   /* --set-time */
                Sopt = 1;
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

    if ((fd = diod_sock_connect (server, flags)) < 0)
        exit (1);

    if (!(fs = npc_start (fd, fd, msize, 0))) {
        errn (np_rerror (), "error negotiating protocol with server");
        goto done;
    }
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0) {
        errn (np_rerror (), "error authenticating to server");
        goto done;
    }
    if (!(root = npc_attach (fs, afid, aname, uid))) {
        errn (np_rerror (), "error attaching to aname='%s'", aname);
        goto done;
    }
    buf = npc_aget (root, "date");
    if (!buf) {
        errn (np_rerror (), "error reading date");
        goto done;
    }

    int64_t sec = 0, usec = 0;
    if (sscanf (buf, "%"SCNd64".%"SCNd64" %d.%d", &sec, &usec,
                                    &tz.tz_minuteswest, &tz.tz_dsttime) != 4) {
        msg ("error scanning returned date: %s", buf);
        goto done;
    }
    tv.tv_sec = sec;
    tv.tv_usec = usec;

    if (Sopt) {
        if (settimeofday (&tv, &tz) < 0)
            err_exit ("settimeofday");
    } else {
        time_t t = tv.tv_sec;
        printf ("%s", ctime (&t));
    }
done:
    if (buf)
        free (buf);
    if (root && npc_clunk (root) < 0)
        errn_exit (np_rerror (), "error clunking %s", aname);
    if (afid && npc_clunk (afid) < 0)
        errn_exit (np_rerror (), "error clunking afid");
    if (fs)
        npc_finish (fs);
    close (fd);

    diod_log_fini ();

    exit (0);
}

static void sigalarm (int arg)
{
    msg_exit ("timed out");
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
