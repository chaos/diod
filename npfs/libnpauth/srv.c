
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "npfs.h"
#include "npclient.h"
#include "npauth.h"

#include "npaimpl.h"

// to be filled in by API client.
char *srvid = "nobody";;
char *srvdom = "@nowhere.com";
char srvkey[7] = { 0 };

typedef int srvfunc(struct npsrvauth *a, char *msg, int len, char *resp, int resplen);

struct authstate {
	struct npsrvauth state;
	srvfunc *srv;
	char readbuf[256];
	int readcnt;
};

struct authstate *
newAuthState(srvfunc *srv)
{
	struct authstate *a;

	a = malloc(sizeof *a);
	if(a) {
		memset(a, 0, sizeof *a);
		a->srv = srv;
		a->state.ids = srvid;
		a->state.dom = srvdom;
		a->state.key = srvkey;
		a->readcnt = a->srv(&a->state, NULL, 0, a->readbuf, sizeof a->readbuf);
	}
	return a;
}

static int
startp9any(Npfid *afid, char *aname, Npqid *aqid)
{
	afid->aux = newAuthState(srvp9any);
	aqid->type = Qtauth;
	aqid->version = 0;
	aqid->path = 1;
	return 1;
}

static int
startp9sk1(Npfid *afid, char *aname, Npqid *aqid)
{
	afid->aux = newAuthState(srvp9sk1);
	return 1;
}

static int
checkauth(Npfid *fid, Npfid *afid, char *aname)
{
	if(afid) {
		struct authstate *a = (struct authstate *)afid->aux;

		if(a->state.done
		&& strcmp(fid->user->uname, a->state.idr) == 0)
			return 1;
	}
	return err("bad auth", EPERM);
}

static int
readauth(Npfid *fid, u64 off, u32 cnt, u8 *data)
{
	struct authstate *a = (struct authstate *)fid->aux;
	int n;

	if(a->readcnt == 0 || cnt < a->readcnt)
		return err("botch", EIO);
	n = a->readcnt;
	memcpy(data, a->readbuf, n);
	a->readcnt = 0;
	return n;
}

static int
writeauth(Npfid *fid, u64 off, u32 cnt, u8 *data)
{
	struct authstate *a = (struct authstate *)fid->aux;
	int n;

	if(a->state.done || a->readcnt > 0)
		return err("botch", EIO);
	n = a->srv(&a->state, (char *)data, cnt, a->readbuf, sizeof a->readbuf);
	if(n < 0)
		return 0;
	a->readcnt = n;
	return cnt;
}

static int
clunkauth(Npfid *fid)
{
	struct authstate *a = (struct authstate *)fid->aux;

	if(a) {
		if(a->state.idc)
			free(a->state.idc);
		if(a->state.idr)
			free(a->state.idr);
		free(a);
	}
	fid->aux = NULL;
	return 1;
}

struct Npauth srvauthp9any = {
	startp9any,
	checkauth,
	readauth,
	writeauth,
	clunkauth
};

struct Npauth srvauthp9sk1 = {
	startp9sk1,
	checkauth,
	readauth,
	writeauth,
	clunkauth
};
