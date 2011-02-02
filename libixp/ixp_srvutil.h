
typedef struct IxpDirtab	IxpDirtab;
typedef struct IxpFileId	IxpFileId;
typedef struct IxpPendingLink	IxpPendingLink;
typedef struct IxpPending	IxpPending;
typedef struct IxpQueue		IxpQueue;
typedef struct IxpRequestLink	IxpRequestLink;

typedef IxpFileId* (*IxpLookupFn)(IxpFileId*, char*);

struct IxpPendingLink {
	/* Private members */
	IxpPendingLink*	next;
	IxpPendingLink*	prev;
	IxpFid*		fid;
	IxpQueue*	queue;
	IxpPending*	pending;
};

struct IxpRequestLink {
	/* Private members */
	IxpRequestLink*	next;
	IxpRequestLink*	prev;
	Ixp9Req*	req;
};

struct IxpPending {
	/* Private members */
	IxpRequestLink	req;
	IxpPendingLink	fids;
};

struct IxpDirtab {
	char*	name;
	uint8_t	qtype;
	uint	type;
	uint	perm;
	uint	flags;
};

struct IxpFileId {
	IxpFileId*	next;
	IxpFileIdU	p;
	bool		pending;
	uint		id;
	uint		index;
	IxpDirtab	tab;
	uint		nref;
	char		volatil;
};

enum {
	FLHide = 1,
};

bool	ixp_pending_clunk(Ixp9Req*);
void	ixp_pending_flush(Ixp9Req*);
int	ixp_pending_print(IxpPending*, const char*, ...);
void	ixp_pending_pushfid(IxpPending*, IxpFid*);
void	ixp_pending_respond(Ixp9Req*);
int	ixp_pending_vprint(IxpPending*, const char*, va_list ap);
void	ixp_pending_write(IxpPending*, const char*, long);
IxpFileId*	ixp_srv_clonefiles(IxpFileId*);
void	ixp_srv_data2cstring(Ixp9Req*);
void	ixp_srv_freefile(IxpFileId*);
void	ixp_srv_readbuf(Ixp9Req*, char*, uint);
void	ixp_srv_readdir(Ixp9Req*, IxpLookupFn, void (*)(IxpStat*, IxpFileId*));
bool	ixp_srv_verifyfile(IxpFileId*, IxpLookupFn);
void	ixp_srv_walkandclone(Ixp9Req*, IxpLookupFn);
void	ixp_srv_writebuf(Ixp9Req*, char**, uint*, uint);
char*	ixp_srv_writectl(Ixp9Req*, char* (*)(void*, IxpMsg*));
IxpFileId* ixp_srv_getfile(void);


