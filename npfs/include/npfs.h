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

#ifdef _WIN32
  #include <windows.h>
  #include <winsock2.h>
  #include "winthread.h"
  #define inline
  #define snprintf _snprintf
  #define vsnprintf _vsnprintf
  #define strdup _strdup
  static int close(SOCKET s) { return closesocket(s); }
  static int read(SOCKET s, char *buf, int len) { return recv(s, buf, len, 0); }
  static int write(SOCKET s, char *buf, int len) { return send(s, buf, len, 0); }
  static void sleep(DWORD val) { Sleep(val * 1000); }

  typedef UINT8  u8;
  typedef UINT16 u16;
  typedef UINT32 u32;
  typedef UINT64 u64;
  typedef UINT32 uid_t; // XXX
  typedef UINT32 gid_t;
  typedef int socklen_t;

  enum {
	ECONNABORTED = 53,
  };

#else // !_WIN32
  #include <pthread.h>
  #include <sys/types.h>
  #include <stdint.h>

  typedef uint8_t   u8;
  typedef uint16_t u16;
  typedef uint32_t u32;
  typedef uint64_t u64;
#endif


typedef struct Npstr Npstr;
typedef struct Npqid Npqid;
typedef struct Npstat Npstat;
typedef struct Npwstat Npwstat;
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
typedef struct Npfile Npfile;
typedef struct Npfilefid Npfilefid;
typedef struct Npfileops Npfileops;
typedef struct Npdirops Npdirops;
typedef struct Nppoll Nppoll;
typedef struct Npollfd Npollfd;

/* 9p2000.h extensions */
typedef struct Npstatfs Npstatfs;
typedef struct Nplock Nplock;
typedef struct Npflock Npflock;
typedef struct Nprename Nprename;

/* message types */
enum {
	Tfirst		= 100,
	Tversion	= 100,
	Rversion,
	Tauth		= 102,
	Rauth,
	Tattach		= 104,
	Rattach,
	Terror		= 106,
	Rerror,
	Tflush		= 108,
	Rflush,
	Twalk		= 110,
	Rwalk,
	Topen		= 112,
	Ropen,
	Tcreate		= 114,
	Rcreate,
	Tread		= 116,
	Rread,
	Twrite		= 118,
	Rwrite,
	Tclunk		= 120,
	Rclunk,
	Tremove		= 122,
	Rremove,
	Tstat		= 124,
	Rstat,
	Twstat		= 126,
	Rwstat,
	/* 9p2000.h extensions */
	Taread          = 128,
	Raread,
	Tawrite         = 130,
	Rawrite,
	Tstatfs         = 132,
	Rstatfs,
	Tlock           = 134,
	Rlock,
	Tflock          = 136,
	Rflock,
	Trename         = 148,
	Rrename,

	Rlast
};

/* lock cmd values */
enum {
	P9_LOCK_GETLK = 0x00,
	P9_LOCK_SETLK = 0x01,
	P9_LOCK_SETLKW = 0x02,
};

/* lock type values */
enum {
	P9_LOCK_RDLCK = 0x00,
	P9_LOCK_WRLCK = 0x01,
	P9_LOCK_UNLCK = 0x02,
};

/* flock op values */
enum {
	P9_FLOCK_SH = 0x01,
	P9_FLOCK_EX = 0x02,
	P9_FLOCK_UN = 0x03,
	P9_FLOCK_NB = 0x04,
};

/* datacheck values */
enum {
	P9_CHECK_NONE = 0,
	P9_CHECK_ADLER32 = 1,
};


/* modes */
enum {
	Oread		= 0x00,
	Owrite		= 0x01,
	Ordwr		= 0x02,
	Oexec		= 0x03,
	Oexcl		= 0x04,
	Otrunc		= 0x10,
	Orexec		= 0x20,
	Orclose		= 0x40,
	Oappend		= 0x80,

	Ouspecial	= 0x100,	/* internal use */
};

/* permissions */
enum {
	Dmdir		= 0x80000000,
	Dmappend	= 0x40000000,
	Dmexcl		= 0x20000000,
	Dmmount		= 0x10000000,
	Dmauth		= 0x08000000,
	Dmtmp		= 0x04000000,
	Dmsymlink	= 0x02000000,
	Dmlink		= 0x01000000,

	/* 9P2000.u extensions */
	Dmdevice	= 0x00800000,
	Dmnamedpipe	= 0x00200000,
	Dmsocket	= 0x00100000,
	Dmsetuid	= 0x00080000,
	Dmsetgid	= 0x00040000,
};

