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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <zlib.h>
#include "npfs.h"
#include "npfsimpl.h"

struct cbuf {
	unsigned char *sp;
	unsigned char *p;
	unsigned char *ep;
};

static inline void
buf_init(struct cbuf *buf, void *data, int datalen)
{
	buf->sp = buf->p = data;
	buf->ep = data + datalen;
}

static inline int
buf_check_overflow(struct cbuf *buf)
{
	return buf->p > buf->ep;
}

static inline int
buf_check_size(struct cbuf *buf, int len)
{
	if (buf->p+len > buf->ep) {
		if (buf->p < buf->ep)
			buf->p = buf->ep + 1;

		return 0;
	}

	return 1;
}

static inline void *
buf_alloc(struct cbuf *buf, int len)
{
	void *ret = NULL;

	if (buf_check_size(buf, len)) {
		ret = buf->p;
		buf->p += len;
	}

	return ret;
}

static inline void
buf_put_int8(struct cbuf *buf, u8 val, u8* pval)
{
	if (buf_check_size(buf, 1)) {
		buf->p[0] = val;
		buf->p++;

		if (pval)
			*pval = val;
	}
}

static inline void
buf_put_int16(struct cbuf *buf, u16 val, u16 *pval)
{
	if (buf_check_size(buf, 2)) {
		buf->p[0] = val;
		buf->p[1] = val >> 8;
		buf->p += 2;

		if (pval)
			*pval = val;

	}
}

static inline void
buf_put_int32(struct cbuf *buf, u32 val, u32 *pval)
{
	if (buf_check_size(buf, 4)) {
		buf->p[0] = val;
		buf->p[1] = val >> 8;
		buf->p[2] = val >> 16;
		buf->p[3] = val >> 24;
		buf->p += 4;

		if (pval)
			*pval = val;
	}
}

static inline void
buf_put_int64(struct cbuf *buf, u64 val, u64 *pval)
{
	if (buf_check_size(buf, 8)) {
		buf->p[0] = val;
		buf->p[1] = val >> 8;
		buf->p[2] = val >> 16;
		buf->p[3] = val >> 24;
		buf->p[4] = val >> 32;
		buf->p[5] = val >> 40;
		buf->p[6] = val >> 48;
		buf->p[7] = val >> 56;
		buf->p += 8;

		if (pval)
			*pval = val;
	}
}

static inline void
buf_put_str(struct cbuf *buf, char *s, Npstr *ps)
{
	int slen = 0;

	if (s)
		slen = strlen(s);

	if (buf_check_size(buf, 2+slen)) {
		ps->len = slen;
		buf_put_int16(buf, slen, NULL);
		ps->str = buf_alloc(buf, slen);
		memmove(ps->str, s, slen);
	}
}

static inline void
buf_put_qid(struct cbuf *buf, Npqid *qid, Npqid *pqid)
{
	buf_put_int8(buf, qid->type, &pqid->type);
	buf_put_int32(buf, qid->version, &pqid->version);
	buf_put_int64(buf, qid->path, &pqid->path);
}

static inline void
buf_put_wstat(struct cbuf *bufp, Npwstat *wstat, Npstat* stat, int statsz, int dotu)
{
	buf_put_int16(bufp, statsz, &stat->size);
	buf_put_int16(bufp, wstat->type, &stat->type);
	buf_put_int32(bufp, wstat->dev, &stat->dev);
	buf_put_qid(bufp, &wstat->qid, &stat->qid);
	buf_put_int32(bufp, wstat->mode, &stat->mode);
	buf_put_int32(bufp, wstat->atime, &stat->atime);
	buf_put_int32(bufp, wstat->mtime, &stat->mtime);
	buf_put_int64(bufp, wstat->length, &stat->length);

	buf_put_str(bufp, wstat->name, &stat->name);
	buf_put_str(bufp, wstat->uid, &stat->uid);
	buf_put_str(bufp, wstat->gid, &stat->gid);
	buf_put_str(bufp, wstat->muid, &stat->muid);

	if (dotu) {
		buf_put_str(bufp, wstat->extension, &stat->extension);
		buf_put_int32(bufp, wstat->n_uid, &stat->n_uid);
		buf_put_int32(bufp, wstat->n_gid, &stat->n_gid);
		buf_put_int32(bufp, wstat->n_muid, &stat->n_muid);
	} else
		np_strzero(&stat->extension);
}

