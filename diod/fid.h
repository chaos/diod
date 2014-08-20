#define DIOD_FID_FLAGS_ROFS       0x01
#define DIOD_FID_FLAGS_MOUNTPT    0x02
#define DIOD_FID_FLAGS_SHAREFD    0x04
#define DIOD_FID_FLAGS_XATTR      0x08

typedef struct {
    Path            path;
    IOCtx           ioctx;
    Xattr           xattr;
    int             flags;
} Fid;

Fid *diod_fidalloc (Npfid *fid, Npstr *ns);
Fid *diod_fidclone (Npfid *newfid, Npfid *fid);
void diod_fiddestroy (Npfid *fid);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
