/* Copyright ©2006-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include "ixp_local.h"

static void handlereq(Ixp9Req *r);

/**
 * Variable: ixp_printfcall
 *
 * When set to a non-null value, ixp_printfcall is called once for
 * every incoming and outgoing Fcall. It is intended to simplify the
 * writing of debugging code for clients, but may be used for any
 * arbitrary purpose.
 *
 * See also:
 *	F<ixp_respond>, F<ixp_serve9conn>
 */
void (*ixp_printfcall)(IxpFcall*);

static int
min(int a, int b) {
	if(a < b)
		return a;
	return b;
}

static char
	Eduptag[] = "tag in use",
	Edupfid[] = "fid in use",
	Enofunc[] = "function not implemented",
	Eopen[] = "fid is already open",
	Enofile[] = "file does not exist",
	Enoread[] = "file not open for reading",
	Enofid[] = "fid does not exist",
	Enotag[] = "tag does not exist",
	Enotdir[] = "not a directory",
	Eintr[] = "interrupted",
	Eisdir[] = "cannot perform operation on a directory";

enum {
	TAG_BUCKETS = 61,
	FID_BUCKETS = 61,
};

struct Ixp9Conn {
	Map		tagmap;
	Map		fidmap;
	MapEnt*		taghash[TAG_BUCKETS];
	MapEnt*		fidhash[FID_BUCKETS];
	Ixp9Srv*	srv;
	IxpConn*	conn;
	IxpMutex	rlock;
	IxpMutex	wlock;
	IxpMsg		rmsg;
	IxpMsg		wmsg;
	int		ref;
};

static void
decref_p9conn(Ixp9Conn *p9conn) {
	thread->lock(&p9conn->wlock);
	if(--p9conn->ref > 0) {
		thread->unlock(&p9conn->wlock);
		return;
	}
	thread->unlock(&p9conn->wlock);

	assert(p9conn->conn == nil);

	thread->mdestroy(&p9conn->rlock);
	thread->mdestroy(&p9conn->wlock);

	ixp_mapfree(&p9conn->tagmap, nil);
	ixp_mapfree(&p9conn->fidmap, nil);

	free(p9conn->rmsg.data);
	free(p9conn->wmsg.data);
	free(p9conn);
}

static void*
createfid(Map *map, int fid, Ixp9Conn *p9conn) {
	IxpFid *f;

	f = emallocz(sizeof *f);
	p9conn->ref++;
	f->conn = p9conn;
	f->fid = fid;
	f->omode = -1;
	f->map = map;
	if(ixp_mapinsert(map, fid, f, false))
		return f;
	free(f);
	return nil;
}

static int
destroyfid(Ixp9Conn *p9conn, ulong fid) {
	IxpFid *f;

	f = ixp_maprm(&p9conn->fidmap, fid);
	if(f == nil)
		return 0;

	if(p9conn->srv->freefid)
		p9conn->srv->freefid(f);

	decref_p9conn(p9conn);
	free(f);
	return 1;
}

static void
handlefcall(IxpConn *c) {
	IxpFcall fcall = {{0}};
	Ixp9Conn *p9conn;
	Ixp9Req *req;

	p9conn = c->aux;

	thread->lock(&p9conn->rlock);
	if(ixp_recvmsg(c->fd, &p9conn->rmsg) == 0)
		goto Fail;
	if(ixp_msg2fcall(&p9conn->rmsg, &fcall) == 0)
		goto Fail;
	thread->unlock(&p9conn->rlock);

	req = emallocz(sizeof *req);
	p9conn->ref++;
	req->conn = p9conn;
	req->srv = p9conn->srv;
	req->ifcall = fcall;
	p9conn->conn = c;

	if(!ixp_mapinsert(&p9conn->tagmap, fcall.hdr.tag, req, false)) {
		ixp_respond(req, Eduptag);
		return;
	}

	handlereq(req);
	return;

Fail:
	thread->unlock(&p9conn->rlock);
	ixp_hangup(c);
	return;
}

