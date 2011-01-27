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
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <zlib.h>
#include "9p.h"
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
buf_put_wstat(struct cbuf *bufp, Npwstat *wstat, Npstat* stat, int statsz, int extended)
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

	if (extended) {
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
buf_get_stat(struct cbuf *buf, Npstat *stat, int extended)
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

	if (extended) {
		buf_get_str(buf, &stat->extension);
		stat->n_uid = buf_get_int32(buf);
		stat->n_gid = buf_get_int32(buf);
		stat->n_muid = buf_get_int32(buf);
	} else
		np_strzero(&stat->extension);
}

static int
size_wstat(Npwstat *wstat, int extended)
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

	if (extended) {
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
	buf_put_int16(bufp, P9_NOTAG, &fc->tag);

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
np_create_rversion(u32 msize, char *version)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4 + 2 + strlen(version); /* msize[4] version[s] */
	fc = np_create_common(bufp, size, P9_RVERSION);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, msize, &fc->msize);
	buf_put_str(bufp, version, &fc->version);

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
	fc = np_create_common(bufp, size, P9_RAUTH);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, aqid, &fc->qid);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rerror(char *ename, int ecode, int extended)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 2 + strlen(ename); /* ename[s] */
	if (extended)
		size += 4; /* ecode[4] */

	fc = np_create_common(bufp, size, P9_RERROR);
	if (!fc)
		return NULL;

	buf_put_str(bufp, ename, &fc->ename);
	if (extended)
		buf_put_int32(bufp, ecode, &fc->ecode);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rerror1(Npstr *ename, int ecode, int extended)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 2 + ename->len + (extended?4:0); /* ename[s] ecode[4] */
	fc = np_create_common(bufp, size, P9_RERROR);
	if (!fc)
		return NULL;

	fc->ename.len = ename->len;
	fc->ename.str = buf_alloc(bufp, ename->len);
	memmove(fc->ename.str, ename->str, ename->len);
	if (extended)
		buf_put_int32(bufp, ecode, &fc->ecode);

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
	fc = np_create_common(bufp, size, P9_RFLUSH);
	if (!fc)
		return NULL;

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
	fc = np_create_common(bufp, size, P9_RATTACH);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, qid, &fc->qid);
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rwalk(int nwqid, Npqid *wqids)
{
	int i, size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	if (nwqid > P9_MAXWELEM) {
		fprintf(stderr, "nwqid > P9_MAXWELEM\n");
		return NULL;
	}

	bufp = &buffer;
	size = 2 + nwqid*13; /* nwqid[2] nwqid*wqid[13] */
	fc = np_create_common(bufp, size, P9_RWALK);
	if (!fc)
		return NULL;

	buf_put_int16(bufp, nwqid, &fc->nwqid);
	for(i = 0; i < nwqid; i++) {
		buf_put_qid(bufp, &wqids[i], &fc->wqids[i]);
	}

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
	fc = np_create_common(bufp, size, P9_ROPEN);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, qid, &fc->qid);
	buf_put_int32(bufp, iounit, &fc->iounit);

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
	fc = np_create_common(bufp, size, P9_RCREATE);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, qid, &fc->qid);
	buf_put_int32(bufp, iounit, &fc->iounit);

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
	fc = np_create_common(bufp, size, P9_RREAD);
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
np_create_rwrite(u32 count)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4; /* count[4] */
	fc = np_create_common(bufp, size, P9_RWRITE);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, count, &fc->count);

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
	fc = np_create_common(bufp, size, P9_RCLUNK);
	if (!fc)
		return NULL;

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
	fc = np_create_common(bufp, size, P9_RREMOVE);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rstat(Npwstat *wstat, int extended)
{
	int size, statsz;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;

	statsz = size_wstat(wstat, extended);
	size = 2 + 2 + statsz; /* stat[n] */
	fc = np_create_common(bufp, size, P9_RSTAT);
	if (!fc)
		return NULL;

	buf_put_int16(bufp, statsz + 2, NULL);
	buf_put_wstat(bufp, wstat, &fc->stat, statsz, extended);

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
	fc = np_create_common(bufp, size, P9_RWSTAT);

	return np_post_check(fc, bufp);
}

