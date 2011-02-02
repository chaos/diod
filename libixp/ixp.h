/* Copyright ©2006-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */

#define VERSION "0.5"
#define COPYRIGHT "©2010 Kris Maglione"

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/select.h>

/**
 * Macro: IXP_API
 * Macro: IXP_NEEDAPI
 * Macro: IXP_MAXAPI
 * Macro: IXP_ASSERT_VERSION
 *
 * IXP_API contains the current libixp API revision number.
 *
 * IXP_NEEDAPI, if defined before ixp.h is included, directs the
 * header to present an older version of the libixp API. This allows
 * code written for older versions of libixp to compile against
 * newer versions without modification. It does not, however, ensure
 * that it will link against a different version of libixp than the
 * ixp.h header belongs to.
 *
 * IXP_MAXAPI, if defined before ixp.h is included, prevents code
 * from compiling with a newer version of libixp than specified.
 *
 * When inserted into any function, IXP_ASSERT_VERSION ensures that
 * the resulting object will fail to link link against any version
 * of libixp with a different API version than it was compiled
 * against.
 */
#define IXP_API 135
#define _IXP_ASSERT_VERSION ixp_version_ ## 135 ## _required

#ifndef IXP_NEEDAPI
#define IXP_NEEDAPI IXP_API
#endif

#ifndef IXP_MAXAPI
#define IXP_MAXAPI IXP_API
#endif

#define IXP_ASSERT_VERSION do _IXP_ASSERT_VERSION = 0; while(0)
extern int _IXP_ASSERT_VERSION;

/* Gunk */
#if IXP_API < IXP_NEEDAPI
#  error A newer version of libixp is needed for this compilation.
#endif
#if IXP_API > IXP_MAXAPI
#  warning This version of libixp has a newer API than this compilation requests.
#endif

#if IXP_NEEDAPI < 127
# undef	uchar
# undef	ushort
# undef	ulong
# undef	vlong
# undef	uvlong
# define	uchar		_ixpuchar
# define	ushort	_ixpushort
# define	ulong	_ixpulong
# define	vlong	_ixpvlong
# define	uvlong	_ixpuvlong

typedef unsigned char	uchar;
typedef uint16_t	ushort;
typedef uint32_t	ulong;
typedef uint64_t	uvlong;

typedef int64_t		vlong;

# define respond ixp_respond
# define serve_9pcon ixp_serve9pconn
#endif

#undef	uint
#define	uint		_ixpuint
typedef unsigned int	uint;

#ifdef KENC
#  define STRUCT(x) struct {x};
#  define UNION(x) union {x};
#elif defined(__GNUC__)
#  define STRUCT(x) __extension__ struct {x};
#  define UNION(x) __extension__ union {x};
#endif
/* End Gunk */

#define IXP_VERSION	"9P2000"
#define IXP_NOTAG	((uint16_t)~0)	/* Dummy tag */
#define IXP_NOFID	(~0U)

enum {
	IXP_MAX_VERSION = 32,
	IXP_MAX_MSG = 8192,
	IXP_MAX_ERROR = 128,
	IXP_MAX_CACHE = 32,
	IXP_MAX_FLEN = 128,
	IXP_MAX_ULEN = 32,
	IXP_MAX_WELEM = 16,
};

/* 9P message types */
enum IxpFType {
	P9_TVersion = 100,
	P9_RVersion,
	P9_TAuth = 102,
	P9_RAuth,
	P9_TAttach = 104,
	P9_RAttach,
	P9_TError = 106, /* illegal */
	P9_RError,
	P9_TFlush = 108,
	P9_RFlush,
	P9_TWalk = 110,
	P9_RWalk,
	P9_TOpen = 112,
	P9_ROpen,
	P9_TCreate = 114,
	P9_RCreate,
	P9_TRead = 116,
	P9_RRead,
	P9_TWrite = 118,
	P9_RWrite,
	P9_TClunk = 120,
	P9_RClunk,
	P9_TRemove = 122,
	P9_RRemove,
	P9_TStat = 124,
	P9_RStat,
	P9_TWStat = 126,
	P9_RWStat,
};

