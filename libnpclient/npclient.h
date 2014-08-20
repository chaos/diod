/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

typedef struct Npcfid Npcfid;
typedef struct Npcfsys Npcfsys;

struct Npcfsys;
struct stat;

struct Npcfid {
	u32		iounit;
	Npcfsys*	fsys;
	u32		fid;
	u64		offset;
	char		*buf;		/* packed p9_dirents or gets buffer */
	int		buf_size;	/* size of 'buf' buffer */
	int		buf_len;	/* length of 'buf' filled (readdir_r) */
	int		buf_used;	/* amount of 'buf' used (readdir_r)*/
};

typedef int (*AuthFun)(Npcfid *afid, u32 uid);

enum {
	NPC_MULTI_RPC=1,	/* use 'mtfsys'c' multi-threaded rpc engine */
	NPC_SHORTREAD_EOF=2,	/* npc_aget, npc_get treat short read as eof */
};

struct utimbuf;
struct dirent;


/**
 ** Basic functions
 **/

/* Given a server already connected on rfd,wfd, send a VERSION request
 * to negotiate 9P2000.L and an msize <= the one provided.
 * Return fsys structure or NULL on error (retrieve with np_rerror ())
 */
Npcfsys* npc_start (int rfd, int wfd, int msize, int flags);

/* Close fd and deallocate file system structure.
 */
void npc_finish (Npcfsys *fs);

/* Obtain an afid from the server using an AUTH request, then
 * call the 'auth' function on the afid to establish it as a credential.
 * Return afid or NULL on error (retrieve with np_rerror ()).
 * Note: server indicates "auth not required" with NULL and np_rerror () == 0.
 */
Npcfid* npc_auth (Npcfsys *fs, char *aname, u32 uid, AuthFun auth);

/* Obtain a fid from the server for the specified aname with an ATTACH request.
 * Optionally present 'afid' as an authentication credential or NULL.
 * Returns fid or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_attach (Npcfsys *fs, Npcfid *afid, char *aname, uid_t uid);

/* Tell the server to forget about 'fid' with a CLUNK request.
 * Return 0 on success, -1 on error (retrieve with np_rerror ()).
 */
int npc_clunk (Npcfid *fid);

/* Send LOPEN request to prepare 'fid' for I/O.
 * 'flags' uses the same bits as open (2), e.g. O_RDONLY, O_WRONLY, O_RDWR.
 * Returns 0 on success, -1 on error (retrieve with np_rerror ()).
 */
int npc_open (Npcfid *fid, u32 flags);

/* Send LCREATE request to create a file in directory 'fid'
 * with specified 'name', 'flags', and 'mode'.
 * Afterward, 'fid' will represent the new file, which can be used for I/O.
 * Returns 0 on success, -1 on error (retrieve with np_rerror ()).
 */ 
int npc_create (Npcfid *fid, char *name, u32 flags, u32 mode, gid_t gid);

/* Read 'count' bytes to 'buf' at 'offset' using READ requests.
 * Less than 'count' may be read if it exceeds the maximum request size
 * or the next call will return EOF.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
int npc_pread (Npcfid *fid, void *buf, u32 count, u64 offset);

/* Write 'count' bytes from 'buf' at 'offset' using WRITE request.
 * Less than 'count' may be written if it exceeds the maximum request size.
 * Returns bytes written or -1 on error (retrieve with np_rerror ()).
 */
int npc_pwrite (Npcfid *fid, void *buf, u32 count, u64 offset);

/* Descend a directory represnted by 'fid' by walking successive path
 * elements in 'path'.  Multiple WALK requests will be sent depending on
 * the number of path elements.  If 'path' is NULL, call npc_clone().
 * Returns a new fid representing path, or NULL on error (retrieve with
 * np_rerror ()).
 */
Npcfid *npc_walk (Npcfid *fid, char *path);

/* Clone a fid.  Returns a new fid representing the same path as 'fid',
 * or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_clone(Npcfid *fid);

/* Send a MKDIR request to create 'name' in parent directory 'fid',
 * with 'mode' bits as in open (2).
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_mkdir (Npcfid *fid, char *name, u32 mode);

/* Send a GETATTR request to get information on 'fid'.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_getattr (Npcfid *fid, u64 request_mask, u64 *valid, struct p9_qid *qid,
	         u32 *mode, u32 *uid, u32 *gid, u64 *nlink, u64 *rdev,
		 u64 *size, u64 *blksize, u64 *blocks, u64 *atime_sec,
		 u64 *atime_nsec, u64 *mtime_sec, u64 *mtime_nsec,
		 u64 *ctime_sec, u64 *ctime_nsec, u64 *btime_sec,
		 u64 *btime_nsec, u64 *gen, u64 *data_version);

/* Send a SETATTR request to set information about 'fid'.
 */
int npc_setattr (Npcfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
                 u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec);

/* Send REMOVE request to unlink file/dir associated with 'fid', and clunk fid.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_remove (Npcfid *fid);

/* Send READDIR request to list contents of directory 'fid'.
 * 'data' will contain up to 'count' bytes of packed variable length
 * directory structures of the form: qid[13] offset[8] type[1] name[s]
 * The 'offset' returned in the last entry can be passed to a subsequent
 * npc_readdir call in the offset field.  The only legal values for
 * the offset argument are 0 or the last offset returned.
 */
int npc_readdir (Npcfid *fid, u64 offset, char *data, u32 count);

