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
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include <inttypes.h>
#include <dirent.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "xpthread.h"

#include "list.h"
#include "hostlist.h"
#include "diod_log.h"
#include "diod_sock.h"
#include "diod_auth.h"
#include "sample.h"

int mvwprintw(WINDOW *win, int y, int x, const char *fmt, ...)
    __attribute__ ((format (printf, 4, 5)));

#define OPTIONS "h:P:p:"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"hostlist",   required_argument,      0, 'h'},
    {"poll-period",required_argument,      0, 'P'},
    {"port",       required_argument,      0, 'p'},
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

/* Tpool view.
 * This is the actual data we sample.
 */
typedef struct {
    Tpoolkey key;
    sample_t rbytes;
    sample_t wbytes;
    sample_t iops;
    sample_t numfids;
    sample_t numreqs;
    sample_t totreqs;
    sample_t nreqs[P9_RWSTAT + 1];
    sample_t rcount[NPSTATS_RWCOUNT_BINS];
    sample_t wcount[NPSTATS_RWCOUNT_BINS];
} TpoolStats;
static List tpools = NULL;

/* Aname stats.
 * These are derived from tpool data in _update_display_aname ().
 */
typedef struct {
    char *aname;
    double rmbps;
    double wmbps;
    double iops;
    double numfids;
    double numreqs;
    double totreqs;
} AnameStats;

/* Server stats.
 * The first part is state for polling servers.
 * The second part is stats for servers, derived from Tpool stats as needed.
 */
typedef struct {
    char *host;
    char *port;
    double poll_sec;
    int fd;
    Npcfid *root;
    time_t last_poll;
    pthread_t thread;
    /* The following data is only updated when in 'server' view.
     * It is derived from tpool data in _update_display_server ().
     */
    int numanames;
    double rmbps;
    double wmbps;
    double numfids;
    double numreqs;
    double totreqs;
    double numconns;
    sample_t mem_cached;
    sample_t mem_dirty;
    sample_t nfs_ops;
} Server;
static List servers = NULL; /* static list of servers we are polling */

#define TOPWIN_LINES    7

static pthread_mutex_t dtop_lock = PTHREAD_MUTEX_INITIALIZER;

static Server *_create_server (char *host, char *port, double poll_sec);
static void _destroy_server  (Server *sp);
static int _compare_aname(AnameStats *ap1, AnameStats *ap2);
static int _match_aname(AnameStats *ap, char *aname);
static AnameStats *_create_aname (char *aname);
static void _destroy_aname (AnameStats *ap);
static void _destroy_tpool (TpoolStats *tp);
static void _curses_watcher (double update_secs);

static int stale_secs = 5;
static int exiting = 0;

