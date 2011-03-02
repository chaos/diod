/* tattach.c - attach and clunk a file server on fd=0 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"
#include "diod_upool.h"

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
    Npuser *user;

    diod_log_init (argv[0]);

    if (argc != 2)
        usage ();
    aname = argv[1];

    if (!(user = diod_upool->uid2user (diod_upool, geteuid ())))
        msg_exit ("out of memory");

    if (!(fs = npc_mount (0, aname, user, NULL, NULL)))
        msg_exit ("npc_mount failed");

    npc_umount (fs);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
