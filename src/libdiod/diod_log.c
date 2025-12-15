/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* diod_log.c - distributed I/O daemon logging code */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <limits.h>
#include <ctype.h>
#include <libgen.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "diod_log.h"

static char log_prefix[32] = { 0 };
static char *filename = NULL;
static FILE *logf = NULL;

/* If 'path' is a program path, prefix is "name: ", where name is basename (path).
 * If 'path' ends in a space, omit the ": ", e.g. path of "# " is used verbatim.
 * If 'path' is NULL, there is no log prefix.
 */
void
diod_log_init (char *path)
{
    if (path) {
        char *cp = strrchr (path, '/');
        snprintf (log_prefix,
                  sizeof (log_prefix),
                  "%s%s",
                  cp ? cp + 1 : path,
                  isspace (path[strlen (path) - 1]) ? "" : ": ");
    }
    else
        log_prefix[0] = '\0';
    logf = stderr;
}

void
diod_log_fini (void)
{
    if (logf != NULL)
        fflush (logf);
    if (logf != stdout && logf != stderr && logf != NULL)
        fclose (logf);
}

void
diod_log_set_dest (char *s)
{
    FILE *f;

    if (strcmp (s, "stderr") == 0)
        logf = stderr;
    else if (strcmp (s, "stdout") == 0)
        logf = stdout;
    else if ((f = fopen (s, "a"))) {
        if (logf != stdout && logf != stderr && logf != NULL)
            fclose (logf);
        logf = f;
        filename = s;
    } else
        err_exit ("could not open %s for writing", s);
}

char *
diod_log_get_dest (void)
{
    int len = PATH_MAX + 1;
    char *res = malloc (len);

    if (!res)
        return NULL;
    snprintf (res, len, "%s", logf == stdout ? "stdout" :
                              logf == stderr ? "stderr" :
                              logf == NULL   ? "unknown" : filename);
    return res;
}

static void
_verr (int errnum, const char *fmt, va_list ap)
{
    char buf[128];
    char errbuf[64];

    if (logf) {
#ifndef STRERROR_R_CHAR_P
        strerror_r (errnum, errbuf, sizeof (errbuf)); /* XSI version */
        char *s = errbuf;
#else
        char *s = strerror_r (errnum, errbuf, sizeof (errbuf)); /* GNU version */
#endif
        vsnprintf (buf, sizeof (buf), fmt, ap);  /* ignore overflow */
        fprintf (logf, "%s%s: %s\n", log_prefix, buf, s);
        fflush (logf);
    }
}

void
diod_log_buf (const char *buf)
{
    if (logf && buf) {
        fputs (buf, logf);
        fputc ('\n', logf);
        fflush (logf);
    }
}

static void
diod_log_msg (const char *fmt, va_list ap)
{
    char buf[1024];

    if (logf) {
        vsnprintf (buf, sizeof (buf), fmt, ap);  /* ignore overflow */
        fprintf (logf, "%s%s\n", log_prefix, buf);
        fflush (logf);
    }
}

/* Log message and errno string, then exit.
 */
void
err_exit (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errno, fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message and errno string.
 */
void
err (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errno, fmt, ap);
    va_end (ap);
}

/* Log message and errnum string, then exit.
 */
void
errn_exit (int errnum, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errnum, fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message and errnum string.
 */
void
errn (int errnum, const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    _verr (errnum, fmt, ap);
    va_end (ap);
}


/* Log message, then exit.
 */
void
msg_exit (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    diod_log_msg (fmt, ap);
    va_end (ap);
    exit (1);
}

/* Log message.
 */
void
msg (const char *fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    diod_log_msg (fmt, ap);
    va_end (ap);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
