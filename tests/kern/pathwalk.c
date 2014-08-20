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

/* pathwalk.c - time path walk */

#if HAVE_CONFIG_H
#include "config.h"
#else
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <assert.h>
#include <libgen.h>

#include "diod_log.h"

#define OPTIONS "l:f:crtCFq"

#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"length",          required_argument,  0, 'l'},
    {"files",           required_argument,  0, 'f'},
    {"create",          no_argument,        0, 'c'},
    {"remove",          no_argument,        0, 'r'},
    {"test",            no_argument,        0, 't'},
    {"cacheonly",       no_argument,        0, 'C'},
    {"failsearch",      no_argument,        0, 'F'},
    {"quiet",           no_argument,        0, 'q'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static char **searchpath_create(char *root, int length);
static void searchpath_destroy(char **searchpath, int length);

static void fsobj_create (char **searchpath, int length, int files);
static void fsobj_destroy (char **searchpath, int length, int files);

static int pathwalk (char **searchpath, int length, int files, int target);

static void
usage(void)
{
    fprintf (stderr,
"Usage: pathwalk root-path [-c|-r|-t] [OPTIONS]\n"
"   -l,--length N          set number of directories to search dflt: 16)\n"
"   -f,--files N           set number of files to search for (dflt: 10000)\n"
"   -c,--create            create files and directories\n"
"   -r,--remove            remove files and directories\n"
"   -t,--test              run test\n"
"   -C,--primecache        run one path search before starting timing\n"
"   -F,--failsearch        arrange for search to be unsuccessful\n"
"   -q,--quiet             suppress timing messages\n"
    );
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *root = NULL;
    int length = 16;
    int files = 10000;
    int cflag = 0;
    int rflag = 0;
    int tflag = 0;
    int Cflag = 0;
    int Fflag = 0;
    int qflag = 0;
    int c;
    char **searchpath;
    struct timeval t1, t2, elapsed;
    int target, count;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'l':   /* --length N */
                length = strtoul (optarg, NULL, 10); 
                break;
            case 'f':   /* --files N */
                files = strtoul (optarg, NULL, 10); 
                break;
            case 'c':   /* --create */
                cflag = 1;
                break;
            case 'r':   /* --remove */
                rflag = 1;
                break;
            case 't':   /* --test */
                tflag = 1;
                break;
            case 'C':   /* --primecache */
                Cflag = 1;
                break;
            case 'F':   /* --failsearch */
                Fflag = 1;
                break;
            case 'q':   /* --quiet */
                qflag = 1;
                break;
            default:
                usage ();
                /*NOTREACHED*/
        }
    }
    if (optind != argc - 1 || (!cflag && !rflag && !tflag))
        usage ();
    root = argv[optind++];

    searchpath = searchpath_create (root, length);

    target = Fflag ? -1 : length - 1;

    /* Create root + fs objects.
     */
    if (cflag) {
        if (gettimeofday (&t1, NULL) < 0)
            err_exit ("gettimeofday");
        if (mkdir (root, 0777) < 0)
            err_exit ("mkdir %s", root);
        fsobj_create (searchpath, length, files);
        if (gettimeofday (&t2, NULL) < 0)
            err_exit ("gettimeofday");
        timersub (&t2, &t1, &elapsed);
        if (!qflag)
            msg ("Created %d objects in %lu.%.3lus", length * files + 1,
                 elapsed.tv_sec, elapsed.tv_usec / 1000);
    }

    /* Test
     */
    if (tflag) {
        if (Cflag) {
            if (gettimeofday (&t1, NULL) < 0)
                err_exit ("gettimeofday");
            count = pathwalk (searchpath, length, 1, target);
            if (gettimeofday (&t2, NULL) < 0)
                err_exit ("gettimeofday");
            timersub (&t2, &t1, &elapsed);
            if (!qflag)
                msg ("Found %d/%d files in %d directories in %lu.%.3lus",
                     count, 1, length,
                     elapsed.tv_sec, elapsed.tv_usec / 1000);
        }
        if (gettimeofday (&t1, NULL) < 0)
            err_exit ("gettimeofday");
        count = pathwalk (searchpath, length, files, target);
        if (gettimeofday (&t2, NULL) < 0)
            err_exit ("gettimeofday");
        timersub (&t2, &t1, &elapsed);
        if (!qflag)
            msg ("Found %d/%d files in %d directories in %lu.%.3lus",
                 count, files, length, elapsed.tv_sec, elapsed.tv_usec / 1000);
    }

    /* Remove root + fs objects.
     */
    if (rflag) {
        if (gettimeofday (&t1, NULL) < 0)
            err_exit ("gettimeofday");
        fsobj_destroy (searchpath, length, files);
        if (rmdir (root) < 0)
            err_exit ("rmdir %s", root);
        if (gettimeofday (&t2, NULL) < 0)
            err_exit ("gettimeofday");
        timersub (&t2, &t1, &elapsed);
        if (!qflag)
            msg ("Removed %d objects in %lu.%.3lus", length * files + 1,
                 elapsed.tv_sec, elapsed.tv_usec / 1000);
    }

    searchpath_destroy (searchpath, length);

    exit (0);
}

