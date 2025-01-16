/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#ifndef LIBNPFS_PROTOCOL_H
#define LIBNPFS_PROTOCOL_H

#include "types.h"

enum {
	Tlerror = 6,
	Rlerror,
	Tstatfs = 8,
	Rstatfs,
	Tlopen = 12,
	Rlopen,
	Tlcreate = 14,
	Rlcreate,
	Tsymlink = 16,
	Rsymlink,
	Tmknod = 18,
	Rmknod,
	Trename = 20,
	Rrename,
	Treadlink = 22,
	Rreadlink,
	Tgetattr = 24,
	Rgetattr,
	Tsetattr = 26,
	Rsetattr,
	Txattrwalk = 30,
	Rxattrwalk,
	Txattrcreate = 32,
	Rxattrcreate,
	Treaddir = 40,
	Rreaddir,
	Tfsync = 50,
	Rfsync,
	Tlock = 52,
	Rlock,
	Tgetlock = 54,
	Rgetlock,
	Tlink = 70,
	Rlink,
	Tmkdir = 72,
	Rmkdir,
	Trenameat = 74,
	Rrenameat,
	Tunlinkat = 76,
	Runlinkat,

	Tversion = 100,
	Rversion,
	Tauth = 102,
	Rauth,
	Tattach = 104,
	Rattach,
	Terror = 106,
	Rerror,
	Tflush = 108,
	Rflush,
	Twalk = 110,
	Rwalk,
	Topen = 112,
	Ropen,
	Tcreate = 114,
	Rcreate,
	Tread = 116,
	Rread,
	Twrite = 118,
	Rwrite,
	Tclunk = 120,
	Rclunk,
	Tremove = 122,
	Rremove,
	Tstat = 124,
	Rstat,
	Twstat = 126,
	Rwstat,
};

/* qid.types */
enum {
	Qtdir		= 0x80,
	Qtappend	= 0x40,
	Qtexcl		= 0x20,
	Qtmount		= 0x10,
	Qtauth		= 0x08,
	Qttmp		= 0x04,
	Qtsymlink	= 0x02,
	Qtlink		= 0x01,
	Qtfile		= 0x00,
};

#define NOTAG           (u16)(~0)
#define NOFID           (u32)(~0)
#define NONUNAME	(u32)(~0)

#define MAXWELEM        16 // Twalk
#define IOHDRSZ         24 // Twrite, Rread
#define DIRHDRSZ        24 // Treaddir

// request_mask for Tgetattr, valid for Rgetattr
enum {
	Gamode		= 0x0001ULL,
	Ganlink		= 0x0002ULL,
	Gauid		= 0x0004ULL,
	Gagid		= 0x0008ULL,
	Gardev		= 0x0010ULL,
	Gaatime		= 0x0020ULL,
	Gamtime		= 0x0040ULL,
	Gactime		= 0x0080ULL,
	Gaino		= 0x0100ULL,
	Gasize		= 0x0200ULL,
	Gablocks	= 0x0400ULL,

	Gabtime		= 0x0800ULL,
	Gagen		= 0x1000ULL,
	Gadataversion	= 0x2000ULL,

	Gabasic		= 0x07ffULL,
	Gaall		= 0x3fffULL,
};

// valid for Tsetattr
enum {
	Samode		= 0x0001UL,
	Sauid		= 0x0002UL,
	Sagid		= 0x0004UL,
	Sasize		= 0x0008UL,
	Saatime		= 0x0010UL,
	Samtime		= 0x0020UL,
	Sactime		= 0x0040UL,
	Saatimeset	= 0x0080UL,
	Samtimeset	= 0x0100UL,
};

// flags for Tlopen
enum {
	Ordonly		= 00000000,
	Owronly		= 00000001,
	Ordwr		= 00000002,
	Onoaccess	= 00000003,
	Ocreate		= 00000100,
	Oexcl		= 00000200,
	Onoctty		= 00000400,
	Otrunc		= 00001000,
	Oappend		= 00002000,
	Ononblock	= 00004000,
	Odsync		= 00010000,
	Ofasync		= 00020000,
	Odirect		= 00040000,
	Olargefile	= 00100000,
	Odirectory	= 00200000,
	Onofollow	= 00400000,
	Onoatime	= 01000000,
	Ocloexec	= 02000000,
	Osync		= 04000000,
};

// flags for Tlock
enum {
	Lblock		= 1,
	Lreclaim	= 2,
};

// status for Rlock
enum {
	Lsuccess	= 0,
	Lblocked	= 1,
	Lerror		= 2,
	Lgrace		= 3,
};

// type for Tgetlock, Rgetlock, Tlock
enum {
	Lrdlck		= 0,
	Lwrlck		= 1,
	Lunlck		= 2,
};

