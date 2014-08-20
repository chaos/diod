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

/* diod_conf.c - config registry for distributed I/O daemon */

/* Attributes are set with the following precedence:
 *
 *    command line, config file, compiled-in default
 *
 * Users should call:
 * 1) diod_conf_init () - sets initial defaults
 * 2) diod_config_init_config_file () - override defaults with config file
 * 3) diod_conf_set_* - override config file with command line
 *
 * Config file can be reloaded on SIGHUP - any command line settings
 * will be protected from update with readonly flag (see below)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#if defined(HAVE_LUA_H) && defined(HAVE_LUALIB_H)
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#endif
#include <stdarg.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>

#include "list.h"

#include "diod_conf.h"
#include "diod_log.h"

#ifndef _PATH_PROC_MOUNTS
#define _PATH_PROC_MOUNTS "/proc/mounts"
#endif

/* ro_mask values to protect attribute from overwrite by config file */
#define RO_DEBUGLEVEL           0x00000001
#define RO_NWTHREADS            0x00000002
#define RO_FOREGROUND           0x00000004
#define RO_AUTH_REQUIRED        0x00000008
#define RO_RUNASUID             0x00000010
#define RO_USERDB               0x00000020
#define RO_LISTEN               0x00000040
#define RO_MAXMMAP              0x00000080
#define RO_EXPORTS              0x00000100
#define RO_STATSLOG             0x00000200
#define RO_CONFIGPATH           0x00000400
#define RO_LOGDEST              0x00000800
#define RO_EXPORTALL            0x00001000
#define RO_EXPORTOPTS           0x00002000
#define RO_ALLSQUASH            0x00004000
#define RO_SQUASHUSER           0x00008000
#define RO_STATFS_PASSTHRU      0x00010000
#define RO_AUTH_REQUIRED_CTL    0x00020000

typedef struct {
    int          debuglevel;
    int          nwthreads;
    int          foreground;
    int          auth_required;
    int          statfs_passthru;
    int          userdb;
    int          allsquash;
    char        *squashuser;
    uid_t        runasuid;
    List         listen;
    int          exportall;
    char        *exportopts;
    List         exports;
    char        *configpath;
    char        *logdest;
    int          ro_mask; 
} Conf;

static Conf config;

static char *
_xstrdup (char *s)
{
    char *cpy = strdup (s);
    if (!cpy)
        msg_exit ("out of memory");
    return cpy;
}

static List
_xlist_create (ListDelF f)
{
    List new = list_create (f);
    if (!new)
        msg_exit ("out of memory");
    return new;
}

static void
_xlist_append (List l, void *item)
{
    if (!list_append (l, item))
        msg_exit ("out of memory");
}

static Export *
_create_export (char *path)
{
    Export *x = malloc (sizeof (*x));
    if (!x)
        return NULL;
    if (!(x->path = strdup (path))) {
        free (x);
        return NULL;
    }
    x->opts = NULL;
    x->hosts = NULL;
    x->users = NULL;
    x->oflags = 0;
    return x;
}

static Export *
_xcreate_export (char *path)
{
    Export *x = _create_export (path);
    if (!x)
        msg_exit ("out of memory");
    return x;
}

static void
_destroy_export (Export *x)
{
    free (x->path);
    if (x->opts)
        free (x->opts);
    if (x->hosts)
        free (x->hosts);
    if (x->users)
        free (x->users);
    free (x);
}

void
diod_conf_init (void)
{
    config.debuglevel = DFLT_DEBUGLEVEL;
    config.nwthreads = DFLT_NWTHREADS;
    config.foreground = DFLT_FOREGROUND;
    config.auth_required = DFLT_AUTH_REQUIRED;
    config.statfs_passthru = DFLT_STATFS_PASSTHRU;
    config.userdb = DFLT_USERDB;
    config.allsquash = DFLT_ALLSQUASH;
    config.squashuser = _xstrdup (DFLT_SQUASHUSER);
    config.runasuid = DFLT_RUNASUID;
    config.listen = _xlist_create ((ListDelF)free);
    _xlist_append (config.listen, _xstrdup (DFLT_LISTEN));
    config.exports = _xlist_create ((ListDelF)_destroy_export);
    config.exportall = DFLT_EXPORTALL;
    config.exportopts = NULL;
#if defined(DFLT_CONFIGPATH)
    config.configpath = _xstrdup (DFLT_CONFIGPATH);
#else
    config.configpath = NULL;
#endif
    config.logdest = _xstrdup (DFLT_LOGDEST);
    config.ro_mask = 0;
}