#if HAVE_LARGEIO
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
	fc = np_create_common(bufp, size, P9_RAREAD);
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
np_create_rawrite(u32 count)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 4; /* count[4] */
	fc = np_create_common(bufp, size, P9_RAWRITE);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, count, &fc->count);

	return np_post_check(fc, bufp);
}
#endif

#if HAVE_DOTL
Npfcall *
np_create_rlopen(Npqid *qid, u32 iounit)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = sizeof(*qid) + sizeof(u32);
	fc = np_create_common(bufp, size, P9_RLOPEN);
	if (!fc)
		return NULL;

	buf_put_qid(bufp, qid, &fc->u.rlopen.qid);
	buf_put_int32(bufp, iounit, &fc->u.rlopen.iounit);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rgetattr(u64 st_result_mask, struct p9_qid *qid, u32 st_mode,
  		u32 st_uid, u32 st_gid, u64 st_nlink, u64 st_rdev,
		u64 st_size, u64 st_blksize, u64 st_blocks,
		u64 st_atime_sec, u64 st_atime_nsec,
		u64 st_mtime_sec, u64 st_mtime_nsec,
		u64 st_ctime_sec, u64 st_ctime_nsec,
		u64 st_btime_sec, u64 st_btime_nsec,
		u64 st_gen, u64 st_data_version)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = sizeof(u64) + sizeof(*qid) + 3*sizeof(u32) + 15*sizeof(u64);
	fc = np_create_common(bufp, size, P9_RGETATTR);
	if (!fc)
		return NULL;

	buf_put_int64(bufp, st_result_mask, &fc->u.rgetattr.s.st_result_mask);
	buf_put_qid(bufp, qid, &fc->u.rgetattr.s.qid);
	buf_put_int32(bufp, st_mode, &fc->u.rgetattr.s.st_mode);
	buf_put_int32(bufp, st_uid, &fc->u.rgetattr.s.st_uid);
	buf_put_int32(bufp, st_gid, &fc->u.rgetattr.s.st_gid);
	buf_put_int64(bufp, st_nlink, &fc->u.rgetattr.s.st_nlink);
	buf_put_int64(bufp, st_rdev, &fc->u.rgetattr.s.st_rdev);
	buf_put_int64(bufp, st_size, &fc->u.rgetattr.s.st_size);
	buf_put_int64(bufp, st_blksize, &fc->u.rgetattr.s.st_blksize);
	buf_put_int64(bufp, st_blocks, &fc->u.rgetattr.s.st_blocks);
	buf_put_int64(bufp, st_atime_sec, &fc->u.rgetattr.s.st_atime_sec);
	buf_put_int64(bufp, st_atime_nsec, &fc->u.rgetattr.s.st_atime_nsec);
	buf_put_int64(bufp, st_mtime_sec, &fc->u.rgetattr.s.st_mtime_sec);
	buf_put_int64(bufp, st_mtime_nsec, &fc->u.rgetattr.s.st_mtime_nsec);
	buf_put_int64(bufp, st_ctime_sec, &fc->u.rgetattr.s.st_ctime_sec);
	buf_put_int64(bufp, st_ctime_nsec, &fc->u.rgetattr.s.st_ctime_nsec);
	buf_put_int64(bufp, st_btime_sec, &fc->u.rgetattr.s.st_btime_sec);
	buf_put_int64(bufp, st_btime_nsec, &fc->u.rgetattr.s.st_btime_nsec);
	buf_put_int64(bufp, st_gen, &fc->u.rgetattr.s.st_gen);
	buf_put_int64(bufp, st_data_version, &fc->u.rgetattr.s.st_data_version);

	return np_post_check(fc, bufp);
}

/* srv->readdir () should:
 * 1) call np_alloc_rreaddir ()
 * 2) copy up to count bytes of dirent data in datap
 * 3) call np_set_rreaddir_count () to set actual byte count
 */
