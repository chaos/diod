/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#ifndef LIBNPCLIENT_NPCIMPL_H
#define LIBNPCLIENT_NPCIMPL_H

typedef int (*RpcFun)(Npcfsys *fs, Npfcall *tc, Npfcall **rc);
typedef void (*RefFun)(Npcfsys *fs);
typedef void (*DiscFun)(Npcfsys *fs);

typedef struct Npcreq Npcreq;
typedef struct Npcpool Npcpool;

struct Npcreq {
        Npcfsys*        fsys;

        u16             tag;
        Npfcall*        tc;
        Npfcall*        rc;

        int             ecode;

        void            (*cb)(Npcreq *, void *);
        void*           cba;

        int             flushed;
        Npcreq*         next;
        Npcreq*         prev;
};

struct Npcpool {
	pthread_mutex_t	lock;
	pthread_cond_t	cond;
	u32		maxid;
	int		msize;
	u8*		map;
};

struct Npcfsys {
        pthread_mutex_t lock;

	int		flags;
	u32		msize;
	Nptrans*	trans;

	int		refcount;
	Npcpool*	tagpool;
	Npcpool*	fidpool;

	RpcFun		rpc;
	RefFun		incref;
	RefFun		decref;
	DiscFun		disconnect;

	/* mtfsys only */
        pthread_cond_t  cond;

        Npcreq*         unsent_first;
        Npcreq*         unsent_last;
        Npcreq*         pend_first;

        pthread_t       readproc;
        pthread_t       writeproc;

	int		rfd;
	int		wfd;
};

Npcfsys *npc_create_fsys(int rfd, int wfd, int msize, int flags);
Npcfsys *npc_create_mtfsys(int rfd, int wfd, int msize, int flags);

Npcpool *npc_create_pool(u32 maxid);
void npc_destroy_pool(Npcpool *p);
u32 npc_get_id(Npcpool *p);
void npc_put_id(Npcpool *p, u32 id);

Npcfid *npc_fid_alloc(Npcfsys *fs);
void npc_fid_free(Npcfid *fid);

#endif
