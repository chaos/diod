/* tnpsrv.c - test simple client/server (valgrind me) */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "list.h"
#include "diod_log.h"
#include "diod_conf.h"
#include "diod_sock.h"

#define TEST_MSIZE 8192

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int s[2];
    int flags = SRV_FLAGS_DEBUG_9PTRACE | SRV_FLAGS_DEBUG_USER;
    Npcfsys *fs;
    Npcfid *root0, *root1, *root2;
    char *str;

    diod_log_init (argv[0]);
    diod_conf_init ();

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        err_exit ("socketpair");

    if (!(srv = np_srv_create (16, flags)))
        errn_exit (np_rerror (), "out of memory");
    srv->logmsg = diod_log_msg;
    diod_sock_startfd (srv, s[1], s[1], "loopback");

    if (!(fs = npc_start (s[0], s[0], TEST_MSIZE, 0)))
        errn_exit (np_rerror (), "npc_start");

    if (!(root0 = npc_attach (fs, NULL, "ctl", 0)))
        errn_exit (np_rerror (), "npc_attach");
    if (!(str = npc_aget (root0, "connections")))
        errn_exit (np_rerror (), "npc_aget connections");
    free (str);

    if (!(root1 = npc_attach (fs, NULL, "ctl", 1)))
        errn_exit (np_rerror (), "npc_attach");
    if (!(str = npc_aget (root1, "connections")))
        errn_exit (np_rerror (), "npc_aget connections");
    free (str);

    /* Same user (1) - user cache should be valid, so we won't see a message
     * for this user lookup in the output.
     */
    if (!(root2 = npc_attach (fs, NULL, "ctl", 1)))
        errn_exit (np_rerror (), "npc_attach");
    if (!(str = npc_aget (root2, "null")))
        errn_exit (np_rerror (), "npc_aget null");
    free (str);

    npc_clunk (root0);
    npc_clunk (root1);
    npc_clunk (root2);

    npc_finish (fs);

    np_srv_wait_conncount (srv, 1);

    /* N.B. The conn reader thread runs detached and signals us as it is
     * about to exit.  If we manage to wake up and exit first, valgrind
     * reports the reader's tls as leaked.  Add the sleep to work around
     * this race for now.
     */
    sleep (1);

    np_srv_destroy (srv);

    diod_conf_fini ();
    diod_log_fini ();
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
