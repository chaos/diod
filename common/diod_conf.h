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

void	diod_conf_init (void);
void    diod_conf_init_config_file (char *path);

int     diod_conf_get_debuglevel (void);
void    diod_conf_set_debuglevel (int i);

int     diod_conf_get_nwthreads (void);
void    diod_conf_set_nwthreads (int i);

int     diod_conf_get_foreground (void);
void    diod_conf_set_foreground (int i);

int     diod_conf_get_readahead (void);
void    diod_conf_set_readahead (int i);

int     diod_conf_get_tcpwrappers (void);
void    diod_conf_set_tcpwrappers (int i);

int     diod_conf_get_munge (void);
void    diod_conf_set_munge (int i);

int     diod_conf_get_exit_on_lastuse (void);
void    diod_conf_set_exit_on_lastuse (int i);

List    diod_conf_get_listen (void);
void    diod_conf_set_listen (char *s);

int     diod_conf_match_export (char *path, char *host, char *ip, uid_t uid,
                                int *errp);
void    diod_conf_validate_exports (void);
void    diod_conf_set_export (char *path);
char   *diod_conf_cat_exports (void);


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
