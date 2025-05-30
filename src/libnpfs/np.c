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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include "npfs.h"
#include "npfsimpl.h"

/* wire sizes */
#define QIDSIZE (sizeof(u8) + sizeof(u32) + sizeof(u64))

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
		if (buf->p <= buf->ep)
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
		if (s)
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

	size += sizeof(fc->size) + sizeof(fc->type) + sizeof (fc->tag);
	if (!(fc = malloc(sizeof(Npfcall) + size)))
		return NULL;
	fc->pkt = (u8 *) fc + sizeof(*fc);
	buf_init(bufp, (char *) fc->pkt, size);
	buf_put_int32(bufp, size, &fc->size);
	buf_put_int8(bufp, id, &fc->type);
	buf_put_int16(bufp, NOTAG, &fc->tag);

	return fc;
}

static Npfcall *
np_create_common_static(struct cbuf *bufp, u32 size, u8 id,
			void *buf, int buflen)
{
	Npfcall *fc;

	size += sizeof(fc->size) + sizeof(fc->type) + sizeof (fc->tag);
	NP_ASSERT (buflen >= sizeof(Npfcall) + size);

	fc = buf;
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
		free (fc);
		return NULL;
	}

	return fc;
}

Npfcall *
np_create_tversion(u32 msize, char *version)
{
        int size = sizeof(msize) + sizeof(u16) + strlen(version);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tversion)))
                return NULL;
        buf_put_int32(bufp, msize, &fc->u.tversion.msize);
        buf_put_str(bufp, version, &fc->u.tversion.version);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rversion(u32 msize, char *version)
{
	int size = sizeof(msize)
	 	 + sizeof(u16) + (version ? strlen(version) : 0);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rversion)))
		return NULL;
	buf_put_int32(bufp, msize, &fc->u.rversion.msize);
	buf_put_str(bufp, version, &fc->u.rversion.version);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tauth(u32 fid, char *uname, char *aname, u32 n_uname)
{
        int size = sizeof(u32)
		 + sizeof(u16) + (uname ? strlen (uname) : 0)
		 + sizeof(u16) + (aname ? strlen (aname) : 0)
		 + sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tauth)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.tauth.afid);
        buf_put_str(bufp, uname, &fc->u.tauth.uname);
        buf_put_str(bufp, aname, &fc->u.tauth.aname);
        buf_put_int32(bufp, n_uname, &fc->u.tauth.n_uname);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rauth(Npqid *aqid)
{
	int size = QIDSIZE;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rauth)))
		return NULL;
	buf_put_qid(bufp, aqid, &fc->u.rauth.qid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tflush(u16 oldtag)
{
        int size = sizeof(u16);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tflush)))
                return NULL;
        buf_put_int16(bufp, oldtag, &fc->u.tflush.oldtag);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rflush(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rflush)))
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rflush_static(void *buf, int buflen)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	fc = np_create_common_static(bufp, size, Rflush, buf, buflen);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tattach(u32 fid, u32 afid, char *uname, char *aname, u32 n_uname)
{
        int size = 2*sizeof(u32)
		 + sizeof(u16) + (uname ? strlen(uname) : 0)
		 + sizeof(u16) + (aname ? strlen(aname) : 0)
		 + sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tattach)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.tattach.fid);
        buf_put_int32(bufp, afid, &fc->u.tattach.afid);
        buf_put_str(bufp, uname, &fc->u.tattach.uname);
        buf_put_str(bufp, aname, &fc->u.tattach.aname);
        buf_put_int32(bufp, n_uname, &fc->u.tattach.n_uname);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rattach(Npqid *qid)
{
	int size = QIDSIZE;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rattach)))
		return NULL;
	buf_put_qid(bufp, qid, &fc->u.rattach.qid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_twalk(u32 fid, u32 newfid, u16 nwname, char **wnames)
{
	int i, size = 2*sizeof(u32) + sizeof(u16);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

	NP_ASSERT (nwname <= MAXWELEM);
        for(i = 0; i < nwname; i++)
                size += sizeof(u16) + strlen(wnames[i]);
        if (!(fc = np_create_common(bufp, size, Twalk)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.twalk.fid);
        buf_put_int32(bufp, newfid, &fc->u.twalk.newfid);
        buf_put_int16(bufp, nwname, &fc->u.twalk.nwname);
        for(i = 0; i < nwname; i++)
                buf_put_str(bufp, wnames[i], &fc->u.twalk.wnames[i]);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rwalk(int nwqid, Npqid *wqids)
{
	int i, size = sizeof(u16) + nwqid*QIDSIZE;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	NP_ASSERT (nwqid <= MAXWELEM);
	if (!(fc = np_create_common(bufp, size, Rwalk)))
		return NULL;
	buf_put_int16(bufp, nwqid, &fc->u.rwalk.nwqid);
	for(i = 0; i < nwqid; i++) {
		buf_put_qid(bufp, &wqids[i], &fc->u.rwalk.wqids[i]);
	}

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tread(u32 fid, u64 offset, u32 count)
{
        int size = sizeof(u32) + sizeof(u64) + sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tread)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.tread.fid);
        buf_put_int64(bufp, offset, &fc->u.tread.offset);
        buf_put_int32(bufp, count, &fc->u.tread.count);

        return np_post_check(fc, bufp);
}

Npfcall *
np_alloc_rread(u32 count)
{
	int size = sizeof(u32) + count;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rread)))
		return NULL;
	buf_put_int32(bufp, count, &fc->u.rread.count);
	fc->u.rread.data = buf_alloc(bufp, count);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rread(u32 count, u8* data)
{
	Npfcall *fc;

	if (!(fc = np_alloc_rread(count)))
		return NULL;
	if (fc->u.rread.data)
		memmove(fc->u.rread.data, data, count);

	return fc;
}

