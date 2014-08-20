/*****************************************************************************
 *  Copyright (C) 2010-14 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see http://code.google.com/p/diod.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also: http://www.gnu.org/licenses
 *****************************************************************************/

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

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "list.h"
#include "diod_log.h"
#include "diod_sock.h"
#include "diod_auth.h"

#define OPTIONS "s:m:u:t:S"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"server",  required_argument,      0, 's'},
    {"msize",   required_argument,      0, 'm'},
    {"uid",     required_argument,      0, 'u'},
    {"timeout", required_argument,      0, 't'},
    {"set-time",no_argument,            0, 'S'},
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
    if (sscanf (buf, "%lu.%lu %d.%d", &tv.tv_sec, &tv.tv_usec,
                                    &tz.tz_minuteswest, &tz.tz_dsttime) != 4) {
        msg ("error scanning returned date: %s", buf);
        goto done;
    }
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
