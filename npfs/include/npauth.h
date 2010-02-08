
struct addrinfo;

// client state during auth
typedef struct npcauth {
	struct addrinfo *srv;
	char key[7];
	int gen;
} Npcauth;

// server state during auth
typedef struct npsrvauth {
	int state;
	int gen;
	int done;
	char chc[8], chs[8];
	char *ids, *dom, *key;	// prefilled server identity
	char *idc, *idr;	// auth information, dynamic alloc'd
} Npsrvauth;

extern char *srvid;         // fill with server's username
extern char *srvdom;        // fill with server's domain
extern char srvkey[7];      // fill with server's key

extern Npauth srvauthp9any;
extern Npauth srvauthp9sk1;

// aux must be an npcauth structure.
int authp9any(Npcfid *fid, Npuser *user, void *aux);
int authp9sk1(Npcfid *fid, Npuser *user, void *aux);

int srvp9any(struct npsrvauth *a, char *msg, int len, char *resp, int resplen);
int srvp9sk1(struct npsrvauth *a, char *msg, int len, char *resp, int resplen);

void makeKey(char *pw, char *key);

