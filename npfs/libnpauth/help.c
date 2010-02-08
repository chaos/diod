#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "npfs.h"
#include "npclient.h"
#include "npauth.h"

#include "npaimpl.h"

int
get(Npcfid *fid, char *buf, int sz)
{
	int n;

	n = npc_read(fid, (u8*)buf, sz, fid->offset);
	if(n > 0) {
		if(n != sz)
			return -1;
		fid->offset += n;
	}
	return n;
}

// read NUL-terminated line, expected in a single read operation.
int
getline0(Npcfid *fid, char *buf, int sz)
{
	int n;

	n = npc_read(fid, (u8*)buf, sz, fid->offset);
	if(n <= 0 || buf[n-1] != '\0' || strlen(buf) != n-1)
		return -1;
	if(n > 0)
		fid->offset += n;
	return n;
}

int
put(Npcfid *fid, char *buf, int sz)
{
	int n;

	n = npc_write(fid, (u8*)buf, sz, fid->offset);
	if(n >= 0) {
		if(n != sz)
			return -1;
		fid->offset += n;
	}
	return n;
}

int
putline0(Npcfid *fid, char *fmt, ...)
{
	char buf[128];
	int l;
	va_list ap;

	va_start(ap, fmt);
	l = vsnprintf(buf, sizeof buf - 1, fmt, ap);
	if(l >= sizeof buf - 1)
		return -1;
	va_end(ap);
	return put(fid, buf, l+1);
}

int 
err(char *msg, int no)
{
	np_werror(msg, no);
	return -1;
}

int
getWord(char **buf, char sep, char **retbuf)
{
	char *p;

	p = NULL;
	if(sep)
		p = strchr(*buf, sep);
	if(!p)
		p = *buf + strlen(*buf);
	*retbuf = *buf;
	if(*p)
		*p++ = 0;
	*buf = p;
	return 0;
}