static void
usage (void)
{
    fprintf (stderr,
"Usage: dtop [-p sec] -h hostlist\n"
"   -h,--hostlist HOSTS   hostnames to monitor (default localhost)\n"
"   -P,--poll-period SEC  polling period in seconds (default 1.0)\n"
"   -p,--port PORT        port (default 564)\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    hostlist_t hl = NULL;
    hostlist_iterator_t itr;
    double poll_sec = 1;
    char *host, *port = "564";
    int c;
    sigset_t sigs;
    Server *sp;
    ListIterator si;

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
            case 'P':   /* --poll-period SEC */
                poll_sec = strtod (optarg, NULL);
                if (poll_sec <= 0.0)
                    msg_exit ("polling period should be positive");
                break;
            case 'p':   /* --port PORT */
                port = optarg;
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

    sigemptyset (&sigs);
    sigaddset (&sigs, SIGPIPE);
    if (sigprocmask (SIG_BLOCK, &sigs, NULL) < 0)
        err_exit ("sigprocmask");

    /* Launch readers - create a list of Servers.
     */
    if (!(servers = list_create (NULL)))
        err_exit ("out of memory");
    if (!(itr = hostlist_iterator_create (hl)))
        err_exit ("out of memory");
    while ((host = hostlist_next (itr))) {
        if (!(list_append (servers, _create_server (host, port, poll_sec))))
            err_exit ("out of memory");
    }
    hostlist_iterator_destroy (itr);

    _curses_watcher (poll_sec/2);

    if (!(si = list_iterator_create (servers)))
        msg_exit ("out of memory");
    while ((sp = list_next (si)))
        _destroy_server (sp);
    list_iterator_destroy (si);

    diod_log_fini ();

    exit (0);
}

static void
_update_display_topwin (WINDOW *win)
{
    int y = 0;
    time_t now = time (NULL);
    char *ts = ctime (&now);
    int nservers = 0, upservers = 0;
    double statfs=0, lopen=0, lcreate=0, symlink=0, mknod=0;
    double rename=0, readlink=0, getattr=0, setattr=0, readdir=0;
    double fsync=0, lock=0, getlock=0, link=0, mkdir=0;
    double version=0, auth=0, attach=0, flush=0, walk=0;
    double read=0, write=0, clunk=0, remove=0;
    double rmbps=0, wmbps=0;
    ListIterator itr;
    TpoolStats *tp;
    Server *sp;

    xpthread_mutex_lock (&dtop_lock);
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
    if (!(itr = list_iterator_create (servers)))
        msg_exit ("out of memory");    
    while ((sp = list_next (itr))) {
        if (now - sp->last_poll < stale_secs)
            upservers++;
        nservers++;
    }
    list_iterator_destroy (itr);

    xpthread_mutex_unlock (&dtop_lock);

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
    mvwprintw (win, y, 0,
      "GB/s:%7.3f read   %7.3f write",
      rmbps / 1024, wmbps / 1024);
    mvwprintw (win, y, 57,
      "Live servers: %d/%d", upservers, nservers);
    wrefresh (win);
}

static void
_update_display_tpool (WINDOW *win)
{
    ListIterator itr;
    TpoolStats *tp;
    int y = 0;
    time_t now = time(NULL);

    wclear (win);
    wmove (win, y++, 0);

    wattron (win, A_REVERSE);
    wprintw (win,
             "%10.10s %14.14s %6.6s %5.5s %5.5s %5.5s %5.5s %5.5s\n",
             "server", "aname", "fids", "reqs", "queue", "rMB/s", "wMB/s",
              "IOPS");
    wattroff (win, A_REVERSE);

    xpthread_mutex_lock (&dtop_lock);
    if (!(itr = list_iterator_create (tpools)))
        msg_exit ("out of memory");    
    while ((tp = list_next (itr))) {
        if (sample_val (tp->numfids, now) == 0)
            continue;
        mvwprintw (win, y++, 0,
                "%10.10s %14.14s %6.0f %5.0f %5.0f %5.0f %5.0f %5.0f",
                    tp->key.host, tp->key.aname,
                    sample_val (tp->numfids, now),
                    sample_rate (tp->totreqs, now),
                    sample_val (tp->numreqs, now),
                    sample_rate (tp->rbytes, now) / (1024*1024),
                    sample_rate (tp->wbytes, now) / (1024*1024),
                    sample_rate (tp->iops, now));
    }
    list_iterator_destroy (itr);
    xpthread_mutex_unlock (&dtop_lock);
    wrefresh (win);
}

static void
_update_display_aname (WINDOW *win)
{
    static List anames = NULL;
    ListIterator itr;
    TpoolStats *tp;
    AnameStats *ap;
    int y = 0;
    time_t now = time(NULL);
    int sort_needed = 0;

    wclear (win);
    wmove (win, y++, 0);

    wattron (win, A_REVERSE);
    wprintw (win,
             "%25.25s %6.6s %5.5s %5.5s %5.5s %5.5s %5.5s\n",
             "aname", "fids", "reqs", "queue", "rMB/s", "wMB/s", "IOPS");
    wattroff (win, A_REVERSE);

    if (!anames && !(anames = list_create ((ListDelF)_destroy_aname)))
        err_exit ("out of memory");
    if (!(itr = list_iterator_create (anames)))
        msg_exit ("out of memory");
    while ((ap = list_next (itr))) {
        ap->rmbps = ap->wmbps = ap->iops = ap->numfids
                  = ap->numreqs = ap->totreqs = 0;
    }
    list_iterator_destroy (itr);

    xpthread_mutex_lock (&dtop_lock);
    if (!(itr = list_iterator_create (tpools)))
        msg_exit ("out of memory");
    while ((tp = list_next (itr))) {
        ap = list_find_first (anames, (ListFindF)_match_aname, tp->key.aname);
        if (!ap) {
            ap = _create_aname (tp->key.aname);
            if (!list_append (anames, ap))
                msg_exit ("out of memory");
            sort_needed = 1;
        }
        ap->rmbps += sample_rate (tp->rbytes, now) / (1024*1024);
        ap->wmbps += sample_rate (tp->wbytes, now) / (1024*1024);
        ap->iops += sample_rate (tp->iops, now);
        ap->numfids += sample_val (tp->numfids, now);
        ap->numreqs += sample_val (tp->numreqs, now);
        ap->totreqs += sample_rate (tp->totreqs, now);
    }
    list_iterator_destroy (itr);
    xpthread_mutex_unlock (&dtop_lock);

    if (sort_needed)
        list_sort (anames, (ListCmpF)_compare_aname);
    if (!(itr = list_iterator_create (anames)))
        msg_exit ("out of memory");
    while ((ap = list_next (itr))) {
        if (ap->numfids == 0)
            continue;
        mvwprintw (win, y++, 0,
                "%25.25s %6.0f %5.0f %5.0f %5.0f %5.0f %5.0f",
                    ap->aname,
                    ap->numfids,
                    ap->totreqs,
                    ap->numreqs,
                    ap->rmbps,
                    ap->wmbps,
                    ap->iops);
    }
    list_iterator_destroy (itr);
    wrefresh (win);
}


static void
_update_display_rwcount (WINDOW *win)
{
    ListIterator itr;
    TpoolStats *tp;
    int y = 0;
    time_t now = time(NULL);

    wclear (win);
    wmove (win, y++, 0);

    wattron (win, A_REVERSE);
    wprintw (win, 
             "%9.9s %10.10s "
             "%5.5s %5.5s %5.5s %5.5s %5.5s "
             "%5.5s %5.5s %5.5s %5.5s %5.5s",
             "server", "aname",
             "r<4K", "r<8K", "r<16K", "r<32K", "r<64K",
             "w<4K", "w<8K", "w<16K", "w<32K", "w<64K");
    wattroff (win, A_REVERSE);

    xpthread_mutex_lock (&dtop_lock);
    if (!(itr = list_iterator_create (tpools)))
        msg_exit ("out of memory");    
    while ((tp = list_next (itr))) {
        if (sample_val (tp->numfids, now) == 0)
            continue;
        mvwprintw (win, y++, 0,
             "%9.9s %10.10s "
             "%5.0f %5.0f %5.0f %5.0f %5.0f "
             "%5.0f %5.0f %5.0f %5.0f %5.0f",
                    tp->key.host, tp->key.aname,
                    sample_rate (tp->rcount[0], now),
                    sample_rate (tp->rcount[1], now),
                    sample_rate (tp->rcount[2], now),
                    sample_rate (tp->rcount[3], now),
                    sample_rate (tp->rcount[4], now),
                    sample_rate (tp->wcount[0], now),
                    sample_rate (tp->wcount[1], now),
                    sample_rate (tp->wcount[2], now),
                    sample_rate (tp->wcount[3], now),
                    sample_rate (tp->wcount[4], now));
    }
    list_iterator_destroy (itr);
    xpthread_mutex_unlock (&dtop_lock);
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
    TpoolStats *tp;
    time_t now = time (NULL);
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
    xpthread_mutex_lock (&dtop_lock);
    if (!(itr = list_iterator_create (servers)))
        msg_exit ("out of memory");    
    while ((sp = list_next (itr))) {
        sp->numanames = sp->numfids = sp->numreqs = sp->totreqs = 0;
        sp->rmbps = sp->wmbps = 0;
    }
    list_iterator_destroy (itr);

    if (!(itr = list_iterator_create (tpools)))
        msg_exit ("out of memory");    
    while ((tp = list_next (itr))) {
        if (sample_val (tp->numfids, now) == 0)
            continue;
        sp = list_find_first (servers, (ListFindF)_match_serverhost,
                              tp->key.host);
        if (sp) {
            sp->numanames++;
            sp->numfids += sample_val (tp->numfids, now);
            sp->totreqs += sample_rate (tp->totreqs, now);
            sp->numreqs += sample_val (tp->numreqs, now); /* queue */
            sp->rmbps += sample_rate (tp->rbytes, now) / (1024*1024);
            sp->wmbps += sample_rate (tp->wbytes, now) / (1024*1024);
        }
    }
    list_iterator_destroy (itr);

    /* iterate thru servers displaying data */
    if (!(itr = list_iterator_create (servers)))
        msg_exit ("out of memory");    
    while ((sp = list_next (itr))) {
        mvwprintw (win, y, 0, "%10.10s %5.0d %5.0f ",
                   sp->host, sp->numanames, sp->numconns);
        if (now - sp->last_poll < stale_secs) {
            mvwprintw (win, y, 23,
            "%6.0f %5.0f %5.0f %5.0f %5.0f %5.0f %5.0f %5.0f",
            sp->numfids,
            sp->totreqs,
            sp->numreqs,
            sp->rmbps,
            sp->wmbps,
            sample_val (sp->mem_cached, now) / 1024,
            sample_val (sp->mem_dirty, now) / 1024,
            sample_rate (sp->nfs_ops, now));
        } else {
            char *s = strdup (ctime (&sp->last_poll));
            if (!s)
                msg_exit ("out of memory");
            s[strlen (s) - 1] = '\0';
            mvwprintw (win, y, 23, "[last heard from on %s]", s);
            free (s);
        }
        y++;
    }
    list_iterator_destroy (itr);
    xpthread_mutex_unlock (&dtop_lock);

    wrefresh (win);
}

static void
_update_display_help (WINDOW *win)
{
    int y = 0;

    wclear (win);
    mvwprintw (win, y++, 0, "Help for Interactive Commands - dtop "
               "version %s.%s", META_VERSION, META_RELEASE);
    y++;
    mvwprintw (win, y++, 2, "a   (default) Aname view");
    mvwprintw (win, y++, 2, "t             Tpool server/aname view");
    mvwprintw (win, y++, 2, "s             Diod server view");
    mvwprintw (win, y++, 2, "c             Display I/O size histograms ");
    mvwprintw (win, y++, 2, "h|?           Display this help screen");
    mvwprintw (win, y++, 2, "q             Quit");
    wrefresh (win);
}

typedef enum {
    VIEW_TPOOL, VIEW_SERVER, VIEW_ANAME, VIEW_RWCOUNT, VIEW_HELP
} view_t;

static void
_curses_watcher (double update_secs)
{
    WINDOW *topwin;
    WINDOW *subwin;
    int c;
    view_t view = VIEW_ANAME;

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
        switch (view) {
            case VIEW_TPOOL:
                _update_display_topwin (topwin);
                _update_display_tpool (subwin);
                break;
            case VIEW_SERVER:
                _update_display_topwin (topwin);
                _update_display_server (subwin);
                break;
            case VIEW_ANAME:
                _update_display_topwin (topwin);
                _update_display_aname (subwin);
                break;
            case VIEW_RWCOUNT:
                _update_display_topwin (topwin);
                _update_display_rwcount (subwin);
                break;
             case VIEW_HELP:
                _update_display_help (topwin);
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
            case 't': /* tpool view */
                view = VIEW_TPOOL;
                break;
            case 'a': /* aname view */
                view = VIEW_ANAME;
                break;
            case 'c': /* rwcount view */
                view = VIEW_RWCOUNT;
                break;
            case 'h': /* help view */
            case '?':
                view = VIEW_HELP;
                break;
            case ERR: /* timeout */
                break;
        }
    }
    exiting = 1;
}