void
diod_conf_fini (void)
{
    if (config.listen)
        list_destroy (config.listen);
    if (config.exports)
        list_destroy (config.exports);
    if (config.configpath)
        free (config.configpath);
    if (config.logdest)
        free (config.logdest);
    if (config.squashuser)
        free (config.squashuser);
    if (config.exportopts)
        free (config.exportopts);
}

/* logdest - logging destination
 */
char *diod_conf_get_logdest (void) { return config.logdest; }
int diod_conf_opt_logdest (void) { return config.ro_mask & RO_LOGDEST; }
void diod_conf_set_logdest (char *s)
{
    if (config.logdest)
        free (config.logdest);
    config.logdest = _xstrdup (s);
    config.ro_mask |= RO_LOGDEST;
}

/* configpath - config file path
 *    (set in diod_conf_init_config_file)
 */
char *diod_conf_get_configpath (void) { return config.configpath; }
int diod_conf_opt_configpath (void) { return config.ro_mask & RO_CONFIGPATH; }

/* debuglevel - turn debug channels on/off
 */
int diod_conf_get_debuglevel (void) { return config.debuglevel; }
int diod_conf_opt_debuglevel (void) { return config.ro_mask & RO_DEBUGLEVEL; }
void diod_conf_set_debuglevel (int i)
{
    config.debuglevel = i & 0xffff;
    config.ro_mask |= RO_DEBUGLEVEL;
}

/* nwthreads - number of worker threads to spawn in libnpfs
 */
int diod_conf_get_nwthreads (void) { return config.nwthreads; }
int diod_conf_opt_nwthreads (void) { return config.ro_mask & RO_NWTHREADS; }
void diod_conf_set_nwthreads (int i)
{
    config.nwthreads = i;
    config.ro_mask |= RO_NWTHREADS;
}

/* foreground - run daemon in foreground
 */
int diod_conf_get_foreground (void) { return config.foreground; }
int diod_conf_opt_foreground (void) { return config.ro_mask & RO_FOREGROUND; }
void diod_conf_set_foreground (int i)
{
    config.foreground = i;
    config.ro_mask |= RO_FOREGROUND;
}

/* auth_required - whether to accept unauthenticated attaches
 */
int diod_conf_get_auth_required (void) { return config.auth_required; }
int diod_conf_opt_auth_required (void) { return config.ro_mask & RO_AUTH_REQUIRED; }
void diod_conf_set_auth_required (int i)
{
    config.auth_required = i;
    config.ro_mask |= RO_AUTH_REQUIRED;
}

/* statfs_passthru - whether statfs should return host f_type or V9FS_MAGIC
 */
int diod_conf_get_statfs_passthru (void) { return config.statfs_passthru; }
int diod_conf_opt_statfs_passthru (void) { return config.ro_mask & RO_STATFS_PASSTHRU; }
void diod_conf_set_statfs_passthru (int i)
{
    config.statfs_passthru = i;
    config.ro_mask |= RO_STATFS_PASSTHRU;
}

/* userdb - whether to do passwd/group lookup
 */
int diod_conf_get_userdb (void) { return config.userdb; }
int diod_conf_opt_userdb (void) { return config.ro_mask & RO_USERDB; }
void diod_conf_set_userdb (int i)
{
    config.userdb = i;
    config.ro_mask |= RO_USERDB;
}


/* allsquash - run server as squash suer and remap all attaches
 */
int diod_conf_get_allsquash (void) { return config.allsquash; }
int diod_conf_opt_allsquash (void) { return config.ro_mask & RO_ALLSQUASH; }
void diod_conf_set_allsquash (int i)
{
    config.allsquash = i;
    config.ro_mask |= RO_ALLSQUASH;
}

/* squashuser - override 'nobody' as the squash user
 */
char *diod_conf_get_squashuser(void) { return config.squashuser; }
int diod_conf_opt_squashuser(void) { return config.ro_mask & RO_SQUASHUSER; }
void diod_conf_set_squashuser(char *user)
{
    if (config.squashuser)
        free (config.squashuser);
    config.squashuser = _xstrdup (user);
    config.ro_mask |= RO_SQUASHUSER;
}

/* runasuid - set to run server as one user (mount -o access=uid)
 */
uid_t diod_conf_get_runasuid (void) { return config.runasuid; }
int diod_conf_opt_runasuid (void) { return config.ro_mask & RO_RUNASUID; }
void diod_conf_set_runasuid (uid_t uid)
{
    config.runasuid = uid;
    config.ro_mask |= RO_RUNASUID;
}

