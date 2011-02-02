/* Copyright ©2007-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "ixp_local.h"

#define nelem(ary) (sizeof(ary) / sizeof(*ary))

enum {
	RootFid = 1,
};

static int
min(int a, int b) {
	if(a < b)
		return a;
	return b;
}

static IxpCFid*
getfid(IxpClient *c) {
	IxpCFid *f;

	thread->lock(&c->lk);
	f = c->freefid;
	if(f != nil)
		c->freefid = f->next;
	else {
		f = emallocz(sizeof *f);
		f->client = c;
		f->fid = ++c->lastfid;
		thread->initmutex(&f->iolock);
	}
	f->next = nil;
	f->open = 0;
	thread->unlock(&c->lk);
	return f;
}

static void
putfid(IxpCFid *f) {
	IxpClient *c;

	c = f->client;
	thread->lock(&c->lk);
	if(f->fid == c->lastfid) {
		c->lastfid--;
		thread->mdestroy(&f->iolock);
		free(f);
	}else {
		f->next = c->freefid;
		c->freefid = f;
	}
	thread->unlock(&c->lk);
}

static int
dofcall(IxpClient *c, IxpFcall *fcall) {
	IxpFcall *ret;

	ret = muxrpc(c, fcall);
	if(ret == nil)
		return 0;
	if(ret->hdr.type == RError) {
		werrstr("%s", ret->error.ename);
		goto fail;
	}
	if(ret->hdr.type != (fcall->hdr.type^1)) {
		werrstr("received mismatched fcall");
		goto fail;
	}
	memcpy(fcall, ret, sizeof *fcall);
	free(ret);
	return 1;
fail:
	ixp_freefcall(fcall);
	free(ret);
	return 0;
}

/**
 * Function: ixp_unmount
 *
 * Unmounts the client P<client> and frees its data structures.
 *
 * See also:
 *	F<ixp_mount>
 */
void
ixp_unmount(IxpClient *client) {
	IxpCFid *f;

	shutdown(client->fd, SHUT_RDWR);
	close(client->fd);

	muxfree(client);

	while((f = client->freefid)) {
		client->freefid = f->next;
		thread->mdestroy(&f->iolock);
		free(f);
	}
	free(client->rmsg.data);
	free(client->wmsg.data);
	free(client);
}

static void
allocmsg(IxpClient *c, int n) {
	c->rmsg.size = n;
	c->wmsg.size = n;
	c->rmsg.data = erealloc(c->rmsg.data, n);
	c->wmsg.data = erealloc(c->wmsg.data, n);
}

/**
 * Function: ixp_mount
 * Function: ixp_mountfd
 * Function: ixp_nsmount
 * Type: IxpClient
 *
 * Params:
 *	fd:      A file descriptor which is already connected
 *	         to a 9P server.
 *	address: An address (in Plan 9 resource fomat) at
 *	         which to connect to a 9P server.
 *	name:    The name of a socket in the process's canonical
 *	         namespace directory.
 *
 * Initiate a 9P connection with the server at P<address>,
 * connected to on P<fd>, or under the process's namespace
 * directory as P<name>.
 *
 * Returns:
 *	A pointer to a new 9P client.
 * See also:
 *	F<ixp_open>, F<ixp_create>, F<ixp_remove>, F<ixp_unmount>
 */

IxpClient*
ixp_mountfd(int fd) {
	IxpClient *c;
	IxpFcall fcall;

	c = emallocz(sizeof *c);
	c->fd = fd;

	muxinit(c);

	allocmsg(c, 256);
	c->lastfid = RootFid;
	/* Override tag matching on TVersion */
	c->mintag = IXP_NOTAG;
	c->maxtag = IXP_NOTAG+1;

	fcall.hdr.type = TVersion;
	fcall.version.msize = IXP_MAX_MSG;
	fcall.version.version = IXP_VERSION;

	if(dofcall(c, &fcall) == 0) {
		ixp_unmount(c);
		return nil;
	}

	if(strcmp(fcall.version.version, IXP_VERSION)
	|| fcall.version.msize > IXP_MAX_MSG) {
		werrstr("bad 9P version response");
		ixp_unmount(c);
		return nil;
	}

	c->mintag = 0;
	c->maxtag = 255;
	c->msize = fcall.version.msize;

	allocmsg(c, fcall.version.msize);
	ixp_freefcall(&fcall);

	fcall.hdr.type = TAttach;
	fcall.hdr.fid = RootFid;
	fcall.tattach.afid = IXP_NOFID;
	fcall.tattach.uname = getenv("USER");
	fcall.tattach.aname = "";
	if(dofcall(c, &fcall) == 0) {
		ixp_unmount(c);
		return nil;
	}

	return c;
}

