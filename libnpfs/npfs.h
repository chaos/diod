/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
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

typedef struct p9_str Npstr;
typedef struct p9_qid Npqid;
typedef struct Npfcall Npfcall;
typedef struct Npfid Npfid;
typedef struct Npfidpool Npfidpool;
typedef struct Npbuf Npbuf;
typedef struct Nptrans Nptrans;
typedef struct Npconn Npconn;
typedef struct Npreq Npreq;
typedef struct Npwthread Npwthread;
typedef struct Npauth Npauth;
typedef struct Npsrv Npsrv;
typedef struct Npuser Npuser;
typedef struct Npgroup Npgroup;
typedef struct Npuserpool Npuserpool;

#if HAVE_LARGEIO
/* datacheck values */
enum {
	P9_CHECK_NONE = 0,
	P9_CHECK_ADLER32 = 1,
};
#define P9_AIOHDRSZ (P9_IOHDRSZ+4+1+4)
#endif 

#define FID_HTABLE_SIZE 64

struct Npfcall {
	u32		size;
	u8		type;
	u16		tag;
	u8*		pkt;

	u32		fid;
	u32		msize;			/* P9_TVERSION, P9_RVERSION */
	Npstr		version;		/* P9_TVERSION, P9_RVERSION */
	u32		afid;			/* P9_TAUTH, P9_TATTACH */
	Npstr		uname;			/* P9_TAUTH, P9_TATTACH */
	Npstr		aname;			/* P9_TAUTH, P9_TATTACH */
	Npqid		qid;			/* P9_RAUTH, P9_RATTACH, P9_ROPEN, P9_RCREATE */
	Npstr		ename;			/* P9_RERROR */
	u16		oldtag;			/* P9_TFLUSH */
	u32		newfid;			/* P9_TWALK */
	u16		nwname;			/* P9_TWALK */
	Npstr		wnames[P9_MAXWELEM];	/* P9_TWALK */
	u16		nwqid;			/* P9_RWALK */
	Npqid		wqids[P9_MAXWELEM];	/* P9_RWALK */
	u8		mode;			/* P9_TOPEN, P9_TCREATE */
	u32		iounit;			/* P9_ROPEN, P9_RCREATE */
	Npstr		name;			/* P9_TCREATE */
	u32		perm;			/* P9_TCREATE */
	u64		offset;			/* P9_TREAD, P9_TWRITE */
	u32		count;			/* P9_TREAD, P9_RREAD, P9_TWRITE, P9_RWRITE */
	u8*		data;			/* P9_RREAD, P9_TWRITE */

	/* 9P2000.u extensions */
	u32		ecode;			/* P9_RERROR */
	Npstr		extension;		/* P9_TCREATE */
	u32		n_uname;
	union {
	   struct p9_rlerror rlerror;
	   struct p9_tstatfs tstatfs;
	   struct p9_rstatfs rstatfs;
	   struct p9_tlopen tlopen;
	   struct p9_rlopen rlopen;
	   struct p9_tlcreate tlcreate;
	   struct p9_rlcreate rlcreate;
	   struct p9_tsymlink tsymlink;
	   struct p9_rsymlink rsymlink;
	   struct p9_tmknod tmknod;
	   struct p9_rmknod rmknod;
	   struct p9_trename trename;
	   struct p9_rrename rrename;
	   struct p9_treadlink treadlink;
	   struct p9_rreadlink rreadlink;
	   struct p9_tgetattr tgetattr;
	   struct p9_rgetattr rgetattr;
	   struct p9_tsetattr tsetattr;
	   struct p9_rsetattr rsetattr;
	   struct p9_txattrwalk txattrwalk;
	   struct p9_rxattrwalk rxattrwalk;
	   struct p9_txattrcreate txattrcreate;
	   struct p9_rxattrcreate rxattrcreate;
	   struct p9_treaddir treaddir;
	   struct p9_rreaddir rreaddir;
	   struct p9_tfsync tfsync;
	   struct p9_rfsync rfsync;
	   struct p9_tlock tlock;
	   struct p9_rlock rlock;
	   struct p9_tgetlock tgetlock;
	   struct p9_rgetlock rgetlock;
	   struct p9_tlink tlink;
	   struct p9_rlink rlink;
	   struct p9_tmkdir tmkdir;
	   struct p9_rmkdir rmkdir;
#if HAVE_LARGEIO
	   struct p9_tawrite tawrite;
	   struct p9_rawrite rawrite;
	   struct p9_taread taread;
	   struct p9_raread raread;
#endif
	} u;
	Npfcall*	next;
};


