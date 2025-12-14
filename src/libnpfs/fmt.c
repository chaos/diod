/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/param.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>

#include "npfs.h"
#include "npfsimpl.h"

static void
np_printqid(char *s, int len, Npqid *q)
{
	int n = 0;
	char buf[10];

	if (q->type & Qtdir)
		buf[n++] = 'd';
	if (q->type & Qtappend)
		buf[n++] = 'a';
	if (q->type & Qtauth)
		buf[n++] = 'A';
	if (q->type & Qtexcl)
		buf[n++] = 'l';
	if (q->type & Qttmp)
		buf[n++] = 't';
	if (q->type & Qtsymlink)
		buf[n++] = 'L';
	buf[n] = '\0';

	spf (s, len, "(%.16"PRIx64" %x '%s')", q->path, q->version, buf);
}

static void
np_sndump(char *s, int len, u8 *data, int datalen)
{
	int i;

	for (i = 0; i < datalen; i++) {
		spf (s, len, "%02x", data[i]);
		if (i%4 == 3)
			spf (s, len, " ");
		if (i%32 == 31 && i + 1 < datalen)
			spf (s, len, "\n");
	}
}

static void
_chomp(char *s)
{
	int len = strlen(s);

	if (s[len - 1] == '\n')
		s[len - 1] = '\0';
}

static char *
np_timestr(const u64 sec, const u64 nsec)
{
	const time_t t = sec;
	char *s = "0";

	if (sec > 0) {
		s = ctime(&t);
		_chomp(s);
	}
	return s;
}

static void
np_printdentry(char *s, int len, Npqid *qid, char *dname, u8 type, u64 off)
{
	spf (s, len, "\n");
	np_printqid (s, len, qid);
	spf (s, len, " %u %-21ju %s", type, (uintmax_t)off, dname);
}

static void
np_printdents(char *s, int len, u8 *buf, int buflen)
{
	int res;

	do {
		Npqid qid;
		char dname[PATH_MAX + 1];
		u64 offset;
		u8 type;

		res = np_deserialize_p9dirent (&qid, &offset, &type, dname,
						sizeof (dname), buf, buflen);
		if (res > 0) {
			np_printdentry (s, len, &qid, dname, type, offset);
			buf += res;
			buflen -= res;
		}
	} while (res > 0);
}

static void
np_printdata(char *s, int len, u8 *buf, int buflen)
{
	if (buflen > 0)
		spf (s, len, "\n");
	np_sndump(s, len, buf, buflen < 64 ? buflen : 64);
}

static void
np_printlocktype(char *s, int len, u8 type)
{
	switch (type) {
		case Lrdlck:
			spf (s, len, "%s", "Lrdlck");
			break;
		case Lwrlck:
			spf (s, len, "%s", "Lwrlck");
			break;
		case Lunlck:
			spf (s, len, "%s", "Lunlck");
			break;
		default:
			spf (s, len, "%u", type);
			break;
	}
}

static void
np_printlockstatus(char *s, int len, u8 status)
{
	switch (status) {
		case Lsuccess:
			spf (s, len, "%s", "Lsuccess");
			break;
		case Lblocked:
			spf (s, len, "%s", "Lblocked");
			break;
		case Lerror:
			spf (s, len, "%s", "Lerror");
			break;
		case Lgrace:
			spf (s, len, "%s", "Lgrace");
			break;
		default:
			spf (s, len, "%u", status);
			break;
	}
}

