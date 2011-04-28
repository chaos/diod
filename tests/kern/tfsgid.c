/* tfsgid.c - ensure files are created with proper gid */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "diod_log.h"

int
main (int argc, char *argv[])
{
    int fd;
    gid_t egid, gid;
    struct stat sb;

    diod_log_init (argv[0]);

    if (argc != 3) {
        msg ("Usage: tfsgid gid path");
        exit (1);
    }
    egid = strtoul (argv[1], NULL, 10);
    gid = getgid ();
    if (setegid (egid) < 0)
        err_exit ("setegid");
    if (access (argv[2], F_OK) == 0)
        msg_exit ("file exists");
    if ((fd = open (argv[2], O_CREAT, 0644)) < 0)
        err_exit ("open");
    (void)close (fd);
    if (stat (argv[2], &sb) < 0)
        err_exit ("stat");
#define SAME "same as"
#define DIFF "different than"
    msg ("file gid is %s egid and %s gid",
        sb.st_gid == egid ? SAME : DIFF,
        sb.st_gid == gid ? SAME : DIFF);
    (void)unlink (argv[2]);

    exit(0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

