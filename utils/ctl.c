/*****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see <http://code.google.com/p/diod/>.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/

/* ctl.c - manipulate diodctl pseudo-file system */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdint.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <libgen.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "list.h"
#include "diod_log.h"
#include "diod_upool.h"
#include "diod_sock.h"
#include "diod_auth.h"

#include "opt.h"
#include "ctl.h"

/* Trim leading and trailing whitespace from a string, then duplicate
 * what's left, if anything; else return NULL .
 * Exit on malloc failure.
 */
static char *
_strdup_trim (char *s)
{
    int n = strlen (s) - 1;
    char *cpy;

    while (n >= 0) {
        if (!isspace (s[n]))
            break;
        s[n--] = '\0';
    }
    while (*s && isspace (*s))
        s++;
    if (strlen (s) == 0)
        return NULL;
    if (!(cpy = strdup (s)))
        err_exit ("out of memory");
    return cpy;
}

/* Write jobid into ctl file, then read port number back.
 */
static char *
_getport (Npcfsys *fs, char *jobid)
{
    Npcfid *fid = NULL;
    char *port = NULL;
    char buf[64];

    if (!jobid)
        jobid = "nojob";
    if (!(fid = npc_open (fs, "ctl", O_RDWR))) {
        err ("ctl: open");
        goto error;
    }
    if (npc_puts (fid, jobid) < 0) {
        err ("ctl: write");
        goto error;
    }
    if (npc_lseek (fid, 0, SEEK_SET) < 0) {
        err ("ctl: seek");
        goto error;
    }
    if (!npc_gets (fid, buf, sizeof (buf))) {
        err ("ctl: read");
        goto error;
    }
    port = _strdup_trim (buf);
    if (port == NULL) {
        msg ("ctl: error reading port");
        goto error;
    }
    if (npc_close (fid) < 0) {
        err ("ctl: close");
        goto error;
    }
    return port;
error:
    if (fid)
        (void)npc_close (fid);
    if (port)
        free (port);
    return NULL;
}

/* Read export list from exports file.
 */
static List
_getexports (Npcfsys *fs)
{
    Npcfid *fid = NULL;
    List l = NULL;
    char buf[PATH_MAX];

    if (!(fid = npc_open (fs, "exports", O_RDONLY))) {
        err ("exports: open");
        goto error;
    }
    if (!(l = list_create((ListDelF)free)))
        err_exit ("out of memory");
    errno = 0;
    while (npc_gets (fid, buf, sizeof (buf))) {
        char *line = _strdup_trim (buf);

        if (line && !list_append (l, line))
            msg_exit ("out of memory");
    }
    if (errno) {
        err ("exports: read");
        goto error;
    }
    if (list_count (l) == 0) {
        msg ("exports: empty");
        goto error;
    }
    if (npc_close (fid) < 0) {
        err ("exports: close");
        goto error;
    }
    return l;
error:
    if (fid)
        (void)npc_close (fid);
    if (l)
        list_destroy (l);
    return NULL;
}

int
ctl_query (char *host, char *jobid, char **portp, List *exportsp)
{
    int fd;
    Npcfsys *fs = NULL;
    char *port = NULL;
    List exports = NULL;

    if ((fd = diod_sock_connect (host, "10005", 1, 0)) < 0) {
        err ("connect failed");
        goto error;
    }
    if (!(fs = npc_mount (fd, 8192, "/diodctl", diod_auth_client_handshake))) {
        err ("npc_mount");
        close (fd);
        goto error;
    }
    if (portp && !(port = _getport (fs, jobid)))
        goto error;
    if (exportsp && !(exports = _getexports (fs)))
        goto error;
    if (npc_umount (fs) < 0) {
        err ("umount");
        goto error;
    }
    if (portp)
        *portp = port;
    if (exportsp)
        *exportsp = exports;
    return 0;
error:
    if (fs)
        (void)npc_umount (fs);
    if (port)
        free (port);
    if (exports)
        list_destroy (exports);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
