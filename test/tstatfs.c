/* tstatfs.c - compare statfs results on two dirs */

#include <sys/vfs.h>
#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char *argv[])
{
    struct statfs s1, s2;
    int diff = 0;

    if (argc != 3) {
        fprintf (stderr, "Usage: tstatfs dir1 dir2\n");
        exit (1);
    }

    if (statfs (argv[1], &s1) < 0) {
        perror (argv[1]);
        exit (1);
    }
    if (statfs (argv[2], &s2) < 0) {
        perror (argv[2]);
        exit (1);
    }

    if (s1.f_type != s2.f_type) {
        fprintf (stderr, "f_type differs\n");
        diff++;
    }
    if (s1.f_bsize != s2.f_bsize ) {
        fprintf (stderr, "f_bsize differs\n");
        diff++;
    }
    if (s1.f_blocks != s2.f_blocks) {
        fprintf (stderr, "f_blocks differs\n");
        diff++;
    }
    if (s1.f_bfree != s2.f_bfree) {
        fprintf (stderr, "f_bfree differs\n");
        diff++;
    }
    if (s1.f_bavail != s2.f_bavail) {
        fprintf (stderr, "f_bavail differs\n");
        diff++;
    }
    if (s1.f_files != s2.f_files) {
        fprintf (stderr, "f_files differs\n");
        diff++;
    }
    if (s1.f_ffree != s2.f_ffree) {
        fprintf (stderr, "f_ffree differs\n");
        diff++;
    }
    if (memcmp (&s1.f_fsid, &s2.f_fsid, sizeof (s1.f_fsid)) != 0) {
        fprintf (stderr, "f_fsid differs\n");
        diff++;
    }
    if (s1.f_namelen != s2.f_namelen) {
        fprintf (stderr, "f_namelen differs\n");
        diff++;
    }
    fprintf (stderr, "%d differences\n", diff);
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
