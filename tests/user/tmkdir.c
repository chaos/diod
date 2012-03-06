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
_mkdir_p (Npcfid *root, char *path, mode_t mode)
{
    struct stat sb;
    char *cpy;
    int res = 0;

    if (npc_stat (root, path, &sb) == 0) {
        if (!S_ISDIR (sb.st_mode)) {
            np_uerror (ENOTDIR);
            return -1;
        }
        return 0;
    }
    if (!(cpy = strdup (path))) {
        np_uerror (ENOMEM);
        return -1;
    }
    res = _mkdir_p (root, dirname (cpy), mode);
    free (cpy);
    if (res == 0)
        res = npc_mkdir_bypath (root, path, mode);

    return res;
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    uid_t uid = geteuid ();
    char *aname, *path;
    int fd = 0; /* stdin */

    diod_log_init (argv[0]);

    if (argc != 3)
        usage ();
    aname = argv[1];
    path = argv[2];

    if (!(fs = npc_start (fd, fd, 65536+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn (np_rerror (), "npc_clunk afid");

    if (_mkdir_p (root, path, 0755) < 0)
        errn_exit (np_rerror (), "mkdir");

    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