void
np_set_rread_count(Npfcall *fc, u32 count)
{
	int size = sizeof(u32) + sizeof(u8) + sizeof(u16)
		 + sizeof(u32) + count;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	NP_ASSERT (count <= fc->u.rread.count);
	buf_init(bufp, (char *) fc->pkt, size);
	buf_put_int32(bufp, size, &fc->size);
	buf_init(bufp, (char *) fc->pkt + 7, size - 7);
	buf_put_int32(bufp, count, &fc->u.rread.count);
}

Npfcall *
np_create_twrite(u32 fid, u64 offset, u32 count, u8 *data)
{
        int size = sizeof(u32) + sizeof(u64) + sizeof(u32) + count;
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Twrite)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.twrite.fid);
        buf_put_int64(bufp, offset, &fc->u.twrite.offset);
        buf_put_int32(bufp, count, &fc->u.twrite.count);
        fc->u.twrite.data = buf_alloc(bufp, count);
        if (fc->u.twrite.data)
                memmove(fc->u.twrite.data, data, count);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rwrite(u32 count)
{
	int size = sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rwrite)))
		return NULL;
	buf_put_int32(bufp, count, &fc->u.rwrite.count);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tclunk(u32 fid)
{
        int size = sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tclunk)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.tclunk.fid);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rclunk(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rclunk)))
		return NULL;
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tremove(u32 fid)
{
        int size = sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tremove)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.tremove.fid);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rremove(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rremove)))
		return NULL;
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rlerror(u32 ecode)
{
	int size = sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rlerror)))
		return NULL;
	buf_put_int32(bufp, ecode, &fc->u.rlerror.ecode);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rlerror_static(u32 ecode, void *buf, int bufsize)
{
	int size = sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	fc = np_create_common_static(bufp, size, Rlerror, buf, bufsize);
	buf_put_int32(bufp, ecode, &fc->u.rlerror.ecode);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tstatfs(u32 fid)
{
	int size = sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tstatfs)))
		return NULL;
	buf_put_int32(bufp, fid,    &fc->u.tstatfs.fid);

	return np_post_check(fc, bufp);
}


