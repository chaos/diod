/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#ifndef LIBNPFS_NPFSIMPL_H
#define LIBNPFS_NPFSIMPL_H

#include "npfs.h"

/* fcall.c */
Npfcall *np_version(Npreq *req, Npfcall *tc);
Npfcall *np_auth(Npreq *req, Npfcall *tc);
Npfcall *np_attach(Npreq *req, Npfcall *tc);
int np_flush(Npreq *req, Npfcall *tc);
Npfcall *np_walk(Npreq *req, Npfcall *tc);
Npfcall *np_open(Npreq *req, Npfcall *tc);
Npfcall *np_create(Npreq *req, Npfcall *tc);
Npfcall *np_read(Npreq *req, Npfcall *tc);
Npfcall *np_write(Npreq *req, Npfcall *tc);
Npfcall *np_clunk(Npreq *req, Npfcall *tc);
Npfcall *np_remove(Npreq *req, Npfcall *tc);
Npfcall *np_statfs(Npreq *req, Npfcall *tc);
Npfcall *np_lopen(Npreq *req, Npfcall *tc);
Npfcall *np_lcreate(Npreq *req, Npfcall *tc);
Npfcall *np_symlink(Npreq *req, Npfcall *tc);
Npfcall *np_mknod(Npreq *req, Npfcall *tc);
Npfcall *np_rename(Npreq *req, Npfcall *tc);
Npfcall *np_readlink(Npreq *req, Npfcall *tc);
Npfcall *np_getattr(Npreq *req, Npfcall *tc);
Npfcall *np_setattr(Npreq *req, Npfcall *tc);
Npfcall *np_xattrwalk(Npreq *req, Npfcall *tc);
Npfcall *np_xattrcreate(Npreq *req, Npfcall *tc);
Npfcall *np_readdir(Npreq *req, Npfcall *tc);
Npfcall *np_fsync(Npreq *req, Npfcall *tc);
Npfcall *np_lock(Npreq *req, Npfcall *tc);
Npfcall *np_getlock(Npreq *req, Npfcall *tc);
Npfcall *np_link(Npreq *req, Npfcall *tc);
Npfcall *np_mkdir(Npreq *req, Npfcall *tc);
Npfcall *np_renameat(Npreq *req, Npfcall *tc);
Npfcall *np_unlinkat(Npreq *req, Npfcall *tc);

/* srv.c */
void np_srv_add_req(Npsrv *srv, Npreq *req);
void np_srv_remove_req(Nptpool *tp, Npreq *req);
Npreq *np_req_alloc(Npconn *conn, Npfcall *tc);
Npreq *np_req_ref(Npreq*);
void np_req_unref(Npreq*);

#endif
