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

	return snprintf(s, len, " (%.16llx %x '%s')", (unsigned long long)q->path, q->version, buf);
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
static int
np_printlstat(char *s, int len, struct p9_stat_dotl *lp)
{
	int n;

	n = snprintf(s,len, "qid ");
	n += np_printqid(s+n,len-n, &lp->qid);
	n += snprintf(s+n,len-n, " st_mode %"PRIu32, lp->st_mode);
	n += snprintf(s+n,len-n, " st_uid %"PRIu32, lp->st_uid);
	n += snprintf(s+n,len-n, " st_gid %"PRIu32, lp->st_gid);
	n += snprintf(s+n,len-n, " st_nlink %"PRIu64, lp->st_nlink);
	n += snprintf(s+n,len-n, " st_rdev %"PRIu64, lp->st_rdev);
	n += snprintf(s+n,len-n, " st_size %"PRIu64, lp->st_size);
	n += snprintf(s+n,len-n, " st_blksize %"PRIu64, lp->st_blksize);
	n += snprintf(s+n,len-n, " st_blocks %"PRIu64"\n", lp->st_blocks);
	n += snprintf(s+n,len-n, " st_atime_sec %"PRIu64, lp->st_atime_sec);
	n += snprintf(s+n,len-n, " st_atime_nsec %"PRIu64"\n", lp->st_atime_nsec);
	n += snprintf(s+n,len-n, " st_mtime_sec %"PRIu64, lp->st_mtime_sec);
	n += snprintf(s+n,len-n, " st_mtime_nsec %"PRIu64"\n", lp->st_mtime_nsec);
	n += snprintf(s+n,len-n, " st_ctime_sec %"PRIu64, lp->st_ctime_sec);
	n += snprintf(s+n,len-n, " st_ctime_nsec %"PRIu64"\n", lp->st_ctime_nsec);
	n += snprintf(s+n,len-n, " st_btime_sec %"PRIu64, lp->st_btime_sec);
	n += snprintf(s+n,len-n, " st_btime_nsec %"PRIu64"\n", lp->st_btime_nsec);
	n += snprintf(s+n,len-n, " st_gen %"PRIu64, lp->st_gen);
	n += snprintf(s+n,len-n, " st_data_version %"PRIu64, lp->st_data_version);

	return n;
}

