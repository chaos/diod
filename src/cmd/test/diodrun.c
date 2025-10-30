/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* diodrun.c - start diod server and run test script
 *
 * If --socketpair is specified, a socketpair is created.
 * The server options to claim its end are added to the server command line.
 * The client is passed its end as DIOD_SERVER_FD.
 *
 * Otherwise, a unique temporary directory is created.
 * The server listens on a unix domain socket within it.
 * The client is passed the socket path as DIOD_SOCKET.
 * The directory is cleaned up when client and server are complete.
 *
 * Termination:
 * In --socketpair mode, the server exits when client fd closes.
 * Otherwise, due to diod --socktest, the server shuts down when its
 * connection count drops to zero.
 */

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
#include <stdbool.h>
#include <getopt.h>

#include "src/libdiod/diod_log.h"

static const char *options = "s";

static const struct option longopts[] = {
    {"socketpair",          no_argument,        0,  's'},
    {0, 0, 0, 0}
};


static void
usage (void)
{
    fprintf (stderr,
"Usage: diodrun [OPTIONS] srv-cmd tst-cmd\n"
"    -s,--socketpair     Use socketpair to connect client and server.\n"
);
    exit (1);
}

static void
log_status (int s, char *cmd)
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

static void
setenvf (const char *name, const char *fmt, ...)
{
    char val[1024];
    va_list ap;

    va_start (ap, fmt);
    if (vsnprintf (val, sizeof (val), fmt, ap) >= sizeof (val))
        err_exit ("setenv buffer overflow");
    va_end (ap);
    if (setenv (name, val, 1) < 0)
        err_exit ("setenv");
}

/* Run the client, usually a test script.
 * Call setsid() to put the client command in a new session.  Without this,
 * the terminal seems to get mangled under parallel make check, e.g. -jN.
 * In socketpair mode, the server exits when the test script exits
 * and implicitly closes its end of socketpair.
 * In unix mode, the test script has to manage server connections such
 * that only at the end of the script does the connection count drop to zero.
 */
pid_t
client (const char *cmd, int *pair, const char *sockpath)
{
    pid_t pid;

    if ((pid = fork ()) < 0)
        err_exit ("client fork");
    if (pid != 0)
        return pid;
    if ((setsid () == -1))
        err_exit ("setsid");
    if (pair) {
        close (pair[0]);
        setenvf ("DIOD_SERVER_FD", "%d", pair[1]);
    }
    else {
        setenvf ("DIOD_SOCKET", "%s", sockpath);
    }
    execl ("/bin/sh", "sh", "-c", cmd, NULL);
    err_exit ("client exec");
}

/* Run the diod server.
 * Append diod options needed for socketpair vs unix mode.
 * Call setsid() to put the server command in a new session.  Without this,
 * sharness test output gets mangled when diod is run under sudo(8).
 */
pid_t
server (const char *server_command, int *pair, const char *sockpath)
{
    char cmd[1024];
    size_t cmdsize = sizeof (cmd);
    pid_t pid;

    if ((pid = fork ()) < 0)
        err_exit ("server fork");
    if (pid != 0)
        return pid;
    if ((setsid () == -1))
        err_exit ("setsid");
    if (pair) {
        close (pair[1]);
        if (snprintf (cmd,
                      cmdsize,
                      "%s -r%d -w%d",
                      server_command,
                      pair[0],
                      pair[0]) >= cmdsize)
            err_exit ("error building server command line");
    }
    else {
        if (snprintf (cmd,
                      cmdsize,
                      "%s --listen=%s --socktest",
                      server_command,
                      sockpath) >= cmdsize)
            err_exit ("error building server command line");
    }
    execl ("/bin/sh", "sh", "-c", cmd, NULL);
    err_exit ("client exec");
}

int
main (int argc, char *argv[])
{
    int pair[2];
    const char *client_command;
    const char *server_command;
    pid_t client_pid;
    pid_t server_pid;
    int status;
    int c;
    bool socketpair_flag = false;
    char tmpdir[1024];
    char sockpath[1024];

    diod_log_init ("diodrun");

    opterr = 0;
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
        switch (c) {
            case 's': // --socketpair
                socketpair_flag = true;
                break;
            default:
                usage ();
        }
    }
    if (argc - optind != 2)
        usage ();
    server_command = argv[optind++];
    client_command = argv[optind++];

    diod_log_init (argv[0]);

    if (socketpair_flag) {
        if (socketpair (AF_LOCAL, SOCK_STREAM, 0, pair) < 0)
            err_exit ("socketpair");
    }
    else {
        const char *systmp = getenv ("TMPDIR");
        if (!systmp)
            systmp = "/tmp";
        if (snprintf (tmpdir,
                      sizeof (tmpdir),
                      "%s/diodrun.XXXXXX",
                      systmp) >= sizeof (tmpdir))
            msg_exit ("buffer overflow");
        if (!mkdtemp (tmpdir))
            err_exit ("mkdtemp");
        if (snprintf (sockpath,
                      sizeof (sockpath),
                      "%s/sock",
                      tmpdir) >= sizeof (sockpath))
            msg_exit ("buffer overflow");
    }

    client_pid = client (client_command,
                         socketpair_flag ? pair : NULL,
                         socketpair_flag ? NULL : sockpath);

    server_pid = server (server_command,
                         socketpair_flag ? pair : NULL,
                         socketpair_flag ? NULL : sockpath);

    /* Close parental dups of sockets.
     */
    if (socketpair_flag) {
        close (pair[0]);
        close (pair[1]);
    }

    /* Wait for the server.
     */
    if (waitpid (server_pid, &status, 0) < 0)
        err_exit ("waitpid server");
    if (!WIFEXITED (status)
        || WEXITSTATUS (status) != 0) {
        log_status (status, "server");
        exit (1);
    }

    /* Wait for the client.
     */
    if (waitpid (client_pid, &status, 0) < 0)
        err_exit ("waitpid client");
    if (!WIFEXITED (status)
        || WEXITSTATUS (status) != 0) {
        log_status (status, "client");
        exit (1);
    }

    if (!socketpair_flag) {
        (void)unlink (sockpath);
        (void)unlink (tmpdir);
    }
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
