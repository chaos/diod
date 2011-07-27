/* tflock2.c - test BSD advisory file locks within one process */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/file.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

int
main (int argc, char *argv[])
{
    int fd, fd2;

    if (argc != 2) {
        fprintf (stderr, "Usage: tflock2 file\n");
        exit (1);
    }
    /* check that two fd's in the same process cannot get conflicting locks */
    if ((fd = open (argv[1], O_RDWR)) < 0) {
        perror (argv[1]);
        exit (1);
    }
    fprintf (stderr, "fd: open\n");
    if (flock (fd, LOCK_EX | LOCK_NB) < 0) {
        perror ("flock");
        exit (1);
    }
    fprintf (stderr, "fd: write-locked\n");
    if ((fd2 = open (argv[1], O_RDWR)) < 0) {
        perror (argv[1]);
        exit (1);
    }
    fprintf (stderr, "fd2: open\n");
    if (flock (fd2, LOCK_EX | LOCK_NB) < 0)
        perror ("second flock failed as it should - PASS");
    else
        fprintf (stderr, "fd: write-locked - FAIL\n");
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
