/* tstatfs.c - compare statfs results on two dirs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/vfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef V9FS_MAGIC
#define V9FS_MAGIC      0x01021997
#endif

static char *field = NULL;

int
tfield (char *s)
{
    return (!field || !strcmp (field, s));
}

int
main (int argc, char *argv[])
{
    struct statfs s1, s2;
    int diff = 0;

    if (argc != 3 && argc != 4) {
        fprintf (stderr, "Usage: tstatfs dir1 dir2 [field]\n");
        exit (1);
    }
    if (argc == 4)
        field = argv[3];

    if (statfs (argv[1], &s1) < 0) {
        perror (argv[1]);
        exit (1);
    }
    if (statfs (argv[2], &s2) < 0) {
        perror (argv[2]);
        exit (1);
    }
    /* As of diod-1.0.7, the default is to return V9FS_MAGIC.
     * A patch was submitted to let the server control this but
     * as of linux-3.3.3-rc5 it has not gone in upstream.
     * (Without the patch f_type is always V9FS_MAGIC.)
     */
    if (tfield ("type") && s1.f_type != s2.f_type) {
        if (s1.f_type != V9FS_MAGIC && s2.f_type != V9FS_MAGIC) {
            fprintf (stderr, "f_type differs\n");
            diff++;
        }
    }
    if (tfield ("bsize") && s1.f_bsize != s2.f_bsize ) {
        fprintf (stderr, "f_bsize differs\n");
        diff++;
    }
    if (tfield ("blocks") && s1.f_blocks != s2.f_blocks) {
        fprintf (stderr, "f_blocks differs\n");
        diff++;
    }
    if (tfield ("bfree") && s1.f_bfree != s2.f_bfree) {
        fprintf (stderr, "f_bfree differs\n");
        diff++;
    }
    if (tfield ("bavail") && s1.f_bavail != s2.f_bavail) {
        fprintf (stderr, "f_bavail differs\n");
        diff++;
    }
    if (tfield ("files") && s1.f_files != s2.f_files) {
        fprintf (stderr, "f_files differs\n");
        diff++;
    }
    if (tfield ("ffree") && s1.f_ffree != s2.f_ffree) {
        fprintf (stderr, "f_ffree differs\n");
        diff++;
    }
    /* I'm not sure if this is a bug in v9fs or elsewhere, but on 32 bit
     * systems, f_fsid.__val[1] == -1.
     */
    if (tfield ("fsid")) {
        if (s1.f_fsid.__val[0] != s2.f_fsid.__val[0]) {
            fprintf (stderr, "statfs f_fsid differs)\n");
            diff++;
        } else if (s1.f_fsid.__val[1] != -1 && s2.f_fsid.__val[1] != -1
                && s1.f_fsid.__val[1] != s2.f_fsid.__val[1]) {
            fprintf (stderr, "statfs f_fsid differs\n");
            diff++;
        }
    }
    if (tfield ("namelen") && s1.f_namelen != s2.f_namelen) {
        fprintf (stderr, "f_namelen differs\n");
        diff++;
    }
    fprintf (stderr, "%d differences\n", diff);
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