static inline u8
buf_get_int8(struct cbuf *buf)
{
	u8 ret = 0;

	if (buf_check_size(buf, 1)) {
		ret = buf->p[0];
		buf->p++;
	}

	return ret;
}

static inline u16
buf_get_int16(struct cbuf *buf)
{
	u16 ret = 0;

	if (buf_check_size(buf, 2)) {
		ret = buf->p[0] | (buf->p[1] << 8);
		buf->p += 2;
	}

	return ret;
}

static inline u32
buf_get_int32(struct cbuf *buf)
{
	u32 ret = 0;

	if (buf_check_size(buf, 4)) {
		ret = buf->p[0] | (buf->p[1] << 8) | (buf->p[2] << 16) | 
			(buf->p[3] << 24);
		buf->p += 4;
	}

	return ret;
}

static inline u64
buf_get_int64(struct cbuf *buf)
{
	u64 ret = 0;

	if (buf_check_size(buf, 8)) {
		ret = (u64) buf->p[0] | 
			((u64) buf->p[1] << 8) |
			((u64) buf->p[2] << 16) | 
			((u64) buf->p[3] << 24) |
			((u64) buf->p[4] << 32) | 
			((u64) buf->p[5] << 40) |
			((u64) buf->p[6] << 48) | 
			((u64) buf->p[7] << 56);
		buf->p += 8;
	}

	return ret;
}

static inline void
buf_get_str(struct cbuf *buf, Npstr *str)
{
	str->len = buf_get_int16(buf);
	str->str = buf_alloc(buf, str->len);
}

static inline void
buf_get_qid(struct cbuf *buf, Npqid *qid)
{
	qid->type = buf_get_int8(buf);
	qid->version = buf_get_int32(buf);
	qid->path = buf_get_int64(buf);
}

static inline void
buf_get_stat(struct cbuf *buf, Npstat *stat, int dotu)
{
	stat->size = buf_get_int16(buf);
	stat->type = buf_get_int16(buf);
	stat->dev = buf_get_int32(buf);
	buf_get_qid(buf, &stat->qid);
	stat->mode = buf_get_int32(buf);
	stat->atime = buf_get_int32(buf);
	stat->mtime = buf_get_int32(buf);
	stat->length = buf_get_int64(buf);
	buf_get_str(buf, &stat->name);
	buf_get_str(buf, &stat->uid);
	buf_get_str(buf, &stat->gid);
	buf_get_str(buf, &stat->muid);

	if (dotu) {
		buf_get_str(buf, &stat->extension);
		stat->n_uid = buf_get_int32(buf);
		stat->n_gid = buf_get_int32(buf);
		stat->n_muid = buf_get_int32(buf);
	} else
		np_strzero(&stat->extension);
}

static int
size_wstat(Npwstat *wstat, int dotu)
{
	int size = 0;

	if (wstat == NULL)
		return 0;

	size = 2 + 4 + 13 + 4 +  /* type[2] dev[4] qid[13] mode[4] */
		4 + 4 + 8 + 	 /* atime[4] mtime[4] length[8] */
		8;		 /* name[s] uid[s] gid[s] muid[s] */

	if (wstat->name)
		size += strlen(wstat->name);
	if (wstat->uid)
		size += strlen(wstat->uid);
	if (wstat->gid)
		size += strlen(wstat->gid);
	if (wstat->muid)
		size += strlen(wstat->muid);

	if (dotu) {
		size += 4 + 4 + 4 + 2; /* n_uid[4] n_gid[4] n_muid[4] extension[s] */
		if (wstat->extension)
			size += strlen(wstat->extension);
	}

	return size;
}

void
np_strzero(Npstr *str)
{
	str->str = NULL;
	str->len = 0;
}