/* from libc.h in p9p */
enum IxpOMode {
	P9_OREAD	= 0,	/* open for read */
	P9_OWRITE	= 1,	/* write */
	P9_ORDWR	= 2,	/* read and write */
	P9_OEXEC	= 3,	/* execute, == read but check execute permission */
	P9_OTRUNC	= 16,	/* or'ed in (except for exec), truncate file first */
	P9_OCEXEC	= 32,	/* or'ed in, close on exec */
	P9_ORCLOSE	= 64,	/* or'ed in, remove on close */
	P9_ODIRECT	= 128,	/* or'ed in, direct access */
	P9_ONONBLOCK	= 256,	/* or'ed in, non-blocking call */
	P9_OEXCL	= 0x1000,	/* or'ed in, exclusive use (create only) */
	P9_OLOCK	= 0x2000,	/* or'ed in, lock after opening */
	P9_OAPPEND	= 0x4000	/* or'ed in, append only */
};

/* bits in IxpQid.type */
enum IxpQType {
	P9_QTDIR	= 0x80,	/* type bit for directories */
	P9_QTAPPEND	= 0x40,	/* type bit for append only files */
	P9_QTEXCL	= 0x20,	/* type bit for exclusive use files */
	P9_QTMOUNT	= 0x10,	/* type bit for mounted channel */
	P9_QTAUTH	= 0x08,	/* type bit for authentication file */
	P9_QTTMP	= 0x04,	/* type bit for non-backed-up file */
	P9_QTSYMLINK	= 0x02,	/* type bit for symbolic link */
	P9_QTFILE	= 0x00	/* type bits for plain file */
};

/* bits in IxpStat.mode */
enum IxpDMode {
	P9_DMEXEC	= 0x1,		/* mode bit for execute permission */
	P9_DMWRITE	= 0x2,		/* mode bit for write permission */
	P9_DMREAD	= 0x4,		/* mode bit for read permission */

#define P9_DMDIR	0x80000000	/* mode bit for directories */
#define P9_DMAPPEND	0x40000000	/* mode bit for append only files */
#define P9_DMEXCL	0x20000000	/* mode bit for exclusive use files */
#define P9_DMMOUNT	0x10000000	/* mode bit for mounted channel */
#define P9_DMAUTH	0x08000000	/* mode bit for authentication file */
#define P9_DMTMP	0x04000000	/* mode bit for non-backed-up file */
#define P9_DMSYMLINK	0x02000000	/* mode bit for symbolic link (Unix, 9P2000.u) */
#define P9_DMDEVICE	0x00800000	/* mode bit for device file (Unix, 9P2000.u) */
#define P9_DMNAMEDPIPE	0x00200000	/* mode bit for named pipe (Unix, 9P2000.u) */
#define P9_DMSOCKET	0x00100000	/* mode bit for socket (Unix, 9P2000.u) */
#define P9_DMSETUID	0x00080000	/* mode bit for setuid (Unix, 9P2000.u) */
#define P9_DMSETGID	0x00040000	/* mode bit for setgid (Unix, 9P2000.u) */
};