Npfcall *
np_create_rstatfs(u32 type, u32 bsize, u64 blocks, u64 bfree, u64 bavail, u64 files, u64 ffree, u64 fsid, u32 namelen)
{
	int size = 2*sizeof(u32) + 6*sizeof(u64) + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rstatfs)))
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
np_create_tlopen(u32 fid, u32 flags)
{
        int size = sizeof(u32) + sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tlopen)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.tlopen.fid);
        buf_put_int32(bufp, flags, &fc->u.tlopen.flags);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rlopen(Npqid *qid, u32 iounit)
{
	int size = QIDSIZE + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rlopen)))
		return NULL;
	buf_put_qid(bufp, qid, &fc->u.rlopen.qid);
	buf_put_int32(bufp, iounit, &fc->u.rlopen.iounit);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tlcreate(u32 fid, char *name, u32 flags, u32 mode, u32 gid)
{
        int size = sizeof(u32) + sizeof(u16) + strlen(name)
		 + sizeof(u32) + sizeof(u32) + sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tlcreate)))
                return NULL;
        buf_put_int32(bufp, fid, &fc->u.tlcreate.fid);
        buf_put_str(bufp, name, &fc->u.tlcreate.name);
        buf_put_int32(bufp, flags, &fc->u.tlcreate.flags);
        buf_put_int32(bufp, mode, &fc->u.tlcreate.mode);
        buf_put_int32(bufp, gid, &fc->u.tlcreate.gid);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_rlcreate(Npqid *qid, u32 iounit)
{
	int size = QIDSIZE + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rlcreate)))
		return NULL;
	buf_put_qid(bufp, qid, &fc->u.rlcreate.qid);
	buf_put_int32(bufp, iounit, &fc->u.rlcreate.iounit);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tsymlink(u32 fid, char *name, char *symtgt, u32 gid)
{
	int size = sizeof(u32) + sizeof(u16) + strlen(name)
		 + sizeof(u16) + strlen(symtgt) + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tsymlink)))
		return NULL;
        buf_put_int32(bufp, fid, &fc->u.tsymlink.fid);
        buf_put_str(bufp, name, &fc->u.tsymlink.name);
        buf_put_str(bufp, symtgt, &fc->u.tsymlink.symtgt);
        buf_put_int32(bufp, gid, &fc->u.tsymlink.gid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rsymlink(Npqid *qid)
{
	int size = QIDSIZE;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rsymlink)))
		return NULL;
	buf_put_qid(bufp, qid, &fc->u.rsymlink.qid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tmknod (u32 fid, char *name, u32 mode, u32 major, u32 minor, u32 gid)
{
	int size = sizeof(u32) + sizeof(u16) + strlen(name)
		 + sizeof(u32) + sizeof(u32) + sizeof(u32) + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tmknod)))
		return NULL;
        buf_put_int32(bufp, fid, &fc->u.tmknod.fid);
        buf_put_str(bufp, name, &fc->u.tmknod.name);
        buf_put_int32(bufp, mode, &fc->u.tmknod.mode);
        buf_put_int32(bufp, major, &fc->u.tmknod.major);
        buf_put_int32(bufp, minor, &fc->u.tmknod.minor);
        buf_put_int32(bufp, gid, &fc->u.tmknod.gid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rmknod (Npqid *qid)
{
	int size = QIDSIZE;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rmknod)))
		return NULL;
	buf_put_qid(bufp, qid, &fc->u.rmknod.qid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_trename(u32 fid, u32 dfid, char *name)
{
	int size = sizeof(u32) + sizeof(u32) + sizeof(u16) + strlen(name);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Trename)))
		return NULL;
        buf_put_int32(bufp, fid, &fc->u.trename.fid);
        buf_put_int32(bufp, dfid, &fc->u.trename.dfid);
        buf_put_str(bufp, name, &fc->u.trename.name);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rrename(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rrename)))
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_treadlink (u32 fid)
{
	int size = sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Treadlink)))
		return NULL;
        buf_put_int32(bufp, fid, &fc->u.treadlink.fid);

	return np_post_check(fc, bufp);
}


