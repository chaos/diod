/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010-2014 by Lawrence Livermore National Security, LLC.
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

/* rdmatrans.c - I believe originally by Tom Tucker, who added v9fs rdma.
 * See lkml discussion with Tom Tucker and Roland Dreier, Oct 2008.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

typedef struct Rdmatrans Rdmatrans;
typedef struct Rdmactx Rdmactx;

struct Rdmatrans {
	struct Nptrans*			trans;
	pthread_mutex_t			lock;
	pthread_cond_t			cond;
	int				connected;
	size_t				msize;
	size_t				q_depth;
	struct rdma_cm_id*		cm_id;
	struct ibv_context*		context;
	int				fd;
	u8*				rcv_buf;
	u8*				snd_buf;
	int				next_buf;
	struct ibv_pd*			pd;
	struct ibv_mr*			rcv_mr;
	struct ibv_mr*			snd_mr;
	struct ibv_cq*			cq;
	struct ibv_comp_channel*	ch;
	struct ibv_qp*			qp;
	struct rdma_event_channel*	event_ch;

	Rdmactx*			rfirst;
	Rdmactx*			rlast;
};

struct Rdmactx {
	int				used;
	enum ibv_wc_opcode		wc_op;
	struct Rdmatrans*		rdma;
	u32				pos;
	u32				len;
	Rdmactx*			next;
	unsigned char			buf[0];
};

static void rdma_trans_destroy(void *a);
static int rdma_trans_recv(Npfcall **fcp, u32 msize, void *a);
static int rdma_trans_send(Npfcall *fc, void *a);
static void rdma_post_recv(struct Rdmatrans *rdma, Rdmactx *rctx);



/**
 * \brief Create an RDMA transport server
 *
 * \param cmid The CM id passed up in the connect event
 * \param q_depth A hint from the client on the depth of it's SQ/RQ
 * \param msize The max message size
 * \returns A pointer to the newly allocated transport
 */
Nptrans *
np_rdmatrans_create(struct rdma_cm_id *cmid, int q_depth, int msize)
{
	int i, ret;
	u8 *p;
	struct Nptrans *trans;
	struct Rdmatrans *rdma;
	struct ibv_qp_init_attr qp_attr;
	struct rdma_conn_param cparam;

	rdma = calloc(1, sizeof *rdma);
	if (!rdma)
		goto error;

	ret = pthread_mutex_init(&rdma->lock, NULL);
	if (ret)
		goto error;

	ret = pthread_cond_init(&rdma->cond, NULL);
	if (ret)
		goto error;

	rdma->connected = 0;
	rdma->cm_id = cmid;
	rdma->context = cmid->verbs;
	rdma->q_depth = q_depth;
	rdma->msize = msize + sizeof(Rdmactx);

	rdma->pd = ibv_alloc_pd(rdma->context);
	if (!rdma->pd)
		goto error;

	/* Create receive buffer space and register it */
	rdma->rcv_buf = malloc(rdma->msize * q_depth);
	if (!rdma->rcv_buf)
		goto error;

	rdma->rcv_mr = ibv_reg_mr(rdma->pd, rdma->rcv_buf, rdma->msize * q_depth,
				  IBV_ACCESS_LOCAL_WRITE);
	if (!rdma->rcv_mr)
		goto error;

	/* Create send buffer space and register it */
	rdma->snd_buf = malloc(rdma->msize * q_depth);
	if (!rdma->snd_buf)
		goto error;

	rdma->next_buf = 0;
	rdma->snd_mr = ibv_reg_mr(rdma->pd, rdma->snd_buf, rdma->msize * q_depth, 0);
	if (!rdma->snd_mr)
		goto error;

	rdma->ch = ibv_create_comp_channel(rdma->context);
	if (!rdma->ch)
		goto error;

	rdma->fd = rdma->ch->fd;
	rdma->cq = ibv_create_cq(rdma->context, 2*q_depth, rdma, rdma->ch, 0);
	if (!rdma->cq)
		goto error;

	ibv_req_notify_cq(rdma->cq, 0);

	/* Create the CQ */
	memset(&qp_attr, 0, sizeof qp_attr);
	qp_attr.send_cq = rdma->cq;
	qp_attr.recv_cq = rdma->cq;
	qp_attr.cap.max_send_wr = q_depth;
	qp_attr.cap.max_recv_wr = q_depth;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_sge = 1;
	qp_attr.cap.max_inline_data = 64;
	qp_attr.qp_type = IBV_QPT_RC;
	ret = rdma_create_qp(rdma->cm_id, rdma->pd, &qp_attr);
	if (ret)
		goto error;
	rdma->qp = rdma->cm_id->qp;

	p = rdma->rcv_buf;
	for (i = 0; i < q_depth; i++)
		rdma_post_recv(rdma, (Rdmactx *)(p + i*rdma->msize));

	trans = np_trans_create(rdma, rdma_trans_recv,
				      rdma_trans_send,
				      rdma_trans_destroy);
	if (!trans)
		goto error;
	rdma->trans = trans;

	memset(&cparam, 0, sizeof(cparam));
	cparam.responder_resources = 1;
	cparam.initiator_depth = 1;
	cparam.private_data = NULL;
	cparam.private_data_len = 0;
	ret = rdma_accept(cmid, &cparam);
	if (ret) {
		np_uerror(ret);
		goto error;
	}

	rdma->connected = 1;
	return trans;

 error:
	if (rdma)
		rdma_trans_destroy(rdma);

	rdma_reject(cmid, NULL, 0);
	return NULL;
}
 
