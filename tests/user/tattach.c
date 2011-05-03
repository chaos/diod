/* tattach.c - attach and clunk a file server on fd=0 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"
#include "diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tattach aname\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    char *aname;
    int fd = 0; /* stdin */

    diod_log_init (argv[0]);

    if (argc != 2)
        usage ();
    aname = argv[1];

    if (!(fs = npc_start (fd, 8192+24)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, NULL, aname, geteuid (),
                           diod_auth_client_handshake)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, NULL, aname, geteuid ())))
        errn_exit (np_rerror (), "npc_attach");
    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk");
    npc_finish (fs);

    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