static int
np_printiattr(char *s, int len, struct p9_iattr_dotl *ip)
{
	int n;

	n = snprintf(s,len, " mode %"PRIu32, ip->mode);
	n += snprintf(s+n,len-n, " uid %"PRIu32, ip->uid);
	n += snprintf(s+n,len-n, " gid %"PRIu32, ip->gid);
	n += snprintf(s+n,len-n, " size %"PRIu64, ip->size);
	n += snprintf(s+n,len-n, " atime_sec %"PRIu64, ip->atime_sec);
	n += snprintf(s+n,len-n, " atime_nsec %"PRIu64, ip->atime_nsec);
	n += snprintf(s+n,len-n, " mtime_sec %"PRIu64, ip->mtime_sec);
	n += snprintf(s+n,len-n, " mtime_nsec %"PRIu64, ip->mtime_nsec);

	return n;
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
		n += snprintf(s+n,len-n, " mode %"PRIu32, fc->u.tlopen.mode);
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
		n += snprintf(s+n,len-n, " flags %"PRIu32, fc->u.tlcreate.flags);
		n += snprintf(s+n,len-n, " mode %"PRIu32, fc->u.tlcreate.mode);
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
		n += snprintf(s+n,len-n, " mode %"PRIu32, fc->u.tmknod.mode);
		n += snprintf(s+n,len-n, " major %"PRIu32, fc->u.tmknod.major);
		n += snprintf(s+n,len-n, " minor %"PRIu32, fc->u.tmknod.minor);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tmknod.gid);
		break;
	case P9_RMKNOD:
		n += snprintf(s+n,len-n, "P9_RMKNOD tag %d qid", fc->tag);
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
		n += snprintf(s+n,len-n, " target %s", fc->u.rreadlink.target);
		break;
	case P9_TGETATTR:
		n += snprintf(s+n,len-n, "P9_TGETATTR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tgetattr.fid);
		n += snprintf(s+n,len-n, " request_mask %"PRIu64, fc->u.tgetattr.request_mask);
		break;
	case P9_RGETATTR:
		n += snprintf(s+n,len-n, "P9_RGETATTR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " response_mask %"PRIu64, fc->u.rgetattr.response_mask);
		n += snprintf(s+n,len-n, " STAT: ");
		n += np_printlstat(s+n,len-n, &fc->u.rgetattr.s);
		break;
	case P9_TSETATTR:
		n += snprintf(s+n,len-n, "P9_TSETATTR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tsetattr.fid);
		n += snprintf(s+n,len-n, " valid_mask %"PRIu32, fc->u.tsetattr.valid_mask);
		n += snprintf(s+n,len-n, " IATTR: ");
		n += np_printiattr(s+n,len-n, &fc->u.tsetattr.i);
		break;
	case P9_RSETATTR:
		n += snprintf(s+n,len-n, "P9_RSETATTR tag %u", fc->tag);
		break;
	case P9_TXATTRWALK:
		n += snprintf(s+n,len-n, "P9_TXATTRWALK tag %u", fc->tag); /* FIXME */
		break;
	case P9_RXATTRWALK:
		n += snprintf(s+n,len-n, "P9_RXATTRWALK tag %u", fc->tag); /* FIXME */
		break;
	case P9_TXATTRCREATE:
		n += snprintf(s+n,len-n, "P9_TXATTRWALKCREATE tag %u", fc->tag); /* FIXME */
		break;
	case P9_RXATTRCREATE:
		n += snprintf(s+n,len-n, "P9_RXATTRWALKCREATE tag %u", fc->tag); /* FIXME */
		break;
	case P9_TREADDIR:
		n += snprintf(s+n,len-n, "P9_TREADDIR tag %u ", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.treaddir.fid);
		n += snprintf(s+n,len-n, " offset %"PRIu64, fc->u.treaddir.offset);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.treaddir.count);
		break;
	case P9_RREADDIR:
		n += snprintf(s+n,len-n, "P9_RREADDIR tag %u ", fc->tag);
		n += snprintf(s+n,len-n, " count %"PRIu32, fc->u.rreaddir.count);
		/* FIXME decode dents */
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
		/* FIXME decode flock */
		break;
	case P9_RLOCK:
		n += snprintf(s+n,len-n, "P9_RLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " status %u", fc->u.rlock.status);
		break;
	case P9_TGETLOCK:
		n += snprintf(s+n,len-n, "P9_TGETLOCK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tgetlock.fid);
		/* FIXME decode getlock */
		break;
	case P9_RGETLOCK:
		n += snprintf(s+n,len-n, "P9_RGETLOCK tag %u", fc->tag);
		/* FIXME decode getlock */
		break;
	case P9_TLINK:
		n += snprintf(s+n,len-n, "P9_TLINK tag %u", fc->tag);
		n += snprintf(s+n,len-n, " dfid %"PRIu32, fc->u.tlink.dfid);
		n += snprintf(s+n,len-n, " oldfid %"PRIu32, fc->u.tlink.oldfid);
		n += snprintf(s+n,len-n, " newpath %.*s",
			fc->u.tlink.newpath.len, fc->u.tlink.newpath.str);
		break;
	case P9_RLINK:
		n += snprintf(s+n,len-n, "P9_RLINK tag %u", fc->tag);
		break;
	case P9_TMKDIR:
		n += snprintf(s+n,len-n, "P9_TMKDIR tag %u", fc->tag);
		n += snprintf(s+n,len-n, " fid %"PRIu32, fc->u.tmkdir.fid);
		n += snprintf(s+n,len-n, " name %.*s",
			fc->u.tmkdir.name.len, fc->u.tmkdir.name.str);
		n += snprintf(s+n,len-n, " mode %"PRIu32, fc->u.tmkdir.mode);
		n += snprintf(s+n,len-n, " gid %"PRIu32, fc->u.tmkdir.gid);
		break;
	case P9_RMKDIR:
		n += snprintf(s+n,len-n, "P9_RMKDIR tag %u qid ", fc->tag);
		n += np_printqid(s+n, len-n, &fc->u.rmkdir.qid);
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
		n += snprintf(s+n,len-n, "P9_ROPEN tag %u", fc->tag);
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
		n += snprintf(s+n,len-n, "P9_RCREATE tag %u", fc->tag);
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
#if HAVE_LARGEIO
	case P9_TAREAD:
		n += snprintf(s+n,len-n, "P9_TAREAD.diod tag %u fid %d datacheck %d offset %llu count %u rsize %u", 
			fc->tag, fc->fid, fc->datacheck, (unsigned long long)fc->offset,
			fc->count, fc->rsize);
		break;
	case P9_RAREAD:
		n += snprintf(s+n,len-n, "P9_RAREAD.diod tag %u count %u data ", fc->tag, fc->count);
		n += np_printdata(s+n,len-n, fc->data, fc->count);
		break;
		
	case P9_TAWRITE:
		n += snprintf(s+n,len-n, "P9_TAWRITE.diod tag %u fid %d datacheck %d offset %llu count %u rsize %u data ",
			fc->tag, fc->fid, fc->datacheck, (unsigned long long)fc->offset,
			fc->count, fc->rsize);
		n += np_printdata(s+n,len-n, fc->data, fc->rsize);
		break;
	case P9_RAWRITE:
		n += snprintf(s+n,len-n, "P9_RAWRITE.diod tag %u count %u", fc->tag, fc->count);
		break;
#endif
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
