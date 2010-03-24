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

/* diod_conf.c - config registry for distributed I/O daemon */

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
#if HAVE_LUA_H
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#endif
#include <stdarg.h>

#include "list.h"

#include "diod_conf.h"
#include "diod_log.h"


typedef struct {
    int          debuglevel;
    int          nwthreads;
    int          foreground;
    int          munge;
    int          tcpwrappers;
    int          allowprivate;
    int          exit_on_lastuse;
    uid_t        runasuid;
    int          runasuid_valid;
    char        *diodpath;
    List         diodlisten;
    List         diodctllisten;
    List         exports;
} Conf;

static Conf config = {
    .debuglevel     = 0,
    .nwthreads      = 16,
    .foreground     = 0,
    .munge          = 1,
    .tcpwrappers    = 1,
    .allowprivate   = 0,
    .exit_on_lastuse= 0,
    .runasuid_valid = 0,
    .diodpath       = NULL, /* diod_conf_init initializes */
    .diodlisten     = NULL, /* diod_conf_init initializes */
    .diodctllisten  = NULL, /* diod_conf_init initializes */
    .exports        = NULL,
};


void
diod_conf_init (void)
{
    diod_conf_set_diodctllisten ("0.0.0.0:10005");
    diod_conf_set_diodlisten ("0.0.0.0:10006");
    diod_conf_set_diodpath (X_SBINDIR "/diod");
}

int
diod_conf_get_debuglevel (void)
{
    return config.debuglevel;
}

void
diod_conf_set_debuglevel (int i)
{
    config.debuglevel = i;
}

int
diod_conf_get_nwthreads (void)
{
    return config.nwthreads;
}

void
diod_conf_set_nwthreads (int i)
{
    config.nwthreads = i;
}

int
diod_conf_get_foreground (void)
{
    return config.foreground;
}

void
diod_conf_set_foreground (int i)
{
    config.foreground = i;
}

int
diod_conf_get_munge (void)
{
    return config.munge;
}

void
diod_conf_set_munge (int i)
{
    config.munge = i;
}

int
diod_conf_get_tcpwrappers (void)
{
    return config.tcpwrappers;
}

void
diod_conf_set_tcpwrappers (int i)
{
    config.tcpwrappers = i;
}

int
diod_conf_get_allowprivate (void)
{
    return config.allowprivate;
}

void
diod_conf_set_allowprivate (int i)
{
    config.allowprivate = i;
}

int
diod_conf_get_exit_on_lastuse (void)
{
    return config.exit_on_lastuse;
}

void
diod_conf_set_exit_on_lastuse (int i)
{
    config.exit_on_lastuse = i;
}

int
diod_conf_get_runasuid (uid_t *uidp)
{
    if (uidp && config.runasuid_valid )
        *uidp = config.runasuid;
    return config.runasuid_valid;
}

void
diod_conf_set_runasuid (uid_t uid)
{
    config.runasuid_valid = 1;
    config.runasuid = uid;
}

char *
diod_conf_get_diodpath (void)
{
    return config.diodpath;
}

void
diod_conf_set_diodpath (char *s)
{
    if (!(config.diodpath = strdup (s)))
        msg_exit ("out of memory");
}

List
diod_conf_get_diodlisten (void)
{
    return config.diodlisten;
}

void
diod_conf_set_diodlisten (char *s)
{
    char *cpy;

    if (config.diodlisten) {
        list_destroy (config.diodlisten);
        config.diodlisten = NULL;
    }
    if (s) {
        if (!(cpy = strdup (s)))
            msg_exit ("out of memory");
        config.diodlisten = list_create ((ListDelF)free);
        list_append (config.diodlisten, cpy);
    }
}

List
diod_conf_get_diodctllisten (void)
{
    return config.diodctllisten;
}

void
diod_conf_set_diodctllisten (char *s)
{
    char *cpy;

    if (config.diodctllisten) {
        list_destroy (config.diodctllisten);
        config.diodctllisten = NULL;
    }
    if (s) {
        if (!(cpy = strdup (s)))
            msg_exit ("out of memory");
        config.diodctllisten = list_create ((ListDelF)free);
        list_append (config.diodctllisten, cpy);
    }
}

