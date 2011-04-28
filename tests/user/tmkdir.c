/* tmkdir.c - create a 9p directory, recursively */

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
    fprintf (stderr, "Usage: tmkdir aname path\n");
    exit (1);
}

static int
_mkdir_p (Npcfsys *fs, char *path, mode_t mode)
{
    struct stat sb;
    char *cpy;
    int res = 0;

    if (npc_stat (fs, path, &sb) == 0) {
        if (!S_ISDIR (sb.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }
    if (!(cpy = strdup (path))) {
        errno = ENOMEM;
        return -1;
    }
    res = _mkdir_p (fs, dirname (cpy), mode);
    free (cpy);
    if (res == 0)
        res = npc_mkdir (fs, path, mode);

    return res;
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    char *aname, *path;

    diod_log_init (argv[0]);

    if (argc != 3)
        usage ();
    aname = argv[1];
    path = argv[2];

    if (!(fs = npc_mount (0, 65536+24, aname, diod_auth_client_handshake)))
        err_exit ("npc_mount");

    if (_mkdir_p (fs, path, 0755) < 0)
        err_exit ("npc_mkdir");

    if (npc_umount (fs) < 0)
        err_exit ("npc_umount");

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