/* qid.types */
enum {
	Qtdir		= 0x80,
	Qtappend	= 0x40,
	Qtexcl		= 0x20,
	Qtmount		= 0x10,
	Qtauth		= 0x08,
	Qttmp		= 0x04,
	Qtsymlink	= 0x02,
	Qtlink		= 0x01,
	Qtfile		= 0x00,
};

#define NOTAG		(u16)(~0)
#define NOFID		(u32)(~0)
#define MAXWELEM	16
#define IOHDRSZ		24
#define AIOHDRSZ       (IOHDRSZ+4+1+4)
#define FID_HTABLE_SIZE 64

struct Npstr {
	u16		len;
	char*		str;
};

struct Npqid {
	u8		type;
	u32		version;
	u64		path;
};

struct Npstat {
	u16 		size;
	u16 		type;
	u32 		dev;
	Npqid		qid;
	u32 		mode;
	u32 		atime;
	u32 		mtime;
	u64 		length;
	Npstr		name;
	Npstr		uid;
	Npstr		gid;
	Npstr		muid;

	/* 9P2000.u extensions */
	Npstr		extension;
	u32 		n_uid;
	u32 		n_gid;
	u32 		n_muid;
};
 
/* file metadata (stat) structure used to create Twstat message
   It is similar to Npstat, but the strings don't point to 
   the same memory block and should be freed separately
*/
struct Npwstat {
	u16 		size;
	u16 		type;
	u32 		dev;
	Npqid		qid;
	u32 		mode;
	u32 		atime;
	u32 		mtime;
	u64 		length;
	char*		name;
	char*		uid;
	char*		gid;
	char*		muid;
	char*		extension;	/* 9p2000.u extensions */
	u32 		n_uid;		/* 9p2000.u extensions */
	u32 		n_gid;		/* 9p2000.u extensions */
	u32 		n_muid;		/* 9p2000.u extensions */
};

struct Npstatfs {
	u32		type;
	u32		bsize;
	u64		blocks;
	u64		bfree;
	u64		bavail;
	u64		files;
	u64		ffree;
	u64		fsid;
	u32		namelen;
};

struct Nplock {
	u8		type;
	u64		pid;
	u64		start;
	u64		end;
};

struct Npfcall {
	u32		size;
	u8		type;
	u16		tag;
	u8*		pkt;

	u32		fid;
	u32		msize;			/* Tversion, Rversion */
	Npstr		version;		/* Tversion, Rversion */
	u32		afid;			/* Tauth, Tattach */
	Npstr		uname;			/* Tauth, Tattach */
	Npstr		aname;			/* Tauth, Tattach */
	Npqid		qid;			/* Rauth, Rattach, Ropen, Rcreate */
	Npstr		ename;			/* Rerror */
	u16		oldtag;			/* Tflush */
	u32		newfid;			/* Twalk */
	u16		nwname;			/* Twalk */
	Npstr		wnames[MAXWELEM];	/* Twalk */
	u16		nwqid;			/* Rwalk */
	Npqid		wqids[MAXWELEM];	/* Rwalk */
	u8		mode;			/* Topen, Tcreate */
	u32		iounit;			/* Ropen, Rcreate */
	Npstr		name;			/* Tcreate */
	u32		perm;			/* Tcreate */
	u64		offset;			/* Tread, Twrite */
	u32		count;			/* Tread, Rread, Twrite, Rwrite */
	u8*		data;			/* Rread, Twrite */
	Npstat		stat;			/* Rstat, Twstat */

	/* 9P2000.u extensions */
	u32		ecode;			/* Rerror */
	Npstr		extension;		/* Tcreate */
	u32		n_uname;

	/* 9P2000.h extensions */
	u32		rsize;			/* Taread, Tawrite */
	u8		datacheck;		/* Taread, Tawrite */
	u32		check;			/* Raread, Tawrite */
	u8		cmd;			/* Tlock, Tflock */
	Npstatfs	statfs;			/* Rstatfs */
	Nplock		lock;			/* Tlock,Rlock */
	u32		newdirfid;		/* Trename */
	Npstr		newname;		/* Trename */

	Npfcall*	next;
};

