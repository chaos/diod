/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* diodcat.c - cat remote files */

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
#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_sock.h"
#include "src/libdiod/diod_auth.h"

#define OPTIONS "a:s:m:u:t:p"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"aname",   required_argument,      0, 'a'},
    {"server",  required_argument,      0, 's'},
    {"msize",   required_argument,      0, 'm'},
    {"uid",     required_argument,      0, 'u'},
    {"timeout", required_argument,      0, 't'},
    {"privport",no_argument,            0, 'p'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static int catfiles (int fd, uid_t uid, int msize, char *aname,
                     char **av, int ac);
static void sigalarm (int arg);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodcat [OPTIONS] [-s HOST[:PORT]] [-a aname] [file [file...]]\n"
"   -a,--aname NAME       file system (default ctl)\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -m,--msize            msize (default 65536)\n"
"   -u,--uid              authenticate as uid (default is your euid)\n"
"   -t,--timeout SECS     give up after specified seconds\n"
"   -p,--privport         connect from a privileged port (root user only)\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *aname = NULL;
    char *server = NULL;
    int msize = 65536;
    uid_t uid = geteuid ();
    int topt = 0;
    int flags = 0;
    int fd, c;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
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
            case 't':   /* --timeout SECS */
                topt = strtoul (optarg, NULL, 10);
                break;
            case 'p':   /* --privport */
                flags |= DIOD_SOCK_PRIVPORT;
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

    if (!aname)
        aname = "ctl";
    if (catfiles (fd, uid, msize, aname, argv + optind, argc - optind) < 0)
        exit (1);

    close (fd);

    diod_log_fini ();

    exit (0);
}

static void sigalarm (int arg)
{
    msg_exit ("timed out");
}

static int
cat9 (Npcfid *root, char *path)
{
    Npcfid *fid = NULL;
    char *buf = NULL;
    int n, m, done;
    int ret = -1;

    if (!(fid = npc_open_bypath (root, path, O_RDONLY))) {
        errn (np_rerror (), "open %s", path);
        goto done;
    }
    NP_ASSERT (fid->iounit > 0); /* libnpclient invariant */
    if (!(buf = malloc (fid->iounit))) {
        msg ("out of memory");
        goto done;
    }
    while ((n = npc_read (fid, buf, fid->iounit)) > 0) {
        done = 0;
        do {
            if ((m = write (1, buf + done, n - done)) < 0) {
                if (errno != EPIPE)
                    err ("stdout");
                goto done;
            }
            done += m;
        } while (done < n);
    }
    if (n < 0) {
        errn (np_rerror (), "read %s", path);
        goto done;
    }
    ret = 0;
done:
    if (buf)
        free (buf);
    if (fid && npc_clunk (fid) < 0)
        errn (np_rerror (), "clunk %s", path);
    return ret;
}

static int
catfiles (int fd, uid_t uid, int msize, char *aname, char **av, int ac)
{
    Npcfsys *fs = NULL;
    Npcfid *afid = NULL, *root = NULL;
    int i, ret = -1;

    if (!(fs = npc_start (fd, fd, msize, 0))) {
        errn (np_rerror (), "error negotiating protocol with server");
        goto done;
    }
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0) {
        errn (np_rerror (), "error authenticating to server");
        goto done;
    }
    if (!(root = npc_attach (fs, afid, aname, uid))) {
        errn (np_rerror (), "error attaching to aname='%s'", aname ? aname : "");
        goto done;
    }
    if (ac == 0) {
        if (cat9 (root, "version") < 0)
            goto done;
    } else {
        for (i = 0; i < ac; i++) {
            if (cat9 (root, av[i]) < 0)
                goto done;
        }
    }
    ret = 0;
done:
    if (root && npc_clunk (root) < 0) {
        errn (np_rerror (), "error clunking %s", aname);
        ret = -1;
    }
    if (afid && npc_clunk (afid) < 0) {
        errn (np_rerror (), "error clunking afid");
        ret = -1;
    }
    if (fs)
        npc_finish (fs);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
