/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBDIOD_DIOD_FID_H
#define LIBDIOD_DIOD_FID_H

#define DIOD_FID_FLAGS_ROFS       0x01
#define DIOD_FID_FLAGS_MOUNTPT    0x02
#define DIOD_FID_FLAGS_SHAREFD    0x04
#define DIOD_FID_FLAGS_XATTR      0x08

typedef struct {
    Path            path;
    IOCtx           ioctx;
    Xattr           xattr;
    int             flags;
} Fid;

Fid *diod_fidalloc (Npfid *fid, Npstr *ns);
Fid *diod_fidclone (Npfid *newfid, Npfid *fid);
void diod_fiddestroy (Npfid *fid);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