Npfcall *
np_create_rreadlink(char *target)
{
	int size = sizeof(u16) + strlen(target);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rreadlink)))
		return NULL;
	buf_put_str(bufp, target, &fc->u.rreadlink.target);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tgetattr(u32 fid, u64 request_mask)
{
	int size = sizeof(u32) + sizeof(u64);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tgetattr)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.tgetattr.fid);
	buf_put_int64(bufp, request_mask, &fc->u.tgetattr.request_mask);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rgetattr(u64 valid, Npqid *qid, u32 mode,
  		u32 uid, u32 gid, u64 nlink, u64 rdev,
		u64 size, u64 blksize, u64 blocks,
		u64 atime_sec, u64 atime_nsec,
		u64 mtime_sec, u64 mtime_nsec,
		u64 ctime_sec, u64 ctime_nsec,
		u64 btime_sec, u64 btime_nsec,
		u64 gen, u64 data_version)
{
	int bufsize = sizeof(u64) + QIDSIZE + 3*sizeof(u32) + 15*sizeof(u64);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, bufsize, Rgetattr)))
		return NULL;
	buf_put_int64(bufp, valid, &fc->u.rgetattr.valid);
	buf_put_qid(bufp, qid, &fc->u.rgetattr.qid);
	buf_put_int32(bufp, mode, &fc->u.rgetattr.mode);
	buf_put_int32(bufp, uid, &fc->u.rgetattr.uid);
	buf_put_int32(bufp, gid, &fc->u.rgetattr.gid);
	buf_put_int64(bufp, nlink, &fc->u.rgetattr.nlink);
	buf_put_int64(bufp, rdev, &fc->u.rgetattr.rdev);
	buf_put_int64(bufp, size, &fc->u.rgetattr.size);
	buf_put_int64(bufp, blksize, &fc->u.rgetattr.blksize);
	buf_put_int64(bufp, blocks, &fc->u.rgetattr.blocks);
	buf_put_int64(bufp, atime_sec, &fc->u.rgetattr.atime_sec);
	buf_put_int64(bufp, atime_nsec, &fc->u.rgetattr.atime_nsec);
	buf_put_int64(bufp, mtime_sec, &fc->u.rgetattr.mtime_sec);
	buf_put_int64(bufp, mtime_nsec, &fc->u.rgetattr.mtime_nsec);
	buf_put_int64(bufp, ctime_sec, &fc->u.rgetattr.ctime_sec);
	buf_put_int64(bufp, ctime_nsec, &fc->u.rgetattr.ctime_nsec);
	buf_put_int64(bufp, btime_sec, &fc->u.rgetattr.btime_sec);
	buf_put_int64(bufp, btime_nsec, &fc->u.rgetattr.btime_nsec);
	buf_put_int64(bufp, gen, &fc->u.rgetattr.gen);
	buf_put_int64(bufp, data_version, &fc->u.rgetattr.data_version);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tsetattr(u32 fid, u32 valid, u32 mode, u32 uid, u32 gid,
	 u64 size, u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec)
{
	int bufsize = 5*sizeof(u32) + 5*sizeof(u64);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, bufsize, Tsetattr)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.tsetattr.fid);
	buf_put_int32(bufp, valid, &fc->u.tsetattr.valid);
	buf_put_int32(bufp, mode, &fc->u.tsetattr.mode);
	buf_put_int32(bufp, uid, &fc->u.tsetattr.uid);
	buf_put_int32(bufp, gid, &fc->u.tsetattr.gid);
	buf_put_int64(bufp, size, &fc->u.tsetattr.size);
	buf_put_int64(bufp, atime_sec, &fc->u.tsetattr.atime_sec);
	buf_put_int64(bufp, atime_nsec, &fc->u.tsetattr.atime_nsec);
	buf_put_int64(bufp, mtime_sec, &fc->u.tsetattr.mtime_sec);
	buf_put_int64(bufp, mtime_nsec, &fc->u.tsetattr.mtime_nsec);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rsetattr(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rsetattr)))
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_txattrwalk(u32 fid, u32 attrfid, char *name)
{
	int size = 2*sizeof(u32) + sizeof(u16) + (name ? strlen (name) : 0);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Txattrwalk)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.txattrwalk.fid);
	buf_put_int32(bufp, attrfid, &fc->u.txattrwalk.attrfid);
	buf_put_str(bufp, name, &fc->u.txattrwalk.name);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rxattrwalk(u64 size)
{
	int bufsize = sizeof(u64);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, bufsize, Rxattrwalk)))
		return NULL;
	buf_put_int64(bufp, size, &fc->u.rxattrwalk.size);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_txattrcreate(u32 fid, char *name, u64 size, u32 flag)
{
	int bufsize = sizeof(u32)
		    + sizeof(u16) + (name ? strlen (name) : 0)
		    + sizeof(u64) + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, bufsize, Txattrcreate)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.txattrcreate.fid);
	buf_put_str(bufp, name, &fc->u.txattrcreate.name);
	buf_put_int64(bufp, size, &fc->u.txattrcreate.size);
	buf_put_int32(bufp, flag, &fc->u.txattrcreate.flag);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rxattrcreate(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rxattrcreate)))
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rreaddir(u32 count)
{
	int size = sizeof(u32) + count;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rreaddir)))
		return NULL;
	buf_put_int32(bufp, count, &fc->u.rreaddir.count);
	fc->u.rreaddir.data = buf_alloc(bufp, count);

	return np_post_check(fc, bufp);
}

void
np_finalize_rreaddir(Npfcall *fc, u32 count)
{
	int size = sizeof(u32) + sizeof(u8) + sizeof(u16)
		 + sizeof(u32) + count;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	NP_ASSERT (count <= fc->u.rreaddir.count);

	buf_init(bufp, (char *) fc->pkt, size);
	buf_put_int32(bufp, size, &fc->size);
	buf_init(bufp, (char *) fc->pkt + 7, size - 7);
	buf_put_int32(bufp, count, &fc->u.rreaddir.count);
}