#ifdef IXP_NO_P9_
#  define TVersion P9_TVersion
#  define RVersion P9_RVersion
#  define TAuth P9_TAuth
#  define RAuth P9_RAuth
#  define TAttach P9_TAttach
#  define RAttach P9_RAttach
#  define TError P9_TError
#  define RError P9_RError
#  define TFlush P9_TFlush
#  define RFlush P9_RFlush
#  define TWalk P9_TWalk
#  define RWalk P9_RWalk
#  define TOpen P9_TOpen
#  define ROpen P9_ROpen
#  define TCreate P9_TCreate
#  define RCreate P9_RCreate
#  define TRead P9_TRead
#  define RRead P9_RRead
#  define TWrite P9_TWrite
#  define RWrite P9_RWrite
#  define TClunk P9_TClunk
#  define RClunk P9_RClunk
#  define TRemove P9_TRemove
#  define RRemove P9_RRemove
#  define TStat P9_TStat
#  define RStat P9_RStat
#  define TWStat P9_TWStat
#  define RWStat P9_RWStat
#
#  define OREAD P9_OREAD
#  define OWRITE P9_OWRITE
#  define ORDWR P9_ORDWR
#  define OEXEC P9_OEXEC
#  define OTRUNC P9_OTRUNC
#  define OCEXEC P9_OCEXEC
#  define ORCLOSE P9_ORCLOSE
#  define ODIRECT P9_ODIRECT
#  define ONONBLOCK P9_ONONBLOCK
#  define OEXCL P9_OEXCL
#  define OLOCK P9_OLOCK
#  define OAPPEND P9_OAPPEND
#
#  define QTDIR P9_QTDIR
#  define QTAPPEND P9_QTAPPEND
#  define QTEXCL P9_QTEXCL
#  define QTMOUNT P9_QTMOUNT
#  define QTAUTH P9_QTAUTH
#  define QTTMP P9_QTTMP
#  define QTSYMLINK P9_QTSYMLINK
#  define QTFILE P9_QTFILE
#  define DMDIR P9_DMDIR
#  define DMAPPEND P9_DMAPPEND
#  define DMEXCL P9_DMEXCL
#  define DMMOUNT P9_DMMOUNT
#  define DMAUTH P9_DMAUTH
#  define DMTMP P9_DMTMP
#
#  define DMSYMLINK P9_DMSYMLINK
#  define DMDEVICE P9_DMDEVICE
#  define DMNAMEDPIPE P9_DMNAMEDPIPE
#  define DMSOCKET P9_DMSOCKET
#  define DMSETUID P9_DMSETUID
#  define DMSETGID P9_DMSETGID
#endif

typedef struct IxpMap IxpMap;
typedef struct Ixp9Conn Ixp9Conn;
typedef struct Ixp9Req Ixp9Req;
typedef struct Ixp9Srv Ixp9Srv;
typedef struct IxpCFid IxpCFid;
typedef struct IxpClient IxpClient;
typedef struct IxpConn IxpConn;
typedef struct IxpFid IxpFid;
typedef struct IxpMsg IxpMsg;
typedef struct IxpQid IxpQid;
typedef struct IxpRpc IxpRpc;
typedef struct IxpServer IxpServer;
typedef struct IxpStat IxpStat;
typedef struct IxpTimer IxpTimer;

typedef struct IxpMutex IxpMutex;
typedef struct IxpRWLock IxpRWLock;
typedef struct IxpRendez IxpRendez;
typedef struct IxpThread IxpThread;

/* Threading */
enum {
	IXP_ERRMAX = IXP_MAX_ERROR,
};

struct IxpMutex {
	void*	aux;
};

struct IxpRendez {
	IxpMutex*	mutex;
	void*	aux;
};

struct IxpRWLock {
	void*	aux;
};

enum IxpMsgMode {
	MsgPack,
	MsgUnpack,
};
struct IxpMsg {
	char*	data; /* Begining of buffer. */
	char*	pos;  /* Current position in buffer. */
	char*	end;  /* End of message. */ 
	uint	size; /* Size of buffer. */
	uint	mode; /* MsgPack or MsgUnpack. */
};

struct IxpQid {
	uint8_t		type;
	uint32_t	version;
	uint64_t	path;
	/* Private members */
	uint8_t		dir_type;
};

/* stat structure */
struct IxpStat {
	uint16_t	type;
	uint32_t	dev;
	IxpQid		qid;
	uint32_t	mode;
	uint32_t	atime;
	uint32_t	mtime;
	uint64_t	length;
	char*	name;
	char*	uid;
	char*	gid;
	char*	muid;
};

typedef struct IxpFHdr		IxpFHdr;
typedef struct IxpFError	IxpFError;
typedef struct IxpFROpen	IxpFRAttach;
typedef struct IxpFRAuth	IxpFRAuth;
typedef struct IxpFROpen	IxpFRCreate;
typedef struct IxpFROpen	IxpFROpen;
typedef struct IxpFIO		IxpFRRead;
typedef struct IxpFRStat	IxpFRStat;
typedef struct IxpFVersion	IxpFRVersion;
typedef struct IxpFRWalk	IxpFRWalk;
typedef struct IxpFAttach	IxpFTAttach;
typedef struct IxpFAttach	IxpFTAuth;
typedef struct IxpFTCreate	IxpFTCreate;
typedef struct IxpFTFlush	IxpFTFlush;
typedef struct IxpFTCreate	IxpFTOpen;
typedef struct IxpFIO		IxpFTRead;
typedef struct IxpFVersion	IxpFTVersion;
typedef struct IxpFTWalk	IxpFTWalk;
typedef struct IxpFIO		IxpFTWrite;
typedef struct IxpFTWStat	IxpFTWStat;
typedef struct IxpFAttach	IxpFAttach;
typedef struct IxpFIO		IxpFIO;
typedef struct IxpFVersion	IxpFVersion;