static void
handlereq(Ixp9Req *r) {
	Ixp9Conn *p9conn;
	Ixp9Srv *srv;

	p9conn = r->conn;
	srv = p9conn->srv;

	if(ixp_printfcall)
		ixp_printfcall(&r->ifcall);

	switch(r->ifcall.hdr.type) {
	default:
		ixp_respond(r, Enofunc);
		break;
	case TVersion:
		if(!strcmp(r->ifcall.version.version, "9P"))
			r->ofcall.version.version = "9P";
		else if(!strcmp(r->ifcall.version.version, "9P2000"))
			r->ofcall.version.version = "9P2000";
		else
			r->ofcall.version.version = "unknown";
		r->ofcall.version.msize = r->ifcall.version.msize;
		ixp_respond(r, nil);
		break;
	case TAttach:
		if(!(r->fid = createfid(&p9conn->fidmap, r->ifcall.hdr.fid, p9conn))) {
			ixp_respond(r, Edupfid);
			return;
		}
		/* attach is a required function */
		srv->attach(r);
		break;
	case TClunk:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if(!srv->clunk) {
			ixp_respond(r, nil);
			return;
		}
		srv->clunk(r);
		break;
	case TFlush:
		if(!(r->oldreq = ixp_mapget(&p9conn->tagmap, r->ifcall.tflush.oldtag))) {
			ixp_respond(r, Enotag);
			return;
		}
		if(!srv->flush) {
			ixp_respond(r, Enofunc);
			return;
		}
		srv->flush(r);
		break;
	case TCreate:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if(r->fid->omode != -1) {
			ixp_respond(r, Eopen);
			return;
		}
		if(!(r->fid->qid.type&QTDIR)) {
			ixp_respond(r, Enotdir);
			return;
		}
		if(!p9conn->srv->create) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->create(r);
		break;
	case TOpen:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if((r->fid->qid.type&QTDIR) && (r->ifcall.topen.mode|P9_ORCLOSE) != (P9_OREAD|P9_ORCLOSE)) {
			ixp_respond(r, Eisdir);
			return;
		}
		r->ofcall.ropen.qid = r->fid->qid;
		if(!p9conn->srv->open) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->open(r);
		break;
	case TRead:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if(r->fid->omode == -1 || r->fid->omode == P9_OWRITE) {
			ixp_respond(r, Enoread);
			return;
		}
		if(!p9conn->srv->read) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->read(r);
		break;
	case TRemove:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if(!p9conn->srv->remove) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->remove(r);
		break;
	case TStat:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if(!p9conn->srv->stat) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->stat(r);
		break;
	case TWalk:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if(r->fid->omode != -1) {
			ixp_respond(r, "cannot walk from an open fid");
			return;
		}
		if(r->ifcall.twalk.nwname && !(r->fid->qid.type&QTDIR)) {
			ixp_respond(r, Enotdir);
			return;
		}
		if((r->ifcall.hdr.fid != r->ifcall.twalk.newfid)) {
			if(!(r->newfid = createfid(&p9conn->fidmap, r->ifcall.twalk.newfid, p9conn))) {
				ixp_respond(r, Edupfid);
				return;
			}
		}else
			r->newfid = r->fid;
		if(!p9conn->srv->walk) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->walk(r);
		break;
	case TWrite:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if((r->fid->omode&3) != P9_OWRITE && (r->fid->omode&3) != P9_ORDWR) {
			ixp_respond(r, "write on fid not opened for writing");
			return;
		}
		if(!p9conn->srv->write) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->write(r);
		break;
	case TWStat:
		if(!(r->fid = ixp_mapget(&p9conn->fidmap, r->ifcall.hdr.fid))) {
			ixp_respond(r, Enofid);
			return;
		}
		if(~r->ifcall.twstat.stat.type) {
			ixp_respond(r, "wstat of type");
			return;
		}
		if(~r->ifcall.twstat.stat.dev) {
			ixp_respond(r, "wstat of dev");
			return;
		}
		if(~r->ifcall.twstat.stat.qid.type || (ulong)~r->ifcall.twstat.stat.qid.version || ~r->ifcall.twstat.stat.qid.path) {
			ixp_respond(r, "wstat of qid");
			return;
		}
		if(r->ifcall.twstat.stat.muid && r->ifcall.twstat.stat.muid[0]) {
			ixp_respond(r, "wstat of muid");
			return;
		}
		if(~r->ifcall.twstat.stat.mode && ((r->ifcall.twstat.stat.mode&DMDIR)>>24) != (r->fid->qid.type&QTDIR)) {
			ixp_respond(r, "wstat on DMDIR bit");
			return;
		}
		if(!p9conn->srv->wstat) {
			ixp_respond(r, Enofunc);
			return;
		}
		p9conn->srv->wstat(r);
		break;
	/* Still to be implemented: auth */
	}
}

