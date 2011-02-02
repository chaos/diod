/* Copyright ©2006-2010 Kris Maglione <maglione.k at Gmail>
 * Copyright ©2004-2006 Anselm R. Garbe <garbeam at gmail dot com>
 * See LICENSE file for license details.
 */
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include "ixp_local.h"

/**
 * Function: ixp_listen
 * Type: IxpConn
 *
 * Params:
 *	fs:    The file descriptor on which to listen.
 *	aux:   A piece of data to store in the connection's
 *	       P<aux> member of the IxpConn data structure.
 *	read:  The function called when the connection has
 *	       data available to read.
 *	close: A cleanup function called when the
 *	       connection is closed.
 *
 * Starts the server P<srv> listening on P<fd>. The optional
 * P<read> and P<close> callbacks are called with the IxpConn
 * structure for the connection as their sole argument.
 *
 * Returns:
 *	Returns the connection's new IxpConn data structure.
 *
 * See also:
 *	F<ixp_serverloop>, F<ixp_serve9conn>, F<ixp_hangup>
 */
IxpConn*
ixp_listen(IxpServer *srv, int fd, void *aux,
		void (*read)(IxpConn*),
		void (*close)(IxpConn*)
		) {
	IxpConn *c;

	c = emallocz(sizeof *c);
	c->fd = fd;
	c->aux = aux;
	c->srv = srv;
	c->read = read;
	c->close = close;
	c->next = srv->conn;
	srv->conn = c;
	return c;
}

/**
 * Function: ixp_hangup
 * Function: ixp_server_close
 *
 * ixp_hangup closes a connection, and stops the server
 * listening on it. It calls the connection's close
 * function, if it exists. ixp_server_close calls ixp_hangup
 * on all of the connections on which the server is
 * listening.
 *
 * See also:
 *	F<ixp_listen>, S<IxpServer>, S<IxpConn>
 */

void
ixp_hangup(IxpConn *c) {
	IxpServer *s;
	IxpConn **tc;

	s = c->srv;
	for(tc=&s->conn; *tc; tc=&(*tc)->next)
		if(*tc == c) break;
	assert(*tc == c);

	*tc = c->next;
	c->closed = 1;
	if(c->close)
		c->close(c);
	else
		shutdown(c->fd, SHUT_RDWR);

	close(c->fd);
	free(c);
}

void
ixp_server_close(IxpServer *s) {
	IxpConn *c, *next;

	for(c = s->conn; c; c = next) {
		next = c->next;
		ixp_hangup(c);
	}
}

static void
prepare_select(IxpServer *s) {
	IxpConn *c;

	FD_ZERO(&s->rd);
	for(c = s->conn; c; c = c->next)
		if(c->read) {
			if(s->maxfd < c->fd)
				s->maxfd = c->fd;
			FD_SET(c->fd, &s->rd);
		}
}

static void
handle_conns(IxpServer *s) {
	IxpConn *c, *n;
	for(c = s->conn; c; c = n) {
		n = c->next;
		if(FD_ISSET(c->fd, &s->rd))
			c->read(c);
	}
}

/**
 * Function: ixp_serverloop
 * Type: IxpServer
 *
 * Enters the main loop of the server. Exits when
 * P<srv>->running becomes false, or when select(2) returns an
 * error other than EINTR.
 *
 * Returns:
 *	Returns 0 when the loop exits normally, and 1 when
 *	it exits on error. V<errno> or the return value of
 *	F<ixp_errbuf> may be inspected.
 * See also:
 *	F<ixp_listen>, F<ixp_settimer>
 */

int
ixp_serverloop(IxpServer *srv) {
	timeval *tvp;
	timeval tv;
	long timeout;
	int r;

	srv->running = 1;
	thread->initmutex(&srv->lk);
	while(srv->running) {
		tvp = nil;
		timeout = ixp_nexttimer(srv);
		if(timeout > 0) {
			tv.tv_sec = timeout/1000;
			tv.tv_usec = timeout%1000 * 1000;
			tvp = &tv;
		}

		if(srv->preselect)
			srv->preselect(srv);

		if(!srv->running)
			break;

		prepare_select(srv);
		r = thread->select(srv->maxfd + 1, &srv->rd, 0, 0, tvp);
		if(r < 0) {
			if(errno == EINTR)
				continue;
			return 1;
		}
		handle_conns(srv);
	}
	return 0;
}