char *
np_strdup(Npstr *str)
{
	char *ret;

	ret = malloc(str->len + 1);
	if (ret) {
		memmove(ret, str->str, str->len);
		ret[str->len] = '\0';
	}

	return ret;
}

int
np_strcmp(Npstr *str, char *cs)
{
	int ret;

	ret = strncmp(str->str, cs, str->len);
	if (!ret && cs[str->len])
		ret = 1;

	return ret;
}

int
np_strncmp(Npstr *str, char *cs, int len)
{
	int ret;

	if (str->len >= len)
		ret = strncmp(str->str, cs, len);
	else
		ret = np_strcmp(str, cs);

	return ret;
}

void
np_set_tag(Npfcall *fc, u16 tag)
{
	fc->tag = tag;
	fc->pkt[5] = tag;
	fc->pkt[6] = tag >> 8;
}

static Npfcall *
np_create_common(struct cbuf *bufp, u32 size, u8 id)
{
	Npfcall *fc;

	size += 4 + 1 + 2; /* size[4] id[1] tag[2] */
	fc = malloc(sizeof(Npfcall) + size);
	if (!fc)
		return NULL;

	fc->pkt = (u8 *) fc + sizeof(*fc);
	buf_init(bufp, (char *) fc->pkt, size);
	buf_put_int32(bufp, size, &fc->size);
	buf_put_int8(bufp, id, &fc->type);
	buf_put_int16(bufp, NOTAG, &fc->tag);

	return fc;
}

static Npfcall *
np_post_check(Npfcall *fc, struct cbuf *bufp)
{
	if (buf_check_overflow(bufp)) {
		//fprintf(stderr, "buffer overflow\n");
		free (fc);
		return NULL;
	}

//	fprintf(stderr, "serialize dump: ");
//	dumpdata(fc->pkt, fc->size);
	return fc;
}

