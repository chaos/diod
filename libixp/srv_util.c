/* Copyright ©2006-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "ixp_local.h"

typedef void*	IxpFileIdU;

static char
	Enofile[] = "file not found";

#include "ixp_srvutil.h"

struct IxpQueue {
	IxpQueue*	link;
	char*		dat;
	long		len;
};

#define QID(t, i) (((int64_t)((t)&0xFF)<<32)|((i)&0xFFFFFFFF))

static IxpFileId*	free_fileid;

/**
 * Function: ixp_srv_getfile
 * Type: IxpFileId
 *
 * Obtain an empty, reference counted IxpFileId struct.
 *
 * See also:
 *	F<ixp_srv_clonefiles>, F<ixp_srv_freefile>
 */
IxpFileId*
ixp_srv_getfile(void) {
	IxpFileId *file;
	int i;

	if(!free_fileid) {
		i = 15;
		file = emallocz(i * sizeof *file);
		for(; i; i--) {
			file->next = free_fileid;
			free_fileid = file++;
		}
	}
	file = free_fileid;
	free_fileid = file->next;
	file->p = nil;
	file->volatil = 0;
	file->nref = 1;
	file->next = nil;
	file->pending = false;
	return file;
}

/**
 * Function: ixp_srv_freefile
 *
 * Decrease the reference count of the given IxpFileId,
 * and push it onto the free list when it reaches 0;
 *
 * See also:
 *	F<ixp_srv_getfile>
 */
void
ixp_srv_freefile(IxpFileId *fileid) {
	if(--fileid->nref)
		return;
	free(fileid->tab.name);
	fileid->next = free_fileid;
	free_fileid = fileid;
}

/**
 * Function: ixp_srv_clonefiles
 *
 * Increase the reference count of every IxpFileId linked
 * to P<fileid>.
 *
 * See also:
 *	F<ixp_srv_getfile>
 */
IxpFileId*
ixp_srv_clonefiles(IxpFileId *fileid) {
	IxpFileId *r;

	r = emalloc(sizeof *r);
	memcpy(r, fileid, sizeof *r);
	r->tab.name = estrdup(r->tab.name);
	r->nref = 1;
	for(fileid=fileid->next; fileid; fileid=fileid->next)
		assert(fileid->nref++);
	return r;
}

/**
 * Function: ixp_srv_readbuf
 * Function: ixp_srv_writebuf
 *
 * Utility functions for handling TRead and TWrite requests for
 * files backed by in-memory buffers. For both functions, P<buf>
 * points to a buffer and P<len> specifies the length of the
 * buffer. In the case of ixp_srv_writebuf, these values add a
 * level of pointer indirection, and updates the values if they
 * change.
 *
 * If P<max> has a value other than 0, ixp_srv_writebuf will
 * truncate any writes to that point in the buffer. Otherwise,
 * P<*buf> is assumed to be malloc(3) allocated, and is
 * reallocated to fit the new data as necessary. The buffer is
 * is always left nul-terminated.
 *
 * Bugs:
 *	ixp_srv_writebuf always truncates its buffer to the end
 *	of the most recent write.
 */

void
ixp_srv_readbuf(Ixp9Req *req, char *buf, uint len) {

	if(req->ifcall.io.offset >= len)
		return;

	len -= req->ifcall.io.offset;
	if(len > req->ifcall.io.count)
		len = req->ifcall.io.count;
	req->ofcall.io.data = emalloc(len);
	memcpy(req->ofcall.io.data, buf + req->ifcall.io.offset, len);
	req->ofcall.io.count = len;
}

void
ixp_srv_writebuf(Ixp9Req *req, char **buf, uint *len, uint max) {
	IxpFileId *file;
	char *p;
	uint offset, count;

	file = req->fid->aux;

	offset = req->ifcall.io.offset;
	if(file->tab.perm & DMAPPEND)
		offset = *len;

	if(offset > *len || req->ifcall.io.count == 0) {
		req->ofcall.io.count = 0;
		return;
	}

	count = req->ifcall.io.count;
	if(max && (offset + count > max))
		count = max - offset;

	*len = offset + count;
	if(max == 0)
		*buf = erealloc(*buf, *len + 1);
	p = *buf;

	memcpy(p+offset, req->ifcall.io.data, count);
	req->ofcall.io.count = count;
	p[offset+count] = '\0';
}

