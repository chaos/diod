/* 
   dbench version 2
   Copyright (C) Andrew Tridgell 1999
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "dbench.h"

#define MAX_FILES 1000

struct sockio {
	char buf[70000];
	int sock;
};

/* emulate a single SMB packet exchange */
static void do_packets(struct child_struct *child, int send_size, int recv_size)
{
	struct sockio *sockio = (struct sockio *)child->private;
	uint32 *ubuf = (uint32 *)sockio->buf;

	ubuf[0] = htonl(send_size-4);
	ubuf[1] = htonl(recv_size-4);

	if (write_sock(sockio->sock, sockio->buf, send_size) != send_size) {
		printf("error writing %d bytes\n", (int)send_size);
		exit(1);
	}

	if (read_sock(sockio->sock, sockio->buf, 4) != 4) {
		printf("error reading header\n");
		exit(1);
	}

	if (ntohl(ubuf[0]) != (unsigned)(recv_size-4)) {
		printf("lost sync (%d %d)\n", 
		       (int)recv_size-4, (int)ntohl(ubuf[0]));
		exit(1);
	}

	if (recv(sockio->sock, sockio->buf, recv_size-4, MSG_WAITALL|MSG_TRUNC) != 
	    recv_size-4) {
		printf("error reading %d bytes\n", (int)recv_size-4);
		exit(1);
	}

	if (ntohl(ubuf[0]) != (unsigned)(recv_size-4)) {
		printf("lost sync (%d %d)\n", 
		       (int)recv_size-4, (int)ntohl(ubuf[0]));
	}
}


void nb_setup(struct child_struct *child)
{
	struct sockio *sockio;
	sockio = calloc(1, sizeof(struct sockio));
	child->private = sockio;
	child->rate.last_time = timeval_current();
	child->rate.last_bytes = 0;
	
	sockio->sock = open_socket_out(options.server, TCP_PORT);
	if (sockio->sock == -1) {
		printf("client %d failed to start\n", child->id);
		exit(1);
	}
	set_socket_options(sockio->sock, options.tcp_options);

	do_packets(child, 8, 8);
}


void nb_unlink(struct child_struct *child, const char *fname, int attr, const char *status)
{
	(void)child;
	(void)attr;
	(void)status;
        do_packets(child, 39+2+strlen(fname)*2+2, 39);
}

void nb_mkdir(struct child_struct *child, const char *dname, const char *status)
{
	(void)child;
	(void)status;
        do_packets(child, 39+2+strlen(dname)*2+2, 39);
}

void nb_rmdir(struct child_struct *child, const char *fname, const char *status)
{
	(void)child;
	(void)status;
        do_packets(child, 39+2+strlen(fname)*2+2, 39);
}

void nb_createx(struct child_struct *child, const char *fname, 
		uint32_t create_options, uint32_t create_disposition, int fnum,
		const char *status)
{
	(void)child;
	(void)create_options;
	(void)create_disposition;
	(void)fnum;
	(void)status;
        do_packets(child, 70+2+strlen(fname)*2+2, 39+12*4);
}

void nb_writex(struct child_struct *child, int handle, int offset, 
	       int size, int ret_size, const char *status)
{
	(void)child;
	(void)handle;
	(void)offset;
	(void)ret_size;
	(void)status;
        do_packets(child, 39+20+size, 39+16);
	child->bytes += size;
}

void nb_readx(struct child_struct *child, int handle, int offset, 
	      int size, int ret_size, const char *status)
{
	(void)child;
	(void)handle;
	(void)offset;
	(void)size;
	(void)status;
        do_packets(child, 39+20, 39+20+ret_size);
	child->bytes += ret_size;
}

void nb_close(struct child_struct *child, int handle, const char *status)
{
	(void)child;
	(void)handle;
	(void)status;
        do_packets(child, 39+8, 39);
}

void nb_rename(struct child_struct *child, const char *old, const char *new, const char *status)
{
	(void)child;
	(void)status;
        do_packets(child, 39+8+2*strlen(old)+2*strlen(new), 39);
}

void nb_flush(struct child_struct *child, int handle, const char *status)
{
	(void)child;
	(void)handle;
	(void)status;
        do_packets(child, 39+2, 39);
}

void nb_qpathinfo(struct child_struct *child, const char *fname, int level, 
		  const char *status)
{
	(void)child;
	(void)level;
	(void)status;
        do_packets(child, 39+16+2*strlen(fname), 39+32);
}

void nb_qfileinfo(struct child_struct *child, int handle, int level, const char *status)
{
	(void)child;
	(void)level;
	(void)handle;
	(void)status;
        do_packets(child, 39+20, 39+32);
}

void nb_qfsinfo(struct child_struct *child, int level, const char *status)
{
	(void)child;
	(void)level;
	(void)status;
        do_packets(child, 39+20, 39+32);
}

void nb_findfirst(struct child_struct *child, const char *fname, int level, int maxcnt, 
		  int count, const char *status)
{
	(void)child;
	(void)level;
	(void)maxcnt;
	(void)status;
        do_packets(child, 39+20+strlen(fname)*2, 39+90*count);
}

void nb_cleanup(struct child_struct *child)
{
	(void)child;
}

void nb_deltree(struct child_struct *child, const char *dname)
{
	(void)child;
	(void)dname;
}

void nb_sfileinfo(struct child_struct *child, int handle, int level, const char *status)
{
	(void)child;
	(void)handle;
	(void)level;
	(void)status;
        do_packets(child, 39+32, 39+8);
}

void nb_lockx(struct child_struct *child, int handle, uint32_t offset, int size, 
	      const char *status)
{
	(void)child;
	(void)handle;
	(void)offset;
	(void)size;
	(void)status;
        do_packets(child, 39+12, 39);
}

void nb_unlockx(struct child_struct *child,
		int handle, uint32_t offset, int size, const char *status)
{
	(void)child;
	(void)handle;
	(void)offset;
	(void)size;
	(void)status;
        do_packets(child, 39+12, 39);
}

void nb_sleep(struct child_struct *child, int usec, const char *status)
{
	(void)child;
	(void)usec;
	(void)status;
	usleep(usec);
}
