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

/* diod_log.c - distributed I/O daemon logging code */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <libgen.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "diod_log.h"

static char *prog = NULL;

typedef enum { DEST_STDERR, DEST_SYSLOG } dest_t;
static dest_t dest = DEST_STDERR;

void diod_log_init (char *p)
{
    prog = basename (p);
    openlog (prog, LOG_NDELAY | LOG_PID, LOG_DAEMON);
}

void diod_log_fini (void)
{
    closelog ();
}

void diod_log_to_syslog (void)
{
    dest = DEST_SYSLOG;
}

static void _verr (int errnum, const char *fmt, va_list ap)
{
    char buf[256];
    char errbuf[64];

    strerror_r (errnum, errbuf, sizeof (errbuf));

    vsnprintf (buf, sizeof (buf), fmt, ap);  /* ignore overflow */
    switch (dest) {
        case DEST_STDERR:
            fprintf (stderr, "%s: %s: %s\n", prog, buf, errbuf);
            break;
        case DEST_SYSLOG:
            syslog (LOG_ERR, "%s: %s", buf, errbuf);
            break;
    }
}

static void _vlog (const char *fmt, va_list ap)
{
    char buf[256];

    vsnprintf (buf, sizeof (buf), fmt, ap);  /* ignore overflow */
    switch (dest) {
        case DEST_STDERR:
            fprintf (stderr, "%s: %s\n", prog, buf);
            break;
        case DEST_SYSLOG:
            syslog (LOG_ERR, "%s", buf);
            break;
    }
}

/* Log message and errno string, then exit.
 */
void err_exit (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errno, fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message and errno string.
 */
void err (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errno, fmt, ap);
    va_end (ap);
}

/* Log message and errnum string, then exit.
 */
void errn_exit (int errnum, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errnum, fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message and errnum string.
 */
void errn (int errnum, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errnum, fmt, ap);
    va_end (ap);
}


/* Log message, then exit.
 */
void msg_exit (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _vlog (fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message.
 */
void msg (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _vlog (fmt, ap);
    va_end (ap);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
