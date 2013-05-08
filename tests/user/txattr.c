/* txattr.c - test if a file system supports extended attributes */

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
#include <stdarg.h>
#include <sys/types.h>
#include <attr/xattr.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>


static void
usage (void)
{
    fprintf (stderr, "Usage: txattr path\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *path;

    if (argc != 2)
        usage ();
    path = argv[1];

    if (getxattr (path, "user.foo", NULL, 0) && errno == ENOTSUP)
        exit (1);
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
