/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* diodrun.c - start diod and a 9p test joined by socketpair
 *
 * The server uses fd 0 for input and output.
 * The client can either use fd 0 or an environment-passed fd.
 * The server exits when the client closes its connection.
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

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static const char *options = "";

static const struct option longopts[] = {
    {0, 0, 0, 0}
};


static void
usage (void)
{
    fprintf (stderr, "Usage: diodrun [OPTIONS] srv-cmd tst-cmd\n");
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

int
main (int argc, char *argv[])
{
    int s[2];
    char *client_command;
    char *server_command;
    int client_status;
    int server_status;
    pid_t pid;
    int c;

    diod_log_init ("diodrun");

    opterr = 0;
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
        switch (c) {
            default:
                usage ();
        }
    }
    if (argc - optind != 2)
        usage ();
    server_command = argv[optind++];
    client_command = argv[optind++];

    diod_log_init (argv[0]);

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        err_exit ("socketpair");

    switch ((pid = fork ())) {
        case -1:
            err_exit ("fork");
            /*NOTREACHED*/
        case 0:     /* child - client */
            /* Close the server side of the socketpair in the client.
             */
            close (s[0]);
            /* Pass the client side of the socketpair in DIOD_SERVER_FD.
             */
            char val[32];
            snprintf (val, sizeof (val), "%d", s[1]);
            if (setenv ("DIOD_SERVER_FD", val, 1) < 0)
                err_exit ("setenv");
            /* Run the client command.
             */
            client_status = system (client_command);
            if (client_status < 0)
                err_exit ("client fork");
            if (!WIFEXITED (client_status)
                || WEXITSTATUS (client_status) != 0) {
                log_status (client_status, "client");
                exit (1);
            }
            exit (0);
            /*NOTREACHED*/
        default:    /* parent - server */
            /* Close the client side of the socketpair in the server.
             */
            close (s[1]);
            /* Dup the server side of the socketpair onto stdin.
             */
            if (dup2 (s[0], 0) < 0)
                err_exit ("server dup2");
            close (s[0]);
            server_status = system (server_command);
            if (server_status < 0)
                err_exit ("server fork");
            close (0);
            break;
    }

    /* The server has already completed.
     * Now wait for the child to finish and log if it didn't exit cleanly.
     */
    if (waitpid (pid, &client_status, 0) < 0)
        err_exit ("waitpid");

    if (!WIFEXITED (server_status)
        || WEXITSTATUS (server_status) != 0) {
        log_status (server_status, "server");
        exit (1);
    }
    if (!WIFEXITED (client_status)
        || WEXITSTATUS (client_status) != 0) {
        exit (1);
    }
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
