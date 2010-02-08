
enum {
	AuthTreq = 1, AuthChal, AuthPass, AuthOK, AuthErr, AuthMod,
	AuthTs = 64, AuthTc, AuthAs, AuthAc, AuthTp, AuthHr,
};

struct DES_ks;

struct ticketreq {
	char type;
	char *ids;
	char *dom;
	char *ch;
	char *idc;
	char *idr;
};

struct ticket {
	char type;
	char *ch;
	char *idc;
	char *idr;
	char *key;
};

struct auth {
	char type;
	char *ch;
	int gen;
};

void setKey(unsigned char *key, struct DES_ks *sched);
int _encrypt(unsigned char *buf, int n, char *key);
int _decrypt(unsigned char *buf, int n, char *key);
void getRand(char *buf, int sz);

int decTicketReq(char *buf, struct ticketreq *r);
int encTicketReq(char *buf, struct ticketreq *r);
int decTicket(char *buf, struct ticket *r, char *key);
int encTicket(char *buf, struct ticket *r, char *key);
int decAuth(char *buf, struct auth *r, char *key);
int encAuth(char *buf, struct auth *r, char *key);

int get(Npcfid *fid, char *buf, int sz);
int getline0(Npcfid *fid, char *buf, int sz);
int put(Npcfid *fid, char *buf, int sz);
int putline0(Npcfid *fid, char *fmt, ...);
int err(char *msg, int no);
int getWord(char **buf, char sep, char **retbuf);