struct IxpFHdr {
	uint8_t		type;
	uint16_t	tag;
	uint32_t	fid;
};
struct IxpFVersion {
	IxpFHdr		hdr;
	uint32_t	msize;
	char*		version;
};
struct IxpFTFlush {
	IxpFHdr		hdr;
	uint16_t	oldtag;
};
struct IxpFError {
	IxpFHdr		hdr;
	char*		ename;
};
struct IxpFROpen {
	IxpFHdr		hdr;
	IxpQid		qid; /* +Rattach */
	uint32_t	iounit;
};
struct IxpFRAuth {
	IxpFHdr		hdr;
	IxpQid		aqid;
};
struct IxpFAttach {
	IxpFHdr		hdr;
	uint32_t	afid;
	char*		uname;
	char*		aname;
};
struct IxpFTCreate {
	IxpFHdr		hdr;
	uint32_t	perm;
	char*		name;
	uint8_t		mode; /* +Topen */
};
struct IxpFTWalk {
	IxpFHdr	hdr;
	uint32_t	newfid;
	uint16_t	nwname;
	char*		wname[IXP_MAX_WELEM];
};
struct IxpFRWalk {
	IxpFHdr		hdr;
	uint16_t	nwqid;
	IxpQid		wqid[IXP_MAX_WELEM];
};
struct IxpFIO {
	IxpFHdr		hdr;
	uint64_t	offset; /* Tread, Twrite */
	uint32_t	count; /* Tread, Twrite, Rread */
	char*		data; /* Twrite, Rread */
};
struct IxpFRStat {
	IxpFHdr		hdr;
	uint16_t	nstat;
	uint8_t*	stat;
};
struct IxpFTWStat {
	IxpFHdr		hdr;
	IxpStat		stat;
};

#if IXP_NEEDAPI <= 89
/* from fcall(3) in plan9port */
typedef struct IxpFcall IxpFcall; /* Deprecated */
struct IxpFcall {		  /* Deprecated */
	uint8_t type;
	uint16_t tag;
	uint32_t fid;

	UNION (
		STRUCT ( /* Tversion, Rversion */
			uint32_t	msize;
			char	*version;
		)
		STRUCT ( /* Tflush */
			uint16_t	oldtag;
		)
		STRUCT ( /* Rerror */
			char	*ename;
		)
		STRUCT ( /* Ropen, Rcreate */
			IxpQid	qid; /* +Rattach */
			uint32_t	iounit;
		)
		STRUCT ( /* Rauth */
			IxpQid	aqid;
		)
		STRUCT ( /* Tauth, Tattach */
			uint32_t	afid;
			char	*uname;
			char	*aname;
		)
		STRUCT ( /* Tcreate */
			uint32_t	perm;
			char	*name;
			uint8_t	mode; /* +Topen */
		)
		STRUCT ( /* Twalk */
			uint32_t	newfid;
			uint16_t	nwname;
			char	*wname[IXP_MAX_WELEM];
		)
		STRUCT ( /* Rwalk */
			uint16_t	nwqid;
			IxpQid	wqid[IXP_MAX_WELEM];
		)
		STRUCT (
			uint64_t	offset; /* Tread, Twrite */
			uint32_t	count; /* Tread, Twrite, Rread */
			char	*data; /* Twrite, Rread */
		)
		STRUCT ( /* Rstat */
			uint16_t	nstat;
			char	*stat;
		)
		STRUCT ( /* Twstat */
			IxpStat	st;
		)
	)
};
#else
/**
 * Type: IxpFcall
 * Type: IxpFType
 * Type: IxpFAttach
 * Type: IxpFError
 * Type: IxpFHdr
 * Type: IxpFIO
 * Type: IxpFRAuth
 * Type: IxpFROpen
 * Type: IxpFRStat
 * Type: IxpFRWalk
 * Type: IxpFTCreate
 * Type: IxpFTFlush
 * Type: IxpFTWStat
 * Type: IxpFTWalk
 * Type: IxpFVersion
 *
 * The IxpFcall structure represents a 9P protocol message. The
 * P<hdr> element is common to all Fcall types, and may be used to
 * determine the type and tag of the message. The IxpFcall type is
 * used heavily in server applications, where it both presents a
 * request to handler functions and returns a response to the
 * client.
 *
 * Each member of the IxpFcall structure represents a certain
 * message type, which can be discerned from the P<hdr.type> field.
 * This value corresponds to one of the IxpFType constants. Types
 * with significant overlap use the same structures, thus TRead and
 * RWrite are both represented by IxpFIO and can be accessed via the
 * P<io> member as well as P<tread> and P<rwrite> respectively.
 *
 * See also:
 *	T<Ixp9Srv>, T<Ixp9Req>
 */
