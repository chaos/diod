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

extern Npsrv *srv;
extern int debuglevel;
extern int sameuser;
extern int use_aio;
extern pthread_t aio_thread;

Npfcall* npfs_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname);
int npfs_clone(Npfid *fid, Npfid *newfid);
int npfs_walk(Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall* npfs_open(Npfid *fid, u8 mode);
Npfcall* npfs_create(Npfid *fid, Npstr *name, u32 perm, u8 mode, 
	Npstr *extension);
Npfcall* npfs_read(Npfid *fid, u64 offset, u32 count, Npreq *);
Npfcall* npfs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *);
Npfcall* npfs_clunk(Npfid *fid);
Npfcall* npfs_remove(Npfid *fid);
Npfcall* npfs_stat(Npfid *fid);
Npfcall* npfs_wstat(Npfid *fid, Npstat *stat);
void npfs_flush(Npreq *req);
void npfs_fiddestroy(Npfid *fid);
int npfs_aio_init(int n);
void *npfs_aio_proc(void *a);
