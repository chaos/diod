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
#include <stdarg.h>
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
    fprintf (stderr, "Usage: twrite aname infile outfile\n");
    exit (1);
}

static void
_copy_to9 (int fd, Npcfid *fid)
{
    int n, m, done;
    u8 buf[65536];

    while ((n = read (fd, buf, sizeof (buf))) > 0) {
        done = 0;
        do {
            m = npc_write (fid, buf + done, n - done);
            if (m < 0) {
                err ("npc_write");
                return;
            }
            done += m;
        } while (done < n);
    }
    if (n < 0)
        err ("read");
}            

int
main (int argc, char *argv[])
{
    Npcfid *root, *fid;
    char *aname, *infile, *outfile;
    int fd;

    diod_log_init (argv[0]);

    if (argc != 4)
        usage ();
    aname = argv[1];
    infile = argv[2];
    outfile = argv[3];

    if ((fd = open (infile, O_RDONLY)) < 0)
        err_exit ("open");

    if (!(root = npc_mount (0, 0, 65536+24, aname, diod_auth)))
        errn_exit (np_rerror (), "npc_mount");
    if ((fid = npc_create_bypath (root, outfile, O_WRONLY, 0644, getegid()))) {
        _copy_to9 (fd, fid);
        if (npc_clunk (fid) < 0)
            err ("npc_clunk");
    } else
        errn (np_rerror (), "npc_create_bypath");
    npc_umount (root);

    if (close (fd) < 0)
        err_exit ("close");

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
