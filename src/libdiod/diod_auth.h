/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef _LIBDIOD_DIOD_AUTH_H
#define _LIBDIOD_DIOD_AUTH_H

#include "src/libnpfs/npfs.h"

extern Npauth *diod_auth_functions;

struct Npcfid;

int diod_auth (struct Npcfid *afid, u32 uid);

#endif
