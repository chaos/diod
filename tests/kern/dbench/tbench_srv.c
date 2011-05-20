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

struct options options = {
	.tcp_options = TCP_OPTIONS
};

static void server(int fd)
{
	char buf[70000];
	unsigned *ibuf = (unsigned *)buf;
	uint32_t n;

	signal(SIGPIPE, SIG_IGN);
	
	printf("^"); fflush(stdout);

	while (1) {
		if (read_sock(fd, buf, 4) != 4) break;
		n = ntohl(ibuf[0]);
		if (n+4 >= sizeof(buf)) {
			printf("overflow in server!\n");
			exit(1);
		}
		if (read_sock(fd, buf+4, n) != (int)n) break;
		n = ntohl(ibuf[1]);
		ibuf[0] = htonl(n);
		if (write_sock(fd, buf, n+4) != (int)(n+4)) break;
	}

	exit(0);
}

static void listener(void)
{
	int sock;

	sock = open_socket_in(SOCK_STREAM, TCP_PORT);

	if (listen(sock, 20) == -1) {
		fprintf(stderr,"listen failed\n");
		exit(1);
	}

	printf("waiting for connections\n");

	signal(SIGCHLD, SIG_IGN);

	while (1) {
		struct sockaddr addr;
		socklen_t in_addrlen = sizeof(addr);
		int fd;

		while (waitpid((pid_t)-1,(int *)NULL, WNOHANG) > 0) ;

		fd = accept(sock, &addr, &in_addrlen);

		if (fd != -1) {
			if (fork() == 0) server(fd);
			close(fd);
		}
	}
}


static void usage(void)
{
	printf("usage: tbench_srv [OPTIONS]\n"
	       "options:\n"
	       "  -t options       set socket options\n");
	exit(1);
}


static void process_opts(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "t:")) != -1) {
		switch (c) {
		case 't':
			options.tcp_options = optarg;
			break;
		default:
			usage();
		}
	}
}


 int main(int argc, char *argv[])
{
	process_opts(argc, argv);

	listener();
	return 0;
}