static int
_match_tpool (TpoolStats *x, Tpoolkey *key)
{
    if (!strcmp (key->host, x->key.host) && !strcmp(key->aname, x->key.aname))
        return 1;
    return 0;
}

static TpoolStats *
_create_tpool (Tpoolkey *key)
{
    TpoolStats *tp;
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
    for (i = 0; i < NPSTATS_RWCOUNT_BINS; i++) {
        tp->rcount[i] = sample_create (stale_secs);
        tp->wcount[i] = sample_create (stale_secs);
    }

    return tp;
}

static void
_destroy_tpool (TpoolStats *tp)
{
    int i;

    for (i = 0; i < NPSTATS_RWCOUNT_BINS; i++) {
        sample_destroy (tp->rcount[i]);
        sample_destroy (tp->wcount[i]);
    }
    for (i = 0; i < sizeof(tp->nreqs)/sizeof(tp->nreqs[0]); i++)
        sample_destroy (tp->nreqs[i]);
    sample_destroy (tp->numreqs);
    sample_destroy (tp->totreqs);
    sample_destroy (tp->numfids);
    sample_destroy (tp->iops);
    sample_destroy (tp->rbytes);
    sample_destroy (tp->wbytes);
    free (tp);
}

static char *
_numerical_suffix (char *s, unsigned long *np)
{
    char *p = s + strlen (s);

    while (p > s && isdigit (*(p - 1)))
        p--;
    if (*p)
        *np = strtoul (p, NULL, 10);
    return p;
}