struct Npfid {
	pthread_mutex_t	lock;
	Npconn*		conn;
	u32		fid;
	int		refcount;
	u8		type;
	Npuser*		user;
	void*		aux;

	Npfid*		next;	/* list of fids within a bucket */
	Npfid*		prev;
};

struct Npbuf {
	void*		aux;
	int		count;
	u8*		data;
	int		pos;
	Npbuf*		next;
};

struct Nptrans {
	void*		aux;
	int		(*read)(u8 *, u32, void *);
	int		(*write)(u8 *, u32, void *);
	void		(*destroy)(void *);
};

struct Npfidpool {
	pthread_mutex_t	lock;
	int		size;
	Npfid**		htable;
};

struct Npconn {
	pthread_mutex_t	lock;
	pthread_mutex_t	wlock;
	int		refcount;

	int		resetting;
	pthread_cond_t	resetcond;
	pthread_cond_t	resetdonecond;

	u32		msize;
	int		shutdown;
	Npsrv*		srv;
	Nptrans*	trans;
	Npfidpool*	fidpool;
	int		freercnum;
	Npfcall*	freerclist;
	void*		aux;
	pthread_t	rthread;

	Npconn*		next;	/* list of connections within a server */
};

struct Npreq {
	pthread_mutex_t	lock;
	int		refcount;
	Npconn*		conn;
	u16		tag;
	Npfcall*	tcall;
	Npfcall*	rcall;
	int		cancelled;
	int		responded;
	Npreq*		flushreq;
	Npfid*		fid;

	Npreq*		next;	/* list of all outstanding requests */
	Npreq*		prev;	/* used for requests that are worked on */
	Npwthread*	wthread;/* for requests that are worked on */
};

struct Npwthread {
	Npsrv*		srv;
	int		shutdown;
	pthread_t	thread;

	Npwthread	*next;
};

struct Npauth {
	int	(*startauth)(Npfid *afid, char *aname, Npqid *aqid);
	int	(*checkauth)(Npfid *fid, Npfid *afid, char *aname);
	int	(*read)(Npfid *fid, u64 offset, u32 count, u8 *data);
	int	(*write)(Npfid *fid, u64 offset, u32 count, u8 *data);
	int	(*clunk)(Npfid *fid);
};	

enum {
	DEBUG_9P_TRACE=0x01,
        DEBUG_9P_ERRORS=0x02,
};

struct Npsrv {
	u32		msize;
	void*		srvaux;
	void*		treeaux;
	int		debuglevel;
	void		(*debugprintf)(const char *, ...);
	Npauth*		auth;
	Npuserpool*	upool;

	void		(*start)(Npsrv *);
	void		(*shutdown)(Npsrv *);
	void		(*destroy)(Npsrv *);
	void		(*connopen)(Npconn *);
	void		(*connclose)(Npconn *);
	void		(*fiddestroy)(Npfid *);

