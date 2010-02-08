
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "npfs.h"
#include "npclient.h"
#include "npauth.h"

#include "npaimpl.h"

#ifdef _WIN32
#define EPROTONOSUPPORT 93 // XXX
#endif

struct authproto {
	char *name;
	int (*auth)(Npcfid *fid, Npuser *user, void *aux);
};

static struct authproto authprotos[] = {
	{ "p9sk1", authp9sk1 },
	{ 0, 0 },
};

int
srvp9any(struct npsrvauth *a, char *msg, int len, char *resp, int resplen)
{
	char *proto, *dom;
	int r;

	switch(a->state) {
	case 0:
		if(len != 0
		|| resplen < strlen(a->dom) + 32)
			return err("internal error", EINVAL);
		sprintf(resp, "v.2 p9sk1@%s", a->dom);
		a->state++;
		return strlen(resp) + 1;

	case 1:
		if(len == 0 
		|| msg[len-1] != '\0'
		|| strlen(msg) != len-1
		|| getWord(&msg, ' ', &proto) == -1
		|| getWord(&msg, 0, &dom) == -1
		|| strcmp(dom, a->dom) != 0)
			return err("botch", EINVAL);
		if(strcmp(proto, "p9sk1") != 0)
			return err("unsupported", EPROTONOSUPPORT);
		if(resplen < 32)
			return err("internal error", EINVAL);
		strcpy(resp, "OK");
		a->state++;
		return strlen(resp) + 1;

	default:
		a->state -= 1; // map state [2..] to [1..] (skip state 0)
		r = srvp9sk1(a, msg, len, resp, resplen);
		a->state += 1;
		return r;
	}
}


int
authp9any(Npcfid *afid, Npuser *user, void *aux)
{
	char buf[128], *p, *word, *proto, *dom;
	int (*found)(Npcfid *afid, Npuser *user, void *aux);
	int v2, i;

	if(getline0(afid, buf, sizeof buf) <= 0)
		return err("botch", EINVAL);

	p = buf;
	found = 0;
	v2 = 0;
	if(strncmp(p, "v.2 ", 4) == 0) {
		v2 = 1;
		p += 4;
	}
	while(*p && !found) {
		if(getWord(&p, ' ', &word) == -1
		|| getWord(&word, '@', &proto) == -1
		|| getWord(&word, 0, &dom) == -1)
			return err("botch", EINVAL);
		for(i = 0; authprotos[i].name; i++) {
			if(strcmp(authprotos[i].name, "p9sk1") == 0) {
				found = authprotos[i].auth;
				break;
			}
		}
	}
	if(!found)
		return err("unsupported", EPROTONOSUPPORT);

	if(putline0(afid, "%s %s", proto, dom) <= 0)
		return err("botch", EINVAL);

	if(v2) {
		if(getline0(afid, buf, sizeof buf) <= 0)
			return err("botch", EINVAL);
		if(strcmp(buf, "OK") != 0)
			return err("botch", EINVAL);
	}
	return found(afid, user, aux);
}

