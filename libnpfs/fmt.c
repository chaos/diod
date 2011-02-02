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
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

static int
np_printperm(char *s, int len, int perm)
{
	int n;
	char b[10];

	n = 0;
	if (perm & P9_DMDIR)
		b[n++] = 'd';
	if (perm & P9_DMAPPEND)
		b[n++] = 'a';
	if (perm & P9_DMAUTH)
		b[n++] = 'A';
	if (perm & P9_DMEXCL)
		b[n++] = 'l';
	if (perm & P9_DMTMP)
		b[n++] = 't';
	if (perm & P9_DMDEVICE)
		b[n++] = 'D';
	if (perm & P9_DMSOCKET)
		b[n++] = 'S';
	if (perm & P9_DMNAMEDPIPE)
		b[n++] = 'P';
        if (perm & P9_DMSYMLINK)
                b[n++] = 'L';
        b[n] = '\0';

        return snprintf(s, len, "%s%03o", b, perm&0777);
}             

static int
np_printqid(char *s, int len, Npqid *q)
{
	int n;
	char buf[10];

	n = 0;
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
np_snprintstat(char *s, int len, Npstat *st, int dotu)
{
	int n;

	n = snprintf(s, len, "'%.*s' '%.*s' '%.*s' '%.*s' q ", 
		st->name.len, st->name.str, st->uid.len, st->uid.str,
		st->gid.len, st->gid.str, st->muid.len, st->muid.str);

	n += np_printqid(s + n, len - n, &st->qid);
	n += snprintf(s + n, len - n, " m ");
	n += np_printperm(s + n, len - n, st->mode);
	n += snprintf(s + n, len - n, " at %d mt %d l %llu t %d d %d",
		st->atime, st->mtime, (unsigned long long)st->length, st->type, st->dev);
	if (dotu)
		n += snprintf(s + n, len - n, " ext '%.*s'", st->extension.len, 
			st->extension.str);

	return n;
}

int
np_printstat(FILE *f, Npstat *st, int dotu)
{
	char s[256];

	np_snprintstat(s, sizeof(s), st, dotu);
	return fprintf (f, "%s", s);
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
	//n += snprintf(s + n, len - n, "\n");

	return n;
}

int
np_dump(FILE *f, u8 *data, int datalen)
{
	char s[256];
	
	np_sndump(s, sizeof(s), data, datalen);
	return fprintf (f, "%s", s);
}

#if HAVE_DOTL
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
	/* FIXME: decode directory entries here */
	return np_sndump(s, len, buf, buflen < 64 ? buflen : 64);
}
#endif

static int
np_printdata(char *s, int len, u8 *buf, int buflen)
{
	return np_sndump(s, len, buf, buflen<64?buflen:64);
}

int
np_dumpdata(char *s, int len, u8 *buf, int buflen)
{
	return np_sndump(s, len, buf, buflen);
}