Npfcall *
np_create_tversion(u32 msize, char *version)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 2 + strlen(version); /* msize[4] version[s] */
	fc = np_create_common(bufp, size, Tversion);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, msize, &fc->msize);
	buf_put_str(bufp, version, &fc->version);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rversion(u32 msize, char *version)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 2 + strlen(version); /* msize[4] version[s] */
	fc = np_create_common(bufp, size, Rversion);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, msize, &fc->msize);
	buf_put_str(bufp, version, &fc->version);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tauth(u32 fid, char *uname, char *aname, u32 n_uname, int dotu)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 2 + 2; /* fid[4] uname[s] aname[s] */
	if (uname)
		size += strlen(uname);

	if (aname)
		size += strlen(aname);

	fc = np_create_common(bufp, size, Tauth);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_str(bufp, uname, &fc->uname);
	buf_put_str(bufp, aname, &fc->aname);
	if (dotu)
		buf_put_int32(bufp, n_uname, &fc->n_uname);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rauth(Npqid *aqid)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 13; /* aqid[13] */
	fc = np_create_common(bufp, size, Rauth);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, aqid, &fc->qid);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rerror(char *ename, int ecode, int dotu)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 2 + strlen(ename); /* ename[s] */
	if (dotu)
		size += 4; /* ecode[4] */

	fc = np_create_common(bufp, size, Rerror);
	if (!fc)
		return NULL;

	buf_put_str(bufp, ename, &fc->ename);
	if (dotu)
		buf_put_int32(bufp, ecode, &fc->ecode);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rerror1(Npstr *ename, int ecode, int dotu)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 2 + ename->len + (dotu?4:0); /* ename[s] ecode[4] */
	fc = np_create_common(bufp, size, Rerror);
	if (!fc)
		return NULL;

	fc->ename.len = ename->len;
	fc->ename.str = buf_alloc(bufp, ename->len);
	memmove(fc->ename.str, ename->str, ename->len);
	if (dotu)
		buf_put_int32(bufp, ecode, &fc->ecode);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tflush(u16 oldtag)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 2;
	fc = np_create_common(bufp, size, Tflush);
	if (!fc)
		return NULL;

	buf_put_int16(bufp, oldtag, &fc->oldtag);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rflush(void)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 0;
	fc = np_create_common(bufp, size, Rflush);
	if (!fc)
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tattach(u32 fid, u32 afid, char *uname, char *aname, u32 n_uname, int dotu)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 4 + 2 + 2; /* fid[4] afid[4] uname[s] aname[s] */
	if (uname)
		size += strlen(uname);

	if (aname)
		size += strlen(aname);

	if (dotu)
		size += 4; /* n_uname[4] */

	fc = np_create_common(bufp, size, Tattach);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int32(bufp, afid, &fc->afid);
	buf_put_str(bufp, uname, &fc->uname);
	buf_put_str(bufp, aname, &fc->aname);
	if (dotu)
		buf_put_int32(bufp, n_uname, &fc->n_uname);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rattach(Npqid *qid)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 13; /* qid[13] */
	fc = np_create_common(bufp, size, Rattach);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, qid, &fc->qid);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_twalk(u32 fid, u32 newfid, u16 nwname, char **wnames)
{
	int i, size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	if (nwname > MAXWELEM) {
		fprintf(stderr, "nwqid > MAXWELEM\n");
		return NULL;
	}

	bufp = &buffer;
	size = 4 + 4 + 2 + nwname * 2; /* fid[4] newfid[4] nwname[2] nwname*wname[s] */
	for(i = 0; i < nwname; i++)
		size += strlen(wnames[i]);

	fc = np_create_common(bufp, size, Twalk);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int32(bufp, newfid, &fc->newfid);
	buf_put_int16(bufp, nwname, &fc->nwname);
	for(i = 0; i < nwname; i++)
		buf_put_str(bufp, wnames[i], &fc->wnames[i]);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rwalk(int nwqid, Npqid *wqids)
{
	int i, size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	if (nwqid > MAXWELEM) {
		fprintf(stderr, "nwqid > MAXWELEM\n");
		return NULL;
	}

	bufp = &buffer;
	size = 2 + nwqid*13; /* nwqid[2] nwqid*wqid[13] */
	fc = np_create_common(bufp, size, Rwalk);
	if (!fc)
		return NULL;

	buf_put_int16(bufp, nwqid, &fc->nwqid);
	for(i = 0; i < nwqid; i++) {
		buf_put_qid(bufp, &wqids[i], &fc->wqids[i]);
	}

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_topen(u32 fid, u8 mode)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 1; /* fid[4] mode[1] */
	fc = np_create_common(bufp, size, Topen);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int8(bufp, mode, &fc->mode);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_ropen(Npqid *qid, u32 iounit)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 13 + 4; /* qid[13] iounit[4] */
	fc = np_create_common(bufp, size, Ropen);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, qid, &fc->qid);
	buf_put_int32(bufp, iounit, &fc->iounit);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tcreate(u32 fid, char *name, u32 perm, u8 mode)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 2 + strlen(name) + 4 + 1; /* fid[4] name[s] perm[4] mode[1] */
	fc = np_create_common(bufp, size, Tcreate);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_str(bufp, name, &fc->name);
	buf_put_int32(bufp, perm, &fc->perm);
	buf_put_int8(bufp, mode, &fc->mode);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rcreate(Npqid *qid, u32 iounit)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 13 + 4; /* qid[13] iounit[4] */
	fc = np_create_common(bufp, size, Rcreate);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, qid, &fc->qid);
	buf_put_int32(bufp, iounit, &fc->iounit);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tread(u32 fid, u64 offset, u32 count)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 8 + 4; /* fid[4] offset[8] count[4] */
	fc = np_create_common(bufp, size, Tread);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int64(bufp, offset, &fc->offset);
	buf_put_int32(bufp, count, &fc->count);
	return np_post_check(fc, bufp);
}

Npfcall *
np_alloc_rread(u32 count)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;
	void *p;

	bufp = &buffer;
	size = 4 + count; /* count[4] data[count] */
	fc = np_create_common(bufp, size, Rread);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, count, &fc->count);
	p = buf_alloc(bufp, count);
	fc->data = p;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rread(u32 count, u8* data)
{
	Npfcall *fc;

	fc = np_alloc_rread(count);
	if (fc->data)
		memmove(fc->data, data, count);

	return fc;
}