/* listen - list of host:port strings for diod to listen on.
 */
List diod_conf_get_listen (void) { return config.listen; }
int diod_conf_opt_listen (void) { return config.ro_mask & RO_LISTEN; }
void diod_conf_clr_listen (void)
{
    list_destroy (config.listen);
    config.listen = _xlist_create ((ListDelF)free);
    config.ro_mask |= RO_LISTEN;
}
void diod_conf_add_listen (char *s)
{
    _xlist_append (config.listen, _xstrdup (s));
    config.ro_mask |= RO_LISTEN;
}

/* exports - list of paths of exported file systems
 */
List diod_conf_get_exports (void) { return config.exports; }
int diod_conf_opt_exports (void) { return config.ro_mask & RO_EXPORTS; }
void diod_conf_clr_exports (void)
{
    list_destroy (config.exports);
    config.exports = _xlist_create ((ListDelF)_destroy_export);
    config.ro_mask |= RO_EXPORTS;
}
void diod_conf_add_exports (char *path)
{
    Export *x = _xcreate_export (path);
    _xlist_append (config.exports, x);
    config.ro_mask |= RO_EXPORTS;
}
void diod_conf_validate_exports (void)
{
    ListIterator itr;
    Export *x;

    if (config.exportall == 0 && list_count (config.exports) == 0)
        msg_exit ("no exports defined");
    if ((itr = list_iterator_create (config.exports)) == NULL)
        msg_exit ("out of memory");
    while ((x = list_next (itr))) {
        if (*x->path != '/' && strcmp (x->path, "ctl") != 0)
            msg_exit ("exports should begin with '/'");
        if (strstr (x->path, "/..") != 0)
            msg_exit ("exports should not contain '/..'"); /* FIXME */
    }
    list_iterator_destroy (itr);
}

static void
_parse_expopt (char *s, int *fp)
{
    int flags = 0;
    char *cpy, *item;
    char *saveptr = NULL;

    if (!(cpy = strdup (s)))
        msg_exit ("out of memory");
    item = strtok_r (cpy, ",", &saveptr);
    while (item) {
        if (!strcmp (item, "ro"))
            flags |= XFLAGS_RO;
        else if (!strcmp (item, "suppress"))
            flags |= XFLAGS_SUPPRESS;
        else if (!strcmp (item, "sharefd"))
            flags |= XFLAGS_SHAREFD;
        else if (!strcmp (item, "privport"))
            flags |= XFLAGS_PRIVPORT;
        else if (!strcmp (item, "noauth"))
            flags |= XFLAGS_NOAUTH;
        else
            msg_exit ("unknown export option: %s", item);
        item = strtok_r (NULL, ",", &saveptr);
    }
    free (cpy);
    *fp = flags;
}

/* exportall - export everything in /proc/mounts
 */
int diod_conf_get_exportall (void) { return config.exportall; }
int diod_conf_opt_exportall (void) { return config.ro_mask & RO_EXPORTALL; }
void diod_conf_set_exportall (int i)
{
    config.exportall = i;
    config.ro_mask |= RO_EXPORTALL;
}
List diod_conf_get_mounts (void)
{
    List l = NULL;
    FILE *f = NULL;
    Export *x;
    char *p, *path, buf[1024];

    if (!config.exportall)
        goto error;
    if (!(l = list_create ((ListDelF)_destroy_export)))
        goto error;
    if (!(f = fopen (_PATH_PROC_MOUNTS, "r")))
        goto error;
    while (fgets (buf, sizeof(buf), f) != NULL) {
        if (buf[strlen(buf) - 1] == '\n')
            buf[strlen(buf) - 1] = '\0';
        if (!(p = strchr (buf, ' ')))
            continue;
        path = p + 1;
        if (!(p = strchr (path, ' ')))
            continue;
        *p = '\0';
        if (!(x = _create_export (path)))
            goto error;
        if (config.exportopts)
            x->opts = _xstrdup (config.exportopts);
        if (x->opts)
            _parse_expopt (x->opts, &x->oflags);
        if (!list_append (l, x)) {
            _destroy_export (x);
            goto error;
        }
    }
    fclose (f);
    return l;
error:
    if (f)
        fclose (f);
    if (l)
        list_destroy (l);
    return NULL;
}

/* exportopts - set global export options
 */