static void
_vfprintf (char *path, FILE *f, char *fmt, ...)
{
    va_list ap;
    int ret;

    va_start (ap, fmt);
    ret = vfprintf (f, fmt, ap);
    va_end (ap);
    if (ret < 0)
        err_exit ("write %s", path);
}

static void
_write_list_of_strings (char *path, FILE *f, char *name, List l)
{
    ListIterator itr;
    int first = 1;
    char *s;

    if (!(itr = list_iterator_create (l)))
        err_exit ("out of memory");
    while ((s = list_next (itr))) {
        if (first) {
            _vfprintf (path, f, "%s = { ", name);
            first = 0;
        } else 
            _vfprintf (path, f, ", ");
        _vfprintf (path, f, "\"%s\"", s);
    }
    list_iterator_destroy (itr);
    _vfprintf (path, f, " }\n");
}


/* Generate a config file, containing possible command line overrides. 
 * FIXME: unlink config file on error.
 */
char *
diod_conf_mkconfig (void)
{
    char *cpy, path[] = "/tmp/diod.conf.XXXXXX";
    int fd;
    FILE *f;

    if ((fd = mkstemp (path)) < 0)
        err_exit ("mkstemp %s", path);
    if (fchmod (fd, 0644) < 0)
        err_exit ("chmod %s", path);
    if (!(f = fdopen (fd, "w")))
        err_exit ("fdopen %s", path);

    _vfprintf (path, f, "debuglevel = %d\n", config.debuglevel);
    _vfprintf (path, f, "nwthreads = %d\n", config.nwthreads);
    _vfprintf (path, f, "foreground = %d\n", config.foreground);
    _vfprintf (path, f, "munge = %d\n", config.munge);
    _vfprintf (path, f, "tcpwrappers = %d\n", config.tcpwrappers);
    _vfprintf (path, f, "allowprivate = %d\n", config.allowprivate);
    _vfprintf (path, f, "exit_on_lastuse = %d\n", config.exit_on_lastuse);
    _vfprintf (path, f, "diodpath = \"%s\"\n", config.diodpath);
    _write_list_of_strings (path, f, "diodlisten", config.diodlisten);
    _write_list_of_strings (path, f, "diodctllisten", config.diodctllisten);
    _write_list_of_strings (path, f, "exports", config.exports);

    if (fclose (f) != 0)
        err_exit ("fclose %s", path);
    if (!(cpy = strdup (path)))
        err_exit ("out of memory");

    return cpy;
}

/* Tattach verifies path against configured exports.
 * Return 1 on ALLOWED, 0 on DENIED.  On DENjIED, put errno in *errp.
 * FIXME: verify host/ip/uid once we parse those in config file
 * FIXME: verify path contains no symlinks below export dir
 */
int
diod_conf_match_export (char *path, char *host, char *ip, uid_t uid, int *errp)
{
    ListIterator itr;
    char *el;
    int res = 0;
    int plen = strlen(path);

    if (strstr (path, "/..") != NULL) {
        *errp = EPERM;
        goto done;
    }
    if ((itr = list_iterator_create (config.exports)) == NULL) {
        *errp = ENOMEM;
        goto done;
    }
    while ((el = list_next (itr))) {
        int len = strlen (el);

        if (strcmp (el, "/") == 0) {
            res = 1;
            break;
        }
        while (len > 0 && el[len - 1] == '/')
            len--; 
        if (plen == len && strncmp (el, path, len) == 0) {
            res = 1;
            break;
        }
        if (plen > len && path[len] == '/') {
            res = 1;
            break;
        }
    }
    list_iterator_destroy (itr);
    if (res == 0)
        *errp = EPERM;
done:
    return res;
}

char *
diod_conf_cat_exports (void)
{
    ListIterator itr = NULL;
    char *ret = NULL;
    int len = 0, elen;
    char *el;

    if ((itr = list_iterator_create (config.exports)) == NULL)
        goto done;        
    while ((el = list_next (itr))) {
        elen = strlen (el) + 1;
        ret = ret ? realloc (ret, len + elen + 1) : malloc (elen + 1);
        if (ret == NULL)
            goto done;
        snprintf (ret + len, elen + 1, "%s\n", el);
        len += elen;
    }
    ret[len] = '\0';
done:
    if (itr)
        list_iterator_destroy (itr);
    return ret;
}

