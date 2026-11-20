/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* authattach.c - attach to a diod server with chosen auth behavior
 *
 * Usage: test_authattach [OPTIONS] STEP [STEP ...]
 *
 * Each STEP attaches to the server on one connection, which is kept
 * open across steps, then stats the root and clunks it.  A step is
 * METHOD[:UID][@ANAME], where UID defaults to the effective uid, ANAME
 * defaults to --aname, and METHOD is
 *
 *   munge     Tauth, write a fresh MUNGE credential, Tattach with afid
 *   cred      Tauth, write the credential read from --cred FILE
 *   garbage   Tauth, write a string that is not a MUNGE credential
 *   empty     Tauth, write nothing
 *   none      Tattach with no afid
 *   tpools    attach to ctl with a fresh MUNGE credential and print the
 *             number of thread pools the server has
 *
 * Print "N: ok" or "N: <error>" for each step.  Exit 0 if every step
 * succeeded, otherwise 1.  This lets tests reach server auth paths
 * that diodcli and the kernel client cannot, such as an attach with
 * no afid, and a second attach on an already authenticated connection.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_auth.h"
#include "src/libdiod/diod_sock.h"

static void usage (void)
{
    fprintf (stderr,
"Usage: test_authattach [OPTIONS] METHOD[:UID][@ANAME] ...\n"
"   -a,--aname NAME       file system (default $DIOD_SERVER_ANAME)\n"
"   -s,--server ADDR      server (default $DIOD_SOCKET)\n"
"   -c,--cred FILE        credential for the cred method\n"
"METHOD is munge, cred, garbage, empty, none, or tpools\n");
    exit (1);
}

static const char *options = "a:s:c:";

static const struct option longopts[] = {
    {"aname",   required_argument,      0, 'a'},
    {"server",  required_argument,      0, 's'},
    {"cred",    required_argument,      0, 'c'},
    {0, 0, 0, 0},
};

static char *cred_str;

static int auth_cred (Npcfid *afid, u32 uid)
{
    if (!cred_str) {
        np_uerror (EINVAL);
        return -1;
    }
    return npc_puts (afid, cred_str) < 0 ? -1 : 0;
}

static int auth_garbage (Npcfid *afid, u32 uid)
{
    return npc_puts (afid, "MUNGE:this-is-not-a-credential:") < 0 ? -1 : 0;
}

static int auth_empty (Npcfid *afid, u32 uid)
{
    return 0;
}

static char *read_file (const char *path)
{
    FILE *f;
    char buf[4096];
    size_t n;

    if (!(f = fopen (path, "r")))
        err_exit ("%s", path);
    n = fread (buf, 1, sizeof (buf) - 1, f);
    fclose (f);
    buf[n] = '\0';
    buf[strcspn (buf, "\n")] = '\0';
    return strdup (buf);
}

/* Returns 0 on success, -1 with np_rerror() set on failure.
 */
static int step (Npcfsys *fs, char *aname, const char *method, u32 uid)
{
    AuthFun fun = NULL;
    Npcfid *afid = NULL;
    Npcfid *root = NULL;
    struct stat sb;
    int rc = -1;

    if (!strcmp (method, "munge"))
        fun = diod_auth;
    else if (!strcmp (method, "cred"))
        fun = auth_cred;
    else if (!strcmp (method, "garbage"))
        fun = auth_garbage;
    else if (!strcmp (method, "empty"))
        fun = auth_empty;
    else if (strcmp (method, "none") != 0)
        msg_exit ("unknown method: %s", method);

    np_uerror (0);
    if (fun) {
        /* npc_auth returns NULL with no error if the server answered
         * Tauth with ENOENT, meaning auth is not required.
         */
        if (!(afid = npc_auth (fs, aname, uid, fun)) && np_rerror () != 0)
            goto done;
    }
    if (!(root = npc_attach (fs, afid, aname, uid)))
        goto done;
    if (npc_fstat (root, &sb) < 0)
        goto done;
    rc = 0;
done:
    if (root) {
        int saved = np_rerror ();
        (void)npc_clunk (root);
        np_uerror (saved);
    }
    if (afid) {
        int saved = np_rerror ();
        (void)npc_clunk (afid);
        np_uerror (saved);
    }
    return rc;
}

/* Attach to ctl, read the tpools file, and print the number of lines,
 * one per thread pool.  Returns 0 on success, -1 with np_rerror() set
 * on failure.
 */
static int count_tpools (Npcfsys *fs, u32 uid, int n)
{
    Npcfid *afid, *root;
    char *s, *p;
    int count = 0;

    np_uerror (0);
    if (!(afid = npc_auth (fs, "ctl", uid, diod_auth)) && np_rerror () != 0)
        return -1;
    root = npc_attach (fs, afid, "ctl", uid);
    if (afid) {
        int saved = np_rerror ();
        (void)npc_clunk (afid);
        np_uerror (saved);
    }
    if (!root)
        return -1;
    s = npc_aget (root, "tpools");
    if (!s) {
        int saved = np_rerror ();
        (void)npc_clunk (root);
        np_uerror (saved);
        return -1;
    }
    for (p = s; *p; p++)
        if (*p == '\n')
            count++;
    free (s);
    (void)npc_clunk (root);
    printf ("%d: tpools=%d\n", n, count);
    return 0;
}

int main (int argc, char *argv[])
{
    char *aname = NULL;
    char *server = NULL;
    int server_fd;
    Npcfsys *fs;
    int c, i;
    int failed = 0;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
        switch (c) {
            case 'a':
                aname = optarg;
                break;
            case 's':
                server = optarg;
                break;
            case 'c':
                cred_str = read_file (optarg);
                break;
            default:
                usage ();
        }
    }
    if (optind == argc)
        usage ();
    if (!aname)
        aname = getenv ("DIOD_SERVER_ANAME");
    if (!server)
        server = getenv ("DIOD_SOCKET");
    if (!aname || !server)
        usage ();

    if ((server_fd = diod_sock_connect (server, 0)) < 0)
        exit (1);
    if (!(fs = npc_start (server_fd, server_fd, 65536, 0)))
        errn_exit (np_rerror (), "npc_start");

    for (i = optind; i < argc; i++) {
        char *method = strdup (argv[i]);
        char *p;
        u32 uid = geteuid ();

        char *step_aname = aname;

        if (!method)
            msg_exit ("out of memory");
        if ((p = strchr (method, '@'))) {
            *p++ = '\0';
            step_aname = p;
        }
        if ((p = strchr (method, ':'))) {
            *p++ = '\0';
            uid = strtoul (p, NULL, 10);
        }
        if (!strcmp (method, "tpools")) {
            if (count_tpools (fs, uid, i - optind + 1) < 0) {
                printf ("%d: %s\n", i - optind + 1, strerror (np_rerror ()));
                failed = 1;
            }
        }
        else if (step (fs, step_aname, method, uid) < 0) {
            printf ("%d: %s\n", i - optind + 1, strerror (np_rerror ()));
            failed = 1;
        }
        else
            printf ("%d: ok\n", i - optind + 1);
        fflush (stdout);
        free (method);
    }

    npc_finish (fs);
    close (server_fd);
    free (cred_str);
    diod_log_fini ();

    exit (failed);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
