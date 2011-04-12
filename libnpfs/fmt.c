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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

static int
np_printstr(char *s, int len, char *label, Npstr *str)
{
	return snprintf(s,len, " %s '%.*s'", label, str->len, str->str);
}

static int
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

	return snprintf(s, len, "(%.16llx %x '%s')", (unsigned long long)q->path, q->version, buf);
}

int
np_sndump(char *s, int len, u8 *data, int datalen)
{
	int i, n;

	i = n = 0;
	while (i < datalen) {
		n += snprintf(s + n, len - n, "%02x", data[i]);
		if (i%4 == 3)
			n += snprintf(s + n, len - n, " ");
		if (i%32 == 31 && i + 1 < datalen)
			n += snprintf(s + n, len - n, "\n");

		i++;
	}

	return n;
}

int
np_dump(FILE *f, u8 *data, int datalen)
{
	char s[256];
	
	np_sndump(s, sizeof(s), data, datalen);
	return fprintf (f, "%s", s);
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

static int
np_printdents(char *s, int len, u8 *buf, int buflen)
{
	int n = 0;

	if (buflen > 0)	
		n += snprintf(s+n,len-n, "\n");
	/* FIXME: decode directory entries here */
	return np_sndump(s+n, len-n, buf, buflen < 64 ? buflen : 64);
}

static int
np_printdata(char *s, int len, u8 *buf, int buflen)
{
	int n = 0;

	if (buflen > 0)	
		n += snprintf(s+n, len-n, "\n");
	return np_sndump(s+n, len-n, buf, buflen < 64 ? buflen : 64);
}

static int
np_printlocktype(char *s, int len, u8 type)
{
	int n = 0;

	switch (type) {
		case F_RDLCK:
			n += snprintf(s+n,len-n, "%s", "F_RDLCK");
			break;
		case F_WRLCK:
			n += snprintf(s+n,len-n, "%s", "F_WRLCK");
			break;
		case F_UNLCK:
			n += snprintf(s+n,len-n, "%s", "F_UNLCK");
			break;
		default:
			n += snprintf(s+n,len-n, "%u", type);
			break;
	}
	return n;
}

static int
np_printlockstatus(char *s, int len, u8 status)
{
	int n = 0;

	switch (status) {
		case P9_LOCK_SUCCESS:
			n += snprintf(s+n,len-n, "%s", "P9_LOCK_SUCCESS");
			break;
		case P9_LOCK_BLOCKED:
			n += snprintf(s+n,len-n, "%s", "P9_LOCK_BLOCKED");
			break;
		case P9_LOCK_ERROR:
			n += snprintf(s+n,len-n, "%s", "P9_LOCK_ERROR");
			break;
		case P9_LOCK_GRACE:
			n += snprintf(s+n,len-n, "%s", "P9_LOCK_GRACE");
			break;
		default:
			n += snprintf(s+n,len-n, "%u", status);
			break;
	}
	return n;
}

int
np_snprintfcall(char *s, int len, Npfcall *fc) 
{
	int i, n = 0;

	if (!fc)
		return snprintf(s, len, "NULL");

	switch (fc->type) {
	case P9_TLERROR:
		n += snprintf(s+n,len-n, "P9_TLERROR tag %u", fc->tag);
		break;
	case P9_RLERROR:
		n += snprintf(s+n,len-n, "P9_RLERROR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " ecode %"PRIu32, fc->u.rlerror.ecode);
		break;
	case P9_TSTATFS:
		n += snprintf(s+n,len-n, "P9_TSTATFS tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tstatfs.fid);
		break;
	case P9_RSTATFS:
		n += snprintf(s+n,len-n, "P9_RSTATFS tag %u", fc->tag);
		n += snprintf(s+n,len-n, " type %"PRIu32, fc->u.rstatfs.type);
		n += snprintf(s+n,len-n, " bsize %"PRIu32, fc->u.rstatfs.bsize);
		n += snprintf(s+n,len-n, " blocks %"PRIu64, fc->u.rstatfs.blocks);
		n += snprintf(s+n,len-n, " bfree %"PRIu64, fc->u.rstatfs.bfree);
		n += snprintf(s+n,len-n, " bavail %"PRIu64, fc->u.rstatfs.bavail);
		n += snprintf(s+n,len-n, " files %"PRIu64, fc->u.rstatfs.files);
		n += snprintf(s+n,len-n, " ffree %"PRIu64, fc->u.rstatfs.ffree);
		n += snprintf(s+n,len-n, " fsid %"PRIu64, fc->u.rstatfs.fsid);
		n += snprintf(s+n,len-n, " namelen %"PRIu32, fc->u.rstatfs.namelen);
		break;
	case P9_TLOPEN:
		n += snprintf(s+n,len-n, "P9_TLOPEN tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tlopen.fid);
		n += snprintf(s+n,len-n, " mode 0%"PRIo32, fc->u.tlopen.mode);
		break;
	case P9_RLOPEN:
		n += snprintf(s+n,len-n, "P9_RLOPEN tag %u qid ", fc->tag);
		n += np_printqid(s+n,len-n, &fc->u.rlopen.qid);
		n += snprintf(s+n,len-n, " iounit %"PRIu32, fc->u.rlopen.iounit);
		break;
	case P9_TLCREATE:
		n += snprintf(s+n,len-n, "P9_TLCREATE tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tlcreate.fid);
		n += np_printstr(s+n,len-n, "name", &fc->u.tlcreate.name);
		n += snprintf(s+n,len-n, " flags 0x%"PRIx32, fc->u.tlcreate.flags);
		n += snprintf(s+n,len-n, " mode 0%"PRIo32, fc->u.tlcreate.mode);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tlcreate.gid);
		break;
	case P9_RLCREATE:
		n += snprintf(s+n,len-n, "P9_RLCREATE tag %u qid ", fc->tag);
		n += np_printqid(s+n, len-n, &fc->u.rlcreate.qid);
		n += snprintf(s+n,len-n, " iounit %"PRIu32, fc->u.rlcreate.iounit);
		break;
	case P9_TSYMLINK:
		n += snprintf(s+n,len-n, "P9_TSYMLINK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tsymlink.fid);
		n += np_printstr(s+n,len-n, "name", &fc->u.tsymlink.name);
		n += np_printstr(s+n,len-n, "symtgt", &fc->u.tsymlink.symtgt);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tsymlink.gid);
		break;
	case P9_RSYMLINK:
		n += snprintf(s+n,len-n, "P9_RSYMLINK tag %d qid ", fc->tag);
		n += np_printqid(s+n, len-n, &fc->u.rsymlink.qid);
		break;
	case P9_TMKNOD:
		n += snprintf(s+n,len-n, "P9_TMKNOD tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tmknod.fid);
		n += np_printstr(s+n,len-n, "name", &fc->u.tmknod.name);
		n += snprintf(s+n,len-n, " mode 0%"PRIo32, fc->u.tmknod.mode);
		n += snprintf(s+n,len-n, " major %"PRIu32, fc->u.tmknod.major);
		n += snprintf(s+n,len-n, " minor %"PRIu32, fc->u.tmknod.minor);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tmknod.gid);
		break;
	case P9_RMKNOD:
		n += snprintf(s+n,len-n, "P9_RMKNOD tag %d qid ", fc->tag);
		n += np_printqid(s+n, len-n, &fc->u.rsymlink.qid);
		break;
	case P9_TRENAME:
		n += snprintf(s+n,len-n, "P9_TRENAME tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.trename.fid);
		n += snprintf(s+n,len-n, " dfid%"PRIu32, fc->u.trename.dfid);
		n += np_printstr(s+n,len-n, "name", &fc->u.trename.name);
		break;
	case P9_RRENAME:
		n += snprintf(s+n,len-n, "P9_RRENAME tag %u", fc->tag);
		break;
	case P9_TREADLINK:
		n += snprintf(s+n,len-n, "P9_TREADLINK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.treadlink.fid);
		break;
	case P9_RREADLINK:
		n += snprintf(s+n,len-n, "P9_RREADLINK tag %u", fc->tag);
		n += np_printstr(s+n,len-n, "target", &fc->u.rreadlink.target);
		break;
	case P9_TGETATTR:
		n += snprintf(s+n,len-n, "P9_TGETATTR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tgetattr.fid);
		n += snprintf(s+n,len-n, " request_mask 0x%"PRIx64, fc->u.tgetattr.request_mask);
		break;
	case P9_RGETATTR:
		n += snprintf(s+n,len-n, "P9_RGETATTR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " valid 0x%"PRIx64, fc->u.rgetattr.valid);
		n += snprintf(s+n,len-n, " qid ");
		if ((fc->u.rgetattr.valid & P9_GETATTR_INO))
			n += np_printqid(s+n,len-n, &fc->u.rgetattr.qid);
		else
			n += snprintf(s+n,len-n, "X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_MODE))
			n += snprintf(s+n,len-n, " mode 0%"PRIo32, fc->u.rgetattr.mode);
		else
			n += snprintf(s+n,len-n, " mode X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_UID))
			n += snprintf(s+n,len-n, " uid %"PRIu32, fc->u.rgetattr.uid);
		else
			n += snprintf(s+n,len-n, " uid X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_GID))
			n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.rgetattr.gid);
		else
			n += snprintf(s+n,len-n, " gid X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_NLINK))
			n += snprintf(s+n,len-n, " nlink %"PRIu64, fc->u.rgetattr.nlink);
		else
			n += snprintf(s+n,len-n, " nlink X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_RDEV))
			n += snprintf(s+n,len-n, " rdev %"PRIu64, fc->u.rgetattr.rdev);
		else
			n += snprintf(s+n,len-n, " rdev X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_SIZE))
			n += snprintf(s+n,len-n, " size %"PRIu64, fc->u.rgetattr.size);
		else
			n += snprintf(s+n,len-n, " size X");
		n += snprintf(s+n,len-n, " blksize %"PRIu64, fc->u.rgetattr.blksize);
		if ((fc->u.rgetattr.valid & P9_GETATTR_BLOCKS))
			n += snprintf(s+n,len-n, " blocks %"PRIu64, fc->u.rgetattr.blocks);
		else
			n += snprintf(s+n,len-n, " blocks X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_ATIME))
			n += snprintf(s+n,len-n, " atime %s",
				np_timestr(fc->u.rgetattr.atime_sec, fc->u.rgetattr.atime_nsec));
		else
			n += snprintf(s+n,len-n, " atime X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_MTIME))
			n += snprintf(s+n,len-n, " mtime %s",
				np_timestr(fc->u.rgetattr.mtime_sec, fc->u.rgetattr.mtime_nsec));
		else
			n += snprintf(s+n,len-n, " mtime X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_CTIME))
			n += snprintf(s+n,len-n, " ctime %s",
				np_timestr(fc->u.rgetattr.ctime_sec, fc->u.rgetattr.ctime_nsec));
		else
			n += snprintf(s+n,len-n, " ctime X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_BTIME))
			n += snprintf(s+n,len-n, " btime %s",
				np_timestr(fc->u.rgetattr.btime_sec, fc->u.rgetattr.btime_nsec));
		else
			n += snprintf(s+n,len-n, " btime X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_GEN))
			n += snprintf(s+n,len-n, " gen %"PRIu64, fc->u.rgetattr.gen);
		else
			n += snprintf(s+n,len-n, " gen X");
		if ((fc->u.rgetattr.valid & P9_GETATTR_DATA_VERSION))
			n += snprintf(s+n,len-n, " data_version %"PRIu64, fc->u.rgetattr.data_version);
		else
			n += snprintf(s+n,len-n, " data_version X");
		break;
	case P9_TSETATTR:
		n += snprintf(s+n,len-n, "P9_TSETATTR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tsetattr.fid);
		n += snprintf(s+n,len-n, " valid 0x%"PRIx32, fc->u.tsetattr.valid);
		if ((fc->u.tsetattr.valid & P9_SETATTR_MODE))
			n += snprintf(s+n,len-n, " mode 0%"PRIo32, fc->u.tsetattr.mode);
		else
			n += snprintf(s+n,len-n, " mode X");
		if ((fc->u.tsetattr.valid & P9_SETATTR_UID))
			n += snprintf(s+n,len-n, " uid %"PRIu32, fc->u.tsetattr.uid);
		else
			n += snprintf(s+n,len-n, " uid X");
		if ((fc->u.tsetattr.valid & P9_SETATTR_GID))
			n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tsetattr.gid);
		else
			n += snprintf(s+n,len-n, " gid X");
		if ((fc->u.tsetattr.valid & P9_SETATTR_SIZE))
			n += snprintf(s+n,len-n, " size %"PRIu64, fc->u.tsetattr.size);
		else
			n += snprintf(s+n,len-n, " size X");
		if (!(fc->u.tsetattr.valid & P9_SETATTR_ATIME))
			n += snprintf(s+n,len-n, " atime X");
		else if (!(fc->u.tsetattr.valid & P9_SETATTR_ATIME_SET))
			n += snprintf(s+n,len-n, " atime X");
		else
			n += snprintf(s+n,len-n, " atime %s",
				np_timestr(fc->u.tsetattr.atime_sec, fc->u.tsetattr.atime_nsec));
		if (!(fc->u.tsetattr.valid & P9_SETATTR_MTIME))
			n += snprintf(s+n,len-n, " mtime X");
		else if (!(fc->u.tsetattr.valid & P9_SETATTR_MTIME_SET))
			n += snprintf(s+n,len-n, " mtime now");
		else
			n += snprintf(s+n,len-n, " mtime %s",
				np_timestr(fc->u.tsetattr.mtime_sec, fc->u.tsetattr.mtime_nsec));
		break;
	case P9_RSETATTR:
		n += snprintf(s+n,len-n, "P9_RSETATTR tag %u", fc->tag);
		break;
	case P9_TXATTRWALK: /* FIXME */
		n += snprintf(s+n,len-n, "P9_TXATTRWALK tag %u", fc->tag);
		break;
	case P9_RXATTRWALK: /* FIXME */
		n += snprintf(s+n,len-n, "P9_RXATTRWALK tag %u", fc->tag);
		break;
	case P9_TXATTRCREATE: /* FIXME */
		n += snprintf(s+n,len-n, "P9_TXATTRWALKCREATE tag %u", fc->tag);
		break;
	case P9_RXATTRCREATE: /* FIXME */
		n += snprintf(s+n,len-n, "P9_RXATTRWALKCREATE tag %u", fc->tag);
		break;
	case P9_TREADDIR:
		n += snprintf(s+n,len-n, "P9_TREADDIR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.treaddir.fid);
		n += snprintf(s+n,len-n, " offset %"PRIu64, fc->u.treaddir.offset);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.treaddir.count);
		break;
	case P9_RREADDIR:
		n += snprintf(s+n,len-n, "P9_RREADDIR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.rreaddir.count);
		n += np_printdents(s+n,len-n, fc->u.rreaddir.data, fc->u.rreaddir.count);
		break;
	case P9_TFSYNC:
		n += snprintf(s+n,len-n, "P9_TFSYNC tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tfsync.fid);
		break;
	case P9_RFSYNC:
		n += snprintf(s+n,len-n, "P9_RFSYNC tag %u", fc->tag);
		break;
	case P9_TLOCK:
		n += snprintf(s+n,len-n, "P9_TLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tlock.fid);
		n += snprintf(s+n,len-n, " type ");
		n += np_printlocktype(s+n,len-n, fc->u.tlock.type);
		n += snprintf(s+n,len-n, " flags %"PRIu32, fc->u.tlock.flags);
		n += snprintf(s+n,len-n, " start %"PRIu64, fc->u.tlock.start);
		n += snprintf(s+n,len-n, " length %"PRIu64, fc->u.tlock.length);
		n += snprintf(s+n,len-n, " proc_id %"PRIu32, fc->u.tlock.proc_id);
		n += snprintf(s+n,len-n, " client_id %.*s",
			fc->u.tlock.client_id.len, fc->u.tlock.client_id.str);
		break;
	case P9_RLOCK:
		n += snprintf(s+n,len-n, "P9_RLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " status ");
		n += np_printlockstatus(s+n,len-n, fc->u.rlock.status);
		break;
	case P9_TGETLOCK:
		n += snprintf(s+n,len-n, "P9_TGETLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tgetlock.fid);
		n += snprintf(s+n,len-n, " type ");
		n += np_printlocktype(s+n,len-n, fc->u.tgetlock.type);
		n += snprintf(s+n,len-n, " start %"PRIu64, fc->u.tgetlock.start);
		n += snprintf(s+n,len-n, " length %"PRIu64, fc->u.tgetlock.length);
		n += snprintf(s+n,len-n, " proc_id %"PRIu32, fc->u.tgetlock.proc_id);
		n += snprintf(s+n,len-n, " client_id %.*s",
			fc->u.tgetlock.client_id.len, fc->u.tgetlock.client_id.str);
		break;
	case P9_RGETLOCK:
		n += snprintf(s+n,len-n, "P9_RGETLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " type ");
		n += np_printlocktype(s+n,len-n, fc->u.rgetlock.type);
		n += snprintf(s+n,len-n, " start %"PRIu64, fc->u.rgetlock.start);
		n += snprintf(s+n,len-n, " length %"PRIu64, fc->u.rgetlock.length);
		n += snprintf(s+n,len-n, " proc_id %"PRIu32, fc->u.rgetlock.proc_id);
		n += snprintf(s+n,len-n, " client_id %.*s",
			fc->u.rgetlock.client_id.len, fc->u.rgetlock.client_id.str);
		break;
	case P9_TLINK:
		n += snprintf(s+n,len-n, "P9_TLINK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " dfid %"PRIu32, fc->u.tlink.dfid);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tlink.fid);
		n += np_printstr(s+n,len-n, "name", &fc->u.tlink.name);
		break;
	case P9_RLINK:
		n += snprintf(s+n,len-n, "P9_RLINK tag %u", fc->tag);
		break;
	case P9_TMKDIR:
		n += snprintf(s+n,len-n, "P9_TMKDIR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tmkdir.fid);
		n += np_printstr(s+n,len-n, "name", &fc->u.tmkdir.name);
		n += snprintf(s+n,len-n, " mode 0%"PRIo32, fc->u.tmkdir.mode);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tmkdir.gid);
		break;
	case P9_RMKDIR:
		n += snprintf(s+n,len-n, "P9_RMKDIR tag %u qid ", fc->tag);
		n += np_printqid(s+n, len-n, &fc->u.rmkdir.qid);
		break;
#if HAVE_LARGEIO
	case P9_TAREAD:
		n += snprintf(s+n,len-n, "P9_TAREAD tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.taread.fid);
		n += snprintf(s+n,len-n, " datacheck %u", fc->u.taread.datacheck);
		n += snprintf(s+n,len-n, " offset %"PRIu64, fc->u.taread.offset);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.taread.count);
		n += snprintf(s+n,len-n, " rsize %"PRIu32, fc->u.taread.rsize);
		break;
	case P9_RAREAD:
		n += snprintf(s+n,len-n, "P9_RAREAD tag %u", fc->tag);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.raread.count);
		n += np_printdata(s+n,len-n, fc->u.raread.data, fc->u.raread.count);
		break;
	case P9_TAWRITE:
		n += snprintf(s+n,len-n, "P9_TAWRITE tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tawrite.fid);
		n += snprintf(s+n,len-n, " datacheck %u", fc->u.tawrite.datacheck);
		n += snprintf(s+n,len-n, " offset %"PRIu64, fc->u.tawrite.offset);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.tawrite.count);
		n += snprintf(s+n,len-n, " rsize %"PRIu32, fc->u.tawrite.rsize);
		n += np_printdata(s+n,len-n, fc->u.tawrite.data, fc->u.tawrite.rsize);
		break;
	case P9_RAWRITE:
		n += snprintf(s+n,len-n, "P9_RAWRITE.diod tag %u", fc->tag);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.rawrite.count);
		break;
#endif
	case P9_TVERSION:
		n += snprintf(s+n,len-n, "P9_TVERSION tag %u", fc->tag);
		n += snprintf(s+n,len-n, " msize %u", fc->u.tversion.msize);
		n += np_printstr(s+n,len-n, "version", &fc->u.tversion.version);
		break;
	case P9_RVERSION:
		n += snprintf(s+n,len-n, "P9_RVERSION tag %u msize %u",
			fc->tag, fc->u.rversion.msize);
		n += np_printstr(s+n,len-n, "version", &fc->u.rversion.version);
		break;
	case P9_TAUTH:
		n += snprintf(s+n,len-n, "P9_TAUTH tag %u afid %d",
			fc->tag, fc->u.tauth.afid);
		n += np_printstr(s+n,len-n, "uname", &fc->u.tauth.uname);
		n += np_printstr(s+n,len-n, "aname", &fc->u.tauth.aname);
		if (fc->u.tauth.n_uname != P9_NONUNAME)
			n += snprintf(s+n,len-n, " n_uname %u",
					fc->u.tauth.n_uname);
		else
			n += snprintf(s+n,len-n, " n_uname P9_NONUNAME");
		break;
	case P9_RAUTH:
		n += snprintf(s+n,len-n, "P9_RAUTH tag %u qid ", fc->tag); 
		n += np_printqid(s+n, len-n, &fc->u.rauth.qid);
		break;
	case P9_TATTACH:
		n += snprintf(s+n,len-n, "P9_TATTACH tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %d afid %d",
				fc->u.tattach.fid, fc->u.tattach.afid);
		n += np_printstr(s+n,len-n, "uname", &fc->u.tattach.uname);
		n += np_printstr(s+n,len-n, "aname", &fc->u.tattach.aname);
		if (fc->u.tattach.n_uname != P9_NONUNAME)
			n += snprintf(s+n,len-n, " n_uname %u",
					fc->u.tattach.n_uname);
		else
			n += snprintf(s+n,len-n, " n_uname P9_NONUNAME");
		break;
	case P9_RATTACH:
		n += snprintf(s+n,len-n, "P9_RATTACH tag %u qid ", fc->tag); 
		n += np_printqid(s+n,len-n, &fc->u.rattach.qid);
		break;
	case P9_TFLUSH:
		n += snprintf(s+n,len-n, "P9_TFLUSH tag %u oldtag %u",
				fc->tag, fc->u.tflush.oldtag);
		break;
	case P9_RFLUSH:
		n += snprintf(s+n,len-n, "P9_RFLUSH tag %u", fc->tag);
		break;
	case P9_TWALK:
		n += snprintf(s+n,len-n, "P9_TWALK tag %u fid %d newfid %d nwname %d", 
			fc->tag, fc->u.twalk.fid, fc->u.twalk.newfid, fc->u.twalk.nwname);
		for(i = 0; i < fc->u.twalk.nwname; i++)
			n += snprintf(s+n,len-n, " '%.*s'",
				fc->u.twalk.wnames[i].len, fc->u.twalk.wnames[i].str);
		break;
	case P9_RWALK:
		n += snprintf(s+n,len-n, "P9_RWALK tag %u nwqid %d", fc->tag, fc->u.rwalk.nwqid);
		for(i = 0; i < fc->u.rwalk.nwqid; i++)
			n += np_printqid(s+n,len-n, &fc->u.rwalk.wqids[i]);
		break;
		
	case P9_TREAD:
		n += snprintf(s+n,len-n, "P9_TREAD tag %u fid %d offset %llu count %u", 
			fc->tag, fc->u.tread.fid, (unsigned long long)fc->u.tread.offset, fc->u.tread.count);
		break;
		
	case P9_RREAD:
		n += snprintf(s+n,len-n, "P9_RREAD tag %u count %u", fc->tag, fc->u.rread.count);
		n += np_printdata(s+n,len-n, fc->u.rread.data, fc->u.rread.count);
		break;
		
	case P9_TWRITE:
		n += snprintf(s+n,len-n, "P9_TWRITE tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %d", fc->u.twrite.fid);
		n += snprintf(s+n,len-n, " offset %"PRIu64,
					fc->u.twrite.offset);
		n += snprintf(s+n,len-n, " count %u", fc->u.twrite.count);
		n += np_printdata(s+n,len-n, fc->u.twrite.data, fc->u.twrite.count);
		break;
		
	case P9_RWRITE:
		n += snprintf(s+n,len-n, "P9_RWRITE tag %u count %u", fc->tag, fc->u.rwrite.count);
		break;
		
	case P9_TCLUNK:
		n += snprintf(s+n,len-n, "P9_TCLUNK tag %u fid %d", fc->tag,
				fc->u.tclunk.fid);
		break;
		
	case P9_RCLUNK:
		n += snprintf(s+n,len-n, "P9_RCLUNK tag %u", fc->tag);
		break;
		
	case P9_TREMOVE:
		n += snprintf(s+n,len-n, "P9_TREMOVE tag %u fid %d", fc->tag,
					fc->u.tremove.fid);
		break;
		
	case P9_RREMOVE:
		n += snprintf(s+n,len-n, "P9_RREMOVE tag %u", fc->tag);
		break;
		
	default:
		n += snprintf(s+n,len-n, "unknown type %d", fc->type);
		break;
	}

	return n;
}
