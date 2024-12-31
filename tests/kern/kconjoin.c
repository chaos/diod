/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* kconjoin.c - mount server using socketpair connection and run test */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <stdarg.h>
#include <sched.h>

#include "src/libdiod/diod_log.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define RFDNO   80
#define WFDNO   81

static void
usage (void)
{
    fprintf (stderr, "Usage: conjoin srv-cmd mnt-cmd tst-cmd\n");
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

static int
_interpret_status (int s, char *cmd)
{
    int rc = 1;

    if (WIFEXITED (s)) {
        rc = WEXITSTATUS (s);
        msg ("%s exited with rc=%d", cmd, rc);
    } else if (WIFSIGNALED (s))
        msg ("%s killed with signal %d%s", cmd, WTERMSIG (s),
            WCOREDUMP (s) ? " (core dumped)" : "");
    else if (WIFSTOPPED (s))
        msg ("%s stopped with signal %d", cmd, WSTOPSIG (s));
    else if (WIFCONTINUED (s))
        msg ("%s restarted with SIGCONT", cmd);

    return rc;
}

static void
_movefd (int fd1, int fd2)
{
    if (dup2 (fd1, fd2) < 0)
        err_exit ("dup2");
    (void)close (fd1);
}

int
main (int argc, char *argv[])
{
    int cs = -1, tosrv[2], fromsrv[2];
    char *srvcmd, *mntcmd, *tstcmd;
    pid_t pid;

    if (argc != 4)
        usage ();
    srvcmd = argv[1];
    mntcmd = argv[2];
    tstcmd = argv[3];

    diod_log_init (argv[0]);

    /* We were using a socketpair like ../user/conjoin.c.
     * However it is maybe useful to test on a pair of pipes too (issue 68)
     * so we do that here.
     */
    if (pipe (tosrv) < 0)
        err_exit ("pipe");
    if (pipe (fromsrv) < 0)
        err_exit ("pipe");

    switch ((pid = fork ())) {
        case -1:
            err_exit ("fork");
            /*NOTREACHED*/
        case 0:    /* child (mnt-cmd, tst-cmd) */
            close (tosrv[0]);
            close (fromsrv[1]);
            _movefd (tosrv[1], WFDNO);
            _movefd (fromsrv[0], RFDNO);
            if (unshare (CLONE_NEWNS) < 0)
                err_exit ("unshare");
            /* set mount propagation recursively to private */
            if (mount ("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0)
                err_exit ("cannot change root filesystem propagation");
            if ((cs = system (mntcmd)) == -1)
                err_exit ("failed to run %s", _cmd (mntcmd));
            if (_interpret_status (cs, _cmd (mntcmd)))
                exit (1);
            close (RFDNO);
            close (WFDNO);
            if ((cs = system (tstcmd)) == -1)
                err_exit ("fork for %s leg", _cmd (tstcmd));
            _interpret_status (cs, _cmd (tstcmd));
            exit (0);
            /*NOTREACHED*/
        default:     /* parent (srv-cmd) */
            close (tosrv[1]);
            close (fromsrv[0]);
            _movefd (tosrv[0], RFDNO);
            _movefd (fromsrv[1], WFDNO);
            if ((cs = system (srvcmd)) == -1) {
                err ("fork for %s leg", _cmd (srvcmd));
                break;
            }
            close (RFDNO);
            close (WFDNO);
            break;
    }

    /* reap child */
    if (waitpid (pid, NULL, 0) < 0)
        err_exit ("waitpid");

    /* report status of parent now that child has reported */
    if (cs != -1)
        _interpret_status (cs, _cmd (srvcmd));

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