typedef union IxpFcall	IxpFcall;
union IxpFcall {
	IxpFHdr		hdr;
	IxpFVersion	version;
	IxpFVersion	tversion;
	IxpFVersion	rversion;
	IxpFTFlush	tflush;
	IxpFROpen	ropen;
	IxpFROpen	rcreate;
	IxpFROpen	rattach;
	IxpFError	error;
	IxpFRAuth	rauth;
	IxpFAttach	tattach;
	IxpFAttach	tauth;
	IxpFTCreate	tcreate;
	IxpFTCreate	topen;
	IxpFTWalk	twalk;
	IxpFRWalk	rwalk;
	IxpFTWStat	twstat;
	IxpFRStat	rstat;
	IxpFIO		twrite;
	IxpFIO		rwrite;
	IxpFIO		tread;
	IxpFIO		rread;
	IxpFIO		io;
};
#endif

#ifdef IXP_P9_STRUCTS
typedef IxpFcall	Fcall;
typedef IxpFid		Fid;
typedef IxpQid		Qid;
typedef IxpStat		Stat;
#endif

struct IxpConn {
	IxpServer*	srv;
	void*		aux;	/* Arbitrary pointer, to be used by handlers. */
	int		fd;	/* The file descriptor of the connection. */
	void		(*read)(IxpConn *);
	void		(*close)(IxpConn *);
	char		closed;	/* Non-zero when P<fd> has been closed. */

	/* Private members */
	IxpConn		*next;
};

struct IxpServer {
	IxpConn*	conn;
	IxpMutex	lk;
	IxpTimer*	timer;
	void		(*preselect)(IxpServer*);
	void*		aux;
	int		running;
	int		maxfd;
	fd_set		rd;
};

struct IxpRpc {
	IxpClient*	mux;
	IxpRpc*		next;
	IxpRpc*		prev;
	IxpRendez	r;
	uint		tag;
	IxpFcall*	p;
	int		waiting;
	int		async;
};

struct IxpClient {
	int	fd;
	uint	msize;
	uint	lastfid;

	/* Private members */
	uint		nwait;
	uint		mwait;
	uint		freetag;
	IxpCFid*	freefid;
	IxpMsg		rmsg;
	IxpMsg		wmsg;
	IxpMutex	lk;
	IxpMutex	rlock;
	IxpMutex	wlock;
	IxpRendez	tagrend;
	IxpRpc**	wait;
	IxpRpc*		muxer;
	IxpRpc		sleep;
	int		mintag;
	int		maxtag;
};

struct IxpCFid {
	uint32_t	fid;
	IxpQid		qid;
	uint8_t		mode;
	uint		open;
	uint		iounit;
	uint32_t	offset;
	IxpClient*	client;

	/* Private members */
	IxpCFid*	next;
	IxpMutex	iolock;
};

/**
 * Type: IxpFid
 *
 * Represents an open file for a 9P connection. The same
 * structure persists as long as the file remains open, and is
 * installed in the T<Ixp9Req> structure for any request Fcall
 * which references it. Handlers may use the P<aux> member to
 * store any data which must persist for the life of the open
 * file.
 *
 * See also:
 *	T<Ixp9Req>, T<IxpQid>, T<IxpOMode>
 */
