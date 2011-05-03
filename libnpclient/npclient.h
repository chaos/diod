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
};

typedef int (*AuthFun)(Npcfid *afid, u32 uid);

/* Given a server already connected on fd, send a VERSION request
 * to negotiate 9P2000.L and an msize <= the one provided.
 * Return fsys structure or NULL on error (retrieve with np_rerror ())
 */
Npcfsys* npc_start(int fd, int msize);

/* Close fd and deallocate file system structure.
 */
void npc_finish (Npcfsys *fs);

/* Obtain an afid from the server using an AUTH request.
 * Call the 'auth' function on the afid to establish it as a credential.
 * Return afid or NULL on error (retrieve with np_rerror ()).
 * Also returns NULL if auth is not required (and np_rerror () == 0).
 */
Npcfid* npc_auth (Npcfsys *fs, char *uname, char *aname, u32 uid,
                  AuthFun auth);

/* Obtain a fid from the server for the specified aname with an ATTACH request.
 * Optionally present 'afid' as an authentication credential.  Either uname
 * or uid must be presented.  Set either uname to NULL or uid to P9_NONUNAME.
 * Returns fid or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_attach(Npcfsys *fs, Npcfid *afid, char *uname, char *aname,
                   uid_t uid);

/* Tell the server to forget about 'fid'.
 * Return 0 on success, -1 on error (retrieve with np_rerror ()).
 */
int npc_clunk(Npcfid *fid);

/* Shorthand for start/auth/attach using the caller's effective uid.
 * Returns a fid for the attach or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_mount (int fd, int msize, char *aname, AuthFun auth);

/* Shorthand for clunk/finish.  Always succeeds.
 */
void npc_umount (Npcfid *fid);

/* Prepare 'fid' for I/O.  'mode' uses the same bits as open (2).
 * Returns 0 on success, -1 on error (retrieve with np_rerror ()).
 */
int npc_open (Npcfid *fid, u32 mode);

/* Create a file in directory 'fid' with specified 'name', 'perm', and 'mode'.
 * Afterward, 'fid' will represent the new file, which can be used for I/O.
 * Returns 0 on success, -1 on error (retrieve with np_rerror ()).
 */ 
int npc_create(Npcfid *fid, char *name, u32 perm, u32 mode);

/* Change the file offset associated with 'fid'.  This is like lseek (2).
 * The state is local, kept within the fid.  N.B. SEEK_END doesn't work yet.
 */
u64 npc_lseek(Npcfid *fid, u64 offset, int whence);

/* Shorthand for walk/open.
 * Returns fid for file, or -1 on error (retrieve with np_rerror ()).
 */
Npcfid* npc_open_bypath (Npcfid *root, char *path, u32 mode);

/* Shorthand for walk/create.
 * Returns fid for new file, or -1 on error (retrieve with np_rerror ()).
 */
Npcfid *npc_create_bypath (Npcfid *root, char *path, u32 flags, u32 mode);

/* Read 'count' bytes to 'buf' at 'offset' using READ requests.
 * Less than 'count' may be read if it exceeds the maximum request size
 * or the next call will return EOF.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
int npc_pread(Npcfid *fid, void *buf, u32 count, u64 offset);

/* Like npc_pread (), except issue multiple READ requests until EOF or
 * buffer is exhausted.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
int npc_pread_all(Npcfid *fid, void *buf, u32 count, u64 offset);

/* Like read (2).  Just a npc_pread() using offset stored in fid.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
int npc_read(Npcfid *fid, void *buf, u32 count);

/* Like npc_read (), except issue multiple READ requests until EOF or
 * buffer is exhausted.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
int npc_read_all(Npcfid *fid, void *buf, u32 count);

/* npc_read_all() up to and including the next '\n' character, or until buffer
 * is exhausted, whichever comes first.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
char *npc_gets(Npcfid *fid, char *buf, u32 count);

/* Write 'count' bytes from 'buf' at 'offset' using WRITE request.
 * Less than 'count' may be written if it exceeds the maximum request size.
 * Returns bytes written or -1 on error (retrieve with np_rerror ()).
 */
int npc_pwrite(Npcfid *fid, void *buf, u32 count, u64 offset);

/* Like npc_pwrite (), except issue multiple WRITE requests until
 * buffer is exhausted.
 * Returns bytes written or -1 on error (retrieve with np_rerror ()).
 */
int npc_pwrite_all(Npcfid *fid, void *buf, u32 count, u64 offset);

/* Like write (2).  Just a npc_pwrite () using offset stored in fid.
 * Returns bytes written or -1 on error (retrieve with np_rerror ()).
 */
int npc_write(Npcfid *fid, void *buf, u32 count);

/* Like npc_write () except issue multiple WRITE requests until buffer
 * is exhausted.
 */
int npc_write_all(Npcfid *fid, void *buf, u32 count);

/* Like np_write_all (fid, buf, strlen (buf))
 */
int npc_puts(Npcfid *fid, char *buf);

/* Descend a directory represnted by 'fid' by walking successive path
 * elements in 'path'.  Multiple WALK requests will be sent depending on
 * the number of path elements.  Returns a new fid representing path,
 * or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_walk(Npcfid *fid, char *path);

/* Send a MKDIR request to create 'name' in parent directory 'fid',
 * with 'mode' bits as in open (2).
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_mkdir (Npcfid *fid, char *name, u32 mode);

/* Like mkdir (2).  Shorthand for walk/mkdir/clunk.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_mkdir_bypath (Npcfid *root, char *path, u32 mode);

struct stat;

/* Send a GETATTR request to get stat(2) information on 'fid'.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_getattr (Npcfid *fid, struct stat *sb);

/* Like stat (2).  Shorthand for walk/getattr/clunk.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_getattr_bypath (Npcfid *root, char *path, struct stat *sb);

