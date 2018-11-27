/* tsetxattr.c - set extended attributes */

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
#if HAVE_SYS_XATTR_H
    #include <sys/xattr.h>
#else
    #include <attr/xattr.h>
#endif


#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"
#include "diod_auth.h"

static void
usage (void)
{
    fprintf (stderr, "Usage: tsetxattr aname path attr value\n");
    fprintf (stderr, "       prepend + to attr for XATTR_CREATE\n");
    fprintf (stderr, "       prepend / to attr for XATTR_REPLACE\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    uid_t uid = geteuid ();
    char *aname, *path, *attr, *value;
    int fd = 0; /* stdin */
    int flags = 0;

    diod_log_init (argv[0]);

    if (argc < 3)
        usage ();
    aname = argv[1];
    path = argv[2];
    attr = argv[3];
    if (attr[0] == '+') {
        flags |= XATTR_CREATE;
        attr++;
    } else if (attr[0] == '/') {
        flags |= XATTR_REPLACE;
        attr++;
    }
    value = argv[4];

    if (!(fs = npc_start (fd, fd, 65536+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn (np_rerror (), "npc_clunk afid");

    if (npc_setxattr (root, path, attr, value, strlen (value), flags) < 0)
        errn_exit (np_rerror (), "npc_setxattr %s=%s", attr, value);

    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
