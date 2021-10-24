/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* trename - rename a file */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char *argv[])
{
    if (rename (argv[1], argv[2]) < 0) {
        perror ("rename");
        exit (1);
    }
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