Npfcall *
np_create_treaddir(u32 fid, u64 offset, u32 count)
{
	int size = sizeof(u32) + sizeof(u64) + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Treaddir)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.treaddir.fid);
	buf_put_int64(bufp, offset, &fc->u.treaddir.offset);
	buf_put_int32(bufp, count, &fc->u.treaddir.count);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tfsync(u32 fid, u32 datasync)
{
	int size = sizeof(u32) + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tfsync)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.tfsync.fid);
	buf_put_int32(bufp, datasync, &fc->u.tfsync.datasync);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rfsync(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rfsync)))
		return NULL;
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tlock(u32 fid, u8 type, u32 flags, u64 start, u64 length,
                u32 proc_id, char *client_id)
{
	int size = sizeof(u32) + sizeof(u8) + sizeof(u32) + 2*sizeof(u64)
		 + sizeof(u32) + sizeof(u16) + strlen(client_id);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tlock)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.tlock.fid);
	buf_put_int8(bufp, type, &fc->u.tlock.type);
	buf_put_int32(bufp, flags, &fc->u.tlock.flags);
	buf_put_int64(bufp, start, &fc->u.tlock.start);
	buf_put_int64(bufp, length, &fc->u.tlock.length);
	buf_put_int32(bufp, proc_id, &fc->u.tlock.proc_id);
	buf_put_str(bufp, client_id, &fc->u.tlock.client_id);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rlock(u8 status)
{
	int size = sizeof(u8);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rlock)))
		return NULL;
	buf_put_int8(bufp, status, &fc->u.rlock.status);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tgetlock(u32 fid, u8 type, u64 start, u64 length, u32 proc_id,
                   char *client_id)
{
	int size = sizeof(u32) + sizeof(u8) + 2*sizeof(u64) + sizeof(u32)
		 + sizeof(u16) + strlen(client_id);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tgetlock)))
		return NULL;
	buf_put_int32(bufp, fid, &fc->u.tgetlock.fid);
	buf_put_int8(bufp, type, &fc->u.tgetlock.type);
	buf_put_int64(bufp, start, &fc->u.tgetlock.start);
	buf_put_int64(bufp, length, &fc->u.tgetlock.length);
	buf_put_int32(bufp, proc_id, &fc->u.tgetlock.proc_id);
	buf_put_str(bufp, client_id, &fc->u.tgetlock.client_id);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rgetlock(u8 type, u64 start, u64 length, u32 proc_id, char *client_id)
{
	int size = sizeof(u8) + 2*sizeof(u64) + sizeof(u32)
		 + sizeof(u16) + strlen(client_id);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rgetlock)))
		return NULL;
	buf_put_int8(bufp, type, &fc->u.rgetlock.type);
	buf_put_int64(bufp, start, &fc->u.rgetlock.start);
	buf_put_int64(bufp, length, &fc->u.rgetlock.length);
	buf_put_int32(bufp, proc_id, &fc->u.rgetlock.proc_id);
	buf_put_str(bufp, client_id, &fc->u.rgetlock.client_id);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tlink(u32 dfid, u32 fid, char *name)
{
	int size = 2*sizeof(u32) + sizeof(u16) + strlen(name);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tlink)))
		return NULL;
	buf_put_int32(bufp, dfid, &fc->u.tlink.dfid);
	buf_put_int32(bufp, fid, &fc->u.tlink.fid);
	buf_put_str(bufp, name, &fc->u.tlink.name);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rlink(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rlink)))
		return NULL;
	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tmkdir(u32 dfid, char *name, u32 mode, u32 gid)
{
	int size = sizeof(u32) + 2 + strlen(name) + sizeof(u32) + sizeof(u32);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Tmkdir)))
		return NULL;
	buf_put_int32(bufp, dfid, &fc->u.tmkdir.fid);
	buf_put_str(bufp, name, &fc->u.tmkdir.name);
	buf_put_int32(bufp, mode, &fc->u.tmkdir.mode);
	buf_put_int32(bufp, gid, &fc->u.tmkdir.gid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rmkdir(Npqid *qid)
{
	int size = QIDSIZE;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rmkdir)))
		return NULL;
	buf_put_qid(bufp, qid, &fc->u.rmkdir.qid);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_trenameat(u32 olddirfid, char *oldname, u32 newdirfid, char *newname)
{
	int size = sizeof(u32) + sizeof(u16) + strlen(oldname)
		 + sizeof(u32) + sizeof(u16) + strlen(newname);
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Trenameat)))
		return NULL;
        buf_put_int32(bufp, olddirfid, &fc->u.trenameat.olddirfid);
        buf_put_str(bufp, oldname, &fc->u.trenameat.oldname);
        buf_put_int32(bufp, newdirfid, &fc->u.trenameat.newdirfid);
        buf_put_str(bufp, newname, &fc->u.trenameat.newname);

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_rrenameat(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Rrenameat)))
		return NULL;

	return np_post_check(fc, bufp);
}

Npfcall *
np_create_tunlinkat(u32 dirfid, char *name, u32 flags)
{
        int size = sizeof(u32) + sizeof(u16) + strlen(name) + sizeof(u32);
        struct cbuf buffer;
        struct cbuf *bufp = &buffer;
        Npfcall *fc;

        if (!(fc = np_create_common(bufp, size, Tunlinkat)))
                return NULL;
        buf_put_int32(bufp, dirfid, &fc->u.tunlinkat.dirfid);
        buf_put_str(bufp, name, &fc->u.tunlinkat.name);
        buf_put_int32(bufp, flags, &fc->u.tunlinkat.flags);

        return np_post_check(fc, bufp);
}

Npfcall *
np_create_runlinkat(void)
{
	int size = 0;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npfcall *fc;

	if (!(fc = np_create_common(bufp, size, Runlinkat)))
		return NULL;

	return np_post_check(fc, bufp);
}

