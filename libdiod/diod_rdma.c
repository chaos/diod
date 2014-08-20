/*****************************************************************************
 *  Copyright (C) 2010-14 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see http://code.google.com/p/diod.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also: http://www.gnu.org/licenses
 *****************************************************************************/

/* diod_rdma.c - distributed I/O daemon infiniband rdma operations */

/* based on code in libnpfs/rdmasrv.c by Tom Tucker et al */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>
#include <stdint.h>
#include <syslog.h>
#include <sys/time.h>
#include <poll.h>
#include <pthread.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include "9p.h"
#include "npfs.h"
#include "list.h"

#include "diod_log.h"
#include "diod_rdma.h"

const uint16_t rdma_port = 5640;
const int rdma_qdepth = 64;
const int rdma_maxmsize = 65536;

struct diod_rdma_struct {
    struct rdma_cm_id *listen_id;
    struct rdma_event_channel *event_channel;
    struct sockaddr_in addr;
};

diod_rdma_t
diod_rdma_create (void)
{
    int n;
    diod_rdma_t rdma;

    rdma = malloc (sizeof (*rdma));
    if (!rdma)
        msg_exit ("out of memory");

    rdma->event_channel = rdma_create_event_channel();
    if (!rdma->event_channel)
        msg_exit ("rdma_create_event_channel failed");

    n = rdma_create_id(rdma->event_channel, &rdma->listen_id,
                       NULL, RDMA_PS_TCP);
    if (n)
        errn_exit (n, "rdma_create_id");

    return rdma;
}

void
diod_rdma_destroy (diod_rdma_t rdma)
{
    if (rdma->listen_id) {
        rdma_destroy_id(rdma->listen_id);
        rdma->listen_id = NULL;
    }
    if (rdma->event_channel) {
        rdma_destroy_event_channel (rdma->event_channel);
        rdma->event_channel = NULL;
    }
    free (rdma);
}

int
diod_rdma_listen (diod_rdma_t rdma)
{
    int n;
    
    rdma->addr.sin_family = AF_INET;
    rdma->addr.sin_port = htons(rdma_port);
    rdma->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    n = rdma_bind_addr(rdma->listen_id, (struct sockaddr *)&rdma->addr);
    if (n)
        errn_exit (n, "rdma_bind_addr");

    n = rdma_listen(rdma->listen_id, 1);
    if (n)
        errn (n, "rdma_listen");
  
    return 0;
}

void
diod_rdma_shutdown (diod_rdma_t rdma)
{
    if (rdma->listen_id)
        rdma_destroy_id(rdma->listen_id);
    rdma->listen_id = NULL;
}

void
diod_rdma_accept_one (Npsrv *srv, diod_rdma_t rdma)
{
    Npconn *conn;
    Nptrans *trans;
    struct rdma_cm_event *event;
    struct rdma_cm_id *cmid;
    enum rdma_cm_event_type etype;
    int n;

    n = rdma_get_cm_event(rdma->event_channel, &event);
    if (n)
        errn_exit (n, "rdma_get_cm_event");

    cmid = (struct rdma_cm_id *)event->id;
    etype = event->event;
    rdma_ack_cm_event(event);

    switch (etype) {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            msg ("rdma: connection request");
            trans = np_rdmatrans_create(cmid, rdma_qdepth, rdma_maxmsize);
            if (trans) {
                conn = np_conn_create(srv, trans, "rdma", 0);
                cmid->context = conn;
                np_srv_add_conn(srv, conn);
            } else
                errn (np_rerror (), "np_rdmatrns_create failed");
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            msg ("rdma: connection established");
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            msg ("rdma: connection shutting down");
            conn = cmid->context;
            //np_conn_shutdown(conn);
            /* FIXME: clean up properly */
            break;

        default:
            msg ("rdma: event %d received waiting for a connect request\n",
                 etype);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
