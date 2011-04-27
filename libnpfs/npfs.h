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

#define FID_HTABLE_SIZE 64

struct Npfcall {
	u32		size;
	u8		type;
	u16		tag;
	u8*		pkt;
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

	   struct p9_tversion tversion;
	   struct p9_rversion rversion;
	   struct p9_tauth tauth;
	   struct p9_rauth rauth;
	   struct p9_tattach tattach;
	   struct p9_rattach rattach;
	   struct p9_tflush tflush;
	   struct p9_rflush rflush;
	   struct p9_twalk twalk;
	   struct p9_rwalk rwalk;
	   struct p9_tread tread;
	   struct p9_rread rread;
	   struct p9_twrite twrite;
	   struct p9_rwrite rwrite;
	   struct p9_tclunk tclunk;
	   struct p9_rclunk rclunk;
	   struct p9_tremove tremove;
	   struct p9_rremove rremove;
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

	char		client_id[128];
	u32		authuser;
	u32		msize;
	int		shutdown;
	Npsrv*		srv;
	Nptrans*	trans;
	Npfidpool*	fidpool;
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

enum {
	WT_FLAGS_SETFSID=1,
};

struct Npwthread {
	Npsrv*		srv;
	int		shutdown;
	enum { WT_START, WT_IDLE, WT_WORK, WT_REPLY, WT_SHUT } state;
	pthread_t	thread;
	int		flags;
	u32		fsuid;
	u32		fsgid;
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
	DEBUG_9P_TRACE=1,
	DEBUG_AUTH=2,
	DEBUG_CONN=4,
};

struct Npsrv {
	u32		msize;
	void*		srvaux;
	void*		treeaux;
	int		debuglevel;
	void		(*msg)(const char *, ...);
	Npauth*		auth;

	void		(*fiddestroy)(Npfid *);

	Npfcall*	(*version)(Npconn *conn, u32 msize, Npstr *version);
	Npuser*         (*remapuser)(Npstr *uname, u32 n_uname, Npstr *aname);
	Npfcall*	(*attach)(Npfid *fid, Npfid *afid, Npstr *aname);
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
	Npfcall*	(*xattrwalk)(Npfid *, Npfid *, Npstr *);
	Npfcall*	(*xattrcreate)(Npfid *, Npstr *, u64, u32);
	Npfcall*	(*readdir)(Npfid *, u64, u32, Npreq *);
	Npfcall*	(*fsync)(Npfid *);
	Npfcall*	(*llock)(Npfid *, u8, u32, u64, u64, u32, Npstr *);
	Npfcall*	(*getlock)(Npfid *, u8 type, u64, u64, u32, Npstr *);
	Npfcall*	(*link)(Npfid *, Npfid *, Npstr *);
	Npfcall*	(*mkdir)(Npfid *, Npstr *, u32, u32);

