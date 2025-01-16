/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#ifndef LIBNPFS_TYPES_H
#define LIBNPFS_TYPES_H

#include <stdint.h>

typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct Npstr Npstr;
typedef struct Npqid Npqid;

struct Npqid {
        u8		type;
        u32		version;
        u64		path;
};

struct Npstr {
        u16             len;
        char*           str;
};

#endif