struct IxpFid {
	char*		uid;	/* The uid of the file opener. */
	void*		aux;    /* Arbitrary pointer, to be used by handlers. */
	uint32_t		fid;    /* The ID number of the fid. */
	IxpQid		qid;    /* The filesystem-unique QID of the file. */
	signed char	omode;  /* The open mode of the file. */
	uint		iounit; /* The maximum size of any IO request. */

	/* Private members */
	Ixp9Conn*	conn;
	IxpMap*		map;
};

struct Ixp9Req {
	Ixp9Srv*	srv;
	IxpFid*		fid;    /* Fid structure corresponding to IxpFHdr.fid */
	IxpFid*		newfid; /* Corresponds to IxpFTWStat.newfid */
	Ixp9Req*	oldreq; /* For TFlush requests, the original request. */
	IxpFcall	ifcall; /* The incoming request fcall. */
	IxpFcall	ofcall; /* The response fcall, to be filled by handler. */
	void*		aux;    /* Arbitrary pointer, to be used by handlers. */

	/* Private members */
	Ixp9Conn *conn;
};

struct Ixp9Srv {
	void* aux;
	void (*attach)(Ixp9Req*);
	void (*clunk)(Ixp9Req*);
	void (*create)(Ixp9Req*);
	void (*flush)(Ixp9Req*);
	void (*open)(Ixp9Req*);
	void (*read)(Ixp9Req*);
	void (*remove)(Ixp9Req*);
	void (*stat)(Ixp9Req*);
	void (*walk)(Ixp9Req*);
	void (*write)(Ixp9Req*);
	void (*wstat)(Ixp9Req*);
	void (*freefid)(IxpFid*);
};

/**
 * Type: IxpThread
 * Type: IxpMutex
 * Type: IxpRWLock
 * Type: IxpRendez
 * Variable: ixp_thread
 *
 * The IxpThread structure is used to adapt libixp to any of the
 * myriad threading systems it may be used with. Before any
 * other of libixp's functions is called, ixp_thread may be set
 * to a structure filled with implementations of various locking
 * primitives, along with primitive IO functions which may
 * perform context switches until data is available.
 *
 * The names of the functions should be fairly self-explanitory.
 * Read/write locks should allow multiple readers and a single
 * writer of a shared resource, but should not allow new readers
 * while a writer is waitng for a lock. Mutexes should allow
 * only one accessor at a time. Rendezvous points are similar to
 * pthread condition types. P<errbuf> should return a
 * thread-local buffer or the size IXP_ERRMAX.
 *
 * See also:
 *	F<ixp_pthread_init>, F<ixp_taskinit>, F<ixp_rubyinit>
 */
struct IxpThread {
	/* Read/write lock */
	int	(*initrwlock)(IxpRWLock*);
	void	(*rlock)(IxpRWLock*);
	int	(*canrlock)(IxpRWLock*);
	void	(*runlock)(IxpRWLock*);
	void	(*wlock)(IxpRWLock*);
	int	(*canwlock)(IxpRWLock*);
	void	(*wunlock)(IxpRWLock*);
	void	(*rwdestroy)(IxpRWLock*);
	/* Mutex */
	int	(*initmutex)(IxpMutex*);
	void	(*lock)(IxpMutex*);
	int	(*canlock)(IxpMutex*);
	void	(*unlock)(IxpMutex*);
	void	(*mdestroy)(IxpMutex*);
	/* Rendezvous point */
	int	(*initrendez)(IxpRendez*);
	void	(*sleep)(IxpRendez*);
	int	(*wake)(IxpRendez*);
	int	(*wakeall)(IxpRendez*);
	void	(*rdestroy)(IxpRendez*);
	/* Other */
	char*	(*errbuf)(void);
	ssize_t	(*read)(int, void*, size_t);
	ssize_t	(*write)(int, const void*, size_t);
	int	(*select)(int, fd_set*, fd_set*, fd_set*, struct timeval*);
};