/**
 * Function: ixp_srv_data2cstring
 *
 * Ensure that the data member of P<req> is null terminated,
 * removing any new line from its end.
 *
 * See also:
 *	S<Ixp9Req>
 */
void
ixp_srv_data2cstring(Ixp9Req *req) {
	char *p, *q;
	uint i;

	i = req->ifcall.io.count;
	p = req->ifcall.io.data;
	if(i && p[i - 1] == '\n')
		i--;
	q = memchr(p, '\0', i);
	if(q)
		i = q - p;

	p = erealloc(req->ifcall.io.data, i+1);
	p[i] = '\0';
	req->ifcall.io.data = p;
}

/**
 * Function: ixp_srv_writectl
 *
 * This utility function is meant to simplify the writing of
 * pseudo files to which single-lined commands are written.
 * In order to use this function, the P<aux> member of
 * P<req>->fid must be nul or an S<IxpFileId>.  Each line of the
 * written data is stripped of its trailing newline,
 * nul-terminated, and stored in an S<IxpMsg>. For each line
 * thus prepared, P<fn> is called with the IxpMsg pointer and
 * the the P<p> member of the IxpFileId.
 */
char*
ixp_srv_writectl(Ixp9Req *req, char* (*fn)(void*, IxpMsg*)) {
	char *err, *s, *p, c;
	IxpFileId *file;
	IxpMsg msg;

	file = req->fid->aux;

	ixp_srv_data2cstring(req);
	s = req->ifcall.io.data;

	err = nil;
	c = *s;
	while(c != '\0') {
		while(*s == '\n')
			s++;
		p = s;
		while(*p != '\0' && *p != '\n')
			p++;
		c = *p;
		*p = '\0';

		msg = ixp_message(s, p-s, 0);
		s = fn(file->p, &msg);
		if(s)
			err = s;
		s = p + 1;
	}
	return err;
}

/**
 * Function: ixp_pending_write
 * Function: ixp_pending_print
 * Function: ixp_pending_vprint
 * Function: ixp_pending_pushfid
 * Function: ixp_pending_clunk
 * Function: ixp_pending_flush
 * Function: ixp_pending_respond
 * Type: IxpPending
 *
 * These functions aid in writing virtual files used for
 * broadcasting events or writing data when it becomes
 * available. When a file to be used with these functions is
 * opened, ixp_pending_pushfid should be called with its
 * S<IxpFid> as an argument. This sets the IxpFid's P<pending>
 * member to true.  Thereafter, for each file with its
 * P<pending> member set, ixp_pending_respond should be called
 * for each TRead request, ixp_pending_clunk for each TClunk
 * request, and ixp_pending_flush for each TFlush request.
 *
 * ixp_pending_write queues the data in P<dat> of length P<ndat>
 * to be written to each currently pending fid in P<pending>. If
 * there is a read request pending for a given fid, the data is
 * written immediately. Otherwise, it is written the next time
 * ixp_pending_respond is called. Likewise, if there is data
 * queued when ixp_pending_respond is called, it is written
 * immediately, otherwise the request is queued.
 *
 * ixp_pending_print and ixp_pending_vprint call ixp_pending_write
 * after formatting their arguments with V<ixp_vsmprint>.
 *
 * The IxpPending data structure is opaque and should be
 * initialized zeroed before using these functions for the first
 * time.
 *
 * Returns:
 *	ixp_pending_clunk returns true if P<pending> has any
 *	more pending IxpFids.
 */

void
ixp_pending_respond(Ixp9Req *req) {
	IxpFileId *file;
	IxpPendingLink *p;
	IxpRequestLink *req_link;
	IxpQueue *queue;

	file = req->fid->aux;
	assert(file->pending);
	p = file->p;
	if(p->queue) {
		queue = p->queue;
		p->queue = queue->link;
		req->ofcall.io.data = queue->dat;
		req->ofcall.io.count = queue->len;
		if(req->aux) {
			req_link = req->aux;
			req_link->next->prev = req_link->prev;
			req_link->prev->next = req_link->next;
			free(req_link);
		}
		ixp_respond(req, nil);
		free(queue);
	}else {
		req_link = emallocz(sizeof *req_link);
		req_link->req = req;
		req_link->next = &p->pending->req;
		req_link->prev = req_link->next->prev;
		req_link->next->prev = req_link;
		req_link->prev->next = req_link;
		req->aux = req_link;
	}
}