struct Nprlerror {
	u32		ecode;
};
struct Nptstatfs {
	u32		fid;
};
struct Nprstatfs {
	u32		type;
	u32		bsize;
	u64		blocks;
	u64		bfree;
	u64		bavail;
	u64		files;
	u64		ffree;
	u64		fsid;
	u32		namelen;
};
struct Nptlopen {
	u32		fid;
	u32		flags;
};
struct Nprlopen {
	Npqid		qid;
	u32		iounit;
};
struct Nptlcreate {
	u32		fid;
	Npstr		name;
	u32		flags;
	u32		mode;
	u32		gid;
};
struct Nprlcreate {
	Npqid		qid;
	u32		iounit;
};
struct Nptsymlink {
	u32		fid;
	Npstr		name;
	Npstr		symtgt;
	u32		gid;
};
struct Nprsymlink {
	Npqid		qid;
};
struct Nptmknod {
	u32		fid;
	Npstr		name;
	u32		mode;
	u32		major;
	u32		minor;
	u32		gid;
};
struct Nprmknod {
	Npqid		qid;
};
struct Nptrename {
	u32		fid;
	u32		dfid;
	Npstr		name;
};
// Rrename is empty
struct Nptreadlink {
	u32		fid;
};
struct Nprreadlink {
	Npstr		target;
};
struct Nptgetattr {
	u32		fid;
	u64		request_mask;
};
struct Nprgetattr {
	u64		valid;
	Npqid		qid;
	u32		mode;
	u32		uid;
	u32		gid;
	u64		nlink;
	u64		rdev;
	u64		size;
	u64		blksize;
	u64		blocks;
	u64		atime_sec;
	u64		atime_nsec;
	u64		mtime_sec;
	u64		mtime_nsec;
	u64		ctime_sec;
	u64		ctime_nsec;
	u64		btime_sec;
	u64		btime_nsec;
	u64		gen;
	u64		data_version;
};
struct Nptsetattr {
	u32		fid;
	u32		valid;
	u32		mode;
	u32		uid;
	u32		gid;
	u64		size;
	u64		atime_sec;
	u64		atime_nsec;
	u64		mtime_sec;
	u64		mtime_nsec;
};
// Rsetattr is empty
struct Nptxattrwalk {
	u32		fid;
	u32		attrfid;
	Npstr		name;
};
struct Nprxattrwalk {
	u64		size;
};
struct Nptxattrcreate {
	u32		fid;
	Npstr		name;
	u64		size;
	u32		flag;
};
// Rxattrcreate is empty
struct Nptreaddir {
	u32		fid;
	u64		offset;
	u32		count;
};
struct Nprreaddir {
	u32		count;
	u8*		data;
};
struct Nptfsync {
	u32		fid;
	u32		datasync;
};
// Rfsync is empty
struct Nptlock {
	u32		fid;
	u8		type;
	u32		flags;
	u64		start;
	u64		length;
	u32		proc_id;
	Npstr		client_id;
};
struct Nprlock {
	u8		status;
};
struct Nptgetlock {
	u32		fid;
	u8		type;
	u64		start;
	u64		length;
	u32		proc_id;
	Npstr		client_id;
};
struct Nprgetlock {
	u8		type;
	u64		start;
	u64		length;
	u32		proc_id;
	Npstr		client_id;
};
struct Nptlink {
	u32		dfid;
	u32		fid;
	Npstr		name;
};
// Rlink is empty
struct Nptmkdir {
	u32		fid;
	Npstr		name;
	u32		mode;
	u32		gid;
};
struct Nprmkdir {
	Npqid		qid;
};
struct Nptrenameat {
	u32		olddirfid;
	Npstr		oldname;
	u32		newdirfid;
	Npstr		newname;
};
// Rrenameat is empty
struct Nptunlinkat {
	u32		dirfid;
	Npstr		name;
	u32		flags;
};
// Runlinkat is empty
struct Nptversion {
	u32		msize;
	Npstr		version;
};
struct Nprversion {
	u32		msize;
	Npstr		version;
};
struct Nptauth {
	u32		afid;
	Npstr		uname;
	Npstr		aname;
	u32		n_uname;
};
struct Nprauth {
	Npqid		qid;
};
struct Nprerror {
	Npstr		error;
	u32		errnum;
};
struct Nptflush {
	u16		oldtag;
};
// Rflush is empty
struct Nptattach {
	u32		fid;
	u32		afid;
	Npstr		uname;
	Npstr		aname;
	u32		n_uname;
};
struct Nprattach {
	Npqid		qid;
};
struct Nptwalk {
	u32		fid;
	u32		newfid;
	u16		nwname;
	Npstr		wnames[MAXWELEM];
};
struct Nprwalk {
	u16		nwqid;
	Npqid		wqids[MAXWELEM];
};
struct Nptopen {
	u32		fid;
	u8		mode;
};
struct Npropen {
	Npqid		qid;
	u32		iounit;
};
struct Nptcreate {
	u32		fid;
	Npstr		name;
	u32		perm;
	u8		mode;
	Npstr		extension;
};
struct Nprcreate {
	Npqid		qid;
	u32		iounit;
};
struct Nptread {
	u32		fid;
	u64		offset;
	u32		count;
};
struct Nprread {
	u32		count;
	u8*		data;
};
struct Nptwrite {
	u32		fid;
	u64		offset;
	u32		count;
	u8*		data;
};
struct Nprwrite {
	u32		count;
};
struct Nptclunk {
	u32		fid;
};
struct Nprclunk {
};
struct Nptremove {
	u32		fid;
};
// Rremove is empty

#endif
