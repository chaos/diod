/*
 * Copyright (C) 2008 by Latchesar Ionkov <lucho@ionkov.net>
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
//#define _XOPEN_SOURCE 500
#define _BSD_SOURCE
#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <pwd.h>
#include <grp.h>
#include <assert.h>
#include <sys/syscall.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <sched.h>

typedef uint8_t   u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef struct Conn Conn;
typedef struct Fid Fid;
typedef struct Wthread Wthread;
typedef struct Freq Freq;
typedef struct Qid Qid;

enum {
	Tfirst		= 100,
	Tversion	= 100,
	Rversion,
	Tauth		= 102,
	Rauth,
	Tattach		= 104,
	Rattach,
	Terror		= 106,
	Rerror,
	Tflush		= 108,
	Rflush,
	Twalk		= 110,
	Rwalk,
	Topen		= 112,
	Ropen,
	Tcreate		= 114,
	Rcreate,
	Tread		= 116,
	Rread,
	Twrite		= 118,
	Rwrite,
	Tclunk		= 120,
	Rclunk,
	Tremove		= 122,
	Rremove,
	Tstat		= 124,
	Rstat,
	Twstat		= 126,
	Rwstat,
	Tlast		= 126,

	Fidtblsz	= 8192,
	Fidhtblsz	= 32,
	Maxwelem	= 16,
};

/* modes */
enum {
	Oread		= 0x00,
	Owrite		= 0x01,
	Ordwr		= 0x02,
	Oexec		= 0x03,
	Oexcl		= 0x04,
	Otrunc		= 0x10,
	Orexec		= 0x20,
	Orclose		= 0x40,
	Oappend		= 0x80,
};

/* permissions */
enum {
	Dmdir		= 0x80000000,
	Dmappend	= 0x40000000,
	Dmexcl		= 0x20000000,
	Dmmount		= 0x10000000,
	Dmauth		= 0x08000000,
	Dmtmp		= 0x04000000,
	Dmsymlink	= 0x02000000,
	Dmlink		= 0x01000000,

	/* 9P2000.u extensions */
	Dmdevice	= 0x00800000,
	Dmnamedpipe	= 0x00200000,
	Dmsocket	= 0x00100000,
	Dmsetuid	= 0x00080000,
	Dmsetgid	= 0x00040000,
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

struct Fid {
	u32		fid;
	char*		path;
	int		fd;
	DIR*		dir;
	int		diroffset;
	char*		direntname;
	char		cpath[256];
	u8		type;
	uid_t		uid;
	gid_t		gid;
	Fid*		next;
};

struct Conn {
	int		sock;
	pthread_mutex_t	rlock;
	int		rfutex;
	int		dotu;
	u32		msize;

	/* data for the next message (if size not ~0) */
	u32		size;
	u8		type;
	u16		tag;
	u8		buf[23];
	int		bpos;

	/* used to write to file */
	int		pip[2];
	int		nwthreads;
	Wthread*	wthreads;