/* Used for list_sort () of AnameStats list by aname.
 * Like strcmp, but handle variable-width (unpadded) numerical suffixes, if any.
 */
static int
_compare_aname(AnameStats *ap1, AnameStats *ap2)
{
    unsigned long n1, n2;
    char *p1 = _numerical_suffix (ap1->aname, &n1);
    char *p2 = _numerical_suffix (ap2->aname, &n2);

    if (*p1 && *p2
            && (p1 - ap1->aname) == (p2 - ap2->aname)
            && !strncmp (ap1->aname, ap2->aname, p1 - ap1->aname)) {
        return (n1 < n2 ? -1
              : n1 > n2 ? 1 : 0);
    }
    return strcmp (ap1->aname, ap2->aname);

}

static int
_match_aname(AnameStats *ap, char *aname)
{
    if (!strcmp (aname, ap->aname))
        return 1;
    return 0;
}

static AnameStats *
_create_aname (char *aname)
{
    AnameStats *ap;

    if (!(ap = malloc (sizeof (*ap))))
        msg_exit ("out of memory");
    memset (ap, 0, sizeof (*ap));
    if (!(ap->aname = strdup (aname)))
        msg_exit ("out of memory");
    return ap;
}

static void
_destroy_aname (AnameStats *ap)
{
    free (ap->aname);
    free (ap);
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
    TpoolStats *tp;
    int i;

    memset (stats.nreqs, 0, sizeof(stats.nreqs));
    if (np_decode_tpools_str (s, &stats) < 0) /* mallocs stats.name */
        return;
    snprintf (key.host, sizeof(key.host), "%s", host);
    snprintf (key.aname, sizeof(key.aname), "%s", stats.name);

    xpthread_mutex_lock (&dtop_lock);
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
    for (i = 0; i < NPSTATS_RWCOUNT_BINS; i++) {
        sample_update (tp->rcount[i], (double)stats.rcount[i], t);
        sample_update (tp->wcount[i], (double)stats.wcount[i], t);
    }
    xpthread_mutex_unlock (&dtop_lock);

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
    char *buf, *s, *p;
    int count = 0;

    if (!(buf = npc_aget (sp->root, "connections")))
        return -1;
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

    while (!exiting) {
        if (sp->fd == -1)
            sp->fd = diod_sock_connect (sp->host, sp->port, DIOD_SOCK_QUIET);
        if (sp->fd == -1)
            goto skip;
        if (sp->root == NULL)
            sp->root = npc_mount (sp->fd, sp->fd, 65536, "ctl", diod_auth);
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
    if (sp->root) {
        (void)npc_umount (sp->root);
        sp->root = NULL;
        sp->fd = -1;
    } else if (sp->fd >= 0) {
        (void)close (sp->fd);
        sp->fd = -1;
    }
    return NULL;
}

static Server *
_create_server (char *host, char *port, double poll_sec)
{
    Server *sp;
    int err;

    if (!(sp = malloc (sizeof (Server))))
        err_exit ("out of memory");
    memset (sp, 0, sizeof (*sp));
    sp->poll_sec = poll_sec;
    if (!(sp->host = strdup (host)))
        err_exit ("out of memory");
    if (!(sp->port = strdup (port)))
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

static void
_destroy_server (Server *sp)
{
    int err;

    if ((err = pthread_join (sp->thread, NULL)))
        errn_exit (err, "pthread_join");
    sample_destroy (sp->nfs_ops); 
    sample_destroy (sp->mem_dirty); 
    sample_destroy (sp->mem_cached); 
    free (sp->port);
    free (sp->host);
    free (sp);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
