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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tread aname path\n");
    exit (1);
}

static int
_cat (Npcfid *fid)
{
    int n;
    u8 buf[65536];
    u64 offset = 0;

    while ((n = npc_read (fid, buf, sizeof (buf), offset)) > 0) {
        offset += n;
        fwrite (buf, n, 1, stdout);
    }
    return n;
}            

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *fid;
    char *aname, *path;

    diod_log_init (argv[0]);

    if (argc != 3)
        usage ();
    aname = argv[1];
    path = argv[2];

    if (!(fs = npc_mount (0, 65536+24, aname, geteuid ())))
        err_exit ("npc_mount");

    if (!(fid = npc_open (fs, path, O_RDONLY)))
        err_exit ("npc_open");

    if (_cat (fid) < 0)
        err ("npc_cat");

    if (npc_close (fid) < 0)
        err_exit ("npc_close");

    if (npc_umount (fs) < 0)
        err_exit ("npc_umount");

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