struct Npfid {
	pthread_mutex_t	lock;
	Npconn*		conn;
	u32		fid;
	int		refcount;
	u16		omode;
	u8		type;
	u32		diroffset;
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
	int		dotu;
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
	int		dotu;		/* 9P2000.u support flag */
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
	Npfcall*	(*open)(Npfid *fid, u8 mode);
	Npfcall*	(*create)(Npfid *fid, Npstr* name, u32 perm, u8 mode, 
				Npstr* extension);
	Npfcall*	(*read)(Npfid *fid, u64 offset, u32 count, Npreq *req);
	Npfcall*	(*write)(Npfid *fid, u64 offset, u32 count, u8 *data, 
				Npreq *req);
	Npfcall*	(*clunk)(Npfid *fid);
	Npfcall*	(*remove)(Npfid *fid);
	Npfcall*	(*stat)(Npfid *fid);
	Npfcall*	(*wstat)(Npfid *fid, Npstat *stat);

	/* 9p2000.h extensions */
	Npfcall*	(*aread)(Npfid *fid, u8 datacheck, u64 offset,
				 u32 count, u32 rsize, Npreq *req);
	Npfcall*	(*awrite)(Npfid *fid, u64 offset, u32 count,
				  u32 rsize, u8 *data, Npreq *req);
	Npfcall*	(*statfs)(Npfid *fid);
	Npfcall*	(*plock)(Npfid *fid, u8 cmd, Nplock *flck);
	Npfcall*	(*flock)(Npfid *fid, u8 cmd);
	Npfcall*	(*rename)(Npfid *fid, Npfid *newdirfid, Npstr *newname);

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

struct Npfile {
	pthread_mutex_t	lock;
	int		refcount;
	Npfile*		parent;
	Npqid		qid;
	u32		mode;
	u32		atime;
	u32		mtime;
	u64		length;
	char*		name;
	Npuser*		uid;
	Npgroup*	gid;
	Npuser*		muid;
	char*		extension;
	int		excl;
	void*		ops;
	void*		aux;