void
np_set_rread_count(Npfcall *fc, u32 count)
{
	int size;
	struct cbuf buffer;
	struct cbuf *bufp;

	assert(count <= fc->count);
	bufp = &buffer;
	size = 4 + 1 + 2 + 4 + count; /* size[4] id[1] tag[2] count[4] data[count] */

	buf_init(bufp, (char *) fc->pkt, size);
	buf_put_int32(bufp, size, &fc->size);
	buf_init(bufp, (char *) fc->pkt + 7, size - 7);
	buf_put_int32(bufp, count, &fc->count);
}

Npfcall *
np_create_twrite(u32 fid, u64 offset, u32 count, u8 *data)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;
	void *p;

	bufp = &buffer;
	size = 4 + 8 + 4 + count; /* fid[4] offset[8] count[4] data[count] */
	fc = np_create_common(bufp, size, Twrite);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int64(bufp, offset, &fc->offset);
	buf_put_int32(bufp, count, &fc->count);
	p = buf_alloc(bufp, count);
	fc->data = p;
	if (fc->data)
		memmove(fc->data, data, count);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rwrite(u32 count)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4; /* count[4] */
	fc = np_create_common(bufp, size, Rwrite);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, count, &fc->count);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tclunk(u32 fid)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4;	/* fid[4] */
	fc = np_create_common(bufp, size, Tclunk);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rclunk(void)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 0;
	fc = np_create_common(bufp, size, Rclunk);
	if (!fc)
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tremove(u32 fid)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4;	/* fid[4] */
	fc = np_create_common(bufp, size, Tremove);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	return np_post_check(fc, bufp);
}
Npfcall *
np_create_rremove(void)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 0;
	fc = np_create_common(bufp, size, Rremove);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tstat(u32 fid)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4;	/* fid[4] */
	fc = np_create_common(bufp, size, Tstat);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rstat(Npwstat *wstat, int dotu)
{
	int size, statsz;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;

	statsz = size_wstat(wstat, dotu);
	size = 2 + 2 + statsz; /* stat[n] */
	fc = np_create_common(bufp, size, Rstat);
	if (!fc)
		return NULL;

	buf_put_int16(bufp, statsz + 2, NULL);
	buf_put_wstat(bufp, wstat, &fc->stat, statsz, dotu);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_twstat(u32 fid, Npwstat *wstat, int dotu)
{
	int size, statsz;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;

	statsz = size_wstat(wstat, dotu);
	size = 4 + 2 + 2 + statsz; /* fid[4] stat[n] */
	fc = np_create_common(bufp, size, Twstat);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int16(bufp, statsz + 2, NULL);
	buf_put_wstat(bufp, wstat, &fc->stat, statsz, dotu);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rwstat(void)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 0;
	fc = np_create_common(bufp, size, Rwstat);

	return np_post_check(fc, bufp);
}

/* 9p2000.h extensions */

Npfcall *
np_create_taread(u32 fid, u8 datacheck, u64 offset, u32 count, u32 rsize)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 1 + 8 + 4	/* fid[4] datacheck[1] offset[8] count[4] */
	     + 4;		/*   rsize[4] */
	fc = np_create_common(bufp, size, Taread);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int8(bufp, datacheck, &fc->datacheck);
	buf_put_int64(bufp, offset, &fc->offset);
	buf_put_int32(bufp, count, &fc->count);
	buf_put_int32(bufp, rsize, &fc->rsize);
	return np_post_check(fc, bufp);
}

/* N.B. srv->aread handler should
 * 1. call np_create_raread()
 * 2. fill in fc->data
 * 3. call np_finalize_raread() 
*/

Npfcall *
np_create_raread(u32 count)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;
	void *p;

	bufp = &buffer;
	size = 4 + count + 4; /* count[4] data[count] check[4] */
	fc = np_create_common(bufp, size, Raread);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, count, &fc->count);
	p = buf_alloc(bufp, count);
	fc->data = p;

	return np_post_check(fc, bufp);
}