static void
rdma_trans_destroy(void *a)
{
	Rdmatrans *rdma;
	struct ibv_qp_attr attr;

	rdma = a;
	if (rdma->connected)
		rdma_disconnect(rdma->cm_id);
	if (rdma->qp) {
		attr.qp_state = IBV_QPS_ERR;
		ibv_modify_qp(rdma->qp, &attr, IBV_QP_STATE);
		ibv_destroy_qp(rdma->qp);
	}
	if (rdma->cq)
		ibv_destroy_cq(rdma->cq);
	if (rdma->ch)
		ibv_destroy_comp_channel(rdma->ch);
	if (rdma->snd_mr)
		ibv_dereg_mr(rdma->snd_mr);
	if (rdma->snd_buf)
		free(rdma->snd_buf);
	if (rdma->rcv_mr)
		ibv_dereg_mr(rdma->rcv_mr);
	if (rdma->rcv_buf)
		free(rdma->rcv_buf);
	if (rdma->pd)
		ibv_dealloc_pd(rdma->pd);
	if (rdma->cm_id)
		rdma_destroy_id(rdma->cm_id);
}

static int
rdma_trans_recv(Npfcall **fcp, u32 msize, void *a)
{
	int n, ret, closing = 0;
	struct ibv_cq *cq;
	struct ibv_wc wc;
	void *context;
	Rdmatrans *rdma = (Rdmatrans *)a;
	Rdmactx *ctx;
	Npfcall *fc = NULL;

	if (!(fc = np_alloc_fcall (msize))) {
		np_uerror(ENOMEM);
		return -1;
	}
	pthread_mutex_lock(&rdma->lock);
again:
	if (rdma->rfirst) {
		ctx = rdma->rfirst;

		n = ctx->len - ctx->pos;
		if (n > msize)
			n = msize;

		memmove(fc->pkt, ctx->buf + ctx->pos, n);
		ctx->pos += n;
		if (ctx->pos == ctx->len) {
			rdma->rfirst = ctx->next;
			if (ctx == rdma->rlast)
				rdma->rlast = NULL;

			rdma_post_recv(rdma, ctx);
		}

		pthread_mutex_unlock(&rdma->lock);
		fc->size = n;
		*fcp = fc;
		return 0;
	}

	pthread_mutex_unlock(&rdma->lock);

poll:
	ret = ibv_get_cq_event(rdma->ch, &cq, &context);
	if (ret) {
		np_uerror(ret);
		//fprintf(stderr, "Error %d polling cq\n", ret);
		return -1;
	}
	ibv_ack_cq_events(rdma->cq, 1);

	ibv_req_notify_cq(cq, 0);
	while ((ret = ibv_poll_cq(rdma->cq, 1, &wc)) > 0) {
		/* Check if it's a flush */
		if (wc.status != IBV_WC_SUCCESS) {
			//fprintf(stderr, "cq fail: status %d opcode %d\n",
			//	wc.status, wc.opcode);
			closing = 1;
			continue;
		}

		if (wc.opcode == IBV_WC_RECV) {
			ctx = (Rdmactx *) wc.wr_id;
			pthread_mutex_lock(&rdma->lock);
			ctx->used = 0;
			ctx->len = wc.byte_len;
			ctx->pos = 0;
			if (rdma->rlast)
				rdma->rlast->next = ctx;
			else
				rdma->rfirst = ctx;

			rdma->rlast = ctx;
			ctx->next = NULL;
			goto again;
		} else if (wc.opcode == IBV_WC_SEND) {
			ctx = (Rdmactx *) wc.wr_id;
			pthread_mutex_lock(&rdma->lock);
			ctx->used = 0;
			pthread_cond_signal(&rdma->cond);
			pthread_mutex_unlock(&rdma->lock);
		}
	}

	if (!ret && !closing)
		goto poll;

	np_uerror(ret);
	return -1;
}

