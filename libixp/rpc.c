/* From Plan 9's libmux.
 * Copyright (c) 2003 Russ Cox, Massachusetts Institute of Technology
 * Distributed under the same terms as libixp.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ixp_local.h"

static int gettag(IxpClient*, IxpRpc*);
static void puttag(IxpClient*, IxpRpc*);
static void enqueue(IxpClient*, IxpRpc*);
static void dequeue(IxpClient*, IxpRpc*);

void
muxinit(IxpClient *mux)
{
	mux->tagrend.mutex = &mux->lk;
	mux->sleep.next = &mux->sleep;
	mux->sleep.prev = &mux->sleep;
	thread->initmutex(&mux->lk);
	thread->initmutex(&mux->rlock);
	thread->initmutex(&mux->wlock);
	thread->initrendez(&mux->tagrend);
}

void
muxfree(IxpClient *mux)
{
	thread->mdestroy(&mux->lk);
	thread->mdestroy(&mux->rlock);
	thread->mdestroy(&mux->wlock);
	thread->rdestroy(&mux->tagrend);
	free(mux->wait);
}

static void
initrpc(IxpClient *mux, IxpRpc *r)
{
	r->mux = mux;
	r->waiting = 1;
	r->r.mutex = &mux->lk;
	r->p = nil;
	thread->initrendez(&r->r);
}

static void
freemuxrpc(IxpRpc *r)
{
	thread->rdestroy(&r->r);
}

static int
sendrpc(IxpRpc *r, IxpFcall *f)
{
	int ret;
	IxpClient *mux;
	
	ret = 0;
	mux = r->mux;
	/* assign the tag, add selves to response queue */
	thread->lock(&mux->lk);
	r->tag = gettag(mux, r);
	f->hdr.tag = r->tag;
	enqueue(mux, r);
	thread->unlock(&mux->lk);

	thread->lock(&mux->wlock);
	if(!ixp_fcall2msg(&mux->wmsg, f) || !ixp_sendmsg(mux->fd, &mux->wmsg)) {
		/* werrstr("settag/send tag %d: %r", tag); fprint(2, "%r\n"); */
		thread->lock(&mux->lk);
		dequeue(mux, r);
		puttag(mux, r);
		thread->unlock(&mux->lk);
		ret = -1;
	}
	thread->unlock(&mux->wlock);
	return ret;
}

static IxpFcall*
muxrecv(IxpClient *mux)
{
	IxpFcall *f;

	f = nil;
	thread->lock(&mux->rlock);
	if(ixp_recvmsg(mux->fd, &mux->rmsg) == 0)
		goto fail;
	f = emallocz(sizeof *f);
	if(ixp_msg2fcall(&mux->rmsg, f) == 0) {
		free(f);
		f = nil;
	}
fail:
	thread->unlock(&mux->rlock);
	return f;
}

static void
dispatchandqlock(IxpClient *mux, IxpFcall *f)
{
	int tag;
	IxpRpc *r2;

	tag = f->hdr.tag - mux->mintag;
	thread->lock(&mux->lk);
	/* hand packet to correct sleeper */
	if(tag < 0 || tag >= mux->mwait) {
		fprintf(stderr, "libixp: recieved unfeasible tag: %d (min: %d, max: %d)\n", f->hdr.tag, mux->mintag, mux->mintag+mux->mwait);
		goto fail;
	}
	r2 = mux->wait[tag];
	if(r2 == nil || r2->prev == nil) {
		fprintf(stderr, "libixp: recieved message with bad tag\n");
		goto fail;
	}
	r2->p = f;
	dequeue(mux, r2);
	thread->wake(&r2->r);
	return;
fail:
	ixp_freefcall(f);
	free(f);
}

static void
electmuxer(IxpClient *mux)
{
	IxpRpc *rpc;

	/* if there is anyone else sleeping, wake them to mux */
	for(rpc=mux->sleep.next; rpc != &mux->sleep; rpc = rpc->next){
		if(!rpc->async){
			mux->muxer = rpc;
			thread->wake(&rpc->r);
			return;
		}
	}
	mux->muxer = nil;
}

IxpFcall*
muxrpc(IxpClient *mux, IxpFcall *tx)
{
	IxpRpc r;
	IxpFcall *p;

	initrpc(mux, &r);
	if(sendrpc(&r, tx) < 0)
		return nil;

	thread->lock(&mux->lk);
	/* wait for our packet */
	while(mux->muxer && mux->muxer != &r && !r.p)
		thread->sleep(&r.r);

	/* if not done, there's no muxer; start muxing */
	if(!r.p){
		assert(mux->muxer == nil || mux->muxer == &r);
		mux->muxer = &r;
		while(!r.p){
			thread->unlock(&mux->lk);
			p = muxrecv(mux);
			if(p == nil){
				/* eof -- just give up and pass the buck */
				thread->lock(&mux->lk);
				dequeue(mux, &r);
				break;
			}
			dispatchandqlock(mux, p);
		}
		electmuxer(mux);
	}
	p = r.p;
	puttag(mux, &r);
	thread->unlock(&mux->lk);
	if(p == nil)
		werrstr("unexpected eof");
	return p;
}

static void
enqueue(IxpClient *mux, IxpRpc *r)
{
	r->next = mux->sleep.next;
	r->prev = &mux->sleep;
	r->next->prev = r;
	r->prev->next = r;
}

static void
dequeue(IxpClient *mux, IxpRpc *r)
{
	r->next->prev = r->prev;
	r->prev->next = r->next;
	r->prev = nil;
	r->next = nil;
}

static int 
gettag(IxpClient *mux, IxpRpc *r)
{
	int i, mw;
	IxpRpc **w;

	for(;;){
		/* wait for a free tag */
		while(mux->nwait == mux->mwait){
			if(mux->mwait < mux->maxtag-mux->mintag){
				mw = mux->mwait;
				if(mw == 0)
					mw = 1;
				else
					mw <<= 1;
				w = realloc(mux->wait, mw * sizeof *w);
				if(w == nil)
					return -1;
				memset(w+mux->mwait, 0, (mw-mux->mwait) * sizeof *w);
				mux->wait = w;
				mux->freetag = mux->mwait;
				mux->mwait = mw;
				break;
			}
			thread->sleep(&mux->tagrend);
		}

		i=mux->freetag;
		if(mux->wait[i] == 0)
			goto Found;
		for(; i<mux->mwait; i++)
			if(mux->wait[i] == 0)
				goto Found;
		for(i=0; i<mux->freetag; i++)
			if(mux->wait[i] == 0)
				goto Found;
		/* should not fall out of while without free tag */
		abort();
	}

Found:
	mux->nwait++;
	mux->wait[i] = r;
	r->tag = i+mux->mintag;
	return r->tag;
}

static void
puttag(IxpClient *mux, IxpRpc *r)
{
	int i;

	i = r->tag - mux->mintag;
	assert(mux->wait[i] == r);
	mux->wait[i] = nil;
	mux->nwait--;
	mux->freetag = i;
	thread->wake(&mux->tagrend);
	freemuxrpc(r);
}