/* Return index of searchpath in which 'name' was found or -1 if not found.
 */
static int
_search (char **searchpath, int length, char *name)
{
    char path[PATH_MAX + 1];
    struct stat sb;
    int i;

    for (i = 0; i < length; i++) {
        snprintf (path, sizeof (path), "%s/%s", searchpath[i], name);
        if (stat (path, &sb) == 0)
            break;
    }
    return i < length ? i : -1;
}

/* Search for a file that will be found in the last directory of searchpath.
 */
static int
pathwalk (char **searchpath, int length, int files, int target)
{
    char name[PATH_MAX + 1];
    int i, found;
    int count = 0;

    for (i = 0; i < files; i++) {
        snprintf (name, sizeof (name), "%d.%d", target, i);
        found = _search (searchpath, length, name);
        assert (found == target);
        if (found != -1)
            count++;
    }
    return count;
}

static void
searchpath_destroy(char **searchpath, int length)
{
    int i;

    for (i = 0; i < length; i++)
        free (searchpath[i]);
    free (searchpath);
}

static char **
searchpath_create (char *root, int length)
{
    char path[PATH_MAX + 1];
    char **searchpath;
    int i;

    if (!(searchpath = malloc (sizeof (char *) * length)))
        errn_exit (ENOMEM, "malloc searchpath");
    for (i = 0; i < length; i++) {
        snprintf (path, sizeof (path), "%s/%d", root, i);
        if (!(searchpath[i] = strdup (path)))
            errn_exit (ENOMEM, "strdup searchpath");
    }
    return searchpath;
}

static void
fsobj_create (char **searchpath, int length, int files)
{
    char path[PATH_MAX + 1];
    int i, j, fd;

    for (i = 0; i < length; i++) {
        if (mkdir (searchpath[i], 0777) < 0)
            err_exit ("mkdir %s", searchpath[i]);
        for (j = 0; j < files; j++) {
            snprintf (path, sizeof (path), "%s/%d.%d", searchpath[i], i, j);
            if ((fd = creat (path, 0644)) < 0) 
                err_exit ("creat %s", path);
            if (close (fd) < 0)
                err_exit ("close %s", path);
        }
    }
}

static void
fsobj_destroy (char **searchpath, int length, int files)
{
    char path[PATH_MAX + 1];
    int i, j;

    for (i = 0; i < length; i++) {
        for (j = 0; j < files; j++) {
            snprintf (path, sizeof (path), "%s/%d.%d", searchpath[i], i, j);
            if ((unlink (path)) < 0) 
                err_exit ("unlink %s", path);
        }
        if (rmdir (searchpath[i]) < 0)
            err_exit ("mkdir %s", searchpath[i]);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

