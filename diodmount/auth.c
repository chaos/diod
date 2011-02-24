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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <stdint.h>
#include <netdb.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <ctype.h>
#if HAVE_MUNGE
#define GPL_LICENSED 1
#include <munge.h>
#endif
#include <pwd.h>
#include <libgen.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#include "9p.h"
#include "npfs.h"
#include "list.h"
#include "diod_log.h"
#include "diod_upool.h"
#include "diod_sock.h"
#include "opt.h"
#include "util.h"
#include "auth.h"

/* Create a munge credential for the effective uid and return it as a string
 * that must be freed by the caller.
 */
char *
auth_mkuser (char *payload)
{
    char *u;
#if HAVE_MUNGE
    int paylen = payload ? strlen(payload) + 1 : 0;
    munge_ctx_t ctx;
    munge_err_t err;

    if (!(ctx = munge_ctx_create ()))
        msg_exit ("out of memory");
    err = munge_encode (&u, ctx, payload, paylen);
    if (err != EMUNGE_SUCCESS)
        msg_exit ("munge_encode: %s", munge_strerror (err));
    munge_ctx_destroy (ctx);
#else
    struct passwd *pwd;

    if (!(pwd = getpwuid (geteuid ())))
        msg_exit ("could not look up uid %d", geteuid ());
    if (!(u = strdup (pwd->pw_name)))
        msg_exit ("out of memory");
#endif
    return u;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