Npfcall *
np_alloc_rreaddir(u32 count, u8 **datap)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;
	void *p;

	bufp = &buffer;
	size = sizeof(u32) + count;
	fc = np_create_common(bufp, size, P9_RREADDIR);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, count, &fc->u.rreaddir.count);
	p = buf_alloc(bufp, count);
	*datap = fc->u.rreaddir.data = p;

	return np_post_check(fc, bufp);
}

void
np_set_rreaddir_count(Npfcall *fc, u32 count)
{
	int size;
	struct cbuf buffer;
	struct cbuf *bufp;

	assert(count <= fc->u.rreaddir.count);
	bufp = &buffer;
	size = sizeof(u32) + count;

	buf_init(bufp, (char *) fc->pkt, size);
	buf_put_int32(bufp, size, &fc->size);
	buf_init(bufp, (char *) fc->pkt + 7, size - 7);
	buf_put_int32(bufp, count, &fc->u.rreaddir.count);
}

Npfcall *
np_create_rstatfs(u32 type, u32 bsize, u64 blocks, u64 bfree, u64 bavail, u64 files, u64 ffree, u64 fsid, u32 namelen)
{
	int size;
	Npfcall *fc;
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	size = 2*sizeof(u32) + 6*sizeof(u64) + sizeof(u32);
	fc = np_create_common(bufp, size, P9_RSTATFS);
	if (!fc)
		return NULL;

	buf_put_int32(bufp, type,    &fc->u.rstatfs.type);	
	buf_put_int32(bufp, bsize,   &fc->u.rstatfs.bsize);
	buf_put_int64(bufp, blocks,  &fc->u.rstatfs.blocks);
	buf_put_int64(bufp, bfree,   &fc->u.rstatfs.bfree);
	buf_put_int64(bufp, bavail,  &fc->u.rstatfs.bavail);
	buf_put_int64(bufp, files,   &fc->u.rstatfs.files);
	buf_put_int64(bufp, ffree,   &fc->u.rstatfs.ffree);
	buf_put_int64(bufp, fsid,    &fc->u.rstatfs.fsid);
	buf_put_int32(bufp, namelen, &fc->u.rstatfs.namelen);

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
	fc = np_create_common(bufp, size, P9_RRENAME);
	if (!fc)
		return NULL;

	return np_post_check(fc, bufp);
}
#endif