void
np_finalize_raread(Npfcall *fc, u32 count, u8 datacheck)
{
	int size;
	struct cbuf buffer;
	struct cbuf *bufp;
	u32 check = 0;
	void *p;

	assert(count <= fc->count);
	bufp = &buffer;
	size = 4 + 1 + 2 + 4 + count + 4; /* size[4] id[1] tag[2] */
					  /*   count[4] data[count] check[4] */
	buf_init(bufp, (char *) fc->pkt, size);
	buf_put_int32(bufp, size, &fc->size);
	buf_init(bufp, (char *) fc->pkt + 7, size - 7);
	buf_put_int32(bufp, count, &fc->count);
	p = buf_alloc(bufp, count);
	if (datacheck == P9_CHECK_ADLER32) {
		check = adler32(0L, Z_NULL, 0);
		check = adler32(check, p, count);
	}
	buf_put_int32(bufp, check, &fc->check);
}

Npfcall *
np_create_tawrite(u32 fid, u8 datacheck, u64 offset, u32 count, u32 rsize,
		  u8 *data)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;
	void *p;

	bufp = &buffer;
	size = 4 + 1 + 8 + 4	/* fid[4] datacheck[1] offset[8] count[4] */
	     + 4 + rsize; 	/*   rsize[4] data[rsize] */
	fc = np_create_common(bufp, size, Tawrite);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int8(bufp, datacheck, &fc->datacheck);
	buf_put_int64(bufp, offset, &fc->offset);
	buf_put_int32(bufp, count, &fc->count);
	buf_put_int32(bufp, rsize, &fc->rsize);
	p = buf_alloc(bufp, rsize);
	fc->data = p;
	if (fc->data)
		memmove(fc->data, data, rsize);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rawrite(u32 count)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4; /* count[4] */
	fc = np_create_common(bufp, size, Rawrite);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, count, &fc->count);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tstatfs(u32 fid)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4; /* fid[4] */
	fc = np_create_common(bufp, size, Tstatfs);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rstatfs(u32 type, u32 bsize, u64 blocks, u64 bfree, u64 bavail, u64 files, u64 ffree, u64 fsid, u32 namelen)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 4 + 8 + 8 /* type[4] bsize[4] blocks[8] bfree[8] bavail[8] */
         + 8 + 8 + 8 + 8 + 4;/* bavail[8] files[8] ffree[8] fsid[8] namelen[4]*/
	fc = np_create_common(bufp, size, Rstatfs);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, type,    &fc->statfs.type);
	buf_put_int32(bufp, bsize,   &fc->statfs.bsize);
	buf_put_int64(bufp, blocks,  &fc->statfs.blocks);
	buf_put_int64(bufp, bfree,   &fc->statfs.bfree);
	buf_put_int64(bufp, bavail,  &fc->statfs.bavail);
	buf_put_int64(bufp, files,   &fc->statfs.files);
	buf_put_int64(bufp, ffree,   &fc->statfs.ffree);
	buf_put_int64(bufp, fsid,    &fc->statfs.fsid);
	buf_put_int32(bufp, namelen, &fc->statfs.namelen);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tlock(u32 fid, u8 cmd, u8 type, u64 pid,
                u64 start, u64 end)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 1 + 1 	/* fid[4] cmd[1] type[1]  */
		 + 8 + 8 + 8;	/* pid[8] start[8] end[8] */
	fc = np_create_common(bufp, size, Tlock);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid,     &fc->fid);
	buf_put_int8(bufp,  cmd,     &fc->cmd);
	buf_put_int8(bufp,  type,    &fc->lock.type);
	buf_put_int64(bufp, pid,     &fc->lock.pid);
	buf_put_int64(bufp, start,   &fc->lock.start);
	buf_put_int64(bufp, end,     &fc->lock.end);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rlock(u8 type, u64 pid, u64 start, u64 end)
{
        int size;
        Npfcall *fc;
        struct cbuf buffer;
        struct cbuf *bufp;

        bufp = &buffer;
	size = 1 + 8 + 8 + 8; /* type[1] pid[8] start[8] end[8] */
        fc = np_create_common(bufp, size, Rlock);
        if (!fc)
                return NULL;

	buf_put_int8(bufp,  type,    &fc->lock.type);
        buf_put_int64(bufp, pid,     &fc->lock.pid);
        buf_put_int64(bufp, start,   &fc->lock.start);
        buf_put_int64(bufp, end,     &fc->lock.end);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_tflock(u32 fid, u8 cmd)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 1; /* fid[4] cmd[1] */
	fc = np_create_common(bufp, size, Tflock);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int8(bufp, cmd, &fc->cmd);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rflock(void)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 0;
	fc = np_create_common(bufp, size, Rflock);
	if (!fc)
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_trename(u32 fid, u32 newdirfid, char *newname)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 4 + 2 + strlen(newname); /* fid[4] newdirfid[4] newname[s] */
	fc = np_create_common(bufp, size, Trename);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, fid, &fc->fid);
	buf_put_int32(bufp, newdirfid, &fc->newdirfid);
	buf_put_str(bufp, newname, &fc->newname);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rrename(void)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 0;
	fc = np_create_common(bufp, size, Rrename);
	if (!fc)
		return NULL;

	return np_post_check(fc, bufp);
}