char *diod_conf_get_exportopts (void) { return config.exportopts; }
int diod_conf_opt_exportopts (void) { return config.ro_mask & RO_EXPORTOPTS; }
void diod_conf_set_exportopts(char *opts)
{
    if (config.exportopts)
        free (config.exportopts);
    config.exportopts = _xstrdup (opts);
    config.ro_mask |= RO_EXPORTOPTS;
}

#if defined(HAVE_LUA_H) && defined(HAVE_LUALIB_H)
static int
_lua_getglobal_int (char *path, lua_State *L, char *key, int *ip)
{
    int res = 0;

    lua_getglobal (L, key);
    if (!lua_isnil (L, -1)) {
        if (!lua_isnumber (L, -1))
            msg_exit ("%s: `%s' should be number", path, key);
        if (ip)
            *ip = (int)lua_tonumber (L, -1);
        res = 1;
    }
    lua_pop (L, 1);

    return res;
}

static int
_lua_getglobal_string (char *path, lua_State *L, char *key, char **sp)
{
    int res = 0;
    char *cpy;

    lua_getglobal (L, key);
    if (!lua_isnil (L, -1)) {
        if (!lua_isstring (L, -1))
            msg_exit ("%s: `%s' should be string", path, key);
        if (sp) {
            cpy = _xstrdup ((char *)lua_tostring (L, -1));
            if (*sp != NULL)
                free (*sp);
            *sp = cpy;
        }
        res = 1;
    }
    lua_pop (L, 1);

    return res;
}

static int
_lua_getglobal_list_of_strings (char *path, lua_State *L, char *key, List *lp)
{
    int res = 0;
    int i;
    List l;

    lua_getglobal (L, key);
    if (!lua_isnil (L, -1)) {
        if (!lua_istable(L, -1))
            msg_exit ("%s: `%s' should be table", path, key);
        l = _xlist_create ((ListDelF)free);
        for (i = 1; ;i++) {
            lua_pushinteger(L, (lua_Integer)i);
            lua_gettable (L, -2);
            if (lua_isnil (L, -1))
                break;
            if (!lua_isstring (L, -1))
                msg_exit ("%s: `%s[%d]' should be string", path, key, i);
            _xlist_append (l, _xstrdup ((char *)lua_tostring (L, -1)));
            lua_pop (L, 1);
        }
        lua_pop (L, 1);
        if (lp) {
            if (*lp != NULL)
                list_destroy (*lp);
            *lp = l;
        } else
            list_destroy (l);
        res = 1;
    }
    lua_pop (L, 1);

    return res;
}

static void
_lua_get_expattr (char *path, int i, lua_State *L, char *key, char **sp)
{
    lua_getfield (L, -1, key);
    if (!lua_isnil (L, -1)) {
         if (!lua_isstring (L, -1))
            msg_exit ("%s: `exports[%d].%s' requires string value",
                      path, i, key);
         *sp = _xstrdup ((char *)lua_tostring (L, -1));
    }
    lua_pop (L, 1);
}

static int
_lua_getglobal_exports (char *path, lua_State *L, List *lp)
{
    Export *x;
    int res = 0;
    int i;
    List l;

    lua_getglobal (L, "exports");
    if (!lua_isnil (L, -1)) {
        if (!lua_istable(L, -1))
            msg_exit ("%s: `exports' should be table", path);
        l = _xlist_create ((ListDelF)_destroy_export);
        for (i = 1; ;i++) {
            lua_pushinteger(L, (lua_Integer)i);
            lua_gettable (L, -2);
            if (lua_isnil (L, -1))
                break;
            if (lua_isstring (L, -1)) {
                x = _xcreate_export ((char *)lua_tostring (L, -1));
                _xlist_append (l, x);
            } else if (lua_istable(L, -1)) {
                char *p = NULL;
                _lua_get_expattr (path, i, L, "path", &p);
                if (!p)
                    msg_exit ("%s: `exports[%d]' path is a required attribute",
                              path, i);
                x = _xcreate_export (p);
                free (p);
                _lua_get_expattr (path, i, L, "opts", &x->opts);
                if (!x->opts && config.exportopts)
                    x->opts = _xstrdup (config.exportopts);
                if (x->opts)
                    _parse_expopt (x->opts, &x->oflags);
                _lua_get_expattr (path, i, L, "users", &x->users);
                _lua_get_expattr (path, i, L, "hosts", &x->hosts);
                /* FIXME: check for illegal export attributes */
                _xlist_append (l, x);
            } else
                msg_exit ("%s: `exports[%d]' should be string/table", path, i);
            lua_pop (L, 1);
        }
        lua_pop (L, 1);
        if (lp) {
            if (*lp != NULL)
                list_destroy (*lp);
            *lp = l;
        } else
            list_destroy (l);
        res = 1;
    }
    lua_pop (L, 1);

    return res;
}

