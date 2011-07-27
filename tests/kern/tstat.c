/* tstat.c - compare stat results on two files */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char *argv[])
{
    struct stat s1, s2;
    int diff = 0;

    if (argc != 3) {
        fprintf (stderr, "Usage: tstat file1 file2\n");
        exit (1);
    }

    if (stat (argv[1], &s1) < 0) {
        perror (argv[1]);
        exit (1);
    }
    if (stat (argv[2], &s2) < 0) {
        perror (argv[2]);
        exit (1);
    }

    if (s1.st_dev != s2.st_dev) {
        fprintf (stderr, "st_dev differs\n"); /* expected failure */
        diff++;
    }
    if (s1.st_ino != s2.st_ino) {
        fprintf (stderr, "st_ino differs\n"); /* expected failure */
        diff++;
    }
    if (s1.st_mode != s2.st_mode) {
        fprintf (stderr, "st_mode differs\n");
        diff++;
    }
    if (s1.st_nlink != s2.st_nlink) {
        fprintf (stderr, "st_nlink differs\n");
        diff++;
    }
    if (s1.st_uid != s2.st_uid) {
        fprintf (stderr, "st_uid differs\n");
        diff++;
    }
    if (s1.st_gid != s2.st_gid) {
        fprintf (stderr, "st_gid differs\n");
        diff++;
    }
    if (s1.st_rdev != s2.st_rdev) {
        fprintf (stderr, "st_rdev differs\n");
        diff++;
    }
    if (s1.st_size != s2.st_size) {
        fprintf (stderr, "st_size differs\n");
        diff++;
    }
    if (s1.st_blksize != s2.st_blksize) {
        fprintf (stderr, "st_blksize differs\n");
        diff++;
    }
    if (s1.st_blocks != s2.st_blocks) {
        fprintf (stderr, "st_blocks differs\n");
        diff++;
    }
    if (s1.st_atime != s2.st_atime) {
        fprintf (stderr, "st_atime differs\n");
        diff++;
    }
    if (s1.st_ctime != s2.st_ctime) {
        fprintf (stderr, "st_ctime differs\n");
        diff++;
    }
    if (s1.st_mtime != s2.st_mtime) {
        fprintf (stderr, "st_mtime differs\n");
        diff++;
    }
    fprintf (stderr, "%d differences\n", diff);
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