/* Xattr functions
 */
ssize_t npc_xattrwalk (Npcfid *fid, Npcfid *attrfid, char *name);
int npc_xattrcreate (Npcfid *fid, char *name, u64 attr_size, u32 flags);


/* TODO:
 * npc_statfs ()
 * npc_symlink ()
 * npc_rename ()
 * npc_readlink ()
 * npc_fsync ()
 * npc_lock ()
 * npc_getlock ()
 * npc_link ()
 */

/**
 ** Convenience wrappers.
 **/

/* Shorthand for start/auth/attach using the caller's effective uid.
 * Employs simple fsys implementation that only allows one outstanding RPC.
 * Returns a fid for the attach or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_mount (int rfd, int wfd, int msize, char *aname, AuthFun auth);

/* Shorthand for clunk/finish.  Always succeeds.
 */
void npc_umount (Npcfid *fid);

/* Shorthand for walk/open.
 * Returns fid for file, or NULL on error (retrieve with np_rerror ()).
 */
Npcfid* npc_open_bypath (Npcfid *root, char *path, u32 mode);

/* Shorthand for walk/create.
 * Returns fid for new file, or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_create_bypath (Npcfid *root, char *path, u32 flags, u32 mode,
			   gid_t gid);

/* Like read (2).  Just a npc_pread() using offset stored in fid.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
int npc_read(Npcfid *fid, void *buf, u32 count);

/* Shorthand for walk/open/read[count]/close, i.e. read the whole file.
 * Returns bytes read, or -1 on error (retrieve with np_rerror ()).
 */
int npc_get(Npcfid *root, char *path, void *buf, u32 count);

/* Same as npc_get but result is a null terminated string that the caller
 * must free.  Returns NULL on error (retrieve with np_rerror ()).
 */
char *npc_aget(Npcfid *root, char *path);

/* npc_read_all() up to and including the next '\n' character, or until buffer
 * is exhausted, whichever comes first.
 * Returns bytes read, 0 on EOF, or -1 on error (retrieve with np_rerror ()).
 */
char *npc_gets(Npcfid *fid, char *buf, u32 count);

/* Like write (2).  Just a npc_pwrite () using offset stored in fid.
 * Returns bytes written or -1 on error (retrieve with np_rerror ()).
 */
int npc_write(Npcfid *fid, void *buf, u32 count);

/* Shorthand for walk/open/write[count]/close, i.e. overwrite entire
 * existing file with buffer.
 * Returns bytes written or -1 on error (retrieve with np_rerror ()).
 */
int npc_put(Npcfid *root, char *path, void *buf, u32 count);

/* Like npc_write(fid,buf,strlen(buf)) except issue multiple writes as needed.
 * Returns bytes written or -1 on error (retrieve with np_rerror ()).
 */
int npc_puts(Npcfid *fid, char *buf);

/* Change the file offset associated with 'fid'.  This is like lseek (2).
 * The state is local, kept within the fid.  N.B. SEEK_END doesn't work yet.
 */
u64 npc_lseek(Npcfid *fid, u64 offset, int whence);

/* Like mkdir (2).  Shorthand for walk/mkdir/clunk.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_mkdir_bypath (Npcfid *root, char *path, u32 mode);

/* Like fstat (2).  Maps directly to a getattr, but we use struct stat.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_fstat (Npcfid *fid, struct stat *sb);

/* Like stat (2).  Shorthand for walk/getattr/clunk.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_stat (Npcfid *root, char *path, struct stat *sb);

/* Like unlink/rmdir (2).  Shorthand for walk/remove.
 * Returns 0 on success or -1 on error (retrieve with np_rerror ()).
 */
int npc_remove_bypath (Npcfid *root, char *path);

/* Shorthand for walk/open, and initializes internally for npc_readdir_r()
 * Returns fid for file, or NULL on error (retrieve with np_rerror ()).
 */
Npcfid *npc_opendir (Npcfid *root, char *path);

/* Read a directory entry from directory 'fid' that was opened with
 * npc_opendir().  Entry points to dirent storage allocated by the caller.
 * Like readdir_r, returns 0 on success (>0) errno on failure.
 * On EOF set result to NULL, otherwise set to entry.
 */
int npc_readdir_r (Npcfid *fid, struct dirent *entry, struct dirent **result);
void npc_seekdir (Npcfid *fid, long offset);
long npc_telldir (Npcfid *fid);

/* Wrappers for setattr.
 */
int npc_fchmod (Npcfid *fid, mode_t mode);
int npc_chmod (Npcfid *root, char *path, mode_t mode);
int npc_fchown (Npcfid *fid, uid_t owner, gid_t group);
int npc_chown (Npcfid *root, char *path, uid_t owner, gid_t group);
int npc_ftruncate (Npcfid *fid, off_t length);
int npc_truncate (Npcfid *root, char *path, off_t length);
int npc_futime (Npcfid *fid, const struct utimbuf *times);
int npc_utime(Npcfid *root, char *path, const struct utimbuf *times);

/* Wrappers for xattrwalk/xattrcreate.
 */
ssize_t npc_listxattr (Npcfid *root, char *path, char *buf, size_t size);
ssize_t npc_getxattr (Npcfid *root, char *path, char *attr,
		      char *buf, size_t size);
int npc_setxattr (Npcfid *root, char *path, char *name, char *val, size_t size,
              int flags);
