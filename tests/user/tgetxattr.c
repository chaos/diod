/* tgetxattr.c - get/list extended attributes */

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
    fprintf (stderr, "Usage: tgetxattr aname path [attr ...]\n");
    exit (1);
}

static int
_getxattr (Npcfid *root, char *path, char *attr)
{
    ssize_t len, nlen;
    char *buf;

    len = npc_getxattr (root, path, attr, NULL, 0);
    if (len < 0)
        return len;
    buf = malloc (len);
    if (!buf)
        msg_exit ("out of memory"); 
    memset (buf, 0, len);
    nlen = npc_getxattr (root, path, attr, buf, len);
    if (nlen < 0)
        goto done;
    msg ("%s=\"%.*s\"", attr, (int)len, buf);
done:
    free (buf);
    return nlen;
}

static int
_listxattr (Npcfid *root, char *path)
{
    ssize_t len, nlen;
    char *buf;
    int i, count;

    len = npc_listxattr (root, path, NULL, 0);
    if (len < 0)
        return len;
    buf = malloc (len);
    if (!buf)
        msg_exit ("out of memory"); 
    memset (buf, 0, len);
    nlen = npc_listxattr (root, path, buf, len);
    if (nlen < 0)
        goto done;
    for (i = 0, count = 0; i < len && buf[i] != '\0'; count++) {
        char *key = &buf[i];
        int klen = (len - i);

        /* N.B. Skip non-user.*, or we may see security.selinux
         * (depending on host config) and fail test output comparison.
         */
        if (!strncmp (&buf[i], "user.", klen < 5 ? klen : 5))
            msg ("%.*s", klen, key);
        while (i < len && buf[i] != '\0')
            i++;
        i++;
    }
done:
    free (buf);
    return nlen;
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root;
    uid_t uid = geteuid ();
    char *aname, *path;
    int fd = 0; /* stdin */
    int i;

    diod_log_init (argv[0]);

    if (argc < 3)
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

    if (argc == 3) {
        if (_listxattr (root, path) < 0)
            errn_exit (np_rerror (), "_listxattr");
    } else {
        for (i = 3; i < argc; i++)
            if (_getxattr (root, path, argv[i]) < 0)
                errn_exit (np_rerror (), "_getxattr");
    }

    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");
    npc_finish (fs);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