	/* Fid stuff */
	Fid fidtbl[Fidtblsz];
	Fid *fidhtbl[Fidhtblsz];
	pthread_mutex_t	wlock;
	int		wfutex;
};

struct Wthread {
	pthread_t	tid;
	Conn*		conn;
	u16		tag;
	Freq*		freqs;
	u8		stk[32768];
};

struct Freq {
	u16		tag;
	Freq*		next;
};

struct Qid {
	u8		type;
	u32		version;
	u64		path;
};

static int p9version(Conn *, u8, u16, u32, u8 *);
static int p9auth(Conn *, u8, u16, u32, u8 *);
static int p9attach(Conn *, u8, u16, u32, u8 *);
static int p9flush(Conn *, u8, u16, u32, u8 *);
static int p9walk(Conn *, u8, u16, u32, u8 *);
static int p9open(Conn *, u8, u16, u32, u8 *);
static int p9create(Conn *, u8, u16, u32, u8 *);
static int p9read(Conn *, u8, u16, u32, u8 *);
static int p9write(Conn *, u8, u16, u32, u8 *);
static int p9clunk(Conn *, u8, u16, u32, u8 *);
static int p9remove(Conn *, u8, u16, u32, u8 *);
static int p9stat(Conn *, u8, u16, u32, u8 *);
static int p9wstat(Conn *, u8, u16, u32, u8 *);

static int connproc(void *a);
void conndestroy(Conn *conn);
static int writen(Conn *conn, u8 *buf, int buflen);
static int rflush(Conn *conn, u16 tag, u8 *buf);

static int debuglevel;
static int msize = 8192;
static int dotu = 1;
static int sameuser = 0;

static int (*fhndlrs[])(Conn *, u8, u16, u32, u8 *) = {
	p9version,
	p9auth,
	p9attach,
	NULL,
	p9flush,
	p9walk,
	p9open,
	p9create,
	p9read,
	p9write,
	p9clunk,
	p9remove,
	p9stat,
	p9wstat,
};

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

static inline int
cmpxchg(int *ptr, int old, int new)
{
	int prev;

	asm volatile("lock cmpxchgl %k1,%2"
		: "=a"(prev)
		: "r"(new), "m"(*ptr), "0"(old)
		: "memory");

	return prev;
}

static inline int
xchg(int *ptr, int x)
{
	asm volatile("lock xchgl %k0,%1"
		: "=r" (x)
		: "m" (*ptr), "0" (x)
		: "memory");

	return x;
}

static inline int
dec(int *ptr)
{
        int i;

	i = -1;
        asm volatile("lock xaddl %0, %1"
                     : "+r" (i), "+m" (*ptr)
                     : : "memory");

        return i;
}

static int
futex(int *uaddr, int op, int val, const struct timespec *timeout, int *uaddr2, int val3)
{
	return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

static inline void
flock(int *val)
{
	int n;

	if ((n = cmpxchg(val, 0, 1)) == 0)
		return;

	while (xchg(val, 2) != 0)
		futex(val, FUTEX_WAIT, 2, NULL, NULL, 0);
}

static inline void
fulock(int *val)
{
	unsigned long n;

	if ((n = dec(val)) != 1) {
		*val = 0;
		futex(val, FUTEX_WAKE, 1, NULL, NULL, 0);
	}
}

static void
debug(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (debuglevel)
		vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static inline void
rlock(Conn *conn)
{
	flock(&conn->rfutex);
//	pthread_mutex_lock(&conn->rlock);
}

static inline void
runlock(Conn *conn)
{
	fulock(&conn->rfutex);
//	pthread_mutex_unlock(&conn->rlock);
}

static inline void
wlock(Conn *conn)
{
	flock(&conn->wfutex);
//	pthread_mutex_lock(&conn->wlock);
}

static inline void
wunlock(Conn *conn)
{
	fulock(&conn->wfutex);
//	pthread_mutex_unlock(&conn->wlock);
}

static inline u8 *
pint8(u8 *data, u8 val)
{
	data[0] = val;
	return data + 1;
}

static inline u8 *
pint16(u8 *data, u16 val)
{
	data[0] = val;
	data[1] = val >> 8;
	return data + 2;
}

static inline u8 *
pint32(u8 *data, u32 val)
{
	data[0] = val;
	data[1] = val >> 8;
	data[2] = val >> 16;
	data[3] = val >> 24;
	return data + 4;
}

static inline u8 *
pint64(u8 *data, u64 val)
{
	data[0] = val;
	data[1] = val >> 8;
	data[2] = val >> 16;
	data[3] = val >> 24;
	data[4] = val >> 32;
	data[5] = val >> 40;
	data[6] = val >> 48;
	data[7] = val >> 56;
	return data + 8;
}

static inline int
pstr(u8 *data, u16 sz, char *val)
{
	pint16(data, sz);
	if (sz)
		memmove(data+2, val, sz);

	return sz + 2;
}

static inline int
pstat(char *path, u8 *data, int dotu, int maxsz)
{
	int n, sz, nsz, usz, gsz, msz, esz;
	int mode;
	u32 version;
	u64 qpath;
	char ext[256], ubuf[256], gbuf[256], *name;
	struct passwd pw, *pwp;
	struct group grp, *pgrp;
	struct stat st;

	if (lstat(path, &st) < 0)
		return -1;

	name = strrchr(path, '/');
	if (name)
		name++;
	else
		name = path;

	mode = 0;
	if (S_ISDIR(st.st_mode))
		mode |= Dmdir;

	ext[0] = '\0';
	if (dotu) {
		if (S_ISLNK(st.st_mode)) {
			mode |= Dmsymlink;
			n = readlink(path, ext, sizeof(ext) - 1);
			if (n < 0)
				return -1;
		}

		if (S_ISSOCK(st.st_mode))
			mode |= Dmsocket;
		if (S_ISFIFO(st.st_mode))
			mode |= Dmnamedpipe;
		if (S_ISBLK(st.st_mode) || S_ISCHR(st.st_mode)) {
			snprintf(ext, sizeof(ext), "%c %u %u", S_ISCHR(st.st_mode)?'c':'b',
				major(st.st_rdev), minor(st.st_rdev));

			mode |= Dmdevice;
		}

		if (st.st_mode & S_ISUID)
			mode |= Dmsetuid;
		if (st.st_mode & S_ISGID)
			mode |= Dmsetgid;
        }

	mode |= st.st_mode & 0777;
	nsz = 0;
	usz = 0;
	gsz = 0;
	msz = 0;
	esz = 0;
	if (name)
		nsz = strlen(name);

	if (!dotu) {
		if (getpwuid_r(st.st_uid, &pw, ubuf, sizeof(ubuf), &pwp))
			return -1;

		usz = strlen(pw.pw_name);
		if (getgrgid_r(st.st_gid, &grp, gbuf, sizeof(gbuf), &pgrp))
			return -1;

		gsz = strlen(grp.gr_name);
	}
		
	esz = strlen(ext);
	sz = 2+4+13+4+4+4+8+2+2+2+2+nsz+usz+gsz+msz+esz;
	if (dotu)
		sz += 4+4+4+2;

	if (sz >= maxsz)
		return -1;

	qpath = 0;
	n = sizeof(qpath);
	if (n > sizeof(st.st_ino))
		n = sizeof(st.st_ino);
	memmove(&qpath, &st.st_ino, n);
	version = st.st_mtime ^ (st.st_size << 8);

	pint16(data, sz);
	pint16(data+2, 0);
	pint32(data+4, 0);
	pint8(data+8, mode>>24);
	pint32(data+9, version);
	pint64(data+13, qpath);
	pint32(data+21, mode);
	pint32(data+25, st.st_atime);
	pint32(data+29, st.st_mtime);
	pint64(data+33, st.st_size);
	n = 41;
	n += pstr(data+n, nsz, name);
	n += pstr(data+n, usz, pw.pw_name);
	n += pstr(data+n, gsz, grp.gr_name);
	n += pstr(data+n, 0, NULL);
	if (dotu) {
		n += pstr(data+n, esz, ext);
		pint32(data+n, st.st_uid);
		pint32(data+n+4, st.st_gid);
		pint32(data+n+8, ~0);
	}

	return sz + 2;
}

static inline u8
gint8(u8 *data)
{
	return data[0];
}

static inline u16
gint16(u8 *data)
{
	return data[0] | (data[1]<<8);
}

static inline u32
gint32(u8 *data)
{
	return data[0] | (data[1]<<8) | (data[2]<<16) | (data[3]<<24);
}

static inline u64
gint64(u8 *data)
{
	return (u64)data[0] | ((u64)data[1]<<8) | ((u64)data[2]<<16) | 
		((u64)data[3]<<24) | ((u64)data[4]<<32) | ((u64)data[5]<<40) | 
		((u64)data[6]<<48) | ((u64)data[7]<<56);
}

static inline int
gstr(u8 *data, char **s, int maxsize)
{
	u16 n;

	n = gint16(data);
	if (n >= maxsize)
		return -1;

	if (n) {
		memmove(data, data + 2, n);
		data[n] = '\0';
		*s = (char *) data;
	} else
		*s = NULL;

	return n;
}

Fid *
fidget(Conn *conn, u32 fid)
{
	int hash;
	Fid *f;

	if (fid < Fidtblsz) {
		f = &conn->fidtbl[fid];
		if (f->fid == ~0)
			f = NULL;

		return f;
	}

	hash = fid % Fidhtblsz;
	for(f = conn->fidhtbl[hash]; f != NULL; f = f->next)
		if (f->fid == fid)
			break;

	return f;
}

Fid *
fidcreate(Conn *conn, u32 fid)
{
	int hash;
	Fid *f, *p;

	if (fid < Fidtblsz) {
		f = &conn->fidtbl[fid];
		if (f->fid != ~0)
			return NULL;

		f->fid = fid;
		return f;
	}

	f = calloc(1, sizeof(Fid));
	hash = fid % Fidhtblsz;
	if (!conn->fidhtbl[hash])
		conn->fidhtbl[hash] = f;
	else {
		for(p = conn->fidhtbl[hash]; p->next != NULL; p = p->next)
			;

		p->next = f;
	}

	return f;
}

void
fiddestroy(Conn *conn, Fid *f)
{
	int hash;
	Fid *p;

	if (f->fid < Fidtblsz) {
		memset(f, 0, sizeof(*f));
		f->fid = ~0;
		return;
	}

	hash = f->fid % Fidhtblsz;
	if (f == conn->fidhtbl[hash])
		conn->fidhtbl[hash] = f->next;
	else {
		for(p = conn->fidhtbl[hash]; p->next != NULL; p = p->next)
			if (p->next == f) {
				p->next = f->next;
				break;
			}
	}

	free(f);
}

Conn *
conncreate(int sock, int nwthreads)
{
	int i;
	Conn *conn;

	conn = calloc(1, sizeof(*conn) + nwthreads*sizeof(Wthread));
	conn->sock = sock;
//	pthread_mutex_init(&conn->rlock, NULL);
//	pthread_mutex_init(&conn->wlock, NULL);
	conn->msize = msize;
	conn->dotu = dotu;
	conn->size = ~0;
	conn->bpos = 0;
	pipe(conn->pip);
	for(i = 0; i < Fidtblsz; i++)
		conn->fidtbl[i].fid = ~0;

	conn->nwthreads = 0;
	conn->wthreads = (Wthread *) &conn[1];
	for(i = 0; i < nwthreads; i++) {
		conn->wthreads[i].conn = conn;
		conn->wthreads[i].tag = ~0;

/*
		if (clone(connproc, conn->wthreads[i].stk + sizeof(conn->wthreads[i].stk),
			CLONE_FS | CLONE_FILES | CLONE_VM, &conn->wthreads[i]) < 0) {
			conndestroy(conn);
			return NULL;
		}
*/

		if (pthread_create(&conn->wthreads[i].tid, NULL, connproc, &conn->wthreads[i]) < 0) {
			conndestroy(conn);
			return NULL;
		}

		conn->nwthreads++;
	}

	return conn;
}

void
conndestroy(Conn *conn)
{
	int i;
	void *v;

	close(conn->sock);
	rlock(conn);
	conn->sock = -1;
	runlock(conn);

	for(i = 0; i < conn->nwthreads; i++)
		pthread_join(conn->wthreads[i].tid, &v);

	free(conn);
}

void
conndisconnect(Conn *conn)
{
	close(conn->sock);
	conn->sock = -1;
}

static int
connproc(void *a)
{
	int n;
	u8 type;
	u32 size;
	u16 tag;
	u8 *buf;
	Wthread *wt;
	Conn *conn;
	Freq *freq, *freq1;

	wt = a;
	conn = wt->conn;
	buf = malloc(conn->msize + sizeof(conn->buf));
	while (1) {
		rlock(conn);
		if (conn->sock < 0) {
			runlock(conn);
			return -1;
		}

		if (conn->size == ~0) {
ragain:
			n = read(conn->sock, conn->buf + conn->bpos,
				sizeof(conn->buf) - conn->bpos);

			if (n <= 0) {
				if (n<0 && (errno==EAGAIN || errno==EINTR))
					goto ragain;

				goto disconn;
			}

			conn->bpos += n;
			if (conn->bpos < 7)
				goto ragain;

			size = gint32(conn->buf);
			type = gint8(conn->buf + 4);
			tag = gint16(conn->buf + 5);
			assert(size>0&&size<conn->msize && type>=100);
		} else {
			size = conn->size;
			type = conn->type;
			tag = conn->tag;
		}

		assert(size <= conn->msize);
		conn->size = ~0;
		if (type&1 || type<Tfirst || type>Tlast || !fhndlrs[(type-Tfirst)/2])
			goto disconn;

		assert(wt->tag==(u16)~0 && wt->freqs==NULL);
		wt->tag = tag;
		assert(size>0);
		n = (*fhndlrs[(type-Tfirst)/2])(conn, type, tag, size, buf);
		wlock(conn);
		if (n)
			writen(conn, buf, n);

		freq = wt->freqs;
		while (freq != NULL) {
			n = rflush(conn, tag, buf);
			writen(conn, buf, n);
			freq1 = freq->next;
			free(freq);
			freq = freq1;
		}

		wt->tag = ~0;
		wt->freqs = NULL;
		wunlock(conn);
	}

disconn:
	conndisconnect(conn);
	runlock(conn);

	return -1;
}

static int
writen(Conn *conn, u8 *buf, int buflen)
{
	int n, pos;

	pos = 0;

again:
	n = write(conn->sock, buf + pos, buflen - pos);
	if (n <= 0) {
		if (n<0 && (errno==EAGAIN || errno==EINTR))
			goto again;

		conndisconnect(conn);
		return -1;
	}

	pos += n;
	if (pos < n)
		goto again;

	return buflen;
}

/* reads the rest of the message into buf, sets up the size, type, tag values
   for the next message, and unlocks the read lock */ 
static int
readrest(Conn *conn, u32 size, u8 *buf, int unlock)
{
	int n, sz;
	u8 *p;

	sz = size;
	p = buf;
	assert(sz >= 0);
	/* first get data from conn->buf, if there is any */
	n = conn->bpos - 7;
	if (n > 0) {
//		debug("%%%% move sz %d bpos %d\n", sz, conn->bpos);
		if (n > sz)
			n = sz;

		memmove(p, conn->buf + 7, n);
		sz -= n;
		p += n;
		if (n < conn->bpos-7)
			memmove(conn->buf, conn->buf + 7 + n, conn->bpos - 7 - n);

		conn->bpos -= 7 + n;
	}

	while (sz > 0) {
//		debug("%%%% read sz %d\n", sz);
		n = read(conn->sock, p, sz + (unlock?sizeof(conn->buf):0));
		if (n <= 0) {
			if (n<0 && (errno==EAGAIN || errno==EINTR))
				continue;

			goto error;
		}

		p += n;
		sz -= n;
	}

	/* now we have the whole message in buf, may be some data for the next */
	if (sz <= 0) {
//		debug("%%%% move2 sz %d bpos %d\n", sz, conn->bpos);
		memmove(conn->buf + conn->bpos, buf + size, -sz);
		conn->bpos += -sz;
	}

	if (conn->bpos > 7) {
		conn->size = gint32(conn->buf);
		conn->type = gint8(conn->buf + 4);
		conn->tag = gint16(conn->buf + 5);
		assert(conn->size>0 && conn->size<=conn->msize && conn->type>=100);
	}

	if (unlock)
		runlock(conn);
	return size;

error:
	fprintf(stderr, "readrest error %d\n", errno);
	conndisconnect(conn);
	if (unlock)
		runlock(conn);
	return -1;
}

static int
rerror(Conn *conn, u16 tag, u8 *buf, char *ename, int ecode)
{
	int sz, slen;

	slen = strlen(ename);
	sz = 9 + slen;
	if (conn->dotu)
		sz += 4;

	pint32(buf, sz);
	pint8(buf + 4, Rerror);
	pint16(buf + 5, tag);
	pint16(buf + 7, slen);
	memmove(buf+9, ename, slen);
	if (conn->dotu)
		pint32(buf + 9 + slen, ecode);

	return sz;
}

static int
ruerror(Conn *conn, u16 tag, u8 *buf, int ecode)
{
	char ename[256];

	strerror_r(ecode, ename, sizeof(ename));
	return rerror(conn, tag, buf, ename, ecode);
}

static int
badmsg(Conn *conn, u16 tag, u8 *buf)
{
	fprintf(stderr, "bad message\n");
	return rerror(conn, tag, buf, "invalid message", EINVAL);
}

static int
badfid(Conn *conn, u16 tag, u8 *buf)
{
	return rerror(conn, tag, buf, "invalid fid", EINVAL);
}

static int
rversion(Conn *conn, u16 tag, u8 *buf, char *version, int msize)
{
	int sz, slen;

	slen = strlen(version);
	sz = 13 + slen;
	pint32(buf, sz);
	pint8(buf + 4, Rversion);
	pint16(buf + 5, tag);
	pint32(buf + 7, msize);
	pint16(buf + 11, slen);
	memmove(buf+13, version, slen);
	return sz;
}

static int
rflush(Conn *conn, u16 tag, u8 *buf)
{
	pint32(buf, 7);
	pint8(buf + 4, Rflush);
	pint16(buf + 5, tag);
	return 7;
}

static int
rattach(Conn *conn, u16 tag, u8 *buf, Qid *qid)
{
	pint32(buf, 20);
	pint8(buf + 4, Rattach);
	pint16(buf + 5, tag);
	pint8(buf + 7, qid->type);
	pint32(buf + 8, qid->version);
	pint64(buf + 12, qid->path);
	return 20;
}

static int
rwalk(Conn *conn, u16 tag, u8 *buf, u16 nwqid, Qid *wqid)
{
	int i;

	pint32(buf, 9 + nwqid*13);
	pint8(buf + 4, Rwalk);
	pint16(buf + 5, tag);
	pint16(buf + 7, nwqid);
	for(i = 0; i < nwqid; i++) {
		pint8(buf + 9 + i*13, wqid[i].type);
		pint32(buf + 10 + i*13, wqid[i].version);
		pint64(buf + 14 + i*13, wqid[i].path);
	}

	return 9 + nwqid*13;
}

static int
ropen(Conn *conn, u16 tag, u8 *buf, Qid *qid, u32 iounit)
{
	pint32(buf, 24);
	pint8(buf + 4, Ropen);
	pint16(buf + 5, tag);
	pint8(buf + 7, qid->type);
	pint32(buf + 8, qid->version);
	pint64(buf + 12, qid->path);
	pint32(buf + 20, iounit);
	return 24;
}

static int
rcreate(Conn *conn, u16 tag, u8 *buf, Qid *qid, u32 iounit)
{
	pint32(buf, 24);
	pint8(buf + 4, Rcreate);
	pint16(buf + 5, tag);
	pint8(buf + 7, qid->type);
	pint32(buf + 8, qid->version);
	pint64(buf + 12, qid->path);
	pint32(buf + 20, iounit);
	return 24;
}

static int
rwrite(Conn *conn, u16 tag, u8 *buf, u32 count)
{
	pint32(buf, 11);
	pint8(buf + 4, Rwrite);
	pint16(buf + 5, tag);
	pint32(buf + 7, count);
	return 11;
}

static int
rclunk(Conn *conn, u16 tag, u8 *buf)
{
	pint32(buf, 7);
	pint8(buf + 4, Rclunk);
	pint16(buf + 5, tag);
	return 7;
}

static int
rremove(Conn *conn, u16 tag, u8 *buf)
{
	pint32(buf, 7);
	pint8(buf + 4, Rremove);
	pint16(buf + 5, tag);
	return 7;
}

static int
rstat(Conn *conn, u16 tag, u8 *buf, char *path)
{
	int sz;

	pint8(buf + 4, Rstat);
	pint16(buf + 5, tag);
	sz = pstat(path, buf + 9, conn->dotu, conn->msize - 9);
	if (sz < 0) {
		return rerror(conn, tag, buf, "insufficient msize", EIO);
	}

	pint16(buf + 7, sz);
	pint32(buf, 7 + sz + 2);
	return 9 + sz;
}

static int
rwstat(Conn *conn, u16 tag, u8 *buf)
{
	pint32(buf, 7);
	pint8(buf + 4, Rwstat);
	pint16(buf + 5, tag);
	return 7;
}

static inline void
setuser(uid_t uid, gid_t gid)
{
	if (sameuser)
		return;

	syscall(SYS_setreuid, -1, uid);
	syscall(SYS_setregid, -1, gid);
}

static int
p9version(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	int msize;
	char *version;

	debug("version tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	if (size < 6)
		return badmsg(conn, tag, buf);

	msize = gint32(buf);
	conn->dotu = 0;
	if (gstr(buf + 4, &version, size - 4) < 0)
		return badmsg(conn, tag, buf);

	if (msize < conn->msize)
		conn->msize = msize;

	if (strncmp(version, "9P2000", 6) != 0)
		return rerror(conn, tag, buf, "unsupported version", 0);

	conn->dotu = dotu;
	if (dotu && strcmp(version, "9P2000.u")==0)
		version = "9P2000.u";
	else {
		conn->dotu = 0;
		version = "9P2000";
	}

	return rversion(conn, tag, buf, version, conn->msize);
}


static int
p9auth(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	debug("auth tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	return rerror(conn, tag, buf, "authentication not required", EIO);
}

static int
stat2qid(struct stat *st, Qid *qid)
{
	int n;

	qid->type = 0;
	if (S_ISDIR(st->st_mode))
		qid->type |= Qtdir;

	if (S_ISLNK(st->st_mode))
		qid->type |= Qtsymlink;

        qid->path = 0;
        n = sizeof(qid->path);
        if (n > sizeof(st->st_ino))
                n = sizeof(st->st_ino);
        memmove(&qid->path, &st->st_ino, n);
        qid->version = st->st_mtime ^ (st->st_size << 8);

	return 0;
}

static int
statqid(char *path, Qid *qid)
{
	struct stat st;

	if (lstat(path, &st) < 0)
		return -errno;

	return stat2qid(&st, qid);
}

static int
p9attach(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	int n, m;
	char *uname, *aname;
	u32 nuname;
	Fid *fid;
	Qid qid;
	char ubuf[512];
	struct passwd pw, *pwp;

	debug("attach tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	if (size < 12)
		return badmsg(conn, tag, buf);

	fid = fidcreate(conn, gint32(buf));
	if (!fid)
		return rerror(conn, tag, buf, "cannot allocate fid", EIO);

	if (gint32(buf+4) != ~0)
		return rerror(conn, tag, buf, "invalid afid", EIO);

	n = 8;
	if ((m = gstr(buf+n, &uname, size-n)) < 0)
		return badmsg(conn, tag, buf);

	n += m + 2;
	if ((m = gstr(buf+n, &aname, size-n)) < 0)
		return rerror(conn, tag, buf, "invalid aname", EIO);

	n += m + 2;
	if (conn->dotu)
		nuname = gint32(buf+n);
	else
		nuname = ~0;

	fid->uid = ~0;
	fid->gid = ~0;
	if (nuname != ~0) {
		fid->uid = nuname;
		n = getpwuid_r(nuname, &pw, ubuf, sizeof(ubuf), &pwp);
		if (n)
			return ruerror(conn, tag, buf, n);

		fid->gid = pw.pw_gid;
	} else if (uname) {
		n = getpwnam_r(uname, &pw, ubuf, sizeof(ubuf), &pwp);
		if (n)
			return ruerror(conn, tag, buf, n);

		fid->uid = pw.pw_uid;
		fid->gid = pw.pw_gid;
	}

	setuser(fid->uid, fid->gid);
	if (aname)
		n = strlen(aname) + 1;
	else
		n = 2;

	if (n < sizeof(fid->cpath)) {
		fid->path = fid->cpath;
		if (aname)
			memmove(fid->path, aname, n);
		else
			fid->path = strdup("/");
	} else
		fid->path = strdup(aname);
			

	if ((n = statqid(fid->path, &qid)) < 0) {
		fiddestroy(conn, fid);
		return ruerror(conn, tag, buf, -n);
	}

	fid->type = qid.type;
	return rattach(conn, tag, buf, &qid);
}

static int
p9flush(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	int i;
	u16 oldtag;
	Wthread *wt;
	Freq *freq;

	debug("flush tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	oldtag = gint16(buf);
	if (oldtag == tag)
		goto respond;

	wlock(conn);
	for(i = 0; i < conn->nwthreads; i++) {
		wt = &conn->wthreads[i];
		if (wt->tag == oldtag) {
			freq = malloc(sizeof(*freq));
			freq->tag = oldtag;
			freq->next = wt->freqs;
			wt->freqs = freq;
			wunlock(conn);
			return 0;
		}
	}
	wunlock(conn);

respond:
	return rflush(conn, tag, buf);
}

static int
p9walk(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	int i, n, m, nwname, slen, olen;
	char *path;
	Fid *fid, *newfid;
	char *wnames[Maxwelem];
	u16 wlen[Maxwelem];
	Qid wqids[Maxwelem];

	debug("walk tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	if (size < 10)
		return badmsg(conn, tag, buf);

	fid = fidget(conn, gint32(buf));
	if (!fid)
		return badfid(conn, tag, buf);

	setuser(fid->uid, fid->gid);
	n = gint32(buf + 4);
	if (n != fid->fid) {
		newfid = fidcreate(conn, n);
		if (!newfid)
			return rerror(conn, tag, buf, "fid already exists", EIO);
	} else
		newfid = fid;

	nwname = gint16(buf + 8);
	slen = 0;
	for(i = 0, n = 10, m = 0; i < nwname; i++) {
		wlen[i] = gstr(buf+n, &wnames[i], size-n);
		if (wlen[i] < 0)
			return badmsg(conn, tag, buf);

		n += wlen[i] + 2;
		slen += wlen[i] + 1;
	}

	olen = strlen(fid->path);
	slen += olen + 1;
	if (slen >= sizeof(newfid->cpath))
		path = malloc(slen);
	else
		path = newfid->cpath;

	n = olen;
	memmove(path, fid->path, n+1);
	for(i = 0; i < nwname; i++) {
		path[n++] = '/';
		memmove(&path[n], wnames[i], wlen[i]);
		n += wlen[i];
		path[n] = '\0';
		if (statqid(path, &wqids[i]) < 0)
			break;
	}

	if (nwname && i<=0) {
		if (newfid != fid)
			fiddestroy(conn, newfid);
		else
			path[olen] = '\0';

		if (path != newfid->cpath)
			free(path);

		return ruerror(conn, tag, buf, ENOENT);
	}

	if (newfid->path && newfid->path != newfid->cpath)
		free(newfid->path);

	newfid->path = path;
	if (i==0)
		newfid->type = fid->type;
	else
		newfid->type = wqids[i-1].type;

	return rwalk(conn, tag, buf, i, wqids);
}

int
uflags(u8 mode)
{
	int flags;

	flags = 0;
	switch (mode & 3) {
	case Oexec:
	case Oread:
		flags = O_RDONLY;
		break;
	case Owrite:
		flags = O_WRONLY;
		break;
	case Ordwr:
		flags = O_RDWR;
		break;
	}

	if (mode & Otrunc)
		flags |= O_TRUNC;
	if (mode & Oappend)
		flags |= O_APPEND;
	if (mode & Oexcl)
		flags |= O_EXCL;

	return flags;
}

static int
p9open(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	Fid *fid;
	Qid qid;

	debug("open tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	if (size < 5)
		return badmsg(conn, tag, buf);

	fid = fidget(conn, gint32(buf));
	if (!fid)
		return badfid(conn, tag, buf);

	setuser(fid->uid, fid->gid);
	if (fid->type & Qtdir) {
		fid->dir = opendir(fid->path);
		if (!fid->dir)
			return ruerror(conn, tag, buf, errno);
	} else {
		fid->fd = open(fid->path, uflags(gint8(buf + 4)));
		if (fid->fd < 0)
			return ruerror(conn, tag, buf, errno);
	}

	if (statqid(fid->path, &qid) < 0)
		return ruerror(conn, tag, buf, errno);

	return ropen(conn, tag, buf, &qid, 0);
}


static int
p9create(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	u8 omode;
	char ctype;
	int olen, nlen, n;
	int major, minor;
	u32 perm;
	char *path, *name, *extension, *s;
	Fid *fid, *ofid;
	Qid qid;

	debug("create tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	if (size < 11)
		return badmsg(conn, tag, buf);

	fid = fidget(conn, gint32(buf));
	if (!fid)
		return badfid(conn, tag, buf);

	setuser(fid->uid, fid->gid);
	if (!(fid->type & Qtdir))
		return rerror(conn, tag, buf, "not a directory", ENOTDIR);

	nlen = gstr(buf+4, &name, size-4);
	if (nlen < 0)
		return badmsg(conn, tag, buf);

	perm = gint32(buf+6+nlen);
	omode = gint8(buf+10+nlen);
	if (conn->dotu && gstr(buf+11+nlen, &extension, size-8-nlen) < 0)
		return badmsg(conn, tag, buf);

	olen = strlen(fid->path);
	if (olen+nlen+2 < sizeof(fid->cpath))
		path = fid->cpath;
	else
		path = malloc(olen+nlen+2);

	memmove(path, fid->path, olen);
	path[olen] = '/';
	memmove(path + olen + 1, name, nlen + 1);

	if (perm & Dmdir) {
		if (mkdir(path, perm & 0777) < 0)
			goto uerror;
	} else if (perm & Dmnamedpipe) {
		if (mknod(path, S_IFIFO | (perm&0777), 0) < 0)
			goto uerror;
	} else if (perm & Dmsymlink) {
		if (symlink(extension, path) < 0)
			goto uerror;

		if (chmod(path, perm&0777) < 0)
			goto uerror;
	} else if (perm & Dmlink) {
		n = strtol(extension, &s, 10);
		if (*s != '\0') {
			n = rerror(conn, tag, buf, "invalid link", EINVAL);
			goto error;
		}

		ofid = fidget(conn, n);
		if (!ofid) {
			n = rerror(conn, tag, buf, "invalid fid", EINVAL);
			goto error;
		}

		if (link(ofid->path, path) < 0)
			goto uerror;
	} else if (perm & Dmdevice) {
		if (sscanf(extension, "%c %u %u", &ctype, &major, &minor) != 3) {
			n = rerror(conn, tag, buf, "invalid format", EINVAL);
			goto error;
		}

		switch (ctype) {
		case 'c':
			n = S_IFCHR;
			break;

		case 'b':
			n = S_IFBLK;
			break;

		default:
			n = rerror(conn, tag, buf, "invalid device type", EINVAL);
			goto error;
		}

		if (mknod(path, n & (perm&0777), 0) < 0)
			goto uerror;
	} else {
		fid->fd = open(path, O_CREAT | uflags(omode), perm&0777);
		if (fid->fd < 0)
			goto uerror;
	}

	if (fid->path != fid->cpath)
		free(fid->path);

	fid->path = path;
	if (statqid(fid->path, &qid) < 0)
		return ruerror(conn, tag, buf, errno);

	fid->type = qid.type;
	return rcreate(conn, tag, buf, &qid, 0);

uerror:
	n = ruerror(conn, tag, buf, errno);

error:
	if (path != fid->cpath)
		free(path);
	else
		path[olen] = '\0';

	return n;
}

static int
p9readdir(Conn *conn, u16 tag, u8 *buf, Fid *fid, u64 offset, u32 count)
{
	int n, m, plen;
	char *path, *dname;
	struct dirent *dirent;

	if (offset==0) {
		rewinddir(fid->dir);
		fid->diroffset = 0;
		free(fid->direntname);
		fid->direntname = NULL;
	}

	plen = strlen(fid->path);
	path = malloc(plen + NAME_MAX + 3);
	memmove(path, fid->path, plen);
	path[plen] = '/';
	n = 0;
	dname = fid->direntname;
	while (n < count) {
		if (!dname) {
			dirent = readdir(fid->dir);
			if (!dirent)
				break;

			if (strcmp(dirent->d_name, ".") == 0 ||
					strcmp(dirent->d_name, "..") == 0)
				continue;

			dname = dirent->d_name;
                }

		strcpy(&path[plen + 1], dname);
		m = pstat(path, buf + n + 11, conn->dotu, count + 11 - n);
		if (m < 0) {
			if (n==0) {
				m = errno;
				if (m==0)
					m = EIO;
				return ruerror(conn, tag, buf, m);
			}

			break;
		}

		n += m;
		dname = NULL;
	}

	free(fid->direntname);
	if (dname)
		fid->direntname = strdup(dname);

	pint32(buf, 11 + n);
	pint8(buf+4, Rread);
	pint16(buf+5, tag);
	pint32(buf+7, n);

	return 11 + n;
}

static int
p9read(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	int n, m;
	u32 count;
	u64 offset;
	off_t eoff;
	Fid *fid;
	struct msghdr mhdr;
	struct iovec iov;

	debug("read tag %d\n", tag);
	size -= 7;
	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	if (size < 16)
		return badmsg(conn, tag, buf);

	fid = fidget(conn, gint32(buf));
	if (!fid)
		return badfid(conn, tag, buf);

	setuser(fid->uid, fid->gid);
	offset = gint64(buf+4);
	count = gint32(buf+12);

	if (fid->type & Qtdir)
		return p9readdir(conn, tag, buf, fid, offset, count);

/*
	mhdr.msg_name = NULL;
	mhdr.msg_namelen = 0;
	mhdr.msg_iov = &iov;
	mhdr.msg_iovlen = 0;
	mhdr.msg_control = NULL;
	mhdr.msg_controllen = 0;
	mhdr.msg_flags = MSG_MORE;
	iov.iov_base = buf;
	iov.iov_len = 11;
	while (iov.iov_len > 0) {
		n = sendmsg(conn->sock, &mhdr, MSG_MORE);
		if (n < 0) {
			fprintf(stderr, "ERROR %d\n", errno);
			wunlock(conn);
			return 0;
		}

		iov.iov_base += n;
		iov.iov_len -= n;
	}
*/

#ifndef X
	n = 0;
	while (n < count) {
		m = pread(fid->fd, buf+11+n, count-n, offset+n);
		if (m < 0)
			return ruerror(conn, tag, buf, errno);
		else if (m == 0)
			break;

		n += m;
	}

	pint32(buf, 11+n);
	pint8(buf+4, Rread);
	pint16(buf+5, tag);
	pint32(buf+7, n);

	return 11+n;
#else
	wlock(conn);
	eoff = lseek(fid->fd, 0, SEEK_END);
	if (eoff-offset < count)
		count = eoff - offset;

	pint32(buf, 11 + count);
	pint8(buf+4, Rread);
	pint16(buf+5, tag);
	pint32(buf+7, count);
	n = writen(conn, buf, 11);
	if (n < 0) {
		wunlock(conn);
		return 0;
	}

	eoff = offset;
	while (count) {
		n = sendfile(conn->sock, fid->fd, &eoff, count);
		if (n < 0) {
			if (errno==EAGAIN || errno==EINTR)
				continue;

			// TODO: that's not right
			return ruerror(conn, tag, buf, errno);
		} else if (n == 0)
			break;

		count -= n;
	}
	wunlock(conn);


	return 0;
#endif
}

static int
p9write(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	int i, n, m, c;
	u32 count;
	u64 offset;
	Fid *fid;

	debug("write tag %d\n", tag);
	size -= 7;
	if (size < 16)
		return badmsg(conn, tag, buf);

#ifdef XY
	if (readrest(conn, 16, buf, 0) < 0) {
#else
	if (readrest(conn, size, buf, 1) < 0) {
#endif
		runlock(conn);
		return 0;
	}

	fid = fidget(conn, gint32(buf));
	if (!fid) {
		runlock(conn);
		return badfid(conn, tag, buf);
	}

	setuser(fid->uid, fid->gid);
	offset = gint64(buf+4);
	count = gint32(buf+12);

	if (fid->type & Qtdir) {
		readrest(conn, size - 16, buf, 1);
		return rerror(conn, tag, buf, "permission denied", EPERM);
	}

#ifdef XY
	c = count;
	if (conn->bpos) {
		n = conn->bpos;
		if (count < n)
			n = count;

		i = 0;
		while (i < n) {
			m = pwrite(fid->fd, conn->buf + i, n - i, offset + i);
			if (m < 0)
				return ruerror(conn, tag, buf, errno);
			else if (m == 0)
				goto error;
		}

		if (conn->bpos > count) {
			memmove(conn->buf, conn->buf + count, conn->bpos - count);
			conn->bpos -= count;
		}

		offset += n;
		count -= n;
	}

	while (count > 0) {
		n = splice(conn->sock, NULL, conn->pip[1], NULL, count, 0);
		if (n < 0)
			goto error;

		m = splice(conn->pip[0], NULL, fid->fd, (off_t *) &offset, n, 0);
		if (m < 0)
			goto error;

		if (m != n)
			goto error;

		count -= n;
	}

	runlock(conn);
#else
	c = count;
	n = 0;
	while (n < count) {
		m = pwrite(fid->fd, buf+4+n, count-n, offset+n);
		if (m < 0)
			return ruerror(conn, tag, buf, errno);
		else if (m == 0)
			goto error;

		n += m;
	}

	return rwrite(conn, tag, buf, c);
#endif

error:
	fprintf(stderr, "*** splice error\n");
	conndisconnect(conn);
	return 0;
}


static int
p9clunk(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	Fid *fid;

	debug("clunk tag %d\n", tag);
	size -= 7;
	if (size < 4)
		return badmsg(conn, tag, buf);

	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	fid = fidget(conn, gint32(buf));
	if (!fid) {
		runlock(conn);
		return badfid(conn, tag, buf);
	}

	setuser(fid->uid, fid->gid);
	if (fid->fd)
		close(fid->fd);

	if (fid->dir) {
		closedir(fid->dir);
	}

	if (fid->path != fid->cpath)
		free(fid->path);

	free(fid->direntname);
	fiddestroy(conn, fid);

	return rclunk(conn, tag, buf);
}


static int
p9remove(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	Fid *fid;

	debug("remove tag %d\n", tag);
	size -= 7;
	if (size < 4)
		return badmsg(conn, tag, buf);

	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	fid = fidget(conn, gint32(buf));
	if (!fid) {
		runlock(conn);
		return badfid(conn, tag, buf);
	}

	setuser(fid->uid, fid->gid);
	if (fid->type&Qtdir) {
		if (rmdir(fid->path) < 0)
			return ruerror(conn, tag, buf, errno);
	} else {
		if (unlink(fid->path) < 0)
			return ruerror(conn, tag, buf, errno);
	}

	if (fid->fd)
		close(fid->fd);

	if (fid->path != fid->cpath)
		free(fid->path);

	free(fid->direntname);
	fiddestroy(conn, fid);
	return rremove(conn, tag, buf);
}


static int
p9stat(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	Fid *fid;

	debug("stat tag %d\n", tag);
	size -= 7;
	if (size < 4)
		return badmsg(conn, tag, buf);

	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	fid = fidget(conn, gint32(buf));
	if (!fid) {
		runlock(conn);
		return badfid(conn, tag, buf);
	}

	setuser(fid->uid, fid->gid);
	return rstat(conn, tag, buf, fid->path);
}


static int
p9wstat(Conn *conn, u8 type, u16 tag, u32 size, u8 *buf)
{
	int n, nlen, pos, glen;
	u32 mode, mtime, ngid;
	u64 length;
	char *name, *gid, *p, *npath, ubuf[512];
	Fid *fid;
	struct group grp, *pgrp;
	struct utimbuf tb;

	debug("wstat tag %d\n", tag);
	size -= 7;
	if (size < 53)
		return badmsg(conn, tag, buf);

	if (conn->dotu && size<67)
		return badmsg(conn, tag, buf);

	if (readrest(conn, size, buf, 1) < 0)
		return 0;

	fid = fidget(conn, gint32(buf));
	if (!fid) {
		runlock(conn);
		return badfid(conn, tag, buf);
	}

	setuser(fid->uid, fid->gid);
	mode = gint32(buf+27);
	mtime = gint32(buf+35);
	length = gint64(buf+39);
	nlen = gstr(buf+47, &name, size-47);
	if (nlen<0)
		return badmsg(conn, tag, buf);

	pos = 49 + nlen;
	pos += gint16(buf+pos) + 2;
	if (pos >= size)
		return badmsg(conn, tag, buf);

	glen = gstr(buf+pos, &gid, size-pos);
	if (glen<0)
		return badmsg(conn, tag, buf);

	pos += glen + 2;
	pos += gint16(buf+pos) + 2;
	if (pos >= size)
		return badmsg(conn, tag, buf);

	ngid = ~0;
	if (conn->dotu) {
		pos += gint16(buf+pos) + 2;
		if (pos >= size)
			return badmsg(conn, tag, buf);

		ngid = gint32(buf+pos+4);
	}

	if (ngid==~0 && gid) {
		n = getgrnam_r(gid, &grp, ubuf, sizeof(ubuf), &pgrp);
		if (n)
			return ruerror(conn, tag, buf, n);

		ngid = grp.gr_gid;
	}

	if (mode != ~0) {
		if (chmod(fid->path, mode&0777) < 0)
			return ruerror(conn, tag, buf, errno);
	}
	if (mtime != ~0) {
		tb.actime = 0;
		tb.modtime = mtime;
		if (utime(fid->path, &tb) < 0)
			return ruerror(conn, tag, buf, errno);
	}
	if (ngid != ~0) {
		if (chown(fid->path, -1, ngid) < 0)
			return ruerror(conn, tag, buf, errno);
	}
	if (name) {
		p = strrchr(fid->path, '/');
		if (!p)
			p = fid->path + strlen(fid->path);

		nlen = strlen(name);
		npath = malloc(nlen + (p - fid->path) + 2);
		memmove(npath, fid->path, p - fid->path);
		npath[p - fid->path] = '/';
		memmove(npath + (p - fid->path) + 1, name, nlen);
		npath[(p - fid->path) + 1 + nlen] = 0;
		if (strcmp(npath, fid->path) != 0) {
			if (rename(fid->path, npath) < 0) {
				free(npath);
				return ruerror(conn, tag, buf, errno);
			}

			if (fid->path != fid->cpath)
				free(fid->path);

			fid->path = npath;
		} else
			free(npath);
	}
	if (length != ~0) {
		if (truncate(fid->path, length) < 0)
			return ruerror(conn, tag, buf, errno);
	}

	return rwstat(conn, tag, buf);
}

void
usage()
{
	fprintf(stderr, "npfs: -d -s -p port -w nthreads\n");
	exit(-1);
}

int
main(int argc, char *argv[])
{
	int c, csock, sock;
	socklen_t n;
	int port, nwthreads;
	char *s;
	struct sockaddr_in saddr;
	Conn *conn;

	port = 564;
	nwthreads = 16;
	msize = 8216;
	while ((c = getopt(argc, argv, "dsp:w:m:")) != -1) {
		switch (c) {
		case 'd':
			debuglevel = 1;
			break;

		case 'p':
			port = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 's':
			sameuser = 1;
			break;

		case 'm':
			msize = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		default:
			usage();
		}
	}

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket");
		return -1;
	}

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons(port);
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, (struct sockaddr *) &saddr, sizeof(saddr)) < 0) {
		perror("bind");
		return -1;
	}

	if (listen(sock, 1) < 0) {
		perror("listen");
		return -1;
	}

	n = sizeof(saddr);
	while ((csock = accept(sock, &saddr, &n)) >= 0) {
		conn = conncreate(csock, nwthreads);
	}

	return 0;
}
