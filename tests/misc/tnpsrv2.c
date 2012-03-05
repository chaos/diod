/* tnpsrv2.c - test client/server with diod ops (valgrind me) */

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

#define TEST_ITER 64

int
main (int argc, char *argv[])
{
    Npsrv *srv;
    int s[2];
    int flags = 0;
    Npcfid *root, *dir, *f[TEST_ITER];
    char tmpdir[] = "/tmp/tnpsrv2.XXXXXX";
    int i;
    char tmpstr[PATH_MAX + 1];
    int n, len = 4096*100;
    char *buf = malloc (len);
    char *buf2 = malloc (len);
    char dbuf[TEST_MSIZE - P9_IOHDRSZ];
    struct dirent d, *dp;

    diod_log_init (argv[0]);
    diod_conf_init ();
    diod_conf_set_auth_required (0);

    /* create export */
    if (!mkdtemp (tmpdir))
        err_exit ("mkdtemp");
    diod_conf_add_exports (tmpdir);
    diod_conf_set_exportopts ("sharefd");

    if (socketpair (AF_LOCAL, SOCK_STREAM, 0, s) < 0)
        err_exit ("socketpair");

    if (!(srv = np_srv_create (16, flags)))
        errn_exit (np_rerror (), "np_srv_create");
    if (diod_init (srv) < 0)
        errn_exit (np_rerror (), "diod_init");
    diod_sock_startfd (srv, s[1], s[1], "loopback");

    if (!(root = npc_mount (s[0], s[0], TEST_MSIZE, tmpdir, NULL)))
        errn_exit (np_rerror (), "npc_mount");

    msg ("attached");

    /* create a file, write some data into it, then read it TEST_ITER
     * times, each on a unique fid.  The path and ioctx will be shared due
     * to "sharefd" export option set above.  We want to be sure no memory
     * leaks result from the management of shared paths and io contexts.
     */
    if (!(f[0] = npc_create_bypath (root, "foo", 0, 0644, getgid())))
        errn (np_rerror (), "npc_create_bypath foo");
    else {
        if (!buf || !buf2)
            errn_exit (ENOMEM, "malloc");
        if (npc_clunk (f[0]) < 0)
            errn (np_rerror (), "npc_clunk");

        /* fill it with some data */
        memset (buf, 9, len);
        n = npc_put (root, "foo", buf, len);
        if (n < 0)
            errn (np_rerror (), "npc_put");
        if (n < len)
            msg ("npc_put: short write: %d", n);

        /*  walk */
        for (i = 0; i < TEST_ITER; i++) {
            f[i] = npc_walk (root, "foo");
            if (!f[i])
                errn (np_rerror (), "npc_walk");
        }
        /* open */
        for (i = 0; i < TEST_ITER; i++) {
            if (f[i]) {
                if (npc_open (f[i], O_RDONLY) < 0)
                    errn (np_rerror (), "npc_open");
            }
        }
        /* read (using new fids) */
        for (i = 0; i < TEST_ITER; i++) {
            if (f[i]) {
                memset (buf2, 0, len);
                n = npc_get (root, "foo", buf2, len);
                if (n < 0)
                    errn (np_rerror (), "npc_get");
                if (n < len) 
                    msg ("short read: %d bytes", n);
                if (memcmp (buf, buf2, n) != 0)
                    msg ("memcmp failure");
            }
        }
        /* clunk */
        for (i = 0; i < TEST_ITER; i++) {
            if (f[i]) {
                if (npc_clunk (f[i]) < 0)
                    errn (np_rerror (), "npc_clunk");
            }
        }

        /* remove */
        if (npc_remove_bypath (root, "foo") < 0)
            errn_exit (np_rerror(), "np_remove_bypath");

        free (buf);
        free (buf2);

        msg ("file test finished");
    }

    /* create a directory with TEST_ITER files, and exercise varios ops
     * on the files and directory.
    */
    if (npc_mkdir_bypath (root, "foo", 0755) < 0)
        errn (np_rerror (), "npc_mkdir_bypath");
    else {

        /* create files */
        for (i = 0; i < TEST_ITER; i++) {
            snprintf (tmpstr, sizeof (tmpstr), "foo/%-.200i", i);
            if (!(f[i] = npc_create_bypath (root, tmpstr, 0, 0644, getgid())))
                errn (np_rerror (), "npc_create_bypath %s", tmpstr);
        }
        /* open the directory */
        if ((dir = npc_opendir (root, "foo"))) {

            /* read one chunk of directory (redundant with below) */
            if (npc_readdir (dir, 0, dbuf, sizeof (dbuf)) < 0)
                errn (np_rerror (), "npc_readdir");

            /* list the files in the directory */
            i = 0;
            do {
                if ((n = npc_readdir_r (dir, &d, &dp)) > 0) {
                    errn (n, "npc_readdir_r");
                    break;
                }
                if (dp)
                    i++;
            } while (n == 0 && dp != NULL);
            if (i != TEST_ITER + 2) /* . and .. will appear */
                msg ("readdir found fewer than expected files: %d", i);

            /* close the directory */
            if (npc_closedir (dir) < 0)
                errn (np_rerror (), "npc_closedir foo");
        }
            
        /* remove files (implicit clunk) */
        for (i = 0; i < TEST_ITER; i++) {
            if (f[i]) {
                if (npc_remove (f[i]) < 0)
                    errn (np_rerror (), "npc_remove");
            }
        }

        /* remove directory */
        if (npc_remove_bypath (root, "foo") < 0)
            errn_exit (np_rerror (), "npc_remove_bypath");

        msg ("directory test finished");
    }

    npc_umount (root);

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
