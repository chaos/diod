/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010-14 Lawrence Livermore National Security, LLC.
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