int
np_snprintfcall(char *s, int len, Npfcall *fc, int dotu) 
{
	int i, n = 0;

	if (!fc)
		return snprintf(s, len, "NULL");

	switch (fc->type) {
#if HAVE_DOTL
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
		n += snprintf(s+n,len-n, " name %.*s",
			fc->u.tlcreate.name.len, fc->u.tlcreate.name.str);
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
		n += snprintf(s+n,len-n, " name %.*s",
			fc->u.tsymlink.name.len, fc->u.tsymlink.name.str);
		n += snprintf(s+n,len-n, " symtgt %.*s",
			fc->u.tsymlink.symtgt.len, fc->u.tsymlink.symtgt.str);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tsymlink.gid);
		break;
	case P9_RSYMLINK:
		n += snprintf(s+n,len-n, "P9_RSYMLINK tag %d qid ", fc->tag);
		n += np_printqid(s+n, len-n, &fc->u.rsymlink.qid);
		break;
	case P9_TMKNOD:
		n += snprintf(s+n,len-n, "P9_TMKNOD tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tmknod.fid);
		n += snprintf(s+n,len-n, " name %.*s",
			fc->u.tmknod.name.len, fc->u.tmknod.name.str);
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
		n += snprintf(s+n,len-n, " newdirfid %"PRIu32, fc->u.trename.newdirfid);
		n += snprintf(s+n,len-n, " name %.*s",
			fc->u.trename.name.len, fc->u.trename.name.str);
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
		n += snprintf(s+n,len-n, " target %.*s",
			fc->u.rreadlink.target.len, fc->u.rreadlink.target.str);
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
		n += snprintf(s+n,len-n, " type %u", fc->u.tlock.fl.type);
		n += snprintf(s+n,len-n, " flags %"PRIu32, fc->u.tlock.fl.flags);
		n += snprintf(s+n,len-n, " start %"PRIu64, fc->u.tlock.fl.start);
		n += snprintf(s+n,len-n, " length %"PRIu64, fc->u.tlock.fl.length);
		n += snprintf(s+n,len-n, " proc_id %"PRIu32, fc->u.tlock.fl.proc_id);
		n += snprintf(s+n,len-n, " client_id %.*s",
			fc->u.tlock.fl.client_id.len, fc->u.tlock.fl.client_id.str);
		break;
	case P9_RLOCK:
		n += snprintf(s+n,len-n, "P9_RLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " status %u", fc->u.rlock.status);
		break;
	case P9_TGETLOCK:
		n += snprintf(s+n,len-n, "P9_TGETLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tgetlock.fid);
		n += snprintf(s+n,len-n, " type %u", fc->u.tgetlock.gl.type);
		n += snprintf(s+n,len-n, " start %"PRIu64, fc->u.tgetlock.gl.start);
		n += snprintf(s+n,len-n, " length %"PRIu64, fc->u.tgetlock.gl.length);
		n += snprintf(s+n,len-n, " proc_id %"PRIu32, fc->u.tgetlock.gl.proc_id);
		n += snprintf(s+n,len-n, " client_id %.*s",
			fc->u.tgetlock.gl.client_id.len, fc->u.tgetlock.gl.client_id.str);
		break;
	case P9_RGETLOCK:
		n += snprintf(s+n,len-n, "P9_RGETLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " type %u", fc->u.rgetlock.gl.type);
		n += snprintf(s+n,len-n, " start %"PRIu64, fc->u.rgetlock.gl.start);
		n += snprintf(s+n,len-n, " length %"PRIu64, fc->u.rgetlock.gl.length);
		n += snprintf(s+n,len-n, " proc_id %"PRIu32, fc->u.rgetlock.gl.proc_id);
		n += snprintf(s+n,len-n, " client_id %.*s",
			fc->u.rgetlock.gl.client_id.len, fc->u.rgetlock.gl.client_id.str);
		break;
	case P9_TLINK:
		n += snprintf(s+n,len-n, "P9_TLINK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " dfid %"PRIu32, fc->u.tlink.dfid);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tlink.fid);
		n += snprintf(s+n,len-n, " name %.*s",
			fc->u.tlink.name.len, fc->u.tlink.name.str);
		break;
	case P9_RLINK:
		n += snprintf(s+n,len-n, "P9_RLINK tag %u", fc->tag);
		break;
	case P9_TMKDIR:
		n += snprintf(s+n,len-n, "P9_TMKDIR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tmkdir.fid);
		n += snprintf(s+n,len-n, " name %.*s",
			fc->u.tmkdir.name.len, fc->u.tmkdir.name.str);
		n += snprintf(s+n,len-n, " mode 0%"PRIo32, fc->u.tmkdir.mode);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tmkdir.gid);
		break;
	case P9_RMKDIR:
		n += snprintf(s+n,len-n, "P9_RMKDIR tag %u qid ", fc->tag);
		n += np_printqid(s+n, len-n, &fc->u.rmkdir.qid);
		break;
#endif
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
		n += snprintf(s+n,len-n, " DATA ");
		n += np_printdata(s+n,len-n, fc->u.raread.data, fc->u.raread.count);
		break;
		
	case P9_TAWRITE:
		n += snprintf(s+n,len-n, "P9_TAWRITE tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tawrite.fid);
		n += snprintf(s+n,len-n, " datacheck %u", fc->u.tawrite.datacheck);
		n += snprintf(s+n,len-n, " offset %"PRIu64, fc->u.tawrite.offset);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.tawrite.count);
		n += snprintf(s+n,len-n, " rsize %"PRIu32, fc->u.tawrite.rsize);
		n += snprintf(s+n,len-n, " DATA ");
		n += np_printdata(s+n,len-n, fc->u.tawrite.data, fc->u.tawrite.rsize);
		break;
	case P9_RAWRITE:
		n += snprintf(s+n,len-n, "P9_RAWRITE.diod tag %u", fc->tag);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.rawrite.count);
		break;
#endif
	case P9_TVERSION:
		n += snprintf(s+n,len-n, "P9_TVERSION tag %u msize %u version '%.*s'", 
			fc->tag, fc->msize, fc->version.len, fc->version.str);
		break;

	case P9_RVERSION:
		n += snprintf(s+n,len-n, "P9_RVERSION tag %u msize %u version '%.*s'", 
			fc->tag, fc->msize, fc->version.len, fc->version.str);
		break;

	case P9_TAUTH:
		n += snprintf(s+n,len-n, "P9_TAUTH tag %u afid %d uname %.*s aname %.*s",
			fc->tag, fc->afid, fc->uname.len, fc->uname.str, 
			fc->aname.len, fc->aname.str);
		break;

	case P9_RAUTH:
		n += snprintf(s+n,len-n, "P9_RAUTH tag %u qid ", fc->tag); 
		n += np_printqid(s+n, len-n, &fc->qid);
		break;

	case P9_TATTACH:
		n += snprintf(s+n,len-n, "P9_TATTACH tag %u fid %d afid %d uname %.*s aname %.*s n_uname %u",
			fc->tag, fc->fid, fc->afid, fc->uname.len, fc->uname.str, 
			fc->aname.len, fc->aname.str, fc->n_uname);
		break;

	case P9_RATTACH:
		n += snprintf(s+n,len-n, "P9_RATTACH tag %u qid ", fc->tag); 
		n += np_printqid(s+n,len-n, &fc->qid);
		break;

	case P9_RERROR:
		n += snprintf(s+n,len-n, "P9_RERROR tag %u ename %.*s", fc->tag, 
			fc->ename.len, fc->ename.str);
		if (dotu)
			n += snprintf(s+n,len-n, " ecode %d", fc->ecode);
		break;

	case P9_TFLUSH:
		n += snprintf(s+n,len-n, "P9_TFLUSH tag %u oldtag %u", fc->tag, fc->oldtag);
		break;

	case P9_RFLUSH:
		n += snprintf(s+n,len-n, "P9_RFLUSH tag %u", fc->tag);
		break;

	case P9_TWALK:
		n += snprintf(s+n,len-n, "P9_TWALK tag %u fid %d newfid %d nwname %d", 
			fc->tag, fc->fid, fc->newfid, fc->nwname);
		for(i = 0; i < fc->nwname; i++)
			n += snprintf(s+n,len-n, " '%.*s'", fc->wnames[i].len, 
				fc->wnames[i].str);
		break;
		
	case P9_RWALK:
		n += snprintf(s+n,len-n, "P9_RWALK tag %u nwqid %d", fc->tag, fc->nwqid);
		for(i = 0; i < fc->nwqid; i++)
			n += np_printqid(s+n,len-n, &fc->wqids[i]);
		break;
		
	case P9_TOPEN:
		n += snprintf(s+n,len-n, "P9_TOPEN tag %u fid %d mode %d", fc->tag, fc->fid, 
			fc->mode);
		break;
		
	case P9_ROPEN:
		n += snprintf(s+n,len-n, "P9_ROPEN tag %u qid ", fc->tag);
		n += np_printqid(s+n,len-n, &fc->qid);
		n += snprintf(s+n,len-n, " iounit %d", fc->iounit);
		break;
		
	case P9_TCREATE:
		n += snprintf(s+n,len-n, "P9_TCREATE tag %u fid %d name %.*s perm ",
			fc->tag, fc->fid, fc->name.len, fc->name.str);
		n += np_printperm(s+n,len-n, fc->perm);
		n += snprintf(s+n,len-n, " mode %d", fc->mode);
		if (dotu)
			n += snprintf(s+n,len-n, " ext %.*s", fc->extension.len,
				fc->extension.str);
		break;
		
	case P9_RCREATE:
		n += snprintf(s+n,len-n, "P9_RCREATE tag %u qid ", fc->tag);
		n += np_printqid(s+n,len-n, &fc->qid);
		n += snprintf(s+n,len-n, " iounit %d", fc->iounit);
		break;
		
	case P9_TREAD:
		n += snprintf(s+n,len-n, "P9_TREAD tag %u fid %d offset %llu count %u", 
			fc->tag, fc->fid, (unsigned long long)fc->offset, fc->count);
		break;
		
	case P9_RREAD:
		n += snprintf(s+n,len-n, "P9_RREAD tag %u count %u data ", fc->tag, fc->count);
		n += np_printdata(s+n,len-n, fc->data, fc->count);
		break;
		
	case P9_TWRITE:
		n += snprintf(s+n,len-n, "P9_TWRITE tag %u fid %d offset %llu count %u data ",
			fc->tag, fc->fid, (unsigned long long)fc->offset, fc->count);
		n += np_printdata(s+n,len-n, fc->data, fc->count);
		break;
		
	case P9_RWRITE:
		n += snprintf(s+n,len-n, "P9_RWRITE tag %u count %u", fc->tag, fc->count);
		break;
		
	case P9_TCLUNK:
		n += snprintf(s+n,len-n, "P9_TCLUNK tag %u fid %d", fc->tag, fc->fid);
		break;
		
	case P9_RCLUNK:
		n += snprintf(s+n,len-n, "P9_RCLUNK tag %u", fc->tag);
		break;
		
	case P9_TREMOVE:
		n += snprintf(s+n,len-n, "P9_TREMOVE tag %u fid %d", fc->tag, fc->fid);
		break;
		
	case P9_RREMOVE:
		n += snprintf(s+n,len-n, "P9_RREMOVE tag %u", fc->tag);
		break;
		
	case P9_TSTAT:
		n += snprintf(s+n,len-n, "P9_TSTAT tag %u fid %d", fc->tag, fc->fid);
		break;
		
	case P9_RSTAT:
		n += snprintf(s+n,len-n, "P9_RSTAT tag %u ", fc->tag);
		n += np_snprintstat(s+n,len-n, &fc->stat, dotu);
		break;
		
	case P9_TWSTAT:
		n += snprintf(s+n,len-n, "P9_TWSTAT tag %u fid %d ", fc->tag, fc->fid);
		n += np_snprintstat(s+n,len-n, &fc->stat, dotu);
		break;
		
	case P9_RWSTAT:
		n += snprintf(s+n,len-n, "P9_RWSTAT tag %u", fc->tag);
		break;
	default:
		n += snprintf(s+n,len-n, "unknown type %d", fc->type);
		break;
	}

	return n;
}

int
np_printfcall(FILE *f, Npfcall *fc, int dotu)
{
	char s[256];

	np_snprintfcall (s, sizeof(s), fc, dotu);

	return fprintf (f, "%s", s);
}
