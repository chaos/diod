/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBDIOD_DIOD_XATTR_H
#define LIBDIOD_DIOD_XATTR_H

#include "src/libnpfs/npfs.h"

typedef struct xattr_struct *Xattr;

int xattr_open (Npfid *fid, Npstr *name, u64 *sizep);
int xattr_create (Npfid *fid, Npstr *name, u64 size, u32 flags);
int xattr_close (Npfid *fid);

int xattr_pread (Xattr xattr, void *buf, size_t count, off_t offset);
int xattr_pwrite (Xattr xattr, void *buf, size_t count, off_t offset);

#endif
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
