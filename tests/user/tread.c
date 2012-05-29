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
    fprintf (stderr, "Usage: tread aname outfile [infile]\n");
    fprintf (stderr, "  If no infile, try to read directly from aname\n");
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
            if ((m = write (fd, buf + done, n - done)) < 0) {
                err ("write");
                return;
            }
            done += m;
        } while (done < n);
    
    }
    if (n < 0)
        err ("npc_read");
}            

int
main (int argc, char *argv[])
{
    Npcfid *root, *fid;
    char *aname, *outfile, *infile = NULL;
    int fd;

    diod_log_init (argv[0]);

    if (argc != 4 && argc != 3)
        usage ();
    aname = argv[1];
    outfile = argv[2]; /* UNIX */
    if (argc == 4)
        infile = argv[3]; /* userland 9p */

    if ((fd = open (outfile, O_WRONLY | O_CREAT, 0644)) < 0)
        err_exit ("open");
    if (!(root = npc_mount (0, 0, 65536+24, aname, diod_auth)))
        errn_exit (np_rerror (), "npc_mount");
    if (infile) {
        if ((fid = npc_open_bypath (root, infile, O_RDONLY))) {
            _copy_from9 (fid, fd);
            if (npc_clunk (fid) < 0)
                errn (np_rerror (), "npc_clunk");
        } else
            errn (np_rerror (), "npc_open_bypath");
    } else {
        if ((npc_open (root, O_RDONLY)) == 0) {
            _copy_from9 (root, fd);
        } else
            errn (np_rerror (), "npc_open");
    }
    npc_umount (root);

    if (close (fd) < 0)
        err_exit ("close");

    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
