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

enum {
    //DEBUG_9P_TRACE  = 0x01,
    //DEBUG_9P_ERRORS = 0x02,
    DEBUG_ATOMIC    = 0x04,
    DEBUG_ADVLOCK   = 0x08,
};

void	diod_conf_init (void);
void    diod_conf_init_config_file (char *path);
void    diod_conf_arm_sighup (void);


char   *diod_conf_get_logdest (void);
int     diod_conf_opt_logdest (void);
void    diod_conf_set_logdest (char *s);

char   *diod_conf_get_configpath (void);
int     diod_conf_opt_configpath (void);

int     diod_conf_get_debuglevel (void);
int     diod_conf_opt_debuglevel (void);
void    diod_conf_set_debuglevel (int i);

int     diod_conf_get_nwthreads (void);
int     diod_conf_opt_nwthreads (void);
void    diod_conf_set_nwthreads (int i);

int     diod_conf_get_foreground (void);
int     diod_conf_opt_foreground (void);
void    diod_conf_set_foreground (int i);

int     diod_conf_get_auth_required (void);
int     diod_conf_opt_auth_required (void);
void    diod_conf_set_auth_required (int i);

int     diod_conf_get_allsquash (void);
int     diod_conf_opt_allsquash (void);
void    diod_conf_set_allsquash (int i);

uid_t   diod_conf_get_runasuid (void);
int     diod_conf_opt_runasuid (void);
void    diod_conf_set_runasuid (uid_t uid);

char   *diod_conf_get_diodpath (void);
int     diod_conf_opt_diodpath (void);
void    diod_conf_set_diodpath (char *s);

List    diod_conf_get_diodlisten (void);
int     diod_conf_opt_diodlisten (void);
void    diod_conf_clr_diodlisten (void);
void    diod_conf_add_diodlisten (char *s);

List    diod_conf_get_diodctllisten (void);
int     diod_conf_opt_diodctllisten (void);
void    diod_conf_clr_diodctllisten (void);
void    diod_conf_add_diodctllisten (char *s);

#define XFLAGS_RO           0x01

typedef struct {
    char         *path;
    char         *opts;
    int          oflags;
    char         *users;
    char         *hosts;
} Export;

List    diod_conf_get_exports (void); /* list-o-Export (caller must NOT free) */
int     diod_conf_opt_exports (void);
void    diod_conf_clr_exports (void);
void    diod_conf_add_exports (char *path);
void    diod_conf_validate_exports (void);
List    diod_conf_get_mounts (void); /* list-o-Export (caller must free) */

int     diod_conf_get_exportall (void);
int     diod_conf_opt_exportall (void);
void    diod_conf_set_exportall (int i);

FILE   *diod_conf_get_statslog (void);
int     diod_conf_opt_statslog (void);
void    diod_conf_set_statslog (char *path);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