IxpClient*
ixp_mount(const char *address) {
	int fd;

	fd = ixp_dial(address);
	if(fd < 0)
		return nil;
	return ixp_mountfd(fd);
}

IxpClient*
ixp_nsmount(const char *name) {
	char *address;
	IxpClient *c;

	address = ixp_namespace();
	if(address)
		address = ixp_smprint("unix!%s/%s", address, name);
	if(address == nil)
		return nil;
	c = ixp_mount(address);
	free(address);
	return c;
}

static IxpCFid*
walk(IxpClient *c, const char *path) {
	IxpCFid *f;
	char *p;
	IxpFcall fcall;
	int n;

	p = estrdup(path);
	n = tokenize(fcall.twalk.wname, nelem(fcall.twalk.wname), p, '/');
	f = getfid(c);

	fcall.hdr.type = TWalk;
	fcall.hdr.fid = RootFid;
	fcall.twalk.nwname = n;
	fcall.twalk.newfid = f->fid;
	if(dofcall(c, &fcall) == 0)
		goto fail;
	if(fcall.rwalk.nwqid < n) {
		werrstr("File does not exist");
		if(fcall.rwalk.nwqid == 0)
			werrstr("Protocol botch");
		goto fail;
	}

	f->qid = fcall.rwalk.wqid[n-1];

	ixp_freefcall(&fcall);
	free(p);
	return f;
fail:
	putfid(f);
	free(p);
	return nil;
}

static IxpCFid*
walkdir(IxpClient *c, char *path, const char **rest) {
	char *p;

	p = path + strlen(path) - 1;
	assert(p >= path);
	while(*p == '/')
		*p-- = '\0';

	while((p > path) && (*p != '/'))
		p--;
	if(*p != '/') {
		werrstr("bad path");
		return nil;
	}

	*p++ = '\0';
	*rest = p;
	return walk(c, path);
}

static int
clunk(IxpCFid *f) {
	IxpClient *c;
	IxpFcall fcall;
	int ret;

	c = f->client;

	fcall.hdr.type = TClunk;
	fcall.hdr.fid = f->fid;
	ret = dofcall(c, &fcall);
	if(ret)
		putfid(f);
	ixp_freefcall(&fcall);
	return ret;
}

/**
 * Function: ixp_remove
 *
 * Params:
 *	path: The path of the file to remove.
 *
 * Removes a file or directory from the remote server.
 *
 * Returns:
 *	ixp_remove returns 0 on failure, 1 on success.
 * See also:
 *	F<ixp_mount>
 */

int
ixp_remove(IxpClient *c, const char *path) {
	IxpFcall fcall;
	IxpCFid *f;
	int ret;

	if((f = walk(c, path)) == nil)
		return 0;

	fcall.hdr.type = TRemove;
	fcall.hdr.fid = f->fid;;
	ret = dofcall(c, &fcall);
	ixp_freefcall(&fcall);
	putfid(f);

	return ret;
}

static void
initfid(IxpCFid *f, IxpFcall *fcall) {
	f->open = 1;
	f->offset = 0;
	f->iounit = fcall->ropen.iounit;
	if(f->iounit == 0 || fcall->ropen.iounit > f->client->msize-24)
		f->iounit =  f->client->msize-24;
	f->qid = fcall->ropen.qid;
}

/**
 * Function: ixp_open
 * Function: ixp_create
 * Type: IxpCFid
 * Type: IxpOMode
 *
 * Params:
 *	path: The path of the file to open or create.
 *	perm: The permissions with which to create the new
 *	      file. These will be ANDed with those of the
 *	      parent directory by the server.
 *	mode: The file's open mode.
 *
 * ixp_open and ixp_create each open a file at P<path>.
 * P<mode> must include OREAD, OWRITE, or ORDWR, and may
 * include any of the modes specified in T<IxpOMode>.
 * ixp_create, additionally, creates a file at P<path> if it
 * doesn't already exist.
 *
 * Returns:
 *	A pointer on which to operate on the newly
 *      opened file.
 *
 * See also:
 *	F<ixp_mount>, F<ixp_read>, F<ixp_write>, F<ixp_print>,
 *	F<ixp_fstat>, F<ixp_close>
 */

