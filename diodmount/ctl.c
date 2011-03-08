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
#include "util.h"
#include "ctl.h"

/* Allocate and initialize a query_t struct.
 * Exit on error.
 */
static query_t *
_alloc_query (void)
{
    query_t *r;
    
    if (!(r = malloc (sizeof (*r))))
        err_exit ("out of memory");
    if (!(r->exports = list_create ((ListDelF)free)))
        err_exit ("out of memory");
    r->port = NULL;
    return r;
}

/* Free a query_t struct.
 * This function always succeedes.
 */
void
free_query (query_t *r)
{
    if (r->port)
        free (r->port);
    list_destroy (r->exports);
    free (r);
}

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
static void
_getport (Npcfsys *fs, query_t *q, char *jobid)
{
    Npcfid *fid;
    char buf[64];

    if (!jobid)
        jobid = "nojob";
    if (!(fid = npc_open (fs, "ctl", O_RDWR)))
        err_exit ("ctl: open");
    if (npc_puts (fid, jobid) < 0)
        err_exit ("ctl: write");
    if (!npc_gets (fid, buf, sizeof (buf)))
        err_exit ("ctl: read");
    q->port = _strdup_trim (buf);
    if (npc_close (fid) < 0)
        err_exit ("ctl: close");
    if (q->port == NULL)
        msg_exit ("ctl: error reading port");
}

/* Read export list from exports file.
 */
static void
_getexports (Npcfsys *fs, query_t *q)
{
    Npcfid *fid;
    char buf[PATH_MAX];
    char *line;

    if (!(fid = npc_open (fs, "exports", O_RDONLY)))
        err_exit ("exports: open");
    errno = 0;
    while (npc_gets (fid, buf, sizeof (buf))) {
        if (!(line = _strdup_trim (buf)))
            continue;
        if (!list_append (q->exports, line))
            msg_exit ("out of memory");
    }
    if (errno)
        err_exit ("exports: read");
    if (npc_close (fid) < 0)
        err_exit ("exports: close");
}

/* Interact with diodctl server to determine port number of diod server
 * and a list of exports, returned in a query_t struct that caller must free.
 * If getport is false, skip port query that triggers server creation.
 * Exit on error.
 */
query_t *
ctl_query (char *host, int getport, char *jobid)
{
    int fd;
    Npcfsys *fs;
    query_t *q = _alloc_query ();

    if ((fd = diod_sock_connect (host, "10005", 1, 0)) < 0)
        err_exit ("connect failed");
    if (!(fs = npc_mount (0, 65536+24, "/diodctl", diod_auth_client_handshake)))
        err_exit ("npc_mount");
    if (getport)
        _getport (fs, q, jobid);
    _getexports (fs, q);
    if (npc_umount (fs) < 0)
        err_exit ("mpc_umount");
    return q;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