	Npfcall*	(*version)(Npconn *conn, u32 msize, Npstr *version);
//	Npfcall*	(*auth)(Npfid *afid, Npstr *uname, Npstr *aname);
	Npfcall*	(*attach)(Npfid *fid, Npfid *afid, Npstr *uname, 
				Npstr *aname);
	void		(*flush)(Npreq *req);
	int		(*clone)(Npfid *fid, Npfid *newfid);
	int		(*walk)(Npfid *fid, Npstr *wname, Npqid *wqid);
	Npfcall*	(*read)(Npfid *fid, u64 offset, u32 count, Npreq *req);
	Npfcall*	(*write)(Npfid *fid, u64 offset, u32 count, u8 *data, 
				Npreq *req);
	Npfcall*	(*clunk)(Npfid *fid);
	Npfcall*	(*remove)(Npfid *fid);

	Npfcall*	(*statfs)(Npfid *);
	Npfcall*	(*lopen)(Npfid *, u32);
	Npfcall*	(*lcreate)(Npfid *, Npstr *, u32, u32, u32);
	Npfcall*	(*symlink)(Npfid *, Npstr *, Npstr *, u32);
	Npfcall*	(*mknod)(Npfid *, Npstr *, u32, u32, u32, u32);
	Npfcall*	(*rename)(Npfid *, Npfid *, Npstr *);
	Npfcall*	(*readlink)(Npfid *);
	Npfcall*	(*getattr)(Npfid *, u64);
	Npfcall*	(*setattr)(Npfid *, u32, u32, u32, u32, u64, u64, u64,
				   u64, u64);
	Npfcall*	(*xattrwalk)(void); /* FIXME */
	Npfcall*	(*xattrcreate)(void); /* FIXME */
	Npfcall*	(*readdir)(Npfid *, u64, u32, Npreq *);
	Npfcall*	(*fsync)(Npfid *);
	Npfcall*	(*llock)(Npfid *, u8, u32, u64, u64, u32, Npstr *);
	Npfcall*	(*getlock)(Npfid *, u8 type, u64, u64, u32, Npstr *);
	Npfcall*	(*link)(Npfid *, Npfid *, Npstr *);
	Npfcall*	(*mkdir)(Npfid *, Npstr *, u32, u32);

#if HAVE_LARGEIO
	Npfcall*	(*aread)(Npfid *fid, u8 datacheck, u64 offset,
				 u32 count, u32 rsize, Npreq *req);
	Npfcall*	(*awrite)(Npfid *fid, u64 offset, u32 count,
				  u32 rsize, u8 *data, Npreq *req);
#endif
	/* implementation specific */
	pthread_mutex_t	lock;
	pthread_cond_t	reqcond;
	int		shuttingdown;
	Npconn*		conns;
	int		nwthread;
	Npwthread*	wthreads;
	Npreq*		reqs_first;
	Npreq*		reqs_last;
	Npreq*		workreqs;
};

struct Npuser {
	pthread_mutex_t	lock;
	int			refcount;
	Npuserpool*	upool;
	char*		uname;
	uid_t		uid;
	Npgroup*	dfltgroup;
	int			ngroups;	
	Npgroup**	groups;
	void*		aux;

	Npuser*		next;
};

struct Npgroup {
	pthread_mutex_t	lock;
	int			refcount;
	Npuserpool* upool;
	char*		gname;
	gid_t		gid;
	void*		aux;

	Npgroup*	next;
};

struct Npuserpool {
	void*		aux;
	Npuser*		(*uname2user)(Npuserpool *, char *uname);
	Npuser*		(*uid2user)(Npuserpool *, u32 uid);
	Npgroup*	(*gname2group)(Npuserpool *, char *gname);
	Npgroup*	(*gid2group)(Npuserpool *, u32 gid);
	int		(*ismember)(Npuserpool *, Npuser *u, Npgroup *g);
	void		(*udestroy)(Npuserpool *, Npuser *u);
	void		(*gdestroy)(Npuserpool *, Npgroup *g);
};

extern char *Eunknownfid;
extern char *Ennomem;
extern char *Enoauth;
extern char *Enotimpl;
extern char *Einuse;
extern char *Ebadusefid;
extern char *Enotdir;
extern char *Etoomanywnames;
extern char *Eperm;
extern char *Etoolarge;
extern char *Ebadoffset;
extern char *Edirchange;
extern char *Enotfound;
extern char *Eopen;
extern char *Eexist;
extern char *Enotempty;
extern char *Eunknownuser;
extern Npuserpool *np_default_users;

