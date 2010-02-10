/*****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see <http://code.google.com/p/diod/>.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/

Nptrans *diod_trans_create (int fd, char *host, char *ip, char *svc);
void diod_trans_destroy (void *trans);
char *diod_trans_get_host (Nptrans *trans);
char *diod_trans_get_ip (Nptrans *trans);
char *diod_trans_get_svc (Nptrans *trans);
void diod_trans_set_authuser (Nptrans *trans, uid_t uid);
int diod_trans_get_authuser (Nptrans *trans, uid_t *uidp);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