int
np_deserialize(Npfcall *fc, u8 *data, int extended)
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
	fc->fid = fc->afid = fc->newfid = P9_NOFID;

	switch (fc->type) {
	default:
		fprintf(stderr, "unhandled op: %d\n", fc->type);
		fflush(stderr);
		goto error;

	case P9_TVERSION:
		fc->msize = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->version);
		break;

	case P9_TAUTH:
		fc->afid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->uname);
		buf_get_str(bufp, &fc->aname);
		if(extended)
			fc->n_uname = buf_get_int32(bufp);
		else
			fc->n_uname = ~0;
		break;

	case P9_TFLUSH:
		fc->oldtag = buf_get_int16(bufp);
		break;

	case P9_TATTACH:
		fc->fid = buf_get_int32(bufp);
		fc->afid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->uname);
		buf_get_str(bufp, &fc->aname);
		if(extended)
			fc->n_uname = buf_get_int32(bufp);
		else
			fc->n_uname = ~0;
		break;

	case P9_TWALK:
		fc->fid = buf_get_int32(bufp);
		fc->newfid = buf_get_int32(bufp);
		fc->nwname = buf_get_int16(bufp);
		if (fc->nwname > P9_MAXWELEM)
			goto error;

		for(i = 0; i < fc->nwname; i++) {
			buf_get_str(bufp, &fc->wnames[i]);
		}
		break;

	case P9_TOPEN:
		fc->fid = buf_get_int32(bufp);
		fc->mode = buf_get_int8(bufp);
		break;

	case P9_TCREATE:
		fc->fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->name);
		fc->perm = buf_get_int32(bufp);
		fc->mode = buf_get_int8(bufp);
		if (extended)
			buf_get_str(bufp, &fc->extension);
		else
			np_strzero(&fc->extension);
		break;

	case P9_TREAD:
		fc->fid = buf_get_int32(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		break;

	case P9_TWRITE:
		fc->fid = buf_get_int32(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		fc->data = buf_alloc(bufp, fc->count);
		break;

	case P9_TCLUNK:
	case P9_TREMOVE:
	case P9_TSTAT:
		fc->fid = buf_get_int32(bufp);
		break;

	case P9_TWSTAT:
		fc->fid = buf_get_int32(bufp);
		buf_get_int16(bufp);
		buf_get_stat(bufp, &fc->stat, extended);
		break;
#if HAVE_LARGEIO
	case P9_TAREAD:
		fc->fid = buf_get_int32(bufp);
		fc->datacheck = buf_get_int8(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		fc->rsize = buf_get_int32(bufp);
		break;
	case P9_TAWRITE:
		fc->fid = buf_get_int32(bufp);
		fc->datacheck = buf_get_int8(bufp);
		fc->offset = buf_get_int64(bufp);
		fc->count = buf_get_int32(bufp);
		fc->rsize = buf_get_int32(bufp);
		fc->data = buf_alloc(bufp, fc->rsize);
		fc->check = buf_get_int32(bufp);
		break;
#endif
#if HAVE_DOTL
	case P9_TLOPEN:
		fc->u.tlopen.fid = buf_get_int32(bufp);
		fc->u.tlopen.mode = buf_get_int32(bufp);
		break;
	case P9_TGETATTR:
		fc->u.tgetattr.fid = buf_get_int32(bufp);
		fc->u.tgetattr.request_mask = buf_get_int64(bufp);
		break;
	case P9_TREADDIR:
		fc->u.treaddir.fid = buf_get_int32(bufp);
		fc->u.treaddir.offset = buf_get_int64(bufp);
		fc->u.treaddir.count = buf_get_int32(bufp);
		break;
	case P9_TSTATFS:
		fc->u.tstatfs.fid = buf_get_int32(bufp);
		break;
	case P9_TRENAME:
		fc->u.trename.fid = buf_get_int32(bufp);
		fc->u.trename.newdirfid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.trename.name);
		break;
#endif
	}

	if (buf_check_overflow(bufp))
		goto error;

	return fc->size;

error:
	return 0;
}

#if HAVE_DOTL
/* put a null terminated string on the wire */
static void
buf_put_nstr(struct cbuf *buf, char *s)
{
	int slen = strlen (s) + 1;
	char *dest;

	dest = buf_alloc(buf, slen);

	memmove(dest, s, slen);
}

int
np_serialize_p9_dirent(struct p9_dirent *d, u8 *buf, int buflen)
{
	struct cbuf buffer;
	struct cbuf *bufp;
	int needed;

	needed = sizeof(struct p9_dirent) + strlen(d->d_name) + 1;
	if (needed > buflen)
		return 0;

	bufp = &buffer;
	buf_init(bufp, buf, buflen);
	buf_put_qid(bufp, &d->qid, NULL);
	buf_put_int64(bufp, d->d_off, NULL);
	buf_put_int8(bufp, d->d_type, NULL);
	buf_put_nstr(bufp, d->d_name);
	
	if (buf_check_overflow(bufp))
		return 0;

	return bufp->p - bufp->sp;
}
#endif

int 
np_serialize_stat(Npwstat *wstat, u8* buf, int buflen, int extended)
{
	int statsz;
	struct cbuf buffer;
	struct cbuf *bufp;
	Npstat stat;

	statsz = size_wstat(wstat, extended);

	if (statsz > buflen)
		return 0;

	bufp = &buffer;
	buf_init(bufp, buf, buflen);

	buf_put_wstat(bufp, wstat, &stat, statsz, extended);

	if (buf_check_overflow(bufp))
		return 0;

	return bufp->p - bufp->sp;
}

int 
np_deserialize_stat(Npstat *stat, u8* buf, int buflen, int extended)
{
	struct cbuf buffer;
	struct cbuf *bufp;

	bufp = &buffer;
	buf_init(bufp, buf, buflen);

	buf_get_stat(bufp, stat, extended);

	if (buf_check_overflow(bufp))
		return 0;

	return bufp->p - bufp->sp;
}