Npsrv *np_srv_create(int nwthread);
void np_srv_remove_conn(Npsrv *, Npconn *);
void np_srv_start(Npsrv *);
void np_srv_shutdown(Npsrv *, int wait);
int np_srv_add_conn(Npsrv *, Npconn *);
void np_buf_init(Npbuf *, void *, void (*)(void *), void (*)(void *, int));
void np_buf_set(Npbuf *, u8 *, u32);

Npconn *np_conn_create(Npsrv *, Nptrans *);
void np_conn_incref(Npconn *);
void np_conn_decref(Npconn *);
void np_conn_reset(Npconn *, u32);
void np_conn_shutdown(Npconn *);
void np_conn_respond(Npreq *req);
void np_respond(Npreq *, Npfcall *);

Npfidpool *np_fidpool_create(void);
void np_fidpool_destroy(Npfidpool *);
Npfid *np_fid_find(Npconn *, u32);
Npfid *np_fid_create(Npconn *, u32, void *);
void np_fid_destroy(Npfid *);
void np_fid_incref(Npfid *);
void np_fid_decref(Npfid *);

Nptrans *np_trans_create(void *aux, int (*read)(u8 *, u32, void *),
	int (*write)(u8 *, u32, void *), void (*destroy)(void *));
void np_trans_destroy(Nptrans *);
int np_trans_read(Nptrans *, u8 *, u32);
int np_trans_write(Nptrans *, u8 *, u32);

int np_deserialize(Npfcall*, u8*);
int np_serialize_p9dirent(Npqid *qid, u64 offset, u8 type, char *name, u8 *buf,
                          int buflen);

void np_strzero(Npstr *str);
char *np_strdup(Npstr *str);
int np_strcmp(Npstr *str, char *cs);
int np_strncmp(Npstr *str, char *cs, int len);

void np_set_tag(Npfcall *, u16);
Npfcall *np_create_tversion(u32 msize, char *version);
Npfcall *np_create_rversion(u32 msize, char *version);
Npfcall *np_create_tauth(u32 fid, char *uname, char *aname, u32 n_uname);
Npfcall *np_create_rauth(Npqid *aqid);
Npfcall *np_create_tflush(u16 oldtag);
Npfcall *np_create_rflush(void);
Npfcall *np_create_tattach(u32 fid, u32 afid, char *uname, char *aname, u32 n_uname);
Npfcall *np_create_rattach(Npqid *qid);
Npfcall *np_create_twalk(u32 fid, u32 newfid, u16 nwname, char **wnames);
Npfcall *np_create_rwalk(int nwqid, Npqid *wqids);
Npfcall *np_create_tread(u32 fid, u64 offset, u32 count);
Npfcall *np_create_rread(u32 count, u8* data);
Npfcall *np_create_twrite(u32 fid, u64 offset, u32 count, u8 *data);
Npfcall *np_create_rwrite(u32 count);
Npfcall *np_create_tclunk(u32 fid);
Npfcall *np_create_rclunk(void);
Npfcall *np_create_tremove(u32 fid);
Npfcall *np_create_rremove(void);
Npfcall *np_create_tread(u32 fid, u64 offset, u32 count);
Npfcall * np_alloc_rread(u32);
void np_set_rread_count(Npfcall *, u32);
#if HAVE_LARGEIO
Npfcall *np_create_taread(u32 fid, u8 datacheck, u64 offset, u32 count, u32 rsize);
Npfcall *np_create_raread(u32 count);
void np_finalize_raread(Npfcall *fc, u32 count, u8 datacheck);
Npfcall *np_create_tawrite(u32 fid, u8 datacheck, u64 offset, u32 count, u32 rsize, u8 *data);
Npfcall *np_create_rawrite(u32 count);
#endif
Npfcall *np_create_rlerror(u32 ecode);
Npfcall *np_create_tstatfs(u32 fid);
Npfcall *np_create_rstatfs(u32 type, u32 bsize,
		u64 blocks, u64 bfree, u64 bavail,
		u64 files, u64 ffree, u64 fsid, u32 namelen);