u32
np_peek_size(u8 *buf, int len)
{
	u32 size = 0;

	if (len >= 4)
		size = buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24);
	return size;
}

Npfcall *
np_alloc_fcall(int msize)
{
        Npfcall *fc;

        if ((fc = malloc(sizeof(*fc) + msize))) {
                fc->pkt = (u8*) fc + sizeof(*fc);
		fc->size = msize;
	}

        return fc;
}

int
np_deserialize(Npfcall *fc)
{
	int i;
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;

	buf_init(bufp, fc->pkt, 4);
	fc->size = buf_get_int32(bufp);

	buf_init(bufp, fc->pkt + 4, fc->size - 4);
	fc->type = buf_get_int8(bufp);
	fc->tag = buf_get_int16(bufp);

	switch (fc->type) {
	default:
		goto error;
	case Rlerror:
		fc->u.rlerror.ecode = buf_get_int32(bufp);
		break;
	case Tversion:
		fc->u.tversion.msize = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tversion.version);
		break;
	case Rversion:
                fc->u.rversion.msize = buf_get_int32(bufp);
                buf_get_str(bufp, &fc->u.rversion.version);
                break;
	case Tauth:
		fc->u.tauth.afid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tauth.uname);
		buf_get_str(bufp, &fc->u.tauth.aname);
		fc->u.tauth.n_uname = buf_get_int32(bufp); /* .u extension */
		break;
	case Rauth:
                buf_get_qid(bufp, &fc->u.rauth.qid);
                break;
	case Tflush:
		fc->u.tflush.oldtag = buf_get_int16(bufp);
		break;
	case Rflush:
		break;
	case Tattach:
		fc->u.tattach.fid = buf_get_int32(bufp);
		fc->u.tattach.afid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tattach.uname);
		buf_get_str(bufp, &fc->u.tattach.aname);
		fc->u.tattach.n_uname = buf_get_int32(bufp); /* .u extension */
		break;
	case Rattach:
		buf_get_qid(bufp, &fc->u.rattach.qid);
                break;
	case Twalk:
		fc->u.twalk.fid = buf_get_int32(bufp);
		fc->u.twalk.newfid = buf_get_int32(bufp);
		fc->u.twalk.nwname = buf_get_int16(bufp);
		if (fc->u.twalk.nwname > MAXWELEM)
			goto error;

		for(i = 0; i < fc->u.twalk.nwname; i++) {
			buf_get_str(bufp, &fc->u.twalk.wnames[i]);
		}
		break;
	case Rwalk:
		fc->u.rwalk.nwqid = buf_get_int16(bufp);
                if (fc->u.rwalk.nwqid > MAXWELEM)
                        goto error;
                for(i = 0; i < fc->u.rwalk.nwqid; i++)
                        buf_get_qid(bufp, &fc->u.rwalk.wqids[i]);
                break;
	case Tread:
		fc->u.tread.fid = buf_get_int32(bufp);
		fc->u.tread.offset = buf_get_int64(bufp);
		fc->u.tread.count = buf_get_int32(bufp);
		break;
	case Rread:
		fc->u.rread.count = buf_get_int32(bufp);
                fc->u.rread.data = buf_alloc(bufp, fc->u.rread.count);
                break;
	case Twrite:
		fc->u.twrite.fid = buf_get_int32(bufp);
		fc->u.twrite.offset = buf_get_int64(bufp);
		fc->u.twrite.count = buf_get_int32(bufp);
		fc->u.twrite.data = buf_alloc(bufp, fc->u.twrite.count);
		break;
	case Rwrite:
		fc->u.rwrite.count = buf_get_int32(bufp);
                break;
	case Tclunk:
		fc->u.tclunk.fid = buf_get_int32(bufp);
		break;
	case Rclunk:
		break;
	case Tremove:
		fc->u.tremove.fid = buf_get_int32(bufp);
		break;
	case Rremove:
		break;
	case Tstatfs:
		fc->u.tstatfs.fid = buf_get_int32(bufp);
		break;
	case Rstatfs:
       		fc->u.rstatfs.type = buf_get_int32(bufp);
                fc->u.rstatfs.bsize = buf_get_int32(bufp);
                fc->u.rstatfs.blocks = buf_get_int64(bufp);
                fc->u.rstatfs.bfree = buf_get_int64(bufp);
                fc->u.rstatfs.bavail = buf_get_int64(bufp);
                fc->u.rstatfs.files = buf_get_int64(bufp);
                fc->u.rstatfs.ffree = buf_get_int64(bufp);
                fc->u.rstatfs.fsid = buf_get_int64(bufp);
                fc->u.rstatfs.namelen = buf_get_int32(bufp);
                break;
	case Tlopen:
		fc->u.tlopen.fid = buf_get_int32(bufp);
		fc->u.tlopen.flags = buf_get_int32(bufp);
		break;
	case Rlopen:
		buf_get_qid(bufp, &fc->u.rlopen.qid);
                fc->u.rlopen.iounit = buf_get_int32(bufp);
		break;
	case Tlcreate:
		fc->u.tlcreate.fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tlcreate.name);
		fc->u.tlcreate.flags = buf_get_int32(bufp);
		fc->u.tlcreate.mode = buf_get_int32(bufp);
		fc->u.tlcreate.gid = buf_get_int32(bufp);
		break;
	case Rlcreate:
		buf_get_qid(bufp, &fc->u.rlcreate.qid);
                fc->u.rlcreate.iounit = buf_get_int32(bufp);
		break;
	case Tsymlink:
		fc->u.tsymlink.fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tsymlink.name);
		buf_get_str(bufp, &fc->u.tsymlink.symtgt);
		fc->u.tsymlink.gid = buf_get_int32(bufp);
		break;
	case Rsymlink:
		buf_get_qid(bufp, &fc->u.rsymlink.qid);
		break;
	case Tmknod:
		fc->u.tmknod.fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tmknod.name);
		fc->u.tmknod.mode = buf_get_int32(bufp);
		fc->u.tmknod.major = buf_get_int32(bufp);
		fc->u.tmknod.minor = buf_get_int32(bufp);
		fc->u.tmknod.gid = buf_get_int32(bufp);
		break;
	case Rmknod:
		buf_get_qid(bufp, &fc->u.rmknod.qid);
		break;
	case Trename:
		fc->u.trename.fid = buf_get_int32(bufp);
		fc->u.trename.dfid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.trename.name);
		break;
	case Rrename:
		break;
	case Treadlink:
		fc->u.treadlink.fid = buf_get_int32(bufp);
		break;
	case Rreadlink:
		buf_get_str(bufp, &fc->u.rreadlink.target);
		break;
	case Tgetattr:
		fc->u.tgetattr.fid = buf_get_int32(bufp);
		fc->u.tgetattr.request_mask = buf_get_int64(bufp);
		break;
	case Rgetattr:
		fc->u.rgetattr.valid = buf_get_int64(bufp);
		buf_get_qid(bufp, &fc->u.rgetattr.qid);
		fc->u.rgetattr.mode = buf_get_int32(bufp);
		fc->u.rgetattr.uid = buf_get_int32(bufp);
		fc->u.rgetattr.gid = buf_get_int32(bufp);
		fc->u.rgetattr.nlink = buf_get_int64(bufp);
		fc->u.rgetattr.rdev = buf_get_int64(bufp);
		fc->u.rgetattr.size = buf_get_int64(bufp);
		fc->u.rgetattr.blksize = buf_get_int64(bufp);
		fc->u.rgetattr.blocks = buf_get_int64(bufp);
		fc->u.rgetattr.atime_sec = buf_get_int64(bufp);
		fc->u.rgetattr.atime_nsec = buf_get_int64(bufp);
		fc->u.rgetattr.mtime_sec = buf_get_int64(bufp);
		fc->u.rgetattr.mtime_nsec = buf_get_int64(bufp);
		fc->u.rgetattr.ctime_sec = buf_get_int64(bufp);
		fc->u.rgetattr.ctime_nsec = buf_get_int64(bufp);
		fc->u.rgetattr.btime_sec = buf_get_int64(bufp);
		fc->u.rgetattr.btime_nsec = buf_get_int64(bufp);
		fc->u.rgetattr.gen = buf_get_int64(bufp);
		fc->u.rgetattr.data_version = buf_get_int64(bufp);
		break;
	case Tsetattr:
		fc->u.tsetattr.fid = buf_get_int32(bufp);
		fc->u.tsetattr.valid = buf_get_int32(bufp);
		fc->u.tsetattr.mode = buf_get_int32(bufp);
		fc->u.tsetattr.uid = buf_get_int32(bufp);
		fc->u.tsetattr.gid = buf_get_int32(bufp);
		fc->u.tsetattr.size = buf_get_int64(bufp);
		fc->u.tsetattr.atime_sec = buf_get_int64(bufp);
		fc->u.tsetattr.atime_nsec = buf_get_int64(bufp);
		fc->u.tsetattr.mtime_sec = buf_get_int64(bufp);
		fc->u.tsetattr.mtime_nsec = buf_get_int64(bufp);
		break;
	case Rsetattr:
		break;
	case Txattrwalk:
		fc->u.txattrwalk.fid = buf_get_int32(bufp);
		fc->u.txattrwalk.attrfid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.txattrwalk.name);
		break;
	case Rxattrwalk:
		fc->u.rxattrwalk.size = buf_get_int64(bufp);
		break;
	case Txattrcreate:
		fc->u.txattrcreate.fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.txattrcreate.name);
		fc->u.txattrcreate.size = buf_get_int64(bufp);
		fc->u.txattrcreate.flag = buf_get_int32(bufp);
		break;
	case Rxattrcreate:
		break;
	case Treaddir:
		fc->u.treaddir.fid = buf_get_int32(bufp);
		fc->u.treaddir.offset = buf_get_int64(bufp);
		fc->u.treaddir.count = buf_get_int32(bufp);
		break;
	case Rreaddir:
		fc->u.rreaddir.count = buf_get_int32(bufp);
                fc->u.rreaddir.data = buf_alloc(bufp, fc->u.rreaddir.count);
		break;
	case Tfsync:
		fc->u.tfsync.fid = buf_get_int32(bufp);
		fc->u.tfsync.datasync = buf_get_int32(bufp);
		break;
	case Rfsync:
		break;
	case Tlock:
		fc->u.tlock.fid = buf_get_int32(bufp);
		fc->u.tlock.type = buf_get_int8(bufp);
		fc->u.tlock.flags = buf_get_int32(bufp);
		fc->u.tlock.start = buf_get_int64(bufp);
		fc->u.tlock.length = buf_get_int64(bufp);
		fc->u.tlock.proc_id = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tlock.client_id);
		break;
	case Rlock:
		fc->u.rlock.status = buf_get_int8(bufp);
		break;
	case Tgetlock:
		fc->u.tgetlock.fid = buf_get_int32(bufp);
		fc->u.tgetlock.type = buf_get_int8(bufp);
		fc->u.tgetlock.start = buf_get_int64(bufp);
		fc->u.tgetlock.length = buf_get_int64(bufp);
		fc->u.tgetlock.proc_id = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tgetlock.client_id);
		break;
	case Rgetlock:
		fc->u.rgetlock.type = buf_get_int8(bufp);
		fc->u.rgetlock.start = buf_get_int64(bufp);
		fc->u.rgetlock.length = buf_get_int64(bufp);
		fc->u.rgetlock.proc_id = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.rgetlock.client_id);
		break;
	case Tlink:
		fc->u.tlink.dfid = buf_get_int32(bufp);
		fc->u.tlink.fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tlink.name);
		break;
	case Rlink:
		break;
	case Tmkdir:
		fc->u.tmkdir.fid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tmkdir.name);
		fc->u.tmkdir.mode = buf_get_int32(bufp);
		fc->u.tmkdir.gid = buf_get_int32(bufp);
		break;
	case Rmkdir:
		buf_get_qid(bufp, &fc->u.rmkdir.qid);
		break;
	case Trenameat:
		fc->u.trenameat.olddirfid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.trenameat.oldname);
		fc->u.trenameat.newdirfid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.trenameat.newname);
		break;
	case Rrenameat:
		break;
	case Tunlinkat:
		fc->u.tunlinkat.dirfid = buf_get_int32(bufp);
		buf_get_str(bufp, &fc->u.tunlinkat.name);
		fc->u.tunlinkat.flags = buf_get_int32(bufp);
		break;
	case Runlinkat:
		break;
	}

	if (buf_check_overflow(bufp))
		goto error;

	return fc->size;