/**
 * Function: ixp_respond
 *
 * Sends a response to the given request. The response is
 * constructed from the P<ofcall> member of the P<req> parameter, or
 * from the P<error> parameter if it is non-null. In the latter
 * case, the response is of type RError, while in any other case it
 * is of the same type as P<req>->P<ofcall>, which must match the
 * request type in P<req>->P<ifcall>.
 *
 * See also:
 *	T<Ixp9Req>, V<ixp_printfcall>
 */
void
ixp_respond(Ixp9Req *req, const char *error) {
	Ixp9Conn *p9conn;
	int msize;

	p9conn = req->conn;

	switch(req->ifcall.hdr.type) {
	default:
		if(!error)
			assert(!"Respond called on unsupported fcall type");
		break;
	case TVersion:
		assert(error == nil);
		free(req->ifcall.version.version);

		thread->lock(&p9conn->rlock);
		thread->lock(&p9conn->wlock);
		msize = min(req->ofcall.version.msize, IXP_MAX_MSG);
		p9conn->rmsg.data = erealloc(p9conn->rmsg.data, msize);
		p9conn->wmsg.data = erealloc(p9conn->wmsg.data, msize);
		p9conn->rmsg.size = msize;
		p9conn->wmsg.size = msize;
		thread->unlock(&p9conn->wlock);
		thread->unlock(&p9conn->rlock);
		req->ofcall.version.msize = msize;
		break;
	case TAttach:
		if(error)
			destroyfid(p9conn, req->fid->fid);
		free(req->ifcall.tattach.uname);
		free(req->ifcall.tattach.aname);
		break;
	case TOpen:
	case TCreate:
		if(!error) {
			req->ofcall.ropen.iounit = p9conn->rmsg.size - 24;
			req->fid->iounit = req->ofcall.ropen.iounit;
			req->fid->omode = req->ifcall.topen.mode;
			req->fid->qid = req->ofcall.ropen.qid;
		}
		free(req->ifcall.tcreate.name);
		break;
	case TWalk:
		if(error || req->ofcall.rwalk.nwqid < req->ifcall.twalk.nwname) {
			if(req->ifcall.hdr.fid != req->ifcall.twalk.newfid && req->newfid)
				destroyfid(p9conn, req->newfid->fid);
			if(!error && req->ofcall.rwalk.nwqid == 0)
				error = Enofile;
		}else{
			if(req->ofcall.rwalk.nwqid == 0)
				req->newfid->qid = req->fid->qid;
			else
				req->newfid->qid = req->ofcall.rwalk.wqid[req->ofcall.rwalk.nwqid-1];
		}
		free(*req->ifcall.twalk.wname);
		break;
	case TWrite:
		free(req->ifcall.twrite.data);
		break;
	case TRemove:
		if(req->fid)
			destroyfid(p9conn, req->fid->fid);
		break;
	case TClunk:
		if(req->fid)
			destroyfid(p9conn, req->fid->fid);
		break;
	case TFlush:
		if((req->oldreq = ixp_mapget(&p9conn->tagmap, req->ifcall.tflush.oldtag)))
			ixp_respond(req->oldreq, Eintr);
		break;
	case TWStat:
		ixp_freestat(&req->ifcall.twstat.stat);
		break;
	case TRead:
	case TStat:
		break;		
	/* Still to be implemented: auth */
	}

	req->ofcall.hdr.tag = req->ifcall.hdr.tag;

	if(error == nil)
		req->ofcall.hdr.type = req->ifcall.hdr.type + 1;
	else {
		req->ofcall.hdr.type = RError;
		req->ofcall.error.ename = (char*)error;
	}

	if(ixp_printfcall)
		ixp_printfcall(&req->ofcall);

	ixp_maprm(&p9conn->tagmap, req->ifcall.hdr.tag);;

	if(p9conn->conn) {
		thread->lock(&p9conn->wlock);
		msize = ixp_fcall2msg(&p9conn->wmsg, &req->ofcall);
		if(ixp_sendmsg(p9conn->conn->fd, &p9conn->wmsg) != msize)
			ixp_hangup(p9conn->conn);
		thread->unlock(&p9conn->wlock);
	}

	switch(req->ofcall.hdr.type) {
	case RStat:
		free(req->ofcall.rstat.stat);
		break;
	case RRead:
		free(req->ofcall.rread.data);
		break;
	}
	free(req);
	decref_p9conn(p9conn);
}

