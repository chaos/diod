/*****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security, LLC.
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

/* dtop.c - watch diod performance */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <curses.h>
#include <errno.h>
#include <ctype.h>
#include <libgen.h>
#include <curses.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include <inttypes.h>
#include <dirent.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "list.h"
#include "hostlist.h"
#include "diod_log.h"
#include "diod_sock.h"
#include "diod_auth.h"
#include "sample.h"

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...)
    __attribute__ ((format (printf, 4, 5)));

#define OPTIONS "h:p:"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"hostlist",   required_argument,      0, 'h'},
    {"poll-period",required_argument,      0, 'p'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 64
#endif

typedef struct {
    char host[MAXHOSTNAMELEN];
    char aname[PATH_MAX];
} Tpoolkey; 

typedef struct {
    Tpoolkey key;
    sample_t rbytes;
    sample_t wbytes;
    sample_t iops;
    sample_t numfids;
    sample_t numreqs;
    sample_t totreqs;
    sample_t nreqs[P9_RWSTAT + 1];
} Tpool;

typedef struct {
    char *host;
    double poll_sec;
    int fd;
    Npcfid *root;
    pthread_t thread;
    int anames;
    double rmbps;
    double wmbps;
    double numfids;
    double numreqs;
    double totreqs;
    double numconns;
    sample_t mem_cached;
    sample_t mem_dirty;
    sample_t nfs_ops;
    time_t last_poll;
} Server;

#define TOPWIN_LINES    7

static List tpools = NULL;
static List servers = NULL;
static pthread_mutex_t dtop_lock = PTHREAD_MUTEX_INITIALIZER;

static Server *_server_create (char *host, double poll_sec);
static void _destroy_tpool (Tpool *tp);
static void _curses_watcher (double update_secs);

static int stale_secs = 5;

static void
usage (void)
{
    fprintf (stderr,
"Usage: dtop [-p sec] -h hostlist\n"
"   -h,--hostlist HOSTS   hostnames to monitor (default localhost)\n"
"   -p,--poll-period SEC  polling period in seconds (default 1.0)\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    hostlist_t hl = NULL;
    hostlist_iterator_t itr;
    double poll_sec = 1;
    char *host;
    int c;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'h':   /* --hostlist NAME */
                if (!hl) {
                    if (!(hl = hostlist_create (optarg)))
                        err_exit ("failed to create hostlist");
                } else {
                    if (hostlist_push (hl, optarg) == 0)
                        err_exit ("failed to append to hostlist");
                }
                break;
            case 'p':   /* --poll-period SEC */
                poll_sec = strtod (optarg, NULL);
                if (poll_sec <= 0.0)
                    msg_exit ("polling period should be positive");
                break;
            default:
                usage ();
        }
    }
    if (optind < argc)
        usage ();
    if (!hl) {
        if (!(hl = hostlist_create ("localhost")))
            err_exit ("failed to create hostlist");
    }

    if (!(tpools = list_create ((ListDelF)_destroy_tpool)))
        err_exit ("out of memory");

    /* Launch readers - create a list of Servers.
     */
    if (!(servers = list_create (NULL)))
        err_exit ("out of memory");
    if (!(itr = hostlist_iterator_create (hl)))
        err_exit ("out of memory");
    while ((host = hostlist_next (itr))) {
        if (!(list_append (servers, _server_create (host, poll_sec))))
            err_exit ("out of memory");
    }
    hostlist_iterator_destroy (itr);

    _curses_watcher (poll_sec/2);

    diod_log_fini ();

    exit (0);
}