int
np_deserialize(Npfcall *fc, u8 *data, int dotu)
{
	int i;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	buf_init(bufp, data, 4);
	fc->size = buf_get_int32(bufp);

//	fprintf(stderr, "deserialize dump: ");
//	dumpdata(data, fc->size);

	buf_init(bufp, data + 4, fc->size - 4);
	fc->type = buf_get_int8(bufp);
	fc->tag = buf_get_int16(bufp);
	fc->fid = fc->afid = fc->newfid = NOFID;

	switch (fc->type) {
	default:
		goto error;

	case Tversion:
		fc->msize = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->version);
		break;

	case Rversion:
		fc->msize = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->version);
		break;

	case Tauth:
		fc->afid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->uname);
		buf_get_str(bufp, &fc->aname);
		if(dotu)
			fc->n_uname = buf_get_int32(bufp);
		else
			fc->n_uname = ~0;
		break;

	case Rauth:
		buf_get_qid(bufp, &fc->qid);
		break;

	case Tflush:
		fc->oldtag = buf_get_int16(bufp);
		break;

	case Tattach:
		fc->fid = buf_get_int32(bufp);
		fc->afid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->uname);
		buf_get_str(bufp, &fc->aname);
		if(dotu)
			fc->n_uname = buf_get_int32(bufp);
		else
			fc->n_uname = ~0;
		break;

	case Rattach:
		buf_get_qid(bufp, &fc->qid);
		break;

	case Rerror:
		buf_get_str(bufp, &fc->ename);
		if (dotu)
			fc->ecode = buf_get_int32(bufp);
		else
			fc->ecode = ~0;
		break;

	case Twalk:
		fc->fid = buf_get_int32(bufp);
		fc->newfid = buf_get_int32(bufp);
		fc->nwname = buf_get_int16(bufp);
		if (fc->nwname > MAXWELEM)
			goto error;

		for(i = 0; i < fc->nwname; i++) {
			buf_get_str(bufp, &fc->wnames[i]);
		}
		break;

	case Rwalk:
		fc->nwqid = buf_get_int16(bufp);
		if (fc->nwqid > MAXWELEM)
			goto error;
		for(i = 0; i < fc->nwqid; i++)
			buf_get_qid(bufp, &fc->wqids[i]);
		break;

	case Topen:
		fc->fid = buf_get_int32(bufp);
		fc->mode = buf_get_int8(bufp);
		break;

	case Ropen:
	case Rcreate:
		buf_get_qid(bufp, &fc->qid);
		fc->iounit = buf_get_int32(bufp);
		break;

	case Tcreate:
		fc->fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->name);
		fc->perm = buf_get_int32(bufp);
		fc->mode = buf_get_int8(bufp);
		if (dotu)
			buf_get_str(bufp, &fc->extension);
		else
			np_strzero(&fc->extension);
		break;

	case Tread:
		fc->fid = buf_get_int32(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		break;

	case Rread:
		fc->count = buf_get_int32(bufp);
		fc->data = buf_alloc(bufp, fc->count);
		break;

	case Twrite:
		fc->fid = buf_get_int32(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		fc->data = buf_alloc(bufp, fc->count);
		break;

	case Rwrite:
		fc->count = buf_get_int32(bufp);
		break;

	case Tclunk:
	case Tremove:
	case Tstat:
		fc->fid = buf_get_int32(bufp);
		break;

	case Rflush:
	case Rclunk:
	case Rremove:
	case Rwstat:
		break;

	case Rstat:
		buf_get_int16(bufp);
		buf_get_stat(bufp, &fc->stat, dotu);
		break;

	case Twstat:
		fc->fid = buf_get_int32(bufp);
		buf_get_int16(bufp);
		buf_get_stat(bufp, &fc->stat, dotu);
		break;

	/* 9p2000.h extensions */

	case Taread:
		fc->fid = buf_get_int32(bufp);
		fc->datacheck = buf_get_int8(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		fc->rsize = buf_get_int32(bufp);
		break;
	case Raread:
		fc->count = buf_get_int32(bufp);
		fc->data = buf_alloc(bufp, fc->count);
		fc->check = buf_get_int32(bufp);
		break;

	case Tawrite:
		fc->fid = buf_get_int32(bufp);
		fc->datacheck = buf_get_int8(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		fc->rsize = buf_get_int32(bufp);
		fc->data = buf_alloc(bufp, fc->rsize);
		fc->check = buf_get_int32(bufp);
		break;
	case Rawrite:
		fc->count = buf_get_int32(bufp);
		break;

	case Tstatfs:
		fc->fid = buf_get_int32(bufp);
		break;
	case Rstatfs:
		fc->statfs.type = buf_get_int32(bufp);
		fc->statfs.bsize = buf_get_int32(bufp);
		fc->statfs.blocks = buf_get_int64(bufp);
		fc->statfs.bfree = buf_get_int64(bufp);
		fc->statfs.bavail = buf_get_int64(bufp);
		fc->statfs.files = buf_get_int64(bufp);
		fc->statfs.ffree = buf_get_int64(bufp);
		fc->statfs.fsid = buf_get_int64(bufp);
		fc->statfs.namelen = buf_get_int32(bufp);
		break;

	case Tlock:
		fc->fid = buf_get_int32(bufp);
		fc->cmd = buf_get_int8(bufp);
		fc->lock.type = buf_get_int8(bufp);
		fc->lock.pid = buf_get_int64(bufp);
		fc->lock.start = buf_get_int64(bufp);
		fc->lock.end = buf_get_int64(bufp);
		break;
	case Rlock:
		fc->lock.type = buf_get_int8(bufp);
		fc->lock.pid = buf_get_int64(bufp);
		fc->lock.start = buf_get_int64(bufp);
		fc->lock.end = buf_get_int64(bufp);
		break;

	case Tflock:
		fc->fid = buf_get_int32(bufp);
		fc->cmd = buf_get_int8(bufp);
		break;
	case Rflock:
		break;

	case Trename:
		fc->fid = buf_get_int32(bufp);
		fc->newdirfid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->newname);
		break;
	case Rrename:
		break;

	}

	if (buf_check_overflow(bufp))
		goto error;

	return fc->size;

error:
	return 0;
}

int 
np_serialize_stat(Npwstat *wstat, u8* buf, int buflen, int dotu)
{
	int statsz;
	struct cbuf buffer;
	struct cbuf *bufp;
	Npstat stat;

	statsz = size_wstat(wstat, dotu);

	if (statsz > buflen)
		return 0;

	bufp = &buffer;
	buf_init(bufp, buf, buflen);

	buf_put_wstat(bufp, wstat, &stat, statsz, dotu);

	if (buf_check_overflow(bufp))
		return 0;

	return bufp->p - bufp->sp;
}

int 
np_deserialize_stat(Npstat *stat, u8* buf, int buflen, int dotu)
{
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	buf_init(bufp, buf, buflen);

	buf_get_stat(bufp, stat, dotu);

	if (buf_check_overflow(bufp))
		return 0;

	return bufp->p - bufp->sp;
}
