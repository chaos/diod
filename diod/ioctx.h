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
int     ioctx_readdir_r(IOCtx ioctx, struct dirent *entry,
                        struct dirent **result);
void    ioctx_rewinddir (IOCtx ioctx);
void    ioctx_seekdir (IOCtx ioctx, long offset);
long    ioctx_telldir (IOCtx ioctx);
int     ioctx_fsync (IOCtx ioctx);
int     ioctx_flock (IOCtx ioctx, int operation);
int     ioctx_testlock (IOCtx ioctx, int operation);

u32     ioctx_iounit (IOCtx ioctx);
Npqid   *ioctx_qid (IOCtx ioctx);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