static void
_update_display_topwin (WINDOW *win)
{
    int y = 0;
    time_t now = time (NULL);
    char *ts = ctime (&now);
    double statfs=0, lopen=0, lcreate=0, symlink=0, mknod=0;
    double rename=0, readlink=0, getattr=0, setattr=0, readdir=0;
    double fsync=0, lock=0, getlock=0, link=0, mkdir=0;
    double version=0, auth=0, attach=0, flush=0, walk=0;
    double read=0, write=0, clunk=0, remove=0;
    double rmbps=0, wmbps=0;
    ListIterator itr;
    Tpool *tp;

    pthread_mutex_lock (&dtop_lock);
    if (!(itr = list_iterator_create (tpools)))
        msg_exit ("out of memory");    
    while ((tp = list_next (itr))) {
        statfs   += sample_rate (tp->nreqs[P9_TSTATFS], now);
        lopen    += sample_rate (tp->nreqs[P9_TLOPEN], now);
        lcreate  += sample_rate (tp->nreqs[P9_TLCREATE], now);
        symlink  += sample_rate (tp->nreqs[P9_TSYMLINK], now);
        mknod    += sample_rate (tp->nreqs[P9_TMKNOD], now);
        rename   += sample_rate (tp->nreqs[P9_TRENAME], now);
        readlink += sample_rate (tp->nreqs[P9_TREADLINK], now);
        getattr  += sample_rate (tp->nreqs[P9_TGETATTR], now);
        setattr  += sample_rate (tp->nreqs[P9_TSETATTR], now);
        readdir  += sample_rate (tp->nreqs[P9_TREADDIR], now);
        fsync    += sample_rate (tp->nreqs[P9_TFSYNC], now);
        lock     += sample_rate (tp->nreqs[P9_TLOCK], now);
        getlock  += sample_rate (tp->nreqs[P9_TGETLOCK], now);
        link     += sample_rate (tp->nreqs[P9_TLINK], now);
        mkdir    += sample_rate (tp->nreqs[P9_TMKDIR], now);
        version  += sample_rate (tp->nreqs[P9_TVERSION], now);
        auth     += sample_rate (tp->nreqs[P9_TAUTH], now);
        attach   += sample_rate (tp->nreqs[P9_TATTACH], now);
        flush    += sample_rate (tp->nreqs[P9_TFLUSH], now);
        walk     += sample_rate (tp->nreqs[P9_TWALK], now);
        read     += sample_rate (tp->nreqs[P9_TREAD], now);
        write    += sample_rate (tp->nreqs[P9_TWRITE], now);
        clunk    += sample_rate (tp->nreqs[P9_TCLUNK], now);
        remove   += sample_rate (tp->nreqs[P9_TREMOVE], now);

        rmbps    += sample_rate (tp->rbytes, now) / (1024*1024);
        wmbps    += sample_rate (tp->wbytes, now) / (1024*1024);
    }
    list_iterator_destroy (itr);
    pthread_mutex_unlock (&dtop_lock);

    wclear (win);
    mvwprintw (win, y, 0, "%s", "DIOD - Distributed I/O Daemon");
    mvwprintw (win, y++, 55, "%*s", (int)(strlen (ts) - 1), ts);
    y++; 

    mvwprintw (win, y++, 0,
      "Ops/s: %5.0f statfs  %6.0f lopen    %6.0f lcreate %6.0f symlink %6.0f mknod",
      statfs, lopen, lcreate, symlink, mknod);
    mvwprintw (win, y++, 0,
      "      %6.0f rename  %6.0f readlink %6.0f getattr %6.0f setattr %6.0f link",
      rename, readlink, getattr, setattr, link);
    mvwprintw (win, y++, 0,
      "      %6.0f fsync   %6.0f lock     %6.0f getlock %6.0f readdir %6.0f mkdir",
      fsync, lock, getlock, readdir, mkdir);
    mvwprintw (win, y++, 0,
      "      %6.0f version %6.0f auth     %6.0f attach  %6.0f flush   %6.0f walk",
      version, auth, attach, flush, walk);
    mvwprintw (win, y++, 0,
      "      %6.0f read    %6.0f write    %6.0f clunk   %6.0f remove",
      read, write, clunk, remove);
    y++;
    mvwprintw (win, y++, 0,
      "GB/s:%7.3f read   %7.3f write",
      rmbps / 1024, wmbps / 1024);
    wrefresh (win);
}

