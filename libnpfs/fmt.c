/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
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

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

static void
np_printqid(char *s, int len, Npqid *q)
{
	int n = 0;
	char buf[10];

	if (q->type & P9_QTDIR)
		buf[n++] = 'd';
	if (q->type & P9_QTAPPEND)
		buf[n++] = 'a';
	if (q->type & P9_QTAUTH)
		buf[n++] = 'A';
	if (q->type & P9_QTEXCL)
		buf[n++] = 'l';
	if (q->type & P9_QTTMP)
		buf[n++] = 't';
	if (q->type & P9_QTSYMLINK)
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
np_printdents(char *s, int len, u8 *buf, int buflen)
{
	if (buflen > 0)	
		spf (s, len, "\n");
	/* FIXME: decode directory entries here */
	np_sndump(s, len, buf, buflen < 64 ? buflen : 64);
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
		case P9_LOCK_TYPE_RDLCK:
			spf (s, len, "%s", "P9_LOCK_TYPE_RDLCK");
			break;
		case P9_LOCK_TYPE_WRLCK:
			spf (s, len, "%s", "P9_LOCK_TYPE_WRLCK");
			break;
		case P9_LOCK_TYPE_UNLCK:
			spf (s, len, "%s", "P9_LOCK_TYPE_UNLCK");
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
		case P9_LOCK_SUCCESS:
			spf (s, len, "%s", "P9_LOCK_SUCCESS");
			break;
		case P9_LOCK_BLOCKED:
			spf (s, len, "%s", "P9_LOCK_BLOCKED");
			break;
		case P9_LOCK_ERROR:
			spf (s, len, "%s", "P9_LOCK_ERROR");
			break;
		case P9_LOCK_GRACE:
			spf (s, len, "%s", "P9_LOCK_GRACE");
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
	case P9_TLERROR:
		spf (s, len, "P9_TLERROR tag %u", fc->tag);
		break;
	case P9_RLERROR:
		spf (s, len, "P9_RLERROR tag %u", fc->tag);
		spf (s, len, " ecode %"PRIu32, fc->u.rlerror.ecode);
		break;
	case P9_TSTATFS:
		spf (s, len, "P9_TSTATFS tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tstatfs.fid);
		break;
	case P9_RSTATFS:
		spf (s, len, "P9_RSTATFS tag %u", fc->tag);
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
	case P9_TLOPEN:
		spf (s, len, "P9_TLOPEN tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tlopen.fid);
		spf (s, len, " flags 0%"PRIo32, fc->u.tlopen.flags);
		break;
	case P9_RLOPEN:
		spf (s, len, "P9_RLOPEN tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rlopen.qid);
		spf (s, len, " iounit %"PRIu32, fc->u.rlopen.iounit);
		break;
	case P9_TLCREATE:
		spf (s, len, "P9_TLCREATE tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tlcreate.fid);
		spf (s, len, " name '%.*s'", fc->u.tlcreate.name.len,
					     fc->u.tlcreate.name.str);
		spf (s, len, " flags 0x%"PRIx32, fc->u.tlcreate.flags);
		spf (s, len, " mode 0%"PRIo32, fc->u.tlcreate.mode);
		spf (s, len, " gid %"PRIu32, fc->u.tlcreate.gid);
		break;
	case P9_RLCREATE:
		spf (s, len, "P9_RLCREATE tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rlcreate.qid);
		spf (s, len, " iounit %"PRIu32, fc->u.rlcreate.iounit);
		break;
	case P9_TSYMLINK:
		spf (s, len, "P9_TSYMLINK tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tsymlink.fid);
		spf (s, len, " name '%.*s'", fc->u.tsymlink.name.len,
					     fc->u.tsymlink.name.str);
		spf (s, len, " symtgt '%.*s'", fc->u.tsymlink.symtgt.len,
					       fc->u.tsymlink.symtgt.str);
		spf (s, len, " gid %"PRIu32, fc->u.tsymlink.gid);
		break;
	case P9_RSYMLINK:
		spf (s, len, "P9_RSYMLINK tag %d qid ", fc->tag);
		np_printqid(s, len, &fc->u.rsymlink.qid);
		break;
	case P9_TMKNOD:
		spf (s, len, "P9_TMKNOD tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tmknod.fid);
		spf (s, len, " name '%.*s'", fc->u.tmknod.name.len,
					     fc->u.tmknod.name.str);
		spf (s, len, " mode 0%"PRIo32, fc->u.tmknod.mode);
		spf (s, len, " major %"PRIu32, fc->u.tmknod.major);
		spf (s, len, " minor %"PRIu32, fc->u.tmknod.minor);
		spf (s, len, " gid %"PRIu32, fc->u.tmknod.gid);
		break;
	case P9_RMKNOD:
		spf (s, len, "P9_RMKNOD tag %d qid ", fc->tag);
		np_printqid(s, len, &fc->u.rsymlink.qid);
		break;
	case P9_TRENAME:
		spf (s, len, "P9_TRENAME tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.trename.fid);
		spf (s, len, " dfid%"PRIu32, fc->u.trename.dfid);
		spf (s, len, " name '%.*s'", fc->u.trename.name.len,
					     fc->u.trename.name.str);
		break;
	case P9_RRENAME:
		spf (s, len, "P9_RRENAME tag %u", fc->tag);
		break;
	case P9_TREADLINK:
		spf (s, len, "P9_TREADLINK tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.treadlink.fid);
		break;
	case P9_RREADLINK:
		spf (s, len, "P9_RREADLINK tag %u", fc->tag);
		spf (s, len, " target '%.*s'", fc->u.rreadlink.target.len,
				   	       fc->u.rreadlink.target.str);
		break;
	case P9_TGETATTR:
		spf (s, len, "P9_TGETATTR tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tgetattr.fid);
		spf (s, len, " request_mask 0x%"PRIx64, fc->u.tgetattr.request_mask);
		break;
	case P9_RGETATTR:
		spf (s, len, "P9_RGETATTR tag %u", fc->tag);
		spf (s, len, " valid 0x%"PRIx64, fc->u.rgetattr.valid);
		spf (s, len, " qid ");
		if ((fc->u.rgetattr.valid & P9_STAT_INO))
			np_printqid(s, len, &fc->u.rgetattr.qid);
		else
			spf (s, len, "X");
		if ((fc->u.rgetattr.valid & P9_STAT_MODE))
			spf (s, len, " mode 0%"PRIo32, fc->u.rgetattr.mode);
		else
			spf (s, len, " mode X");
		if ((fc->u.rgetattr.valid & P9_STAT_UID))
			spf (s, len, " uid %"PRIu32, fc->u.rgetattr.uid);
		else
			spf (s, len, " uid X");
		if ((fc->u.rgetattr.valid & P9_STAT_GID))
			spf (s, len, " gid %"PRIu32, fc->u.rgetattr.gid);
		else
			spf (s, len, " gid X");
		if ((fc->u.rgetattr.valid & P9_STAT_NLINK))
			spf (s, len, " nlink %"PRIu64, fc->u.rgetattr.nlink);
		else
			spf (s, len, " nlink X");
		if ((fc->u.rgetattr.valid & P9_STAT_RDEV))
			spf (s, len, " rdev %"PRIu64, fc->u.rgetattr.rdev);
		else
			spf (s, len, " rdev X");
		if ((fc->u.rgetattr.valid & P9_STAT_SIZE))
			spf (s, len, " size %"PRIu64, fc->u.rgetattr.size);
		else
			spf (s, len, " size X");
		spf (s, len, " blksize %"PRIu64, fc->u.rgetattr.blksize);
		if ((fc->u.rgetattr.valid & P9_STAT_BLOCKS))
			spf (s, len, " blocks %"PRIu64, fc->u.rgetattr.blocks);
		else
			spf (s, len, " blocks X");
		if ((fc->u.rgetattr.valid & P9_STAT_ATIME))
			spf (s, len, " atime %s",
				np_timestr(fc->u.rgetattr.atime_sec, fc->u.rgetattr.atime_nsec));
		else
			spf (s, len, " atime X");
		if ((fc->u.rgetattr.valid & P9_STAT_MTIME))
			spf (s, len, " mtime %s",
				np_timestr(fc->u.rgetattr.mtime_sec, fc->u.rgetattr.mtime_nsec));
		else
			spf (s, len, " mtime X");
		if ((fc->u.rgetattr.valid & P9_STAT_CTIME))
			spf (s, len, " ctime %s",
				np_timestr(fc->u.rgetattr.ctime_sec, fc->u.rgetattr.ctime_nsec));
		else
			spf (s, len, " ctime X");
		if ((fc->u.rgetattr.valid & P9_STAT_BTIME))
			spf (s, len, " btime %s",
				np_timestr(fc->u.rgetattr.btime_sec, fc->u.rgetattr.btime_nsec));
		else
			spf (s, len, " btime X");
		if ((fc->u.rgetattr.valid & P9_STAT_GEN))
			spf (s, len, " gen %"PRIu64, fc->u.rgetattr.gen);
		else
			spf (s, len, " gen X");
		if ((fc->u.rgetattr.valid & P9_STAT_DATA_VERSION))
			spf (s, len, " data_version %"PRIu64, fc->u.rgetattr.data_version);
		else
			spf (s, len, " data_version X");
		break;
	case P9_TSETATTR:
		spf (s, len, "P9_TSETATTR tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tsetattr.fid);
		spf (s, len, " valid 0x%"PRIx32, fc->u.tsetattr.valid);
		if ((fc->u.tsetattr.valid & P9_ATTR_MODE))
			spf (s, len, " mode 0%"PRIo32, fc->u.tsetattr.mode);
		else
			spf (s, len, " mode X");
		if ((fc->u.tsetattr.valid & P9_ATTR_UID))
			spf (s, len, " uid %"PRIu32, fc->u.tsetattr.uid);
		else
			spf (s, len, " uid X");
		if ((fc->u.tsetattr.valid & P9_ATTR_GID))
			spf (s, len, " gid %"PRIu32, fc->u.tsetattr.gid);
		else
			spf (s, len, " gid X");
		if ((fc->u.tsetattr.valid & P9_ATTR_SIZE))
			spf (s, len, " size %"PRIu64, fc->u.tsetattr.size);
		else
			spf (s, len, " size X");
		if (!(fc->u.tsetattr.valid & P9_ATTR_ATIME))
			spf (s, len, " atime X");
		else if (!(fc->u.tsetattr.valid & P9_ATTR_ATIME_SET))
			spf (s, len, " atime X");
		else
			spf (s, len, " atime %s",
				np_timestr(fc->u.tsetattr.atime_sec, fc->u.tsetattr.atime_nsec));
		if (!(fc->u.tsetattr.valid & P9_ATTR_MTIME))
			spf (s, len, " mtime X");
		else if (!(fc->u.tsetattr.valid & P9_ATTR_MTIME_SET))
			spf (s, len, " mtime now");
		else
			spf (s, len, " mtime %s",
				np_timestr(fc->u.tsetattr.mtime_sec,
					   fc->u.tsetattr.mtime_nsec));
		break;
	case P9_RSETATTR:
		spf (s, len, "P9_RSETATTR tag %u", fc->tag);
		break;
	case P9_TXATTRWALK:
		spf (s, len, "P9_TXATTRWALK tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.txattrwalk.fid);
		spf (s, len, " attrfid %"PRIu32, fc->u.txattrwalk.attrfid);
		spf (s, len, " name '%.*s'", fc->u.txattrwalk.name.len,
					     fc->u.txattrwalk.name.str);
		break;
	case P9_RXATTRWALK:
		spf (s, len, "P9_RXATTRWALK tag %u", fc->tag);
		spf (s, len, " size %"PRIu64, fc->u.rxattrwalk.size);
		break;
	case P9_TXATTRCREATE:
		spf (s, len, "P9_TXATTRCREATE tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.txattrcreate.fid);
		spf (s, len, " name '%.*s'", fc->u.txattrcreate.name.len,
					     fc->u.txattrcreate.name.str);
		spf (s, len, " size %"PRIu64, fc->u.txattrcreate.size);
		spf (s, len, " flag %"PRIu32, fc->u.txattrcreate.flag);
		break;
	case P9_RXATTRCREATE:
		spf (s, len, "P9_RXATTRCREATE tag %u", fc->tag);
		break;
	case P9_TREADDIR:
		spf (s, len, "P9_TREADDIR tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.treaddir.fid);
		spf (s, len, " offset %"PRIu64, fc->u.treaddir.offset);
		spf (s, len, " count %"PRIu32, fc->u.treaddir.count);
		break;
	case P9_RREADDIR:
		spf (s, len, "P9_RREADDIR tag %u", fc->tag);
		spf (s, len, " count %"PRIu32, fc->u.rreaddir.count);
		np_printdents(s, len, fc->u.rreaddir.data, fc->u.rreaddir.count);
		break;
	case P9_TFSYNC:
		spf (s, len, "P9_TFSYNC tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tfsync.fid);
		break;
	case P9_RFSYNC:
		spf (s, len, "P9_RFSYNC tag %u", fc->tag);
		break;
	case P9_TLOCK:
		spf (s, len, "P9_TLOCK tag %u", fc->tag);
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
	case P9_RLOCK:
		spf (s, len, "P9_RLOCK tag %u", fc->tag);
		spf (s, len, " status ");
		np_printlockstatus(s, len, fc->u.rlock.status);
		break;
	case P9_TGETLOCK:
		spf (s, len, "P9_TGETLOCK tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tgetlock.fid);
		spf (s, len, " type ");
		np_printlocktype(s, len, fc->u.tgetlock.type);
		spf (s, len, " start %"PRIu64, fc->u.tgetlock.start);
		spf (s, len, " length %"PRIu64, fc->u.tgetlock.length);
		spf (s, len, " proc_id %"PRIu32, fc->u.tgetlock.proc_id);
		spf (s, len, " client_id '%.*s'", fc->u.tgetlock.client_id.len,
						  fc->u.tgetlock.client_id.str);
		break;
	case P9_RGETLOCK:
		spf (s, len, "P9_RGETLOCK tag %u", fc->tag);
		spf (s, len, " type ");
		np_printlocktype(s, len, fc->u.rgetlock.type);
		spf (s, len, " start %"PRIu64, fc->u.rgetlock.start);
		spf (s, len, " length %"PRIu64, fc->u.rgetlock.length);
		spf (s, len, " proc_id %"PRIu32, fc->u.rgetlock.proc_id);
		spf (s, len, " client_id '%.*s'", fc->u.rgetlock.client_id.len,
						  fc->u.rgetlock.client_id.str);
		break;
	case P9_TLINK:
		spf (s, len, "P9_TLINK tag %u", fc->tag);
		spf (s, len, " dfid %"PRIu32, fc->u.tlink.dfid);
		spf (s, len, " fid %"PRIu32, fc->u.tlink.fid);
		spf (s, len, " name '%.*s'", fc->u.tlink.name.len,
					     fc->u.tlink.name.str);
		break;
	case P9_RLINK:
		spf (s, len, "P9_RLINK tag %u", fc->tag);
		break;
	case P9_TMKDIR:
		spf (s, len, "P9_TMKDIR tag %u", fc->tag);
		spf (s, len, " fid %"PRIu32, fc->u.tmkdir.fid);
		spf (s, len, " name '%.*s'", fc->u.tmkdir.name.len,
					     fc->u.tmkdir.name.str);
		spf (s, len, " mode 0%"PRIo32, fc->u.tmkdir.mode);
		spf (s, len, " gid %"PRIu32, fc->u.tmkdir.gid);
		break;
	case P9_RMKDIR:
		spf (s, len, "P9_RMKDIR tag %u qid ", fc->tag);
		np_printqid(s, len, &fc->u.rmkdir.qid);
		break;
	case P9_TRENAMEAT:
		spf (s, len, "P9_TRENAMEAT tag %u", fc->tag);
		spf (s, len, " olddirfid %"PRIu32, fc->u.trenameat.olddirfid);
		spf (s, len, " oldname '%.*s'", fc->u.trenameat.oldname.len,
					        fc->u.trenameat.oldname.str);
		spf (s, len, " newdirfid %"PRIu32, fc->u.trenameat.newdirfid);
		spf (s, len, " newname '%.*s'", fc->u.trenameat.newname.len,
					        fc->u.trenameat.newname.str);
		break;
	case P9_RRENAMEAT:
		spf (s, len, "P9_RRENAMEAT tag %u", fc->tag);
		break;
	case P9_TUNLINKAT:
		spf (s, len, "P9_TUNLINKAT tag %u", fc->tag);
		spf (s, len, " dirfid %"PRIu32, fc->u.tunlinkat.dirfid);
		spf (s, len, " name '%.*s'", fc->u.tunlinkat.name.len,
					     fc->u.tunlinkat.name.str);
		spf (s, len, " flags %"PRIu32, fc->u.tunlinkat.flags);
		break;	
	case P9_RUNLINKAT:
		spf (s, len, "P9_RUNLINKAT tag %u", fc->tag);
		break;
	case P9_TVERSION:
		spf (s, len, "P9_TVERSION tag %u", fc->tag);
		spf (s, len, " msize %u", fc->u.tversion.msize);
		spf (s, len, " version '%.*s'", fc->u.tversion.version.len,
					        fc->u.tversion.version.str);
		break;
	case P9_RVERSION:
		spf (s, len, "P9_RVERSION tag %u msize %u",
			fc->tag, fc->u.rversion.msize);
		spf (s, len, " version '%.*s'", fc->u.rversion.version.len,
					        fc->u.rversion.version.str);
		break;
	case P9_TAUTH:
		spf (s, len, "P9_TAUTH tag %u afid %d",
			fc->tag, fc->u.tauth.afid);
		spf (s, len, " uname '%.*s'", fc->u.tauth.uname.len,
					      fc->u.tauth.uname.str);
		spf (s, len, " aname '%.*s'", fc->u.tauth.aname.len,
					      fc->u.tauth.aname.str);
		if (fc->u.tauth.n_uname != P9_NONUNAME)
			spf (s, len, " n_uname %u", fc->u.tauth.n_uname);
		else
			spf (s, len, " n_uname P9_NONUNAME");
		break;
	case P9_RAUTH:
		spf (s, len, "P9_RAUTH tag %u qid ", fc->tag); 
		np_printqid(s, len, &fc->u.rauth.qid);
		break;
	case P9_TATTACH:
		spf (s, len, "P9_TATTACH tag %u", fc->tag);
		spf (s, len, " fid %d afid %d",
				fc->u.tattach.fid, fc->u.tattach.afid);
		spf (s, len, " uname '%.*s'", fc->u.tattach.uname.len,
					      fc->u.tattach.uname.str);
		spf (s, len, " aname '%.*s'", fc->u.tattach.aname.len,
					      fc->u.tattach.aname.str);
		if (fc->u.tattach.n_uname != P9_NONUNAME)
			spf (s, len, " n_uname %u", fc->u.tattach.n_uname);
		else
			spf (s, len, " n_uname P9_NONUNAME");
		break;
	case P9_RATTACH:
		spf (s, len, "P9_RATTACH tag %u qid ", fc->tag); 
		np_printqid(s, len, &fc->u.rattach.qid);
		break;
	case P9_TFLUSH:
		spf (s, len, "P9_TFLUSH tag %u oldtag %u",
				fc->tag, fc->u.tflush.oldtag);
		break;
	case P9_RFLUSH:
		spf (s, len, "P9_RFLUSH tag %u", fc->tag);
		break;
	case P9_TWALK:
		spf (s, len, "P9_TWALK tag %u fid %d newfid %d nwname %d", 
			fc->tag, fc->u.twalk.fid, fc->u.twalk.newfid,
			fc->u.twalk.nwname);
		for(i = 0; i < fc->u.twalk.nwname; i++)
			spf (s, len, " '%.*s'", fc->u.twalk.wnames[i].len,
						fc->u.twalk.wnames[i].str);
		break;
	case P9_RWALK:
		spf (s, len, "P9_RWALK tag %u nwqid %d ",
			fc->tag, fc->u.rwalk.nwqid);
		for(i = 0; i < fc->u.rwalk.nwqid; i++)
			np_printqid(s, len, &fc->u.rwalk.wqids[i]);
		break;
	case P9_TREAD:
		spf (s, len, "P9_TREAD tag %u fid %d offset %"PRIu64" count %u", 
			fc->tag, fc->u.tread.fid, fc->u.tread.offset,
			fc->u.tread.count);
		break;
	case P9_RREAD:
		spf (s, len, "P9_RREAD tag %u count %u", fc->tag,
			fc->u.rread.count);
		np_printdata(s, len, fc->u.rread.data, fc->u.rread.count);
		break;
	case P9_TWRITE:
		spf (s, len, "P9_TWRITE tag %u", fc->tag);
		spf (s, len, " fid %d", fc->u.twrite.fid);
		spf (s, len, " offset %"PRIu64, fc->u.twrite.offset);
		spf (s, len, " count %u", fc->u.twrite.count);
		np_printdata(s, len, fc->u.twrite.data, fc->u.twrite.count);
		break;
	case P9_RWRITE:
		spf (s, len, "P9_RWRITE tag %u count %u", fc->tag, fc->u.rwrite.count);
		break;
	case P9_TCLUNK:
		spf (s, len, "P9_TCLUNK tag %u fid %d", fc->tag, fc->u.tclunk.fid);
		break;
	case P9_RCLUNK:
		spf (s, len, "P9_RCLUNK tag %u", fc->tag);
		break;
	case P9_TREMOVE:
		spf (s, len, "P9_TREMOVE tag %u fid %d", fc->tag, fc->u.tremove.fid);
		break;
	case P9_RREMOVE:
		spf (s, len, "P9_RREMOVE tag %u", fc->tag);
		break;
	/* unused ops that we consider a protocol error */
	case P9_RERROR:
		spf (s, len, "P9_RERROR: deprecated protocol op");
		break;
	case P9_TOPEN:
		spf (s, len, "P9_TOPEN: deprecated protocol op");
		break;
	case P9_ROPEN:
		spf (s, len, "P9_ROPEN: deprecated protocol op");
		break;
	case P9_TCREATE:
		spf (s, len, "P9_TCREATE: deprecated protocol op");
		break;
	case P9_RCREATE:
		spf (s, len, "P9_RCREATE: deprecated protocol op");
		break;
	case P9_TSTAT:
		spf (s, len, "P9_TSTAT: deprecated protocol op");
		break;
	case P9_RSTAT:
		spf (s, len, "P9_RSTAT: deprecated protocol op");
		break;
	default:
		spf (s, len, "unknown protocol op (%d)", fc->type);
		break;
	}
}
