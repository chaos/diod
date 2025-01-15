/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

#ifndef LIBDIOD_DIOD_RDMA_H
#define LIBDIOD_DIOD_RDMA_H

typedef struct diod_rdma_struct *diod_rdma_t;

diod_rdma_t diod_rdma_create (void);
int diod_rdma_listen (diod_rdma_t rdma);
void diod_rdma_accept_one (Npsrv *srv, diod_rdma_t rdma);
void diod_rdma_shutdown (diod_rdma_t rdma);
void diod_rdma_destroy (diod_rdma_t rdma);

#endif

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