error:
	return 0;
}

int
np_serialize_p9dirent(Npqid *qid, u64 offset, u8 type, char *name,
		      u8 *buf, int buflen)
{
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	int size = QIDSIZE + sizeof(u64) + sizeof(u8)
		 + sizeof(u16) + strlen(name);
	Npstr nstr;
	Npqid nqid;

	if (size > buflen)
		return 0;
	buf_init(bufp, buf, buflen);
	buf_put_qid(bufp, qid, &nqid);
	buf_put_int64(bufp, offset, NULL);
	buf_put_int8(bufp, type, NULL);
	buf_put_str(bufp, name, &nstr);

	if (buf_check_overflow(bufp))
		return 0;

	return bufp->p - bufp->sp;
}

int
np_deserialize_p9dirent(Npqid *qid, u64 *offset, u8 *type,
			char *name, int namelen, u8 *buf, int buflen)
{
	struct cbuf buffer;
	struct cbuf *bufp = &buffer;
	Npstr s9;

	buf_init(bufp, buf, buflen);
	buf_get_qid(bufp, qid);
	*offset = buf_get_int64(bufp);
	*type = buf_get_int8(bufp);
	buf_get_str(bufp, &s9);

	if (buf_check_overflow (bufp) || s9.len >= namelen)
		return 0;

	memcpy (name, s9.str, s9.len);
	name[s9.len] = '\0';

	return bufp->p - bufp->sp;
}