IxpCFid*
ixp_create(IxpClient *c, const char *path, uint perm, uint8_t mode) {
	IxpFcall fcall;
	IxpCFid *f;
	char *tpath;;

	tpath = estrdup(path);

	f = walkdir(c, tpath, &path);
	if(f == nil)
		goto done;

	fcall.hdr.type = TCreate;
	fcall.hdr.fid = f->fid;
	fcall.tcreate.name = (char*)(uintptr_t)path;
	fcall.tcreate.perm = perm;
	fcall.tcreate.mode = mode;

	if(dofcall(c, &fcall) == 0) {
		clunk(f);
		f = nil;
		goto done;
	}

	initfid(f, &fcall);
	f->mode = mode;

	ixp_freefcall(&fcall);

done:
	free(tpath);
	return f;
}

IxpCFid*
ixp_open(IxpClient *c, const char *path, uint8_t mode) {
	IxpFcall fcall;
	IxpCFid *f;

	f = walk(c, path);
	if(f == nil)
		return nil;

	fcall.hdr.type = TOpen;
	fcall.hdr.fid = f->fid;
	fcall.topen.mode = mode;

	if(dofcall(c, &fcall) == 0) {
		clunk(f);
		return nil;
	}

	initfid(f, &fcall);
	f->mode = mode;

	ixp_freefcall(&fcall);
	return f;
}

/**
 * Function: ixp_close
 *
 * Closes the file pointed to by P<f> and frees its
 * associated data structures;
 *
 * Returns:
 *	Returns 1 on success, and zero on failure.
 * See also:
 *	F<ixp_mount>, F<ixp_open>
 */

int
ixp_close(IxpCFid *f) {
	return clunk(f);
}

static IxpStat*
_stat(IxpClient *c, ulong fid) {
	IxpMsg msg;
	IxpFcall fcall;
	IxpStat *stat;

	fcall.hdr.type = TStat;
	fcall.hdr.fid = fid;
	if(dofcall(c, &fcall) == 0)
		return nil;

	msg = ixp_message((char*)fcall.rstat.stat, fcall.rstat.nstat, MsgUnpack);

	stat = emalloc(sizeof *stat);
	ixp_pstat(&msg, stat);
	ixp_freefcall(&fcall);
	if(msg.pos > msg.end) {
		free(stat);
		stat = nil;
	}
	return stat;
}

/**
 * Function: ixp_stat
 * Function: ixp_fstat
 * Type: IxpStat
 * Type: IxpQid
 * Type: IxpQType
 * Type: IxpDMode
 *
 * Params:
 *	path: The path of the file to stat.
 *	fid:  An open file descriptor to stat.
 *
 * Stats the file at P<path> or pointed to by P<fid>.
 *
 * Returns:
 *	Returns an IxpStat structure, which must be freed by
 *	the caller with free(3).
 * See also:
 *	F<ixp_mount>, F<ixp_open>
 */

IxpStat*
ixp_stat(IxpClient *c, const char *path) {
	IxpStat *stat;
	IxpCFid *f;

	f = walk(c, path);
	if(f == nil)
		return nil;

	stat = _stat(c, f->fid);
	clunk(f);
	return stat;
}

IxpStat*
ixp_fstat(IxpCFid *fid) {
	return _stat(fid->client, fid->fid);
}

static long
_pread(IxpCFid *f, char *buf, long count, int64_t offset) {
	IxpFcall fcall;
	int n, len;

	len = 0;
	while(len < count) {
		n = min(count-len, f->iounit);

		fcall.hdr.type = TRead;
		fcall.hdr.fid = f->fid;
		fcall.tread.offset = offset;
		fcall.tread.count = n;
		if(dofcall(f->client, &fcall) == 0)
			return -1;
		if(fcall.rread.count > n)
			return -1;

		memcpy(buf+len, fcall.rread.data, fcall.rread.count);
		offset += fcall.rread.count;
		len += fcall.rread.count;

		ixp_freefcall(&fcall);
		if(fcall.rread.count < n)
			break;
	}
	return len;
}

