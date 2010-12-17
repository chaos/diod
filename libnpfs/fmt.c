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
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

static int
np_printperm(char *s, int len, int perm)
{
	int n;
	char b[10];

	n = 0;
	if (perm & Dmdir)
		b[n++] = 'd';
	if (perm & Dmappend)
		b[n++] = 'a';
	if (perm & Dmauth)
		b[n++] = 'A';
	if (perm & Dmexcl)
		b[n++] = 'l';
	if (perm & Dmtmp)
		b[n++] = 't';
	if (perm & Dmdevice)
		b[n++] = 'D';
	if (perm & Dmsocket)
		b[n++] = 'S';
	if (perm & Dmnamedpipe)
		b[n++] = 'P';
        if (perm & Dmsymlink)
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

#ifdef _WIN32
	return snprintf(s, len, " (%.16I64x %x '%s')", (unsigned long long)q->path, q->version, buf);
#else
	return snprintf(s, len, " (%.16llx %x '%s')", (unsigned long long)q->path, q->version, buf);
#endif
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
#ifdef _WIN32
	n += snprintf(s + n, len - n, " at %d mt %d l %I64u t %d d %d",
		st->atime, st->mtime, (unsigned long long)st->length, st->type, st->dev);
#else
	n += snprintf(s + n, len - n, " at %d mt %d l %llu t %d d %d",
		st->atime, st->mtime, (unsigned long long)st->length, st->type, st->dev);
#endif
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
	int i, n, type, fid, tag;

	if (!fc)
		return snprintf(s, len, "NULL");

	type = fc->type;
	fid = fc->fid;
	tag = fc->tag;

	n = 0;
	switch (type) {
	case Tversion:
		n += snprintf(s+n,len-n, "Tversion tag %u msize %u version '%.*s'", 
			tag, fc->msize, fc->version.len, fc->version.str);
		break;

	case Rversion:
		n += snprintf(s+n,len-n, "Rversion tag %u msize %u version '%.*s'", 
			tag, fc->msize, fc->version.len, fc->version.str);
		break;

	case Tauth:
		n += snprintf(s+n,len-n, "Tauth tag %u afid %d uname %.*s aname %.*s",
			tag, fc->afid, fc->uname.len, fc->uname.str, 
			fc->aname.len, fc->aname.str);
		break;

	case Rauth:
		n += snprintf(s+n,len-n, "Rauth tag %u qid ", tag); 
		n += np_printqid(s+n, len-n, &fc->qid);
		break;

	case Tattach:
		n += snprintf(s+n,len-n, "Tattach tag %u fid %d afid %d uname %.*s aname %.*s n_uname %u",
			tag, fid, fc->afid, fc->uname.len, fc->uname.str, 
			fc->aname.len, fc->aname.str, fc->n_uname);
		break;

	case Rattach:
		n += snprintf(s+n,len-n, "Rattach tag %u qid ", tag); 
		n += np_printqid(s+n,len-n, &fc->qid);
		break;

	case Rerror:
		n += snprintf(s+n,len-n, "Rerror tag %u ename %.*s", tag, 
			fc->ename.len, fc->ename.str);
		if (dotu)
			n += snprintf(s+n,len-n, " ecode %d", fc->ecode);
		break;

	case Tflush:
		n += snprintf(s+n,len-n, "Tflush tag %u oldtag %u", tag, fc->oldtag);
		break;

	case Rflush:
		n += snprintf(s+n,len-n, "Rflush tag %u", tag);
		break;

	case Twalk:
		n += snprintf(s+n,len-n, "Twalk tag %u fid %d newfid %d nwname %d", 
			tag, fid, fc->newfid, fc->nwname);
		for(i = 0; i < fc->nwname; i++)
			n += snprintf(s+n,len-n, " '%.*s'", fc->wnames[i].len, 
				fc->wnames[i].str);
		break;
		
	case Rwalk:
		n += snprintf(s+n,len-n, "Rwalk tag %u nwqid %d", tag, fc->nwqid);
		for(i = 0; i < fc->nwqid; i++)
			n += np_printqid(s+n,len-n, &fc->wqids[i]);
		break;
		
	case Topen:
		n += snprintf(s+n,len-n, "Topen tag %u fid %d mode %d", tag, fid, 
			fc->mode);
		break;
		
	case Ropen:
		n += snprintf(s+n,len-n, "Ropen tag %u", tag);
		n += np_printqid(s+n,len-n, &fc->qid);
		n += snprintf(s+n,len-n, " iounit %d", fc->iounit);
		break;
		
	case Tcreate:
		n += snprintf(s+n,len-n, "Tcreate tag %u fid %d name %.*s perm ",
			tag, fid, fc->name.len, fc->name.str);
		n += np_printperm(s+n,len-n, fc->perm);
		n += snprintf(s+n,len-n, " mode %d", fc->mode);
		if (dotu)
			n += snprintf(s+n,len-n, " ext %.*s", fc->extension.len,
				fc->extension.str);
		break;
		
	case Rcreate:
		n += snprintf(s+n,len-n, "Rcreate tag %u", tag);
		n += np_printqid(s+n,len-n, &fc->qid);
		n += snprintf(s+n,len-n, " iounit %d", fc->iounit);
		break;
		
	case Tread:
#ifdef _WIN32
		n += snprintf(s+n,len-n, "Tread tag %u fid %d offset %I64u count %u", 
			tag, fid, (unsigned long long)fc->offset, fc->count);
#else
		n += snprintf(s+n,len-n, "Tread tag %u fid %d offset %llu count %u", 
			tag, fid, (unsigned long long)fc->offset, fc->count);
#endif
		break;
		
	case Rread:
		n += snprintf(s+n,len-n, "Rread tag %u count %u data ", tag, fc->count);
		n += np_printdata(s+n,len-n, fc->data, fc->count);
		break;
		
	case Twrite:
#ifdef _WIN32
		n += snprintf(s+n,len-n, "Twrite tag %u fid %d offset %I64u count %u data ",
			tag, fid, (unsigned long long)fc->offset, fc->count);
#else
		n += snprintf(s+n,len-n, "Twrite tag %u fid %d offset %llu count %u data ",
			tag, fid, (unsigned long long)fc->offset, fc->count);
#endif
		n += np_printdata(s+n,len-n, fc->data, fc->count);
		break;
		
	case Rwrite:
		n += snprintf(s+n,len-n, "Rwrite tag %u count %u", tag, fc->count);
		break;
		
	case Tclunk:
		n += snprintf(s+n,len-n, "Tclunk tag %u fid %d", tag, fid);
		break;
		
	case Rclunk:
		n += snprintf(s+n,len-n, "Rclunk tag %u", tag);
		break;
		
	case Tremove:
		n += snprintf(s+n,len-n, "Tremove tag %u fid %d", tag, fid);
		break;
		
	case Rremove:
		n += snprintf(s+n,len-n, "Rremove tag %u", tag);
		break;
		
	case Tstat:
		n += snprintf(s+n,len-n, "Tstat tag %u fid %d", tag, fid);
		break;
		
	case Rstat:
		n += snprintf(s+n,len-n, "Rstat tag %u ", tag);
		n += np_snprintstat(s+n,len-n, &fc->stat, dotu);
		break;
		
	case Twstat:
		n += snprintf(s+n,len-n, "Twstat tag %u fid %d ", tag, fid);
		n += np_snprintstat(s+n,len-n, &fc->stat, dotu);
		break;
		
	case Rwstat:
		n += snprintf(s+n,len-n, "Rwstat tag %u", tag);
		break;
#if HAVE_LARGEIO
	case Taread:
		n += snprintf(s+n,len-n, "Taread tag %u fid %d datacheck %d offset %llu count %u rsize %u", 
			tag, fid, fc->datacheck, (unsigned long long)fc->offset,
			fc->count, fc->rsize);
		break;
	case Raread:
		n += snprintf(s+n,len-n, "Raread tag %u count %u data ", tag, fc->count);
		n += np_printdata(s+n,len-n, fc->data, fc->count);
		break;
		
	case Tawrite:
		n += snprintf(s+n,len-n, "Tawrite tag %u fid %d datacheck %d offset %llu count %u rsize %u data ",
			tag, fid, fc->datacheck, (unsigned long long)fc->offset,
			fc->count, fc->rsize);
		n += np_printdata(s+n,len-n, fc->data, fc->rsize);
		break;
	case Rawrite:
		n += snprintf(s+n,len-n, "Rawrite tag %u count %u", tag, fc->count);
		break;
#endif
#if HAVE_DOTL
	case Tstatfs:
		n += snprintf(s+n,len-n, "Tstatfs tag %u fid %d", tag, fid);
		break;
	case Rstatfs:
		n += snprintf(s+n,len-n, "Rstatfs tag %u type %d bsize %d blocks %lld bfree %llu bavail %llu files %llu ffree %llu namelen fsid %llu %d", tag,
				fc->statfs.type,
				fc->statfs.bsize,
				(unsigned long long)fc->statfs.blocks,
				(unsigned long long)fc->statfs.bfree,
				(unsigned long long)fc->statfs.bavail,
				(unsigned long long)fc->statfs.files,
				(unsigned long long)fc->statfs.ffree,
				(unsigned long long)fc->statfs.fsid,
				fc->statfs.namelen);
		break;

	case Tlock:
		n += snprintf(s+n,len-n, "Tlock tag %u fid %d cmd %d "
				"type %d pid %llu start %llu end %llu", tag,
				fid, fc->cmd, fc->lock.type,
				(unsigned long long)fc->lock.pid,
				(unsigned long long)fc->lock.start,
				(unsigned long long)fc->lock.end);
		break;
	case Rlock:
		n += snprintf(s+n,len-n, "Rlock tag %u type %d "
				"pid %llu start %llu end %llu", tag,
				fc->lock.type,
				(unsigned long long)fc->lock.pid,
				(unsigned long long)fc->lock.start,
				(unsigned long long)fc->lock.end);
		break;

	case Tflock:
		n += snprintf(s+n,len-n, "Tflock tag %u fid %d cmd %d", tag, fid, 
			fc->cmd);
		break;
	case Rflock:
		n += snprintf(s+n,len-n, "Rflock tag %u", tag);
		break;

	case Trename:
		n += snprintf(s+n,len-n, "Rrename tag %u fid %d newdirfid %d "
				"newname %.*s",
				tag, fid, fc->newdirfid,
				fc->newname.len, fc->newname.str);
		break;
	case Rrename:
		n += snprintf(s+n,len-n, "Rrename tag %u", tag);
		break;
#endif
	default:
		n += snprintf(s+n,len-n, "unknown type %d", type);
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
