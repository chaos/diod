/* tcreate.c - verify file creation mode */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char *argv[])
{
    struct stat sb;
    int mode, nmode;

    umask(0);

    if (argc != 3) {
        fprintf (stderr, "Usage: tcreate file mode\n");
        exit (1);
    }
    mode = strtoul (argv[2], NULL, 0);
    if (creat (argv[1], mode) < 0) {
        perror ("creat");
        exit (1);
    }
    if (stat (argv[1], &sb) < 0) {
        perror ("stat");
        exit (1);
    }
    nmode = sb.st_mode & 0777;
    if (nmode == mode)
        fprintf (stderr, "mode %.4o\n", nmode);
    else
        fprintf (stderr, "mode %.4o != %.4o\n", nmode, mode);
    
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