extern IxpThread*	ixp_thread;
extern int	(*ixp_vsnprint)(char *buf, int nbuf, const char *fmt, va_list);
extern char*	(*ixp_vsmprint)(const char *fmt, va_list);
extern void	(*ixp_printfcall)(IxpFcall*);

/* thread_*.c */
int ixp_taskinit(void);
int ixp_rubyinit(void);
int ixp_pthread_init(void);

#ifdef VARARGCK
#  pragma varargck	argpos	ixp_print	2
#  pragma varargck	argpos	ixp_werrstr	1
#  pragma varargck	argpos	ixp_eprint	1
#endif

/* client.c */
int	ixp_close(IxpCFid*);
long	ixp_pread(IxpCFid*, void*, long, int64_t);
int	ixp_print(IxpCFid*, const char*, ...);
long	ixp_pwrite(IxpCFid*, const void*, long, int64_t);
long	ixp_read(IxpCFid*, void*, long);
int	ixp_remove(IxpClient*, const char*);
void	ixp_unmount(IxpClient*);
int	ixp_vprint(IxpCFid*, const char*, va_list);
long	ixp_write(IxpCFid*, const void*, long);
IxpCFid*	ixp_create(IxpClient*, const char*, uint perm, uint8_t mode);
IxpStat*	ixp_fstat(IxpCFid*);
IxpClient*	ixp_mount(const char*);
IxpClient*	ixp_mountfd(int);
IxpClient*	ixp_nsmount(const char*);
IxpCFid*	ixp_open(IxpClient*, const char*, uint8_t);
IxpStat*	ixp_stat(IxpClient*, const char*);

/* convert.c */
void ixp_pu8(IxpMsg*, uint8_t*);
void ixp_pu16(IxpMsg*, uint16_t*);
void ixp_pu32(IxpMsg*, uint32_t*);
void ixp_pu64(IxpMsg*, uint64_t*);
void ixp_pdata(IxpMsg*, char**, uint);
void ixp_pstring(IxpMsg*, char**);
void ixp_pstrings(IxpMsg*, uint16_t*, char**, uint);
void ixp_pqid(IxpMsg*, IxpQid*);
void ixp_pqids(IxpMsg*, uint16_t*, IxpQid*, uint);
void ixp_pstat(IxpMsg*, IxpStat*);
void ixp_pfcall(IxpMsg*, IxpFcall*);

/* error.h */
char*	ixp_errbuf(void);
void	ixp_errstr(char*, int);
void	ixp_rerrstr(char*, int);
void	ixp_werrstr(const char*, ...);

/* request.c */
void ixp_respond(Ixp9Req*, const char *err);
void ixp_serve9conn(IxpConn*);

/* message.c */
uint16_t	ixp_sizeof_stat(IxpStat*);
IxpMsg	ixp_message(char*, uint len, uint mode);
void	ixp_freestat(IxpStat*);
void	ixp_freefcall(IxpFcall*);
uint	ixp_msg2fcall(IxpMsg*, IxpFcall*);
uint	ixp_fcall2msg(IxpMsg*, IxpFcall*);

/* server.c */
IxpConn* ixp_listen(IxpServer*, int, void*,
		void (*read)(IxpConn*),
		void (*close)(IxpConn*));
void	ixp_hangup(IxpConn*);
int	ixp_serverloop(IxpServer*);
void	ixp_server_close(IxpServer*);

/* socket.c */
int ixp_dial(const char*);
int ixp_announce(const char*);

/* transport.c */
uint ixp_sendmsg(int, IxpMsg*);
uint ixp_recvmsg(int, IxpMsg*);

/* timer.c */
long	ixp_msec(void);
long	ixp_settimer(IxpServer*, long, void (*)(long, void*), void*);
int	ixp_unsettimer(IxpServer*, long);

/* util.c */
void	ixp_cleanname(char*);
void*	ixp_emalloc(uint);
void*	ixp_emallocz(uint);
void	ixp_eprint(const char*, ...);
void*	ixp_erealloc(void*, uint);
char*	ixp_estrdup(const char*);
char*	ixp_namespace(void);
char*	ixp_smprint(const char*, ...);
uint	ixp_strlcat(char*, const char*, uint);
uint	ixp_tokenize(char**, uint len, char*, char);