static int
rdma_trans_send(Npfcall *fc, void *a)
{
	int i, n;
	Rdmatrans *rdma;
	struct ibv_sge sge;
	struct ibv_send_wr wr, *bad_wr;
	Rdmactx *wctx;

	rdma = a;
	pthread_mutex_lock(&rdma->lock);

again:
	for(i = 0, wctx = (Rdmactx *) rdma->snd_buf; i < rdma->q_depth;
			i++, wctx = (Rdmactx *) ((char *) wctx + rdma->msize))
		if (!wctx->used)
			break;

	if (i >= rdma->q_depth) {
		/* wait for a slot */
		pthread_cond_wait(&rdma->cond, &rdma->lock);
		goto again;
	}

	wctx->wc_op = IBV_WC_SEND;
	wctx->rdma = rdma;
	wctx->used = 1;
	wctx->len = fc->size;
	wctx->pos = 0;
	memmove(wctx->buf, fc->pkt, fc->size);
	pthread_mutex_unlock(&rdma->lock);

	sge.addr = (uintptr_t) wctx->buf;
	sge.length = fc->size;
	sge.lkey = rdma->snd_mr->lkey;
	wr.next = NULL;
	wr.wr_id = (u64)(unsigned long)wctx;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	n = ibv_post_send(rdma->qp, &wr, &bad_wr);
	if (n) {
		np_uerror(n);
		return -1;
	}

	return fc->size;
}

/**
 * \brief Post a new receive buffer to the RQ
 *
 * \param rdma Pointer to the rdma transport structure
 * \c RDMA buffer context
 */
static void
rdma_post_recv(struct Rdmatrans *rdma, Rdmactx *rctx)
{
	int n;
	struct ibv_sge sge;
	struct ibv_recv_wr wr, *bad_wr;

	rctx->wc_op = IBV_WC_RECV;
	rctx->rdma = rdma;
	rctx->next = NULL;
	rctx->used = 1;
	sge.addr = (uintptr_t)rctx->buf;
	sge.length = rdma->msize - sizeof(Rdmactx);
	sge.lkey = rdma->rcv_mr->lkey;
	wr.next = NULL;
	wr.wr_id = (u64)(unsigned long)rctx;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	n = ibv_post_recv(rdma->qp, &wr, &bad_wr);
	if (n) {
		np_uerror(n);
		//fprintf(stderr, "Error %d posting recv\n", n);
	}
}
