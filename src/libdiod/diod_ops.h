/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBDIOD_DIOD_OPS_H
#define LIBDIOD_DIOD_OPS_H

#include <sys/types.h>
#include <sys/stat.h>

#include "src/libnpfs/npfs.h"

int diod_init (Npsrv *srv);
void diod_fini (Npsrv *srv);

void diod_ustat2qid (struct stat *st, Npqid *qid);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
