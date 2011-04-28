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
    char *aname;

    diod_log_init (argv[0]);

    if (argc != 2)
        usage ();
    aname = argv[1];

    if (!(fs = npc_mount (0, 8192+24, aname, diod_auth_client_handshake)))
        err_exit ("npc_mount");
    if (npc_umount (fs) < 0)
        err_exit ("npc_umount");

    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