	/* not used -- provided for user's convenience */
	Npfile*		next;
	Npfile*		prev;
	Npfile*		dirfirst;
	Npfile*		dirlast;
};

struct Npfileops {
	void		(*ref)(Npfile *, Npfilefid *);
	void		(*unref)(Npfile *, Npfilefid *);
	int		(*read)(Npfilefid* file, u64 offset, u32 count, 
				u8 *data, Npreq *req);
	int		(*write)(Npfilefid* file, u64 offset, u32 count, 
				u8 *data, Npreq *req);
	int		(*wstat)(Npfile*, Npstat*);
	void		(*destroy)(Npfile*);
	int		(*openfid)(Npfilefid *);
	void		(*closefid)(Npfilefid *);
};

struct Npdirops {
	void		(*ref)(Npfile *, Npfilefid *);
	void		(*unref)(Npfile *, Npfilefid *);
	Npfile*		(*create)(Npfile *dir, char *name, u32 perm, 
				Npuser *uid, Npgroup *gid, char *extension);
	Npfile*		(*first)(Npfile *dir);
	Npfile*		(*next)(Npfile *dir, Npfile *prevchild);
	int		(*wstat)(Npfile*, Npstat*);
	int		(*remove)(Npfile *dir, Npfile *file);
	void		(*destroy)(Npfile*);
	Npfilefid*	(*allocfid)(Npfile *);
	void		(*destroyfid)(Npfilefid *);
};

struct Npfilefid {
	pthread_mutex_t	lock;
	Npfid*		fid;
	Npfile*		file;
	int		omode;
	void*		aux;
	u64		diroffset;
	Npfile*		dirent;
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
void np_conn_reset(Npconn *, u32, int);
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

int np_deserialize(Npfcall*, u8*, int dotu);
int np_serialize_stat(Npwstat *wstat, u8* buf, int buflen, int dotu);
int np_deserialize_stat(Npstat *stat, u8* buf, int buflen, int dotu);

void np_strzero(Npstr *str);
char *np_strdup(Npstr *str);
int np_strcmp(Npstr *str, char *cs);
int np_strncmp(Npstr *str, char *cs, int len);

void np_set_tag(Npfcall *, u16);
Npfcall *np_create_tversion(u32 msize, char *version);
Npfcall *np_create_rversion(u32 msize, char *version);
Npfcall *np_create_tauth(u32 fid, char *uname, char *aname, u32 n_uname, int dotu);
Npfcall *np_create_rauth(Npqid *aqid);
Npfcall *np_create_rerror(char *ename, int ecode, int dotu);
Npfcall *np_create_rerror1(Npstr *ename, int ecode, int dotu);
Npfcall *np_create_tflush(u16 oldtag);
Npfcall *np_create_rflush(void);
Npfcall *np_create_tattach(u32 fid, u32 afid, char *uname, char *aname, u32 n_uname, int dotu);
Npfcall *np_create_rattach(Npqid *qid);
Npfcall *np_create_twalk(u32 fid, u32 newfid, u16 nwname, char **wnames);
Npfcall *np_create_rwalk(int nwqid, Npqid *wqids);
Npfcall *np_create_topen(u32 fid, u8 mode);
Npfcall *np_create_ropen(Npqid *qid, u32 iounit);
Npfcall *np_create_tcreate(u32 fid, char *name, u32 perm, u8 mode);
Npfcall *np_create_rcreate(Npqid *qid, u32 iounit);
Npfcall *np_create_tread(u32 fid, u64 offset, u32 count);
Npfcall *np_create_rread(u32 count, u8* data);
Npfcall *np_create_twrite(u32 fid, u64 offset, u32 count, u8 *data);
Npfcall *np_create_rwrite(u32 count);
Npfcall *np_create_tclunk(u32 fid);
Npfcall *np_create_rclunk(void);
Npfcall *np_create_tremove(u32 fid);
Npfcall *np_create_rremove(void);
Npfcall *np_create_tstat(u32 fid);
Npfcall *np_create_rstat(Npwstat *stat, int dotu);
Npfcall *np_create_twstat(u32 fid, Npwstat *wstat, int dotu);
Npfcall *np_create_rwstat(void);
Npfcall * np_alloc_rread(u32);
void np_set_rread_count(Npfcall *, u32);
/* 9p2000.h */
Npfcall *np_create_taread(u32 fid, u8 datacheck, u64 offset, u32 count, u32 rsize);
Npfcall *np_create_raread(u32 count);
Npfcall *np_create_tawrite(u32 fid, u8 datacheck, u64 offset, u32 count, u32 rsize, u8 *data);
Npfcall *np_create_rawrite(u32 count);
Npfcall *np_create_tstatfs(u32 fid);
Npfcall *np_create_rstatfs(u32 type, u32 bsize, u64 blocks, u64 bfree, u64 bavail, u64 files, u64 ffree, u64 fsid, u32 namelen);
Npfcall *np_create_tlock(u32 fid, u8 cmd, u8 type, u64 pid, u64 start, u64 end);
Npfcall *np_create_rlock(u8 type, u64 pid, u64 start, u64 end);
Npfcall *np_create_tflock(u32 fid, u8 op);
Npfcall *np_create_rflock(void);
Npfcall *np_create_trename(u32 fid, u32 newdirfid, char *newname);
Npfcall *np_create_rrename(void);
void np_finalize_raread(Npfcall *fc, u32 count, u8 datacheck);

int np_printstat(FILE *f, Npstat *st, int dotu);
int np_snprintstat(char *s, int len, Npstat *st, int dotu);
int np_printfcall(FILE *f, Npfcall *fc, int dotu);
int np_snprintfcall(char *s, int len, Npfcall *fc, int dotu);

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

void np_werror(char *ename, int ecode, ...);
void np_rerror(char **ename, int *ecode);
int np_haserror(void);
void np_uerror(int ecode);
void np_suerror(char *s, int ecode);

Npfile* npfile_alloc(Npfile *parent, char *name, u32 mode, u64 qpath, 
	void *ops, void *aux);
void npfile_incref(Npfile *);
int npfile_decref(Npfile *);
Npfile *npfile_find(Npfile *, char *);
int npfile_checkperm(Npfile *file, Npuser *user, int perm);
void npfile_init_srv(Npsrv *, Npfile *);

void npfile_fiddestroy(Npfid *fid);
Npfcall *npfile_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname);
int npfile_clone(Npfid *fid, Npfid *newfid);
int npfile_walk(Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall *npfile_open(Npfid *fid, u8 mode);
Npfcall *npfile_create(Npfid *fid, Npstr* name, u32 perm, u8 mode, Npstr* extension);
Npfcall *npfile_read(Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall *npfile_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req);
Npfcall *npfile_clunk(Npfid *fid);
Npfcall *npfile_remove(Npfid *fid);
Npfcall *npfile_stat(Npfid *fid);
Npfcall *npfile_wstat(Npfid *fid, Npstat *stat);

void *np_malloc(int);
int np_mount(char *mntpt, int mntflags, char *opts);
