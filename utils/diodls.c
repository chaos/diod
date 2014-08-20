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

/* diodls.c - list remote files */

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
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "list.h"
#include "diod_log.h"
#include "diod_sock.h"
#include "diod_auth.h"

#define OPTIONS "a:s:m:u:t:lp"
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

static int lsfiles (int fd, uid_t uid, int msize, char *aname, int lopt,
                    char **av, int ac);
static void sigalarm (int arg);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodcat [OPTIONS] [-s HOST[:PORT]] [-a aname] [file [file...]]\n"
"   -a,--aname NAME       file system (default ctl)\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -l,--long             show stat information too\n"
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
    int lopt = 0;
    int fd, c;
    int flags = 0;

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
            case 'l':   /* --long */
                lopt = 1;
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
    if (lsfiles (fd, uid, msize, aname, lopt, argv + optind, argc - optind) < 0)
        exit (1);

    close (fd);

    diod_log_fini ();

    exit (0);
}

static void sigalarm (int arg)
{
    msg_exit ("timed out");
}

static char *
mode2str (mode_t mode)
{
    static char s[16];

    /* FIXME: represent other file types, sticky bits */
    s[0] = S_ISDIR(mode) ? 'd' : '-';

    s[1] = mode & S_IRUSR ? 'r' : '-';
    s[2] = mode & S_IWUSR ? 'w' : '-';
    s[3] = mode & S_IXUSR ? 'x' : '-';

    s[4] = mode & S_IRGRP ? 'r' : '-';
    s[5] = mode & S_IWGRP ? 'w' : '-';
    s[6] = mode & S_IXGRP ? 'x' : '-';

    s[7] = mode & S_IROTH ? 'r' : '-';
    s[8] = mode & S_IWOTH ? 'w' : '-';
    s[9] = mode & S_IXOTH ? 'x' : '-';

    s[10] = '.';
    s[11] = '\0';

    return s;
}

static void
lsfile_l (Npcfid *dir, char *name)
{
    Npcfid *fid = NULL;
    struct stat sb;
    struct passwd *pw;
    struct group *gr;
    char uid[16], gid[16];
    char *mtime;

    if (!(fid = npc_walk (dir, name))) {
        errn (np_rerror (), "npc_walk %s\n", name);
        goto error;
    }
    if (npc_fstat (fid, &sb) < 0) {
        errn (np_rerror (), "npc_fstat %s\n", name);
        goto error;
    }
    if (npc_clunk (fid) < 0) {
        errn (np_rerror (), "npc_clunk %s\n", name);
        goto error;
    }
    if (!(pw = getpwuid (sb.st_uid)))
        snprintf (uid, sizeof (uid), "%d", sb.st_uid);
    if (!(gr = getgrgid (sb.st_gid)))
        snprintf (gid, sizeof (gid), "%d", sb.st_gid);
    mtime = ctime( &sb.st_mtime);
    printf ("%10s %4lu %s %s %12lu %.*s %s\n",
            mode2str (sb.st_mode),
            sb.st_nlink,
            pw ? pw->pw_name : uid,
            gr ? gr->gr_name : gid,
            sb.st_size,
            (int)strlen (mtime) - 13, mtime + 4,
            name);
    return;
error:
    if (fid)
        (void)npc_clunk (fid);
}

static int
lsdir (int i, int count, Npcfid *root, int lopt, char *path)
{
    Npcfid *dir = NULL;
    struct dirent d, *dp;

    if (!(dir = npc_opendir (root, path))) {
        if (np_rerror () == ENOTDIR) {
            if (lopt)
                lsfile_l (root, path);
            else
                printf ("%s\n", path);
            return 0;
        }
        errn (np_rerror (), "%s", path);
        goto error;
    }
    if (count > 1)
        printf ("%s:\n", path);
    do { 
        if ((npc_readdir_r (dir, &d, &dp)) > 0) {
            errn (np_rerror (), "npc_readdir: %s", path);
            goto error;
        }
        if (dp) {
            if (lopt)
                lsfile_l (dir, dp->d_name);
            else if (strcmp (dp->d_name, ".") && strcmp (dp->d_name, ".."))
                printf ("%s\n", dp->d_name);
        }
    } while (dp != NULL);
    if (npc_clunk (dir) < 0) {
        errn (np_rerror (), "npc_clunk: %s", path);
        goto error;
    }
    if (count > 1 && i < count - 1)
        printf ("\n");
    return 0;
error:
    if (dir)
        (void)npc_clunk (dir);
    return -1;
}

static int
lsfiles (int fd, uid_t uid, int msize, char *aname, int lopt, char **av, int ac)
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
        if (lsdir(1, 1, root, lopt, NULL) < 0)
            goto done;
    } else {
        for (i = 0; i < ac; i++) {
            if (lsdir (i, ac, root, lopt, av[i]) < 0)
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