static void
_update_display_normal (WINDOW *win)
{
    ListIterator itr;
    Tpool *tp;
    int y = 0;
    time_t t = time(NULL);

    wclear (win);
    wmove (win, y++, 0);

    wattron (win, A_REVERSE);
    wprintw (win,
             "%10.10s %14.14s %6.6s %5.5s %5.5s %5.5s %5.5s %5.5s\n",
             "server", "aname", "fids", "reqs", "queue", "rMB/s", "wMB/s",
              "IOPS");
    wattroff (win, A_REVERSE);

    pthread_mutex_lock (&dtop_lock);
    if (!(itr = list_iterator_create (tpools)))
        msg_exit ("out of memory");    
    while ((tp = list_next (itr))) {
        if (sample_val (tp->numfids, t) == 0)
            continue;
        mvwprintw (win, y++, 0,
                "%10.10s %14.14s %6.0f %5.0f %5.0f %5.0f %5.0f %5.0f",
                    tp->key.host, tp->key.aname,
                    sample_val (tp->numfids, t),
                    sample_rate (tp->totreqs, t),
                    sample_val (tp->numreqs, t),
                    sample_rate (tp->rbytes, t) / (1024*1024),
                    sample_rate (tp->wbytes, t) / (1024*1024),
                    sample_rate (tp->iops, t));
    }
    list_iterator_destroy (itr);
    pthread_mutex_unlock (&dtop_lock);
    wrefresh (win);
}

static int
_match_serverhost (Server *sp, char *host)
{
    if (!strcmp (sp->host, host))
        return 1;
    return 0;
}

static void
_update_display_server (WINDOW *win)
{
    ListIterator itr;
    Server *sp;
    Tpool *tp;
    time_t t = time (NULL);
    int y = 0;

    wclear (win);
    wmove (win, y++, 0);

    wattron (win, A_REVERSE);
    wprintw (win,
    "%10.10s %5.5s %5.5s %6.6s %5.5s %5.5s %5.5s %5.5s %5.5s %5.5s %5.5s\n",
    "server", "aname", "conns", "fids", "reqs", "queue", "rMB/s", "wMB/s",
    "cache", "dirty", "nfs/s");
    wattroff (win, A_REVERSE);

    /* zero server stats */
    if (!(itr = list_iterator_create (servers)))
        msg_exit ("out of memory");    
    while ((sp = list_next (itr))) {
        sp->anames = sp->numfids = sp->numreqs = sp->totreqs = 0;
        sp->rmbps = sp->wmbps = 0;
    }
    list_iterator_destroy (itr);

    if (!(itr = list_iterator_create (tpools)))
        msg_exit ("out of memory");    
    while ((tp = list_next (itr))) {
        sp = list_find_first (servers, (ListFindF)_match_serverhost,
                              tp->key.host);
        if (sp) {
            sp->anames++;
            sp->numfids += sample_val (tp->numfids, t);
            sp->totreqs += sample_rate (tp->totreqs, t);
            sp->numreqs += sample_val (tp->numreqs, t); /* queue */
            sp->rmbps += sample_rate (tp->rbytes, t) / (1024*1024);
            sp->wmbps += sample_rate (tp->wbytes, t) / (1024*1024);
        }
    }
    list_iterator_destroy (itr);

    /* iterate thru servers displaying data */
    if (!(itr = list_iterator_create (servers)))
        msg_exit ("out of memory");    
    while ((sp = list_next (itr))) {
        mvwprintw (win, y, 0, "%10.10s ", sp->host);
        if (t - sp->last_poll < stale_secs) {
            mvwprintw (win, y, 11,
            "%5.0d %5.0f %6.0f %5.0f %5.0f %5.0f %5.0f %5.0f %5.0f %5.0f",
            sp->anames,
            sp->numconns,
            sp->numfids,
            sp->totreqs,
            sp->numreqs,
            sp->rmbps,
            sp->wmbps,
            sample_val (sp->mem_cached, t) / 1024,
            sample_val (sp->mem_dirty, t) / 1024,
            sample_rate (sp->nfs_ops, t));
        }
        y++;
        

    }
    list_iterator_destroy (itr);

    wrefresh (win);
}

static void
_update_display_aname (WINDOW *win)
{
    wclear (win);
    wrefresh (win);
}