void
ixp_pending_write(IxpPending *pending, const char *dat, long ndat) {
	IxpRequestLink req_link;
	IxpQueue **qp, *queue;
	IxpPendingLink *pp;
	IxpRequestLink *rp;

	if(ndat == 0)
		return;

	if(pending->req.next == nil) {
		pending->req.next = &pending->req;
		pending->req.prev = &pending->req;
		pending->fids.prev = &pending->fids;
		pending->fids.next = &pending->fids;
	}

	for(pp=pending->fids.next; pp != &pending->fids; pp=pp->next) {
		for(qp=&pp->queue; *qp; qp=&qp[0]->link)
			;
		queue = emallocz(sizeof *queue);
		queue->dat = emalloc(ndat);
		memcpy(queue->dat, dat, ndat);
		queue->len = ndat;
		*qp = queue;
	}

	req_link.next = &req_link;
	req_link.prev = &req_link;
	if(pending->req.next != &pending->req) {
		req_link.next = pending->req.next;
		req_link.prev = pending->req.prev;
		pending->req.prev = &pending->req;
		pending->req.next = &pending->req;
	}
	req_link.prev->next = &req_link;
	req_link.next->prev = &req_link;

	while((rp = req_link.next) != &req_link)
		ixp_pending_respond(rp->req);
}

int
ixp_pending_vprint(IxpPending *pending, const char *fmt, va_list ap) {
	char *dat;
	int res;

	dat = ixp_vsmprint(fmt, ap);
	res = strlen(dat);
	ixp_pending_write(pending, dat, res);
	free(dat);
	return res;
}

int
ixp_pending_print(IxpPending *pending, const char *fmt, ...) {
	va_list ap;
	int res;

	va_start(ap, fmt);
	res = ixp_pending_vprint(pending, fmt, ap);
	va_end(ap);
	return res;
}

void
ixp_pending_pushfid(IxpPending *pending, IxpFid *fid) {
	IxpPendingLink *pend_link;
	IxpFileId *file;

	if(pending->req.next == nil) {
		pending->req.next = &pending->req;
		pending->req.prev = &pending->req;
		pending->fids.prev = &pending->fids;
		pending->fids.next = &pending->fids;
	}

	file = fid->aux;
	pend_link = emallocz(sizeof *pend_link);
	pend_link->fid = fid;
	pend_link->pending = pending;
	pend_link->next = &pending->fids;
	pend_link->prev = pend_link->next->prev;
	pend_link->next->prev = pend_link;
	pend_link->prev->next = pend_link;
	file->pending = true;
	file->p = pend_link;
}

static void
pending_flush(Ixp9Req *req) {
	IxpFileId *file;
	IxpRequestLink *req_link;

	file = req->fid->aux;
	if(file->pending) {
		req_link = req->aux;
		if(req_link) {
			req_link->prev->next = req_link->next;
			req_link->next->prev = req_link->prev;
			free(req_link);
		}
	}
}

void
ixp_pending_flush(Ixp9Req *req) {

	pending_flush(req->oldreq);
}

bool
ixp_pending_clunk(Ixp9Req *req) {
	IxpPending *pending;
	IxpPendingLink *pend_link;
	IxpRequestLink *req_link;
	Ixp9Req *r;
	IxpFileId *file;
	IxpQueue *queue;
	bool more;

	file = req->fid->aux;
	pend_link = file->p;

	pending = pend_link->pending;
	for(req_link=pending->req.next; req_link != &pending->req;) {
		r = req_link->req;
		req_link = req_link->next;
		if(r->fid == pend_link->fid) {
			pending_flush(r);
			ixp_respond(r, "interrupted");
		}
	}

	pend_link->prev->next = pend_link->next;
	pend_link->next->prev = pend_link->prev;

	while((queue = pend_link->queue)) {
		pend_link->queue = queue->link;
		free(queue->dat);
		free(queue);
	}
	more = (pend_link->pending->fids.next == &pend_link->pending->fids);
	free(pend_link);
	ixp_respond(req, nil);
	return more;
}