	/* implementation specific */
	int		shutdown;
	pthread_mutex_t	lock;
	pthread_cond_t	reqcond;
	pthread_cond_t	conncountcond;
	int		conncount;
	int		connhistory;
	Npconn*		conns;
	int		nwthread;
	Npwthread*	wthreads;
	Npreq*		reqs_first;
	Npreq*		reqs_last;
	Npreq*		workreqs;
};

struct Npuser {
	pthread_mutex_t	lock;
	int		refcount;
	char*		uname;
	uid_t		uid;
	gid_t		gid;
	int		nsg;	
	gid_t		sg[64];
};

/* srv.c */
Npsrv *np_srv_create(int nwthread, int wtflags);
void np_srv_destroy(Npsrv *srv);
void np_srv_remove_conn(Npsrv *, Npconn *);
int np_srv_add_conn(Npsrv *, Npconn *);
void np_srv_wait_conncount(Npsrv *srv, int count);
void np_srv_wait_timeout(Npsrv *srv, int inactivity_secs);
void np_srv_shutdown (Npsrv *srv);

/* conn.c */
Npconn *np_conn_create(Npsrv *, Nptrans *, char *);
void np_conn_incref(Npconn *);
void np_conn_decref(Npconn *);
void np_conn_respond(Npreq *req);
void np_respond(Npreq *, Npfcall *);
char *np_conn_get_client_id(Npconn *);
int np_conn_get_authuser(Npconn *, u32 *);
void np_conn_set_authuser(Npconn *, u32);

/* fidpool.c */
Npfidpool *np_fidpool_create(void);
void np_fidpool_destroy(Npfidpool *);
Npfid *np_fid_find(Npconn *, u32);
Npfid *np_fid_create(Npconn *, u32, void *);
void np_fid_destroy(Npfid *);
void np_fid_incref(Npfid *);
void np_fid_decref(Npfid *);

/* trans.c */
Nptrans *np_trans_create(void *aux, int (*read)(u8 *, u32, void *),
	int (*write)(u8 *, u32, void *), void (*destroy)(void *));
void np_trans_destroy(Nptrans *);
int np_trans_read(Nptrans *, u8 *, u32);
int np_trans_write(Nptrans *, u8 *, u32);

/* npstring.c */
void np_strzero(Npstr *str);
char *np_strdup(Npstr *str);
int np_strcmp(Npstr *str, char *cs);
int np_strncmp(Npstr *str, char *cs, int len);
int np_str9cmp(Npstr *s1, Npstr *s2);

/* np.c */
int np_peek_size(u8 *buf, int len);
int np_deserialize(Npfcall*, u8*);
int np_serialize_p9dirent(Npqid *qid, u64 offset, u8 type, char *name, u8 *buf,
                          int buflen);
int np_deserialize_p9dirent(Npqid *qid, u64 *offset, u8 *type, char *name,
			    int namelen, u8 *buf, int buflen);
void np_set_tag(Npfcall *, u16);
Npfcall *np_create_tversion(u32 msize, char *version);
Npfcall *np_create_rversion(u32 msize, char *version);
Npfcall *np_create_tauth(u32 fid, char *uname, char *aname, u32 n_uname);
Npfcall *np_create_rauth(Npqid *aqid);
Npfcall *np_create_tflush(u16 oldtag);
Npfcall *np_create_rflush(void);
Npfcall *np_create_tattach(u32 fid, u32 afid, char *uname, char *aname,
		   u32 n_uname);
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
Npfcall *np_create_tmknod(u32 dfid, char *name, u32 mode,
			  u32 major, u32 minor, u32 gid);
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
Npfcall *np_create_tsetattr(u32 fid, u32 valid, u32 mode, u32 uid, u32 gid,
			    u64 size, u64 atime_sec, u64 atime_nsec,
                            u64 mtime_sec, u64 mtime_nsec);
Npfcall *np_create_rsetattr(void);
Npfcall *np_create_txattrwalk(u32 fid, u32 attrfid, char *name);
Npfcall *np_create_rxattrwalk(u64 size);
Npfcall *np_create_txattrcreate(u32 fid, char *name, u64 size, u32 flag);
Npfcall *np_create_rxattrcreate(void);
Npfcall *np_create_treaddir(u32 fid, u64 offset, u32 count);
Npfcall *np_create_rreaddir(u32 count);
void np_finalize_rreaddir(Npfcall *fc, u32 count);
Npfcall *np_create_tfsync(u32 fid);
Npfcall *np_create_rfsync(void);
Npfcall * np_create_tlock(u32 fid, u8 type, u32 flags, u64 start, u64 length,
			  u32 proc_id, char *client_id);
Npfcall *np_create_rlock(u8 status);
Npfcall *np_create_tgetlock(u32 fid, u8 type, u64 start, u64 length,
			    u32 proc_id, char *client_id);
Npfcall *np_create_rgetlock(u8 type, u64 start, u64 length, u32 proc_id,
			    char *client_id);
Npfcall *np_create_tlink(u32 dfid, u32 fid, char *name);
Npfcall *np_create_rlink(void);
Npfcall *np_create_tmkdir(u32 dfid, char *name, u32 mode, u32 gid);
Npfcall *np_create_rmkdir(struct p9_qid *qid);

/* fmt.c */
void np_snprintfcall(char *s, int len, Npfcall *fc);

/* user.c */
void np_user_incref(Npuser *);
void np_user_decref(Npuser *);
Npuser *np_uid2user (u32 n_uname);
Npuser *np_uname2user (char *uname);
Npuser *np_attach2user (Npstr *uname, u32 n_uname);
int np_setfsid (Npreq *req, Npuser *u, u32 gid_override);

/* fdtrans.c */
Nptrans *np_fdtrans_create(int, int);

/* error.c */
int np_rerror(void);
void np_uerror(int ecode);