void
np_snprintfcall(char *s, int len, Npfcall *fc)
{
	int i;

	s[0] = '\0';
	if (!fc) {
		spf (s, len, "NULL");
		return;
	}

	switch (fc->type) {
	case Tlerror:
		spf (s, len, "Tlerror tag %u", fc->tag);
		break;
	case Rlerror:
		spf (s, len, "Rlerror tag %u", fc->tag);
		spf (s, len, " ecode %"PRIu32, fc->u.rlerror.ecode);
		break;
	case Tstatfs:
		spf (s, len, "Tstatfs tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tstatfs.fid);
		break;
	case Rstatfs:
		spf (s, len, "Rstatfs tag %u", fc->tag);
		spf (s, len, " type %"PRIu32, fc->u.rstatfs.type);
		spf (s, len, " bsize %"PRIu32, fc->u.rstatfs.bsize);
		spf (s, len, " blocks %"PRIu64, fc->u.rstatfs.blocks);
		spf (s, len, " bfree %"PRIu64, fc->u.rstatfs.bfree);
		spf (s, len, " bavail %"PRIu64, fc->u.rstatfs.bavail);
		spf (s, len, " files %"PRIu64, fc->u.rstatfs.files);
		spf (s, len, " ffree %"PRIu64, fc->u.rstatfs.ffree);
		spf (s, len, " fsid %"PRIu64, fc->u.rstatfs.fsid);
		spf (s, len, " namelen %"PRIu32, fc->u.rstatfs.namelen);
		break;
	case Tlopen:
		spf (s, len, "Tlopen tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tlopen.fid);
		spf (s, len, " flags 0%"PRIo32, fc->u.tlopen.flags);
		break;
	case Rlopen:
		spf (s, len, "Rlopen tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rlopen.qid);
		spf (s, len, " iounit %"PRIu32, fc->u.rlopen.iounit);
		break;
	case Tlcreate:
		spf (s, len, "Tlcreate tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tlcreate.fid);
		spf (s, len, " name '%.*s'", fc->u.tlcreate.name.len,
					     fc->u.tlcreate.name.str);
		spf (s, len, " flags 0x%"PRIx32, fc->u.tlcreate.flags);
		spf (s, len, " mode 0%"PRIo32, fc->u.tlcreate.mode);
		spf (s, len, " gid %"PRIu32, fc->u.tlcreate.gid);
		break;
	case Rlcreate:
		spf (s, len, "Rlcreate tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rlcreate.qid);
		spf (s, len, " iounit %"PRIu32, fc->u.rlcreate.iounit);
		break;
	case Tsymlink:
		spf (s, len, "Tsymlink tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tsymlink.fid);
		spf (s, len, " name '%.*s'", fc->u.tsymlink.name.len,
					     fc->u.tsymlink.name.str);
		spf (s, len, " symtgt '%.*s'", fc->u.tsymlink.symtgt.len,
					       fc->u.tsymlink.symtgt.str);
		spf (s, len, " gid %"PRIu32, fc->u.tsymlink.gid);
		break;
	case Rsymlink:
		spf (s, len, "Rsymlink tag %d qid ", fc->tag);
		np_printqid(s, len, &fc->u.rsymlink.qid);
		break;
	case Tmknod:
		spf (s, len, "Tmknod tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tmknod.fid);
		spf (s, len, " name '%.*s'", fc->u.tmknod.name.len,
					     fc->u.tmknod.name.str);
		spf (s, len, " mode 0%"PRIo32, fc->u.tmknod.mode);
		spf (s, len, " major %"PRIu32, fc->u.tmknod.major);
		spf (s, len, " minor %"PRIu32, fc->u.tmknod.minor);
		spf (s, len, " gid %"PRIu32, fc->u.tmknod.gid);
		break;
	case Rmknod:
		spf (s, len, "Rmknod tag %d qid ", fc->tag);
		np_printqid(s, len, &fc->u.rsymlink.qid);
		break;
	case Trename:
		spf (s, len, "Trename tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.trename.fid);
		spf (s, len, " dfid %"PRIu32, fc->u.trename.dfid);
		spf (s, len, " name '%.*s'", fc->u.trename.name.len,
					     fc->u.trename.name.str);
		break;
	case Rrename:
		spf (s, len, "Rrename tag %u", fc->tag);
		break;
	case Treadlink:
		spf (s, len, "Treadlink tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.treadlink.fid);
		break;
	case Rreadlink:
		spf (s, len, "Rreadlink tag %u", fc->tag);
		spf (s, len, " target '%.*s'", fc->u.rreadlink.target.len,
				   	       fc->u.rreadlink.target.str);
		break;
	case Tgetattr:
		spf (s, len, "Tgetattr tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tgetattr.fid);
		spf (s, len, " request_mask 0x%"PRIx64, fc->u.tgetattr.request_mask);
		break;
	case Rgetattr:
		spf (s, len, "Rgetattr tag %u", fc->tag);
		spf (s, len, " valid 0x%"PRIx64, fc->u.rgetattr.valid);
		spf (s, len, " qid ");
		if ((fc->u.rgetattr.valid & Gaino))
			np_printqid(s, len, &fc->u.rgetattr.qid);
		else
			spf (s, len, "X");
		if ((fc->u.rgetattr.valid & Gamode))
			spf (s, len, " mode 0%"PRIo32, fc->u.rgetattr.mode);
		else
			spf (s, len, " mode X");
		if ((fc->u.rgetattr.valid & Gauid))
			spf (s, len, " uid %"PRIu32, fc->u.rgetattr.uid);
		else
			spf (s, len, " uid X");
		if ((fc->u.rgetattr.valid & Gagid))
			spf (s, len, " gid %"PRIu32, fc->u.rgetattr.gid);
		else
			spf (s, len, " gid X");
		if ((fc->u.rgetattr.valid & Ganlink))
			spf (s, len, " nlink %"PRIu64, fc->u.rgetattr.nlink);
		else
			spf (s, len, " nlink X");
		if ((fc->u.rgetattr.valid & Gardev))
			spf (s, len, " rdev %"PRIu64, fc->u.rgetattr.rdev);
		else
			spf (s, len, " rdev X");
		if ((fc->u.rgetattr.valid & Gasize))
			spf (s, len, " size %"PRIu64, fc->u.rgetattr.size);
		else
			spf (s, len, " size X");
		spf (s, len, " blksize %"PRIu64, fc->u.rgetattr.blksize);
		if ((fc->u.rgetattr.valid & Gablocks))
			spf (s, len, " blocks %"PRIu64, fc->u.rgetattr.blocks);
		else
			spf (s, len, " blocks X");
		if ((fc->u.rgetattr.valid & Gaatime))
			spf (s, len, " atime %s",
				np_timestr(fc->u.rgetattr.atime_sec, fc->u.rgetattr.atime_nsec));
		else
			spf (s, len, " atime X");
		if ((fc->u.rgetattr.valid & Gamtime))
			spf (s, len, " mtime %s",
				np_timestr(fc->u.rgetattr.mtime_sec, fc->u.rgetattr.mtime_nsec));
		else
			spf (s, len, " mtime X");
		if ((fc->u.rgetattr.valid & Gactime))
			spf (s, len, " ctime %s",
				np_timestr(fc->u.rgetattr.ctime_sec, fc->u.rgetattr.ctime_nsec));
		else
			spf (s, len, " ctime X");
		if ((fc->u.rgetattr.valid & Gabtime))
			spf (s, len, " btime %s",
				np_timestr(fc->u.rgetattr.btime_sec, fc->u.rgetattr.btime_nsec));
		else
			spf (s, len, " btime X");
		if ((fc->u.rgetattr.valid & Gagen))
			spf (s, len, " gen %"PRIu64, fc->u.rgetattr.gen);
		else
			spf (s, len, " gen X");
		if ((fc->u.rgetattr.valid & Gadataversion))
			spf (s, len, " data_version %"PRIu64, fc->u.rgetattr.data_version);
		else
			spf (s, len, " data_version X");
		break;
	case Tsetattr:
		spf (s, len, "Tsetattr tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tsetattr.fid);
		spf (s, len, " valid 0x%"PRIx32, fc->u.tsetattr.valid);
		if ((fc->u.tsetattr.valid & Samode))
			spf (s, len, " mode 0%"PRIo32, fc->u.tsetattr.mode);
		else
			spf (s, len, " mode X");
		if ((fc->u.tsetattr.valid & Sauid))
			spf (s, len, " uid %"PRIu32, fc->u.tsetattr.uid);
		else
			spf (s, len, " uid X");
		if ((fc->u.tsetattr.valid & Sagid))
			spf (s, len, " gid %"PRIu32, fc->u.tsetattr.gid);
		else
			spf (s, len, " gid X");
		if ((fc->u.tsetattr.valid & Sasize))
			spf (s, len, " size %"PRIu64, fc->u.tsetattr.size);
		else
			spf (s, len, " size X");
		if (!(fc->u.tsetattr.valid & Saatime))
			spf (s, len, " atime X");
		else if (!(fc->u.tsetattr.valid & Saatimeset))
			spf (s, len, " atime X");
		else
			spf (s, len, " atime %s",
				np_timestr(fc->u.tsetattr.atime_sec, fc->u.tsetattr.atime_nsec));
		if (!(fc->u.tsetattr.valid & Samtime))
			spf (s, len, " mtime X");
		else if (!(fc->u.tsetattr.valid & Samtimeset))
			spf (s, len, " mtime now");
		else
			spf (s, len, " mtime %s",
				np_timestr(fc->u.tsetattr.mtime_sec,
					   fc->u.tsetattr.mtime_nsec));
		break;
	case Rsetattr:
		spf (s, len, "Rsetattr tag %u", fc->tag);
		break;
	case Txattrwalk:
		spf (s, len, "Txattrwalk tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.txattrwalk.fid);
		spf (s, len, " attrfid %"PRIu32, fc->u.txattrwalk.attrfid);
		spf (s, len, " name '%.*s'", fc->u.txattrwalk.name.len,
					     fc->u.txattrwalk.name.str);
		break;
	case Rxattrwalk:
		spf (s, len, "Rxattrwalk tag %u", fc->tag);
		spf (s, len, " size %"PRIu64, fc->u.rxattrwalk.size);
		break;
	case Txattrcreate:
		spf (s, len, "Txattrcreate tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.txattrcreate.fid);
		spf (s, len, " name '%.*s'", fc->u.txattrcreate.name.len,
					     fc->u.txattrcreate.name.str);
		spf (s, len, " size %"PRIu64, fc->u.txattrcreate.size);
		spf (s, len, " flag %"PRIu32, fc->u.txattrcreate.flag);
		break;
	case Rxattrcreate:
		spf (s, len, "Rxattrcreate tag %u", fc->tag);
		break;
	case Treaddir:
		spf (s, len, "Treaddir tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.treaddir.fid);
		spf (s, len, " offset %"PRIu64, fc->u.treaddir.offset);
		spf (s, len, " count %"PRIu32, fc->u.treaddir.count);
		break;
	case Rreaddir:
		spf (s, len, "Rreaddir tag %u", fc->tag);
		spf (s, len, " count %"PRIu32, fc->u.rreaddir.count);
		np_printdents(s, len, fc->u.rreaddir.data, fc->u.rreaddir.count);
		break;
	case Tfsync:
		spf (s, len, "Tfsync tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tfsync.fid);
		break;
	case Rfsync:
		spf (s, len, "Rfsync tag %u", fc->tag);
		break;
	case Tlock:
		spf (s, len, "Tlock tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tlock.fid);
		spf (s, len, " type ");
		np_printlocktype(s, len, fc->u.tlock.type);
		spf (s, len, " flags %"PRIu32, fc->u.tlock.flags);
		spf (s, len, " start %"PRIu64, fc->u.tlock.start);
		spf (s, len, " length %"PRIu64, fc->u.tlock.length);
		spf (s, len, " proc_id %"PRIu32, fc->u.tlock.proc_id);
		spf (s, len, " client_id '%.*s'", fc->u.tlock.client_id.len,
						  fc->u.tlock.client_id.str);
		break;
	case Rlock:
		spf (s, len, "Rlock tag %u", fc->tag);
		spf (s, len, " status ");
		np_printlockstatus(s, len, fc->u.rlock.status);
		break;
	case Tgetlock:
		spf (s, len, "Tgetlock tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tgetlock.fid);
		spf (s, len, " type ");
		np_printlocktype(s, len, fc->u.tgetlock.type);
		spf (s, len, " start %"PRIu64, fc->u.tgetlock.start);
		spf (s, len, " length %"PRIu64, fc->u.tgetlock.length);
		spf (s, len, " proc_id %"PRIu32, fc->u.tgetlock.proc_id);
		spf (s, len, " client_id '%.*s'", fc->u.tgetlock.client_id.len,
						  fc->u.tgetlock.client_id.str);
		break;
	case Rgetlock:
		spf (s, len, "Rgetlock tag %u", fc->tag);
		spf (s, len, " type ");
		np_printlocktype(s, len, fc->u.rgetlock.type);
		spf (s, len, " start %"PRIu64, fc->u.rgetlock.start);
		spf (s, len, " length %"PRIu64, fc->u.rgetlock.length);
		spf (s, len, " proc_id %"PRIu32, fc->u.rgetlock.proc_id);
		spf (s, len, " client_id '%.*s'", fc->u.rgetlock.client_id.len,
						  fc->u.rgetlock.client_id.str);
		break;
	case Tlink:
		spf (s, len, "Tlink tag %u", fc->tag);
		spf (s, len, " dfid %"PRIu32, fc->u.tlink.dfid);
		spf (s, len, " fid %"PRIu32, fc->u.tlink.fid);
		spf (s, len, " name '%.*s'", fc->u.tlink.name.len,
					     fc->u.tlink.name.str);
		break;
	case Rlink:
		spf (s, len, "Rlink tag %u", fc->tag);
		break;
	case Tmkdir:
		spf (s, len, "Tmkdir tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tmkdir.fid);
		spf (s, len, " name '%.*s'", fc->u.tmkdir.name.len,
					     fc->u.tmkdir.name.str);
		spf (s, len, " mode 0%"PRIo32, fc->u.tmkdir.mode);
		spf (s, len, " gid %"PRIu32, fc->u.tmkdir.gid);
		break;
	case Rmkdir:
		spf (s, len, "Rmkdir tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rmkdir.qid);
		break;
	case Trenameat:
		spf (s, len, "Trenameat tag %u", fc->tag);
		spf (s, len, " olddirfid %"PRIu32, fc->u.trenameat.olddirfid);
		spf (s, len, " oldname '%.*s'", fc->u.trenameat.oldname.len,
					        fc->u.trenameat.oldname.str);
		spf (s, len, " newdirfid %"PRIu32, fc->u.trenameat.newdirfid);
		spf (s, len, " newname '%.*s'", fc->u.trenameat.newname.len,
					        fc->u.trenameat.newname.str);
		break;
	case Rrenameat:
		spf (s, len, "Rrenameat tag %u", fc->tag);
		break;
	case Tunlinkat:
		spf (s, len, "Tunlinkat tag %u", fc->tag);
		spf (s, len, " dirfid %"PRIu32, fc->u.tunlinkat.dirfid);
		spf (s, len, " name '%.*s'", fc->u.tunlinkat.name.len,
					     fc->u.tunlinkat.name.str);
		spf (s, len, " flags %"PRIu32, fc->u.tunlinkat.flags);
		break;
	case Runlinkat:
		spf (s, len, "Runlinkat tag %u", fc->tag);
		break;
	case Tversion:
		spf (s, len, "Tversion tag %u", fc->tag);
		spf (s, len, " msize %u", fc->u.tversion.msize);
		spf (s, len, " version '%.*s'", fc->u.tversion.version.len,
					        fc->u.tversion.version.str);
		break;
	case Rversion:
		spf (s, len, "Rversion tag %u msize %u",
			fc->tag, fc->u.rversion.msize);
		spf (s, len, " version '%.*s'", fc->u.rversion.version.len,
					        fc->u.rversion.version.str);
		break;
	case Tauth:
		spf (s, len, "Tauth tag %u afid %d",
			fc->tag, fc->u.tauth.afid);
		spf (s, len, " uname '%.*s'", fc->u.tauth.uname.len,
					      fc->u.tauth.uname.str);
		spf (s, len, " aname '%.*s'", fc->u.tauth.aname.len,
					      fc->u.tauth.aname.str);
		if (fc->u.tauth.n_uname != NONUNAME)
			spf (s, len, " n_uname %u", fc->u.tauth.n_uname);
		else
			spf (s, len, " n_uname NONUNAME");
		break;
	case Rauth:
		spf (s, len, "Rauth tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rauth.qid);
		break;
	case Tattach:
		spf (s, len, "Tattach tag %u", fc->tag);
		spf (s, len, " fid %d afid %d",
				fc->u.tattach.fid, fc->u.tattach.afid);
		spf (s, len, " uname '%.*s'", fc->u.tattach.uname.len,
					      fc->u.tattach.uname.str);
		spf (s, len, " aname '%.*s'", fc->u.tattach.aname.len,
					      fc->u.tattach.aname.str);
		if (fc->u.tattach.n_uname != NONUNAME)
			spf (s, len, " n_uname %u", fc->u.tattach.n_uname);
		else
			spf (s, len, " n_uname NONUNAME");
		break;
	case Rattach:
		spf (s, len, "Rattach tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rattach.qid);
		break;
	case Tflush:
		spf (s, len, "Tflush tag %u oldtag %u",
				fc->tag, fc->u.tflush.oldtag);
		break;
	case Rflush:
		spf (s, len, "Rflush tag %u", fc->tag);
		break;
	case Twalk:
		spf (s, len, "Twalk tag %u fid %d newfid %d nwname %d",
			fc->tag, fc->u.twalk.fid, fc->u.twalk.newfid,
			fc->u.twalk.nwname);
		for(i = 0; i < fc->u.twalk.nwname; i++)
			spf (s, len, " '%.*s'", fc->u.twalk.wnames[i].len,
						fc->u.twalk.wnames[i].str);
		break;
	case Rwalk:
		spf (s, len, "Rwalk tag %u nwqid %d ",
			fc->tag, fc->u.rwalk.nwqid);
		for(i = 0; i < fc->u.rwalk.nwqid; i++)
			np_printqid(s, len, &fc->u.rwalk.wqids[i]);
		break;
	case Tread:
		spf (s, len, "Tread tag %u fid %d offset %"PRIu64" count %u",
			fc->tag, fc->u.tread.fid, fc->u.tread.offset,
			fc->u.tread.count);
		break;
	case Rread:
		spf (s, len, "Rread tag %u count %u", fc->tag,
			fc->u.rread.count);
		np_printdata(s, len, fc->u.rread.data, fc->u.rread.count);
		break;
	case Twrite:
		spf (s, len, "Twrite tag %u", fc->tag);
		spf (s, len, " fid %d", fc->u.twrite.fid);
		spf (s, len, " offset %"PRIu64, fc->u.twrite.offset);
		spf (s, len, " count %u", fc->u.twrite.count);
		np_printdata(s, len, fc->u.twrite.data, fc->u.twrite.count);
		break;
	case Rwrite:
		spf (s, len, "Rwrite tag %u count %u", fc->tag, fc->u.rwrite.count);
		break;
	case Tclunk:
		spf (s, len, "Tclunk tag %u fid %d", fc->tag, fc->u.tclunk.fid);
		break;
	case Rclunk:
		spf (s, len, "Rclunk tag %u", fc->tag);
		break;
	case Tremove:
		spf (s, len, "Tremove tag %u fid %d", fc->tag, fc->u.tremove.fid);
		break;
	case Rremove:
		spf (s, len, "Rremove tag %u", fc->tag);
		break;
	/* unused ops that we consider a protocol error */
	case Rerror:
		spf (s, len, "Rerror: deprecated protocol op");
		break;
	case Topen:
		spf (s, len, "Topen: deprecated protocol op");
		break;
	case Ropen:
		spf (s, len, "Ropen: deprecated protocol op");
		break;
	case Tcreate:
		spf (s, len, "Tcreate: deprecated protocol op");
		break;
	case Rcreate:
		spf (s, len, "Rcreate: deprecated protocol op");
		break;
	case Tstat:
		spf (s, len, "Tstat: deprecated protocol op");
		break;
	case Rstat:
		spf (s, len, "Rstat: deprecated protocol op");
		break;
	default:
		spf (s, len, "unknown protocol op (%d)", fc->type);
		break;
	}
}
