#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "npfs.h"
#include "npclient.h"
#include "npauth.h"

#include "npaimpl.h"
#include "ossl.h"

void
setKey(unsigned char *key, DES_key_schedule *sched)
{
	DES_cblock kb;

	// expand 7 bytes to 8 and add parity in low bit
	kb[0] = key[0];
	kb[1] = (key[0] << 7) | (key[1] >> 1);
	kb[2] = (key[1] << 6) | (key[2] >> 2);
	kb[3] = (key[2] << 5) | (key[3] >> 3);
	kb[4] = (key[3] << 4) | (key[4] >> 4);
	kb[5] = (key[4] << 3) | (key[5] >> 5);
	kb[6] = (key[5] << 2) | (key[6] >> 6);
	kb[7] = (key[6] << 1);
	DES_set_odd_parity(&kb);
	DES_set_key_checked(&kb, sched);
}


int
_encrypt(unsigned char *buf, int n, char *key)
{
	DES_key_schedule sched;
	int i, r;

	if(n < 8)
		return -1;
	setKey((unsigned char *)key, &sched);
	n--;
	r = n % 7;
	n /= 7;
	for(i = 0; i < n; i++) {
		DES_ecb_encrypt((DES_cblock *)buf, (DES_cblock *)buf, &sched, DES_ENCRYPT);
		buf += 7;
	}
	if(r)
		DES_ecb_encrypt((DES_cblock *)(buf - 7 + r), (DES_cblock *)(buf - 7 + r), &sched, DES_ENCRYPT);
	return 0;
}

int
_decrypt(unsigned char *buf, int n, char *key)
{
	DES_key_schedule sched;
	int i, r;

	if(n < 8)
		return -1;
	setKey((unsigned char *)key, &sched);
	n --;
	r = n % 7;
	n /= 7;

	buf += n*7;
	if(r)
		DES_ecb_encrypt((DES_cblock *)(buf - 7 + r), (DES_cblock *)(buf - 7 + r), &sched, DES_DECRYPT);
	for(i = 0; i < n; i++) {
		buf -= 7;
		DES_ecb_encrypt((DES_cblock *)buf, (DES_cblock *)buf, &sched, DES_DECRYPT);
	}
	return 0;
}

void
makeKey(char *pw, char *key)
{
	char pwbuf[28];
	unsigned char *t;
	int n, i;

	// always at least 8 characters. NUL terminated before 28 chars
	n = strlen(pw);
	if(n > 27)
		n = 27;
	memset(pwbuf, ' ', 8);
	strncpy(pwbuf, pw, n);
	pwbuf[n] = 0;

	t = (unsigned char *)pwbuf;
	for(;;) {
		for(i = 0; i < 7; i++)
			key[i] = (t[i] >> i) + (t[i+1] << (8 - (i+1)));
		if(n <= 8)
			return;

		n -= 8;
		t += 8;
		if(n < 8) {
			t -= 8 - n;
			n = 8;
		}
		// encrypt next 8, or last 8, with pw.
		_encrypt(t, 8, key);
	}
}

static int
dec4(char **buf, int *x)
{
	char *p = *buf;

	*x = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
	*buf += 4;
	return 0;
}

static int
enc4(char **buf, int x)
{
	char *p = *buf;

	p[0] = x >> 24;
	p[1] = x >> 16;
	p[2] = x >> 8;
	p[3] = x;
	*buf += 4;
	return 0;
}

static int
decFixed(char **buf, int sz, char **r)
{
	*r = *buf;
	*buf += sz;
	return 0;
}

static int
encFixed(char **buf, int sz, char *r)
{
	memcpy(*buf, r, sz);
	*buf += sz;
	return 0;
}

static int
decPad(char **buf, int sz, char **r)
{
	char *p;

	// We give up the ability to decode all possible strings
	// in exchange for the ability to decode in-place.  Our max
	// length is one shorter than what the protocol may dictate.
	// This is unlikely to be a problem.
	*r = *buf;
	p = *buf;
	*buf += sz;
	while(*p && sz) {
		p++;
		sz--;
	}
	if(*p)
		return -1;
	return 0;
}

static int
encPad(char **buf, int sz, char *r)
{
	int l;

	l = strlen(r);
	if(l >= sz)
		return -1;
	strcpy(*buf, r);
	memset(*buf+l, 0, sz-l);
	*buf += sz;
	return 0;
}

int
decTicketReq(char *buf, struct ticketreq *r)
{
	r->type = *buf++;
	if(decPad(&buf, 28, &r->ids) == -1
	|| decPad(&buf, 48, &r->dom) == -1
	|| decFixed(&buf, 8, &r->ch) == -1
	|| decPad(&buf, 28, &r->idc) == -1
	|| decPad(&buf, 28, &r->idr) == -1)
		return -1;
	return 0;
}

int
encTicketReq(char *buf, struct ticketreq *r)
{
	*buf++ = r->type;
	if(encPad(&buf, 28, r->ids) == -1
	|| encPad(&buf, 48, r->dom) == -1
	|| encFixed(&buf, 8, r->ch) == -1
	|| encPad(&buf, 28, r->idc) == -1
	|| encPad(&buf, 28, r->idr) == -1)
		return -1;
	return 0;
}

int
decTicket(char *buf, struct ticket *r, char *key)
{
	_decrypt((unsigned char *)buf, 72, key);
	r->type = *buf++;
	if(decFixed(&buf, 8, &r->ch) == -1
	|| decPad(&buf, 28, &r->idc) == -1
	|| decPad(&buf, 28, &r->idr) == -1
	|| decFixed(&buf, 7, &r->key) == -1)
		return -1;
	return 0;
}

int
encTicket(char *buf, struct ticket *r, char *key)
{
    char *p = buf;
	*p++ = r->type;
	if(encFixed(&p, 8, r->ch) == -1
	|| encPad(&p, 28, r->idc) == -1
	|| encPad(&p, 28, r->idr) == -1
	|| encFixed(&p, 7, r->key) == -1)
		return -1;
	_encrypt((unsigned char *)buf, 72, key);
	return 0;
}

int
decAuth(char *buf, struct auth *r, char *key)
{
	_decrypt((unsigned char *)buf, 13, key);
	r->type = *buf++;
	if(decFixed(&buf, 8, &r->ch) == -1
	|| dec4(&buf, &r->gen) == -1)
		return -1;
	return 0;
}

int
encAuth(char *buf, struct auth *r, char *key)
{
	char *p = buf;

	*p++ = r->type;
	if(encFixed(&p, 8, r->ch) == -1
	|| enc4(&p, r->gen) == -1)
		return -1;
	_encrypt((unsigned char *)buf, 13, key);
	return 0;
}

void
getRand(char *buf, int sz)
{
	int x;

	x = RAND_bytes((unsigned char *)buf, sz);
	assert(x != 0);
}

