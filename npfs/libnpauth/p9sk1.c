
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
  #include <ws2tcpip.h>
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
#endif
#include <errno.h>
#include "npfs.h"
#include "npclient.h"
#include "npauth.h"

#include "npaimpl.h"

static int
con(struct addrinfo *srv)
{
	int s;

	s = socket(srv->ai_family, srv->ai_socktype, 0);
	if(s == -1)
		return -1;
	if (connect(s, srv->ai_addr, sizeof(*srv->ai_addr)) < 0) {
		close(s);
		return -1;
	}
	return s;
}

static int
getTicket(Npcauth *auth, struct ticketreq *treq, char ctick[72], char stick[72])
{
	char treqbuf[141];
	int fd;
	char type;

	fd = con(auth->srv);
	if(fd == -1)
		return err("cant reach auth", EPERM);

	// XXX do we need to handle other type replies?
	if(encTicketReq(treqbuf, treq) == -1
	|| send(fd, treqbuf, sizeof treqbuf, 0) != sizeof treqbuf
	|| recv(fd, &type, 1, 0) != 1
	|| type != AuthOK
	|| recv(fd, ctick, 72, 0) != 72
	|| recv(fd, stick, 72, 0) != 72) {
		close(fd);
		return err("botch", EINVAL);
	}
	close(fd);
	return 0;
}

int
srvp9sk1(struct npsrvauth *a, char *msg, int len, char *resp, int resplen)
{
	struct ticketreq treq;
	struct auth auth;
	struct ticket tick;

	switch(a->state) {
	case 0:
		// state 0 is reserved for cases where server speaks first
		if(len != 0)
			return err("botch", EINVAL);
		a->state++;
		return 0;

	case 1:
		if(len != 8)
			return err("botch", EINVAL);
		memcpy(a->chc, msg, 8);
		getRand(a->chs, 8);

		treq.type = AuthTreq;
		treq.ids = a->ids;
		treq.dom = a->dom;
		treq.ch = a->chs;
		treq.idc = "";
		treq.idr = "";
		if(resplen < 141
		|| encTicketReq(resp, &treq) == -1)
			return err("internal error", EINVAL);
		a->state++;
		return 141;

	case 2:
		if(len != 85)
			return err("botch", EINVAL);
		if(decTicket(msg, &tick, a->key) == -1
		|| tick.type != AuthTs
		|| memcmp(tick.ch, a->chs, 8) != 0
		|| decAuth(msg+72, &auth, tick.key) == -1
		|| auth.type != AuthAc
		|| memcmp(auth.ch, a->chs, 8) != 0
		|| auth.gen != a->gen)
			return err("bad auth", EPERM);
		
		a->idc = strdup(tick.idc);
		a->idr = strdup(tick.idr);
		auth.type = AuthAs;
		memcpy(auth.ch, a->chc, 8);
		auth.gen = a->gen;
		if(resplen < 13 
		|| encAuth(resp, &auth, tick.key) == -1)
			return err("internal error", EINVAL);
		a->state++;
		a->done = 1;
		return 13;

	default:
		return err("botch", EINVAL);
	}
}

int
authp9sk1(Npcfid *afid, Npuser *user, void *aux)
{
	char treqbuf[141], ctickbuf[72], stickbuf[72];
	char authbuf[85], chc[8];
	struct ticketreq treq;
	struct ticket ctick;
	struct auth auth;
	struct npcauth *a = (struct npcauth *)aux;

	// C->S CHc
	getRand(chc, 8);
	if(put(afid, chc, 8) <= 0)
		return err("botch", EINVAL);

	// S->C AuthTreq, IDs, DN, DHs, -, -
	if(get(afid, treqbuf, 141) <= 0
	|| decTicketReq(treqbuf, &treq) == -1)
		return err("botch", EINVAL);
	if(treq.type != AuthTreq)
		return err("botch", EINVAL);

	// forward ticket request to authserver to get tickets.
	treq.idc = user->uname;
	treq.idr = user->uname;
	if(getTicket(a, &treq, ctickbuf, stickbuf) == -1)
		return -1;
	if(decTicket(ctickbuf, &ctick, a->key) == -1)
		return err("botch", EINVAL);
	if(memcmp(treq.ch, ctick.ch, 8) != 0)
		return err("bad auth", EPERM);

	// C->S Ks{AuthTs, CHs, IDc, IDr, Kn}, Kn{AuthAc, CHs}
	auth.type = AuthAc;
	auth.ch = treq.ch;
	auth.gen = a->gen;
	memcpy(authbuf, stickbuf, 72);
	if(encAuth(authbuf+72, &auth, ctick.key) == -1
	|| put(afid, authbuf, sizeof authbuf) == -1)
		return err("botch", EINVAL);

	// S->C Kn{AuthAs, Chc}
	if(get(afid, authbuf, 13) == -1
	|| decAuth(authbuf, &auth, ctick.key))
		return err("botch", EINVAL);
	if(auth.type != AuthAs
	|| memcmp(auth.ch, chc, 8) != 0
	|| auth.gen != a->gen)
		return err("bad server", EPERM);

	a->gen++;
	return 0;
}