Npfcall *np_create_tlopen(u32 fid, u32 mode);
Npfcall *np_create_rlopen(Npqid *qid, u32 iounit);
Npfcall *np_create_tlcreate(u32 fid, char *name, u32 flags, u32 mode, u32 gid);
Npfcall *np_create_rlcreate(struct p9_qid *qid, u32 iounit);
Npfcall *np_create_tsymlink(u32 fid, char *name, char *symtgt, u32 gid);
Npfcall *np_create_rsymlink (struct p9_qid *qid);
Npfcall *np_create_treadlink(u32 fid);
Npfcall *np_create_rreadlink(char *symtgt);
Npfcall *np_create_tmknod(u32 dfid, char *name, u32 mode, u32 major, u32 minor, u32 gid);
Npfcall *np_create_rmknod (struct p9_qid *qid);
Npfcall *np_create_trename(u32 fid, u32 dfid, char *name);
Npfcall *np_create_rrename(void);
Npfcall *np_create_tgetattr(u32 fid, u64 request_mask);
Npfcall *np_create_rgetattr(u64 valid, struct p9_qid *qid,
		u32 mode, u32 uid, u32 gid, u64 nlink, u64 rdev,
                u64 size, u64 blksize, u64 st_blocks,
                u64 atime_sec, u64 atime_nsec,
                u64 mtime_sec, u64 mtime_nsec,
                u64 ctime_sec, u64 ctime_nsec,
                u64 btime_sec, u64 btime_nsec,
                u64 gen, u64 data_version);
Npfcall *np_create_tsetattr(u32 fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size);
Npfcall *np_create_rsetattr(void);
Npfcall *np_create_treaddir(u32 fid, u64 offset, u32 count);
Npfcall *np_create_rreaddir(u32 count);
void np_finalize_rreaddir(Npfcall *fc, u32 count);
Npfcall *np_create_tfsync(u32 fid);
Npfcall *np_create_rfsync(void);
Npfcall *np_create_tlink(u32 dfid, u32 fid, char *name);
Npfcall *np_create_rlink(void);
Npfcall *np_create_tmkdir(u32 dfid, char *name, u32 mode, u32 gid);
Npfcall *np_create_rmkdir(struct p9_qid *qid);

int np_printfcall(FILE *f, Npfcall *fc);
int np_snprintfcall(char *s, int len, Npfcall *fc);

void np_user_incref(Npuser *);
void np_user_decref(Npuser *);
void np_group_incref(Npgroup *);
void np_group_decref(Npgroup *);
int np_change_user(Npuser *u);
Npuser* np_current_user(void);

Npuserpool *np_priv_userpool_create();
Npuser *np_priv_user_add(Npuserpool *up, char *uname, u32 uid, void *aux);
void np_priv_user_del(Npuser *u);
int np_priv_user_setdfltgroup(Npuser *u, Npgroup *g);
Npgroup *np_priv_group_add(Npuserpool *up, char *gname, u32 gid);
void np_priv_group_del(Npgroup *g);
int np_priv_group_adduser(Npgroup *g, Npuser *u);
int np_priv_group_deluser(Npgroup *g, Npuser *u);

Nptrans *np_fdtrans_create(int, int);
Npsrv *np_socksrv_create_tcp(int, int*);
Npsrv *np_pipesrv_create(int nwthreads);
int np_pipesrv_mount(Npsrv *srv, char *mntpt, char *user, int mntflags, char *opts);

Npsrv *np_rdmasrv_create(int nwthreads, int *port);

int np_rerror(void);
void np_uerror(int ecode);

void *np_malloc(int);
