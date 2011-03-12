/* tread.c - copy a 9p file to a regular file */

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
#include "diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tread aname infile outfile\n");
    exit (1);
}

static void
_copy_from9 (Npcfid *fid, int fd)
{
    int n, m, done;
    u8 buf[65536];

    while ((n = npc_read (fid, buf, sizeof (buf))) > 0) {
        done = 0;
        do {
            if ((m = write (fd, buf + done, n - done)) < 0)
                err_exit ("write");
            done += m;
        } while (done < n);
    
    }
    if (n < 0)
        err_exit ("npc_read");
}            

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *fid;
    char *aname, *infile, *outfile;
    int fd;

    diod_log_init (argv[0]);

    if (argc != 4)
        usage ();
    aname = argv[1];
    infile = argv[2];
    outfile = argv[3];

    if ((fd = open (outfile, O_WRONLY | O_CREAT, 0644)) < 0)
        err_exit ("open");

    if (!(fs = npc_mount (0, 65536+24, aname, diod_auth_client_handshake)))
        err_exit ("npc_mount");
    if (!(fid = npc_open (fs, infile, O_RDONLY)))
        err_exit ("npc_open");

    _copy_from9 (fid, fd);

    if (npc_close (fid) < 0)
        err_exit ("npc_close");
    if (npc_umount (fs) < 0)
        err_exit ("npc_umount");

    if (close (fd) < 0)
        err_exit ("close");

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
