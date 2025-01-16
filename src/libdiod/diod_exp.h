/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBDIOD_DIOD_EXP_H
#define LIBDIOD_DIOD_EXP_H

#include "src/libnpfs/npfs.h"

int diod_fetch_xflags (Npstr *aname, int *xfp);
int diod_match_exports (char *path, Npconn *conn, Npuser *user, int *xfp);
char *diod_get_exports (char *name, void *a);

#endif
