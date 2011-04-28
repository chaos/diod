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

/* conjoin.c - start processes joined by socketpair on fd 0 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <stdarg.h>

#include "diod_log.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static void
usage (void)
{
    fprintf (stderr, "Usage: conjoin srv-cmd tst-cmd\n");
    exit (1);
}

static char *
_cmd (char *cmdline)
{
    static char fqcmd[PATH_MAX];
    char *p;

    snprintf (fqcmd, sizeof (fqcmd), "%s", cmdline);
    if ((p = strchr (fqcmd, ' ')))
        *p = '\0';
    return basename (fqcmd);
}

static void
_interpret_status (int s, char *cmd)
{
    if (WIFEXITED (s))
        msg ("%s exited with rc=%d", cmd, WEXITSTATUS (s));
    else if (WIFSIGNALED (s))
        msg ("%s killed with signal %d%s", cmd, WTERMSIG (s),
            WCOREDUMP (s) ? " (core dumped)" : "");
    else if (WIFSTOPPED (s))
        msg ("%s stopped with signal %d", cmd, WSTOPSIG (s));
    else if (WIFCONTINUED (s))
        msg ("%s restarted with SIGCONT", cmd);
}

int
main (int argc, char *argv[])
{
    int cs = -1, s[2];
    char *cmd1, *cmd2;
    pid_t pid;

    if (argc != 3)
        usage ();
    cmd1 = argv[1];
    cmd2 = argv[2];

    diod_log_init (argv[0]);

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        err_exit ("socketpair");

    switch ((pid = fork ())) {
        case -1:
            err_exit ("fork");
            /*NOTREACHED*/
        case 0:     /* child - tst-cmd */
            close (s[0]);
            if (dup2 (s[1], 0) < 0)
                err_exit ("dup2 for %s leg", _cmd (cmd2));
            close (s[1]);
            if ((cs = system (cmd2)) == -1)
                err_exit ("fork for %s leg", _cmd (cmd2));
            _interpret_status (cs, _cmd (cmd2));
            exit (0);
            /*NOTREACHED*/
        default:    /* parent - srv-cmd */
            close (s[1]);
            if (dup2 (s[0], 0) < 0) {
                err ("dup2 for %s leg", _cmd (cmd1));
                break;
            }
            close (s[0]);
            if ((cs = system (cmd1)) == -1)
                err ("fork for %s leg", _cmd (cmd1));
            close (0);
            break;
    }

    /* reap child */
    close (0);
    if (waitpid (pid, NULL, 0) < 0)
        err_exit ("waitpid");

    /* report status of parent now that child has reported */
    if (cs != -1)
        _interpret_status (cs, _cmd (cmd1));

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
