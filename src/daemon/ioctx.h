/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

typedef struct path_struct *Path;
typedef struct ioctx_struct *IOCtx;

int     ppool_init (Npsrv *srv);
void    ppool_fini (Npsrv *srv);

Path    path_create (Npsrv *srv, Npstr *ns);
Path    path_append (Npsrv *srv, Path opath, Npstr *ns);
Path    path_incref (Path path);
void    path_decref (Npsrv *srv, Path path);
char    *path_s (Path path);

int     ioctx_open (Npfid *fid, u32 flags, u32 mode);
int     ioctx_close (Npfid *fid, int seterrno);
int     ioctx_pread (IOCtx ioctx, void *buf, size_t count, off_t offset);
int     ioctx_pwrite (IOCtx ioctx, const void *buf, size_t count, off_t offset);
struct dirent *ioctx_readdir(IOCtx ioctx, long *new_offset);
void    ioctx_rewinddir (IOCtx ioctx);
void    ioctx_seekdir (IOCtx ioctx, long offset);
int     ioctx_fsync (IOCtx ioctx, int datasync);
int     ioctx_flock (IOCtx ioctx, int operation);
int     ioctx_testlock (IOCtx ioctx, int operation);

int     ioctx_stat (IOCtx ioctx, struct stat *sb);
int     ioctx_chmod (IOCtx ioctx, u32 mode);
int     ioctx_chown (IOCtx ioctx, u32 uid, u32 gid);
int     ioctx_truncate (IOCtx ioctx, u64 size);
#if HAVE_UTIMENSAT
int     ioctx_utimensat (IOCtx ioctx, const struct timespec ts[2], int flags);
#else
int     ioctx_utimes (IOCtx ioctx, const utimbuf *times);
#endif


u32     ioctx_iounit (IOCtx ioctx);
Npqid   *ioctx_qid (IOCtx ioctx);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