/**
 * Function: ixp_srv_walkandclone
 * Function: ixp_srv_readdir
 * Function: ixp_srv_verifyfile
 * Type: IxpLookupFn
 *
 * These convenience functions simplify the writing of basic and
 * static file servers. They use a generic file lookup function
 * to simplify the process of walking, cloning, and returning
 * directory listings. Given the S<IxpFileId> of a directory and a
 * filename name should return a new IxpFileId (allocated via
 * F<ixp_srv_getfile>) for the matching directory entry, or null
 * if there is no match. If the passed name is null, P<lookup>
 * should return a linked list of IxpFileIds, one for each child
 * directory entry.
 *
 * ixp_srv_walkandclone handles the moderately complex process
 * of walking from a directory entry and cloning fids, and calls
 * F<ixp_respond>. It should be called in response to a TWalk
 * request.
 *
 * ixp_srv_readdir should be called to handle read requests on
 * directories. It prepares a stat for each child of the
 * directory, taking into account the requested offset, and
 * calls F<ixp_respond>. The P<dostat> parameter must be a
 * function which fills the passed S<IxpStat> pointer based on
 * the contents of the passed IxpFileId.
 *
 * ixp_srv_verifyfile returns whether a file still exists in the
 * filesystem, and should be used by filesystems that invalidate
 * files once they have been deleted.
 *
 * See also:
 *	S<IxpFileId>, S<ixp_getfile>, S<ixp_freefile>
 */
bool
ixp_srv_verifyfile(IxpFileId *file, IxpLookupFn lookup) {
	IxpFileId *tfile;
	int ret;

	if(!file->next)
		return true;

	ret = false;
	if(ixp_srv_verifyfile(file->next, lookup)) {
		tfile = lookup(file->next, file->tab.name);
		if(tfile) {
			if(!tfile->volatil || tfile->p == file->p)
				ret = true;
			ixp_srv_freefile(tfile);
		}
	}
	return ret;
}

void
ixp_srv_readdir(Ixp9Req *req, IxpLookupFn lookup, void (*dostat)(IxpStat*, IxpFileId*)) {
	IxpMsg msg;
	IxpFileId *file, *tfile;
	IxpStat stat;
	char *buf;
	ulong size, n;
	uint64_t offset;

	file = req->fid->aux;

	size = req->ifcall.io.count;
	if(size > req->fid->iounit)
		size = req->fid->iounit;
	buf = emallocz(size);
	msg = ixp_message(buf, size, MsgPack);

	file = lookup(file, nil);
	tfile = file;
	/* Note: The first file is ".", so we skip it. */
	offset = 0;
	for(file=file->next; file; file=file->next) {
		dostat(&stat, file);
		n = ixp_sizeof_stat(&stat);
		if(offset >= req->ifcall.io.offset) {
			if(size < n)
				break;
			ixp_pstat(&msg, &stat);
			size -= n;
		}
		offset += n;
	}
	while((file = tfile)) {
		tfile=tfile->next;
		ixp_srv_freefile(file);
	}
	req->ofcall.io.count = msg.pos - msg.data;
	req->ofcall.io.data = msg.data;
	ixp_respond(req, nil);
}

void
ixp_srv_walkandclone(Ixp9Req *req, IxpLookupFn lookup) {
	IxpFileId *file, *tfile;
	int i;

	file = ixp_srv_clonefiles(req->fid->aux);
	for(i=0; i < req->ifcall.twalk.nwname; i++) {
		if(!strcmp(req->ifcall.twalk.wname[i], "..")) {
			if(file->next) {
				tfile = file;
				file = file->next;
				ixp_srv_freefile(tfile);
			}
		}else{
			tfile = lookup(file, req->ifcall.twalk.wname[i]);
			if(!tfile)
				break;
			assert(!tfile->next);
			if(strcmp(req->ifcall.twalk.wname[i], ".")) {
				tfile->next = file;
				file = tfile;
			}
		}
		req->ofcall.rwalk.wqid[i].type = file->tab.qtype;
		req->ofcall.rwalk.wqid[i].path = QID(file->tab.type, file->id);
	}
	/* There should be a way to do this on freefid() */
	if(i < req->ifcall.twalk.nwname) {
		while((tfile = file)) {
			file=file->next;
			ixp_srv_freefile(tfile);
		}
		ixp_respond(req, Enofile);
		return;
	}
	/* Remove refs for req->fid if no new fid */
	if(req->ifcall.hdr.fid == req->ifcall.twalk.newfid) {
		tfile = req->fid->aux;
		req->fid->aux = file;
		while((file = tfile)) {
			tfile = tfile->next;
			ixp_srv_freefile(file);
		}
	}else
		req->newfid->aux = file;
	req->ofcall.rwalk.nwqid = i;
	ixp_respond(req, nil);
}

