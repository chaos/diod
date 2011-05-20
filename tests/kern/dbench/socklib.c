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

/****************************************************************************
open a socket of the specified type, port and address for incoming data
****************************************************************************/
int open_socket_in(int type, int port)
{
	struct sockaddr_in sock;
	int res;
	int one=1;

	memset((char *)&sock,0, sizeof(sock));
	sock.sin_port = htons(port);
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = 0;
	res = socket(AF_INET, type, 0);
	if (res == -1) { 
		fprintf(stderr, "socket failed\n"); return -1; 
	}

	setsockopt(res,SOL_SOCKET,SO_REUSEADDR,(char *)&one,sizeof(one));

	/* now we've got a socket - we need to bind it */
	if (bind(res, (struct sockaddr * ) &sock,sizeof(sock)) < 0) { 
		return(-1); 
	}

	set_socket_options(res, options.tcp_options);

	return res;
}


/* open a socket to a tcp remote host with the specified port 
   based on code from Warren */
int open_socket_out(const char *host, int port)
{
	int type = SOCK_STREAM;
	struct sockaddr_in sock_out;
	int res;
	struct hostent *hp;  

	res = socket(PF_INET, type, 0);
	if (res == -1) {
		return -1;
	}

	hp = gethostbyname(host);
	if (!hp) {
		fprintf(stderr,"unknown host: %s\n", host);
		return -1;
	}

	memcpy(&sock_out.sin_addr, hp->h_addr, hp->h_length);
	sock_out.sin_port = htons(port);
	sock_out.sin_family = PF_INET;

	set_socket_options(res, options.tcp_options);

	if (connect(res,(struct sockaddr *)&sock_out,sizeof(sock_out))) {
		close(res);
		fprintf(stderr,"failed to connect to %s - %s\n", 
			host, strerror(errno));
		return -1;
	}

	return res;
}



enum SOCK_OPT_TYPES {OPT_BOOL,OPT_INT,OPT_ON};

static const struct
{
  char *name;
  int level;
  int option;
  int value;
  int opttype;
} socket_options[] = {
  {"SO_KEEPALIVE",      SOL_SOCKET,    SO_KEEPALIVE,    0,                 OPT_BOOL},
  {"SO_REUSEADDR",      SOL_SOCKET,    SO_REUSEADDR,    0,                 OPT_BOOL},
  {"SO_BROADCAST",      SOL_SOCKET,    SO_BROADCAST,    0,                 OPT_BOOL},
#ifdef TCP_NODELAY
  {"TCP_NODELAY",       IPPROTO_TCP,   TCP_NODELAY,     0,                 OPT_BOOL},
#endif
#ifdef IPTOS_LOWDELAY
  {"IPTOS_LOWDELAY",    IPPROTO_IP,    IP_TOS,          IPTOS_LOWDELAY,    OPT_ON},
#endif
#ifdef IPTOS_THROUGHPUT
  {"IPTOS_THROUGHPUT",  IPPROTO_IP,    IP_TOS,          IPTOS_THROUGHPUT,  OPT_ON},
#endif
#ifdef SO_SNDBUF
  {"SO_SNDBUF",         SOL_SOCKET,    SO_SNDBUF,       0,                 OPT_INT},
#endif
#ifdef SO_RCVBUF
  {"SO_RCVBUF",         SOL_SOCKET,    SO_RCVBUF,       0,                 OPT_INT},
#endif
#ifdef SO_SNDLOWAT
  {"SO_SNDLOWAT",       SOL_SOCKET,    SO_SNDLOWAT,     0,                 OPT_INT},
#endif
#ifdef SO_RCVLOWAT
  {"SO_RCVLOWAT",       SOL_SOCKET,    SO_RCVLOWAT,     0,                 OPT_INT},
#endif
#ifdef SO_SNDTIMEO
  {"SO_SNDTIMEO",       SOL_SOCKET,    SO_SNDTIMEO,     0,                 OPT_INT},
#endif
#ifdef SO_RCVTIMEO
  {"SO_RCVTIMEO",       SOL_SOCKET,    SO_RCVTIMEO,     0,                 OPT_INT},
#endif
  {NULL,0,0,0,0}};

	

/****************************************************************************
set user socket options
****************************************************************************/
void set_socket_options(int fd, char *options)
{
	char tok[200];

	while (next_token(&options,tok," \t,"))
    {
      int ret=0,i;
      int value = 1;
      char *p;
      BOOL got_value = False;

      if ((p = strchr(tok,'=')))
	{
	  *p = 0;
	  value = atoi(p+1);
	  got_value = True;
	}

      for (i=0;socket_options[i].name;i++)
	if (strcasecmp(socket_options[i].name,tok)==0)
	  break;

      if (!socket_options[i].name)
	{
	  fprintf(stderr, "Unknown socket option %s\n",tok);
	  continue;
	}

      switch (socket_options[i].opttype)
	{
	case OPT_BOOL:
	case OPT_INT:
	  ret = setsockopt(fd,socket_options[i].level,
			   socket_options[i].option,(char *)&value,sizeof(int));
	  break;

	case OPT_ON:
	  if (got_value)
	    fprintf(stderr,"syntax error - %s does not take a value\n",tok);

	  {
	    int on = socket_options[i].value;
	    ret = setsockopt(fd,socket_options[i].level,
			     socket_options[i].option,(char *)&on,sizeof(int));
	  }
	  break;	  
	}
      
      if (ret != 0)
	fprintf(stderr, "Failed to set socket option %s\n",tok);
    }
}

int read_sock(int s, char *buf, int size)
{
	int total=0;

	while (size) {
		int r = recv(s, buf, size, MSG_WAITALL);
		if (r <= 0) {
			if (r == -1) perror("recv");
			break;
		}
		buf += r;
		size -= r;
		total += r;
	}
	return total;
}

int write_sock(int s, char *buf, int size)
{
	int total=0;

	while (size) {
		int r = send(s, buf, size, 0);
		if (r <= 0) {
			if (r == -1) perror("send");
			break;
		}
		buf += r;
		size -= r;
		total += r;
	}
	return total;
}