void
diod_conf_init_config_file (char *path) /* FIXME: ENOMEM is fatal */
{
    if (path) {
        if (config.configpath)
            free (config.configpath);
        config.configpath = _xstrdup (path);
        config.ro_mask |= RO_CONFIGPATH;
    } else {
        if (config.configpath && access (config.configpath, R_OK) == 0)
            path = config.configpath;  /* missing default file is not fatal */
    }
    if (path) {
    	lua_State *L = lua_open ();

        luaopen_base (L);
        luaopen_table (L);
        //luaopen_io (L);
        luaopen_string (L);
        luaopen_math (L);

        if (luaL_loadfile (L, path) || lua_pcall (L, 0, 0, 0))
            msg_exit ("%s", lua_tostring (L, -1));

        /* Don't override cmdline options when rereading config file.
         */
        if (!(config.ro_mask & RO_NWTHREADS)) {
            config.nwthreads = DFLT_NWTHREADS;
            _lua_getglobal_int (path, L, "nwthreads", &config.nwthreads);
        }
        if (!(config.ro_mask & RO_AUTH_REQUIRED)) {
            config.auth_required = DFLT_AUTH_REQUIRED;
            _lua_getglobal_int (path, L, "auth_required",
                                &config.auth_required);
        }
        if (!(config.ro_mask & RO_STATFS_PASSTHRU)) {
            config.statfs_passthru = DFLT_STATFS_PASSTHRU;
            _lua_getglobal_int (path, L, "statfs_passthru",
                                &config.statfs_passthru);
        }
        if (!(config.ro_mask & RO_USERDB)) {
            config.userdb = DFLT_USERDB;
            _lua_getglobal_int (path, L, "userdb", &config.userdb);
        }
        if (!(config.ro_mask & RO_ALLSQUASH)) {
            config.allsquash = DFLT_ALLSQUASH;
            _lua_getglobal_int (path, L, "allsquash", &config.allsquash);
        }
        if (!(config.ro_mask & RO_SQUASHUSER)) {
            free (config.squashuser);
            config.squashuser = _xstrdup (DFLT_SQUASHUSER);
            _lua_getglobal_string (path, L, "squashuser", &config.squashuser);
        }
        if (!(config.ro_mask & RO_LISTEN)) {
            list_destroy (config.listen);
            config.listen = _xlist_create ((ListDelF)free);
            _xlist_append (config.listen, _xstrdup (DFLT_LISTEN));
            _lua_getglobal_list_of_strings (path, L, "listen", &config.listen);
        }
        if (!(config.ro_mask & RO_LOGDEST)) {
            free (config.logdest);
            config.logdest = _xstrdup (DFLT_LOGDEST);
            _lua_getglobal_string (path, L, "logdest", &config.logdest);
        }
        if (!(config.ro_mask & RO_EXPORTALL)) {
            config.exportall = DFLT_EXPORTALL;
            _lua_getglobal_int (path, L, "exportall", &config.exportall);
        }
        if (!(config.ro_mask & RO_EXPORTOPTS)) {
            if (config.exportopts) {
                free (config.exportopts);
                config.exportopts = NULL;
            }
            _lua_getglobal_string (path, L, "exportopts", &config.exportopts);
        }
        if (!(config.ro_mask & RO_EXPORTS)) {
            list_destroy (config.exports);
            config.exports = _xlist_create ((ListDelF)_destroy_export);
            _lua_getglobal_exports (path, L, &config.exports);
        }
        lua_close(L);
    }
}
#else
/* Allow no lua config + empty config file.
 * This is to allow regression tests to specify -c /dev/null and work
 * even if there is no LUA installed.
 */
void
diod_conf_init_config_file (char *path)
{
    struct stat sb;

    if (path) {
        if (config.configpath)
            free (config.configpath);
        config.configpath = _xstrdup (path);
        config.ro_mask |= RO_CONFIGPATH;
    } else {
        if (config.configpath && access (config.configpath, R_OK) == 0)
            path = config.configpath;  /* missing default file is not fatal */
    }
    if (path) {
        if (stat (path, &sb) < 0)
            err_exit ("%s", path);
        if (sb.st_size > 0)
            msg_exit ("no LUA suport - cannot parse contents of %s", path);
    }
}
#endif /* HAVE_LUA_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