static void
_str_list_append (List l, char *s)
{
    char *cpy = strdup (s);

    if (!cpy)
        msg_exit ("out of memory");
    if (!list_append (l, cpy)) {
        free (cpy);
        msg_exit ("out of memory");
    }
}

static List
_str_list_create (void)
{
    List l = list_create ((ListDelF)free);

    if (!l)
        msg_exit ("out of memory");
    return l;
}

void
diod_conf_set_export (char *path)
{
    if (config.exports)
        list_destroy (config.exports);
    config.exports = _str_list_create ();
    _str_list_append (config.exports, path);
}

void
diod_conf_validate_exports (void)
{
    ListIterator itr;
    char *el;
    struct stat sb;

    if (config.exports == NULL)
        msg_exit ("no exports defined");
    if ((itr = list_iterator_create (config.exports)) == NULL)
        msg_exit ("out of memory");
    while ((el = list_next (itr))) {
        if (*el != '/')
            msg_exit ("exports should begin with '/'");
        if (strstr (el, "/..") != 0)
            msg_exit ("exports should not contain '/..'"); /* FIXME */
        if (stat (el, &sb) < 0)
            err_exit ("could not stat %s", el);
        if (!S_ISDIR (sb.st_mode))
            msg_exit ("%s does not refer to a directory", el);
    }
    list_iterator_destroy (itr);
}

#ifdef HAVE_LUA_H
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
_lua_getglobal_uint (char *path, lua_State *L, char *key, unsigned int *ip)
{
    int res = 0;

    lua_getglobal (L, key);
    if (!lua_isnil (L, -1)) {
        if (!lua_isnumber (L, -1))
            msg_exit ("%s: `%s' should be number", path, key);
        if (ip)
            *ip = (unsigned int)lua_tonumber (L, -1);
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
            cpy = strdup ((char *)lua_tostring (L, -1));
            if (!cpy)
                msg_exit ("out of memory");
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
        l = _str_list_create();
        for (i = 1; ;i++) {
            lua_pushinteger(L, (lua_Integer)i);
            lua_gettable (L, -2);
            if (lua_isnil (L, -1))
                break;
            if (!lua_isstring (L, -1))
                msg_exit ("%s: `%s[%d]' should be string", path, key, i);
            _str_list_append (l, (char *)lua_tostring (L, -1));
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
diod_conf_init_config_file (char *path)
{
    lua_State *L;
    static char buf[PATH_MAX];

    if (!path) {
        snprintf (buf, sizeof (buf), "%s/diod.conf", X_SYSCONFDIR);
        if (access (buf, R_OK) == 0)
            path = buf;  /* missing default config file is not fatal */
    }
    if (path) {
        L = lua_open ();
        luaopen_base (L);
        luaopen_table (L);
        //luaopen_io (L);
        luaopen_string (L);
        luaopen_math (L);

        if (luaL_loadfile (L, path) || lua_pcall (L, 0, 0, 0))
            msg_exit ("%s", lua_tostring (L, -1));
        
        _lua_getglobal_int (path, L, "debuglevel", &config.debuglevel);
        _lua_getglobal_int (path, L, "nwthreads", &config.nwthreads);
        _lua_getglobal_int (path, L, "foreground", &config.foreground);
        _lua_getglobal_int (path, L, "munge", &config.munge);
        _lua_getglobal_int (path, L, "tcpwrappers", &config.tcpwrappers);
        _lua_getglobal_int (path, L, "allowprivate", &config.allowprivate);
        _lua_getglobal_int (path, L, "exit_on_lastuse",
                            &config.exit_on_lastuse);
        if (_lua_getglobal_uint (path, L, "runasuid", &config.runasuid))
            config.runasuid_valid = 1;
        _lua_getglobal_string (path, L, "diodpath", &config.diodpath);
        _lua_getglobal_list_of_strings (path, L, "diodctllisten",
                            &config.diodctllisten);
        _lua_getglobal_list_of_strings (path, L, "diodlisten",
                            &config.diodlisten);
        _lua_getglobal_list_of_strings (path, L, "exports", &config.exports);

        lua_close(L);
    }
}
#endif /* HAVE_LUA_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