typedef enum {VIEW_NORMAL, VIEW_SERVER, VIEW_ANAME} view_t;
static void
_curses_watcher (double update_secs)
{
    WINDOW *topwin;
    WINDOW *subwin;
    int c;
    view_t view = VIEW_NORMAL;

    if (!(topwin = initscr ()))
        err_exit ("error initializing parent window");
    if (!(subwin = newwin (256, 80, 9, 0)))
        err_exit ("error initializing subwindow");
    
    raw ();
    noecho ();
    timeout (update_secs * 1000);
    keypad (topwin, TRUE);
    curs_set (0);

    while (!isendwin ()) {
        _update_display_topwin (topwin);
        switch (view) {
            case VIEW_NORMAL:
                _update_display_normal (subwin);
                break;
            case VIEW_ANAME:
                _update_display_aname (subwin);
                break;
            case VIEW_SERVER:
                _update_display_server (subwin);
                break;
        }
        switch ((c = getch ())) {
            case 'q':
            case 0x03:
                delwin (subwin);
                endwin ();
                break;
            case 's': /* server view */
                view = VIEW_SERVER;
                break;
            case 'a': /* aname view */
                view = VIEW_ANAME;
                break;
            case 'n': /* normal view */
                view = VIEW_NORMAL;
                break;
            case ERR: /* timeout */
                break;
        }
    }
}

static int
_match_tpool (Tpool *x, Tpoolkey *key)
{
    if (!strcmp (key->host, x->key.host) && !strcmp(key->aname, x->key.aname))
        return 1;
    return 0;
}

static Tpool *
_create_tpool (Tpoolkey *key)
{
    Tpool *tp;
    int i;

    if (!(tp = malloc (sizeof (*tp))))
        msg_exit ("out of memory");
    memset (tp, 0, sizeof (*tp));
    strcpy (tp->key.host, key->host);
    strcpy (tp->key.aname, key->aname);
    tp->rbytes = sample_create (stale_secs);
    tp->wbytes = sample_create (stale_secs);
    tp->iops = sample_create (stale_secs);
    tp->numfids = sample_create (stale_secs);
    tp->totreqs = sample_create (stale_secs);
    tp->numreqs = sample_create (stale_secs); /* in queue */
    for (i = 0; i < sizeof(tp->nreqs)/sizeof(tp->nreqs[0]); i++)
        tp->nreqs[i] = sample_create (stale_secs);

    return tp;
}

static void
_destroy_tpool (Tpool *tp)
{
    int i;

    for (i = 0; i < sizeof(tp->nreqs)/sizeof(tp->nreqs[0]); i++)
        sample_destroy (tp->nreqs[i]);
    free (tp);
}

static u64
_sum_nreqs (Npstats *sp)
{
    u64 res = 0;
    int i;

    for (i = 0; i < sizeof(sp->nreqs)/sizeof(sp->nreqs[0]); i++)
        res += sp->nreqs[i];
    return res;
}

static void
_update (char *host, time_t t, char *s)
{
    Npstats stats;
    Tpoolkey key;
    Tpool *tp;
    int i;

    memset (stats.nreqs, 0, sizeof(stats.nreqs));
    if (np_decode_tpools_str (s, &stats) < 0) /* mallocs stats.name */
        return;
    snprintf (key.host, sizeof(key.host), "%s", host);
    snprintf (key.aname, sizeof(key.aname), "%s", stats.name);

    pthread_mutex_lock (&dtop_lock);
    if (!(tp = list_find_first (tpools, (ListFindF)_match_tpool, &key))) {
        tp = _create_tpool (&key);
        if (!list_append (tpools, tp))
            msg_exit ("out of memory");
    }
    sample_update (tp->rbytes, (double)stats.rbytes, t);
    sample_update (tp->wbytes, (double)stats.wbytes, t);
    sample_update (tp->iops,
                   (double)(stats.nreqs[P9_TREAD] + stats.nreqs[P9_TWRITE]), t);
    sample_update (tp->numfids, (double)stats.numfids, t);
    sample_update (tp->numreqs, (double)stats.numreqs, t);
    sample_update (tp->totreqs, (double)_sum_nreqs(&stats), t);
    for (i = 0; i < sizeof(tp->nreqs)/sizeof(tp->nreqs[0]); i++)
        sample_update (tp->nreqs[i], (double)stats.nreqs[i], t);
    pthread_mutex_unlock (&dtop_lock);

    if (stats.name)
        free (stats.name);
}

