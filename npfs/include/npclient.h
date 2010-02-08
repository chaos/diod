/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

typedef struct Npcfid Npcfid;
typedef struct Npcfsys Npcfsys;

struct Npcfsys;

struct Npcfid {
	u32		iounit;
	Npcfsys*	fsys;
	u32		fid;
	u64		offset;
	Npqid		qid;
};

Npcfsys* npc_mount(int fd, char *aname, int dotu, Npuser *user, 
	int (*auth)(Npcfid *afid, Npuser *user, void *aux), void *aux);
void npc_umount(Npcfsys *fs);
struct addrinfo *npc_netaddr(char *address, int dfltport);
Npcfsys * npc_netmount(struct addrinfo *address, int dotu, Npuser *user, int dfltport, 
	int (*auth)(Npcfid *afid, Npuser *user, void *aux), void *aux);
Npcfid* npc_create(Npcfsys *fs, char *path, u32 perm, int mode);
Npcfid* npc_open(Npcfsys *fs, char *path, int mode);
int npc_close(Npcfid *fid);
int npc_remove(Npcfsys *fs, char *path);
int npc_read(Npcfid *fid, u8 *buf, u32 count, u64 offset);
int npc_readn(Npcfid *fid, u8 *buf, u32 count, u64 offset);
int npc_write(Npcfid *fid, u8 *buf, u32 count, u64 offset);
int npc_writen(Npcfid *fid, u8 *buf, u32 count, u64 offset);
int npc_dirread(Npcfid *fid, Npwstat **stat);
Npwstat *npc_stat(Npcfsys *fs, char *path);
void npc_emptystat(Npwstat *st);
int npc_wstat(Npcfsys *fs, char *path, Npwstat *st);

int npc_readnb(Npcfid *fid, u8 *buf, u32 count, u64 offset,
	void (*cb)(void *cba, u32 count), void *cba);
int npc_writenb(Npcfid *fid, u8 *buf, u32 count, u64 offset, 
	void (*cb)(void *cba, u32 count), void *cba);
