/* tstat.c - stat a 9p file */

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
#include <libgen.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"
#include "diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tstat aname path1 path2\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfid *root;
    char *aname, *path1, *path2;
    struct stat sb1, sb2;

    diod_log_init (argv[0]);

    if (argc != 4)
        usage ();
    aname = argv[1];
    path1 = argv[2];
    path2 = argv[3];

    if (!(root = npc_mount (0, 0, 65536+24, aname, diod_auth)))
        errn_exit (np_rerror (), "npc_mount");
    if (npc_stat (root, path1, &sb1) < 0)
        errn_exit (np_rerror (), "npc_stat");
    npc_umount (root);

    if (stat (path2, &sb2) < 0)
        err_exit ("stat");

    if (sb1.st_dev != sb2.st_dev)
        msg ("dev differs");
    if (sb1.st_ino != sb2.st_ino)
        msg ("ino differs");
    if (sb1.st_mode != sb2.st_mode)
        msg ("mode differs");
    if (sb1.st_nlink != sb2.st_nlink)
        msg ("nlink differs");
    if (sb1.st_uid != sb2.st_uid)
        msg ("uid differs");
    if (sb1.st_gid != sb2.st_gid)
        msg ("gid differs");
    if (sb1.st_rdev != sb2.st_rdev)
        msg ("rdev differs");
    if (sb1.st_size != sb2.st_size)
        msg ("size differs");
    if (sb1.st_blksize != sb2.st_blksize)
        msg ("blksize differs");
    if (sb1.st_blocks != sb2.st_blocks)
        msg ("blocks differs");
    if (sb1.st_atime != sb2.st_atime)
        msg ("atime differs");
    if (sb1.st_mtime != sb2.st_mtime)
        msg ("mtime differs");
    if (sb1.st_ctime != sb2.st_ctime)
        msg ("ctime differs");

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
