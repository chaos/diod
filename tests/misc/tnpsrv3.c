/* tnpsrv3.c - test diod client/server with root/mult users (valgrind me) */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <dirent.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> 

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "list.h"
#include "diod_log.h"
#include "diod_conf.h"
#include "diod_sock.h"

#include "ops.h"

#define TEST_MSIZE 8192

void
file_test (Npcfid *root)
{
    int n, len = 4096*100;
    char *buf = malloc (len);
    char *buf2 = malloc (len);
    Npcfid *f;

    if (!buf || !buf2)
        errn_exit (ENOMEM, "malloc");

    if (!(f = npc_create_bypath (root, "foo", 0, 0644, getgid()))) {
        errn (np_rerror (), "npc_create_bypath foo");
        goto done;
    }
    if (npc_clunk (f) < 0)
        errn (np_rerror (), "npc_clunk");

    /* fill it with some data */
    memset (buf, 9, len);
    n = npc_put (root, "foo", buf, len);
    if (n < 0)
        errn (np_rerror (), "npc_put");
    if (n < len)
        msg ("npc_put: short write: %d", n);

    /*  walk */
    f = npc_walk (root, "foo");
    if (!f)
        errn (np_rerror (), "npc_walk");
    else {
        /* open */
        if (npc_open (f, O_RDONLY) < 0)
            errn (np_rerror (), "npc_open");

        /* read */
        memset (buf2, 0, len);
        n = npc_get (root, "foo", buf2, len);
        if (n < 0)
            errn (np_rerror (), "npc_get");
        if (n < len) 
            msg ("short read: %d bytes", n);
        if (memcmp (buf, buf2, n) != 0)
            msg ("memcmp failure");

        /* clunk */
        if (npc_clunk (f) < 0)
            errn (np_rerror (), "npc_clunk");
    }

    /* remove */
    if (npc_remove_bypath (root, "foo") < 0)
        errn_exit (np_rerror(), "np_remove_bypath");
done:
    free (buf);
    free (buf2);
}

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int s[2];
    int flags = 0;
    Npcfsys *fs;
    Npcfid *root, *user, *user2;
    char tmpdir[] = "/tmp/tnpsrv2.XXXXXX";

    assert (geteuid () == 0);

    diod_log_init (argv[0]);
    diod_conf_init ();

    /* Allow attach with afid==-1.
     */
    diod_conf_set_auth_required (0);

    /* create export directory: will be owned by root, mode=0700 */
    if (!mkdtemp (tmpdir))
        err_exit ("mkdtemp");
    diod_conf_add_exports (tmpdir);
    diod_conf_set_exportopts ("sharefd");

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        err_exit ("socketpair");

    /* Note: supplementary groups do not work in this mode, however
     * regular uid:gid switching of fsid works.  Enabling DAC_BYPASS
     * assumes v9fs is enforcing permissions, not the case with npclient.
     */
    flags |= SRV_FLAGS_SETFSID;
    if (!(srv = np_srv_create (16, flags)))
        errn_exit (np_rerror (), "np_srv_create");
    if (diod_init (srv) < 0)
        errn_exit (np_rerror (), "diod_init");
    diod_sock_startfd (srv, s[1], s[1], "loopback");

   if (!(fs = npc_start (s[0], s[0], TEST_MSIZE, 0)))
        errn_exit (np_rerror (), "npc_start");

    if (!(root = npc_attach (fs, NULL, tmpdir, 0))) /* attach as uid=0 */
        errn_exit (np_rerror (), "npc_attach");
    if (!(user = npc_attach (fs, NULL, tmpdir, 1))) /* attach as uid=1 */
        errn_exit (np_rerror (), "npc_attach (uid=1)");
    /* attach one more time as uid=1 to exercise user cache under valgrind */
    if (!(user2 = npc_attach (fs, NULL, tmpdir, 1)))
        errn_exit (np_rerror (), "npc_attach (uid=1)");
    msg ("attached");

    file_test (root); /* expect success */
    msg ("root file test done");

    file_test (user); /* expect failure */
    msg ("user file test done");

    if (npc_chmod (root, ".", 0777) < 0)
        errn (np_rerror (), "npc_chmod");
    msg ("chmod 0777 mount point");
    file_test (user); /* expect success */
    msg ("user file test done");

    if (npc_chown (root, ".", 1, 1) < 0)
        errn (np_rerror (), "npc_chmod");
    msg ("chown 1 mount point");
    if (npc_chmod (root, ".", 0700) < 0)
        errn (np_rerror (), "npc_chmod");
    msg ("chmod 0700 mount point");
    file_test (user); /* expect success */
    msg ("user file test done");

    if (npc_clunk (user) < 0)
        errn (np_rerror (), "npc_clunk (uid=1)");
    if (npc_clunk (user2) < 0)
        errn (np_rerror (), "npc_clunk (uid=1)");
    if (npc_clunk (root) < 0)
        errn (np_rerror (), "npc_clunk");
    npc_finish (fs);
    msg ("detached");

    np_srv_wait_conncount (srv, 1);

    /* N.B. The conn reader thread runs detached and signals us as it is
     * about to exit.  If we manage to wake up and exit first, valgrind
     * reports the reader's tls as leaked.  Add the sleep to work around
     * this race for now.
     */
    sleep (1);

    diod_fini (srv);
    np_srv_destroy (srv);

    rmdir (tmpdir);

    diod_conf_fini ();
    diod_log_fini ();
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
