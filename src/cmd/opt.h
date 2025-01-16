/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef CMD_OPT_H
#define CMD_OPT_H

typedef struct opt_struct *Opt;

Opt             opt_create (void);

void            opt_destroy (Opt o);

char           *opt_csv (Opt o);

int             opt_addf (Opt o, const char *fmt, ...)
                          __attribute__ ((format (printf, 2, 3)));

char           *opt_find (Opt o, char *key);

int             opt_delete (Opt o, char *key);

int             opt_scanf (Opt o, const char *fmt, ...)
                          __attribute__ ((format (scanf, 2, 3)));

int             opt_check_allowed_csv (Opt o, const char *s);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