/* Flush a pending request */
static void
voidrequest(void *context, void *arg) {
	Ixp9Req *orig_req, *flush_req;
	Ixp9Conn *conn;

	orig_req = arg;
	conn = orig_req->conn;
	conn->ref++;

	flush_req = emallocz(sizeof *orig_req);
	flush_req->ifcall.hdr.type = TFlush;
	flush_req->ifcall.hdr.tag = IXP_NOTAG;
	flush_req->ifcall.tflush.oldtag = orig_req->ifcall.hdr.tag;
	flush_req->conn = conn;

	flush_req->aux = *(void**)context;
	*(void**)context = flush_req;
}

/* Clunk an open IxpFid */
static void
voidfid(void *context, void *arg) {
	Ixp9Conn *p9conn;
	Ixp9Req *clunk_req;
	IxpFid *fid;

	fid = arg;
	p9conn = fid->conn;
	p9conn->ref++;

	clunk_req = emallocz(sizeof *clunk_req);
	clunk_req->ifcall.hdr.type = TClunk;
	clunk_req->ifcall.hdr.tag = IXP_NOTAG;
	clunk_req->ifcall.hdr.fid = fid->fid;
	clunk_req->fid = fid;
	clunk_req->conn = p9conn;

	clunk_req->aux = *(void**)context;
	*(void**)context = clunk_req;
}

static void
cleanupconn(IxpConn *c) {
	Ixp9Conn *p9conn;
	Ixp9Req *req, *r;

	p9conn = c->aux;
	p9conn->conn = nil;
	req = nil;
	if(p9conn->ref > 1) {
		ixp_mapexec(&p9conn->fidmap, voidfid, &req);
		ixp_mapexec(&p9conn->tagmap, voidrequest, &req);
	}
	while((r = req)) {
		req = r->aux;
		r->aux = nil;
		handlereq(r);
	}
	decref_p9conn(p9conn);
}

/* Handle incoming 9P connections */
/**
 * Type: Ixp9Srv
 * Type: Ixp9Req
 * Function: ixp_serve9conn
 *
 * The ixp_serve9conn handles incoming 9P connections. It is
 * ordinarily passed as the P<read> member to F<ixp_listen> with an
 * Ixp9Srv structure passed as the P<aux> member. The handlers
 * defined in the Ixp9Srv structure are called whenever a matching
 * Fcall type is received. The handlers are expected to call
 * F<ixp_respond> at some point, whether before they return or at
 * some undefined point in the future. Whenever a client
 * disconnects, libixp generates whatever flush and clunk events are
 * required to leave the connection in a clean state and waits for
 * all responses before freeing the connections associated data
 * structures.
 *
 * Whenever a file is closed and an T<IxpFid> is about to be freed,
 * the P<freefid> member is called to perform any necessary cleanup
 * and to free any associated resources.
 *
 * See also:
 *	F<ixp_listen>, F<ixp_respond>, F<ixp_printfcall>,
 *	F<IxpFcall>, F<IxpFid>
 */
void
ixp_serve9conn(IxpConn *c) {
	Ixp9Conn *p9conn;
	int fd;

	fd = accept(c->fd, nil, nil);
	if(fd < 0)
		return;

	p9conn = emallocz(sizeof *p9conn);
	p9conn->ref++;
	p9conn->srv = c->aux;
	p9conn->rmsg.size = 1024;
	p9conn->wmsg.size = 1024;
	p9conn->rmsg.data = emalloc(p9conn->rmsg.size);
	p9conn->wmsg.data = emalloc(p9conn->wmsg.size);

	ixp_mapinit(&p9conn->tagmap, p9conn->taghash, nelem(p9conn->taghash));
	ixp_mapinit(&p9conn->fidmap, p9conn->fidhash, nelem(p9conn->fidhash));
	thread->initmutex(&p9conn->rlock);
	thread->initmutex(&p9conn->wlock);

	ixp_listen(c->srv, fd, p9conn, handlefcall, cleanupconn);
}