static int
_read_ctl_tpools (Server *sp)
{
    time_t now;
    char *buf, *s, *p;

    if (!(buf = npc_aget (sp->root, "tpools")))
        return -1;
    now = time (NULL);
    for (s = buf; s && *s; s = p) {
        p = strchr (s, '\n');
        if (p)
            *p++ = '\0';
        _update (sp->host, now, s);
    }
    free (buf);
    return 0;
}

static int
_read_ctl_meminfo (Server *sp)
{
    time_t now;
    char *buf, *s, *p;

    if (!(buf = npc_aget (sp->root, "meminfo")))
        return -1;
    now = time (NULL);
    for (s = buf; s && *s; s = p) {
        p = strchr (s, '\n');
        if (p)
            *p++ = '\0';
        if (!strncmp (s, "Cached:", 7)) {
            sample_update (sp->mem_cached, strtod (s + 7, NULL), now);
        } else if (!strncmp (s, "Dirty:", 6)) {
            sample_update (sp->mem_dirty, strtod (s + 6, NULL), now);
            break;
        }
    }
    free (buf);
    return 0;
}

/* nfs module may not be loaded, so this is not fatal like the others.
 */
static int
_read_ctl_nfsops (Server *sp)
{
    time_t now;
    char *buf, *s, *p;

    if ((buf = npc_aget (sp->root, "net.rpc.nfs"))) {
        now = time (NULL);
        for (s = buf; s && *s; s = p) {
            p = strchr (s, '\n');
            if (p)
                *p++ = '\0';
            if (!strncmp (s, "rpc", 3)) {
                sample_update (sp->nfs_ops, strtod (s + 3, NULL), now);
                break;
            }
        }
	free (buf);
    }
    return 0;
}

static int
_read_ctl_connections (Server *sp)
{
    time_t now;
    char *buf, *s, *p;
    int count = 0;

    if (!(buf = npc_aget (sp->root, "connections")))
        return -1;
    now = time (NULL);
    for (s = buf; s && *s; s = p) {
        p = strchr (s, '\n');
        if (p)
            *p++ = '\0';
        count++;
    }
    free (buf);
    sp->numconns = count;
    return 0;
}

static void *
_reader (void *arg)
{
    Server *sp = (Server *)arg;

    for (;;) {
        if (sp->fd == -1)
            sp->fd = diod_sock_connect (sp->host, "564", DIOD_SOCK_QUIET);
        if (sp->fd == -1)
            goto skip;
        if (sp->root == NULL)
            sp->root = npc_mount (sp->fd, 65536, "ctl", diod_auth);
        if (sp->root == NULL) {
            (void)close (sp->fd);
            sp->fd = -1;
            goto skip;
        }
        if (_read_ctl_tpools (sp) < 0 || _read_ctl_meminfo (sp) < 0
         || _read_ctl_nfsops (sp) < 0 || _read_ctl_connections (sp) < 0) {
            (void)npc_umount (sp->root); /* closes fd */
            sp->root = NULL;
            sp->fd = -1;
            goto skip;
        }
        sp->last_poll = time (NULL);
skip:
        usleep ((useconds_t)(sp->poll_sec * 1E6));
    }
    return NULL;
}

static Server *
_server_create (char *host, double poll_sec)
{
    Server *sp;
    int err;

    if (!(sp = malloc (sizeof (Server))))
        err_exit ("out of memory");
    memset (sp, 0, sizeof (*sp));
    sp->poll_sec = poll_sec;
    if (!(sp->host = strdup (host)))
        err_exit ("out of memory");
    sp->fd = -1;
    sp->root = NULL;
    sp->mem_cached = sample_create (stale_secs);
    sp->mem_dirty = sample_create (stale_secs);
    sp->nfs_ops = sample_create (stale_secs);
    if ((err = pthread_create (&sp->thread, NULL, _reader, sp)))
        errn_exit (err, "pthread_create");
    return sp;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