/**
 * Function: ixp_read
 * Function: ixp_pread
 *
 * Params:
 *	buf:    A buffer in which to store the read data.
 *	count:  The number of bytes to read.
 *	offset: The offset at which to begin reading.
 *
 * ixp_read and ixp_pread each read P<count> bytes of data
 * from the file pointed to by P<fid>, into P<buf>. ixp_read
 * begins reading at its stored offset, and increments it by
 * the number of bytes read. ixp_pread reads beginning at
 * P<offset> and does not alter P<fid>'s stored offset.
 *
 * Returns:
 *	These functions return the number of bytes read on
 *	success and -1 on failure.
 * See also:
 *	F<ixp_mount>, F<ixp_open>, F<ixp_write>
 */

long
ixp_read(IxpCFid *fid, void *buf, long count) {
	int n;

	thread->lock(&fid->iolock);
	n = _pread(fid, buf, count, fid->offset);
	if(n > 0)
		fid->offset += n;
	thread->unlock(&fid->iolock);
	return n;
}

long
ixp_pread(IxpCFid *fid, void *buf, long count, int64_t offset) {
	int n;

	thread->lock(&fid->iolock);
	n = _pread(fid, buf, count, offset);
	thread->unlock(&fid->iolock);
	return n;
}

static long
_pwrite(IxpCFid *f, const void *buf, long count, int64_t offset) {
	IxpFcall fcall;
	int n, len;

	len = 0;
	do {
		n = min(count-len, f->iounit);
		fcall.hdr.type = TWrite;
		fcall.hdr.fid = f->fid;
		fcall.twrite.offset = offset;
		fcall.twrite.data = (char*)buf + len;
		fcall.twrite.count = n;
		if(dofcall(f->client, &fcall) == 0)
			return -1;

		offset += fcall.rwrite.count;
		len += fcall.rwrite.count;

		ixp_freefcall(&fcall);
		if(fcall.rwrite.count < n)
			break;
	} while(len < count);
	return len;
}

/**
 * Function: ixp_write
 * Function: ixp_pwrite
 *
 * Params:
 *	buf:    A buffer holding the contents to store.
 *	count:  The number of bytes to store.
 *	offset: The offset at which to write the data.
 *
 * ixp_write and ixp_pwrite each write P<count> bytes of
 * data stored in P<buf> to the file pointed to by C<fid>.
 * ixp_write writes its data at its stored offset, and
 * increments it by P<count>. ixp_pwrite writes its data a
 * P<offset> and does not alter C<fid>'s stored offset.
 *
 * Returns:
 *	These functions return the number of bytes actually
 *	written. Any value less than P<count> must be considered
 *	a failure.
 * See also:
 *	F<ixp_mount>, F<ixp_open>, F<ixp_read>
 */

long
ixp_write(IxpCFid *fid, const void *buf, long count) {
	int n;

	thread->lock(&fid->iolock);
	n = _pwrite(fid, buf, count, fid->offset);
	if(n > 0)
		fid->offset += n;
	thread->unlock(&fid->iolock);
	return n;
}

long
ixp_pwrite(IxpCFid *fid, const void *buf, long count, int64_t offset) {
	int n;

	thread->lock(&fid->iolock);
	n = _pwrite(fid, buf, count, offset);
	thread->unlock(&fid->iolock);
	return n;
}

/**
 * Function: ixp_print
 * Function: ixp_vprint
 * Variable: ixp_vsmprint
 *
 * Params:
 *      fid:  An open IxpCFid to which to write the result.
 *	fmt:  The string with which to format the data.
 *	args: A va_list holding the arguments to the format
 *	      string.
 *	...:  The arguments to the format string.
 *
 * These functions act like the standard formatted IO
 * functions. They write the result of the formatting to the
 * file pointed to by C<fid>.
 *
 * V<ixp_vsmprint> may be set to a function which will
 * format its arguments and return a nul-terminated string
 * allocated by malloc(3). The default formats its arguments as
 * printf(3).
 *
 * Returns:
 *	These functions return the number of bytes written.
 *	There is currently no way to detect failure.
 * See also:
 *	F<ixp_mount>, F<ixp_open>, printf(3)
 */

int
ixp_vprint(IxpCFid *fid, const char *fmt, va_list args) {
	char *buf;
	int n;

	buf = ixp_vsmprint(fmt, args);
	if(buf == nil)
		return -1;

	n = ixp_write(fid, buf, strlen(buf));
	free(buf);
	return n;
}

int
ixp_print(IxpCFid *fid, const char *fmt, ...) {
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = ixp_vprint(fid, fmt, ap);
	va_end(ap);

	return n;
}

