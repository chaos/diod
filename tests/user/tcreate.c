/* tcreate.c - try to create a file with bogus gid */

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
#include <stdarg.h>
#include <inttypes.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"
#include "diod_auth.h"

#ifndef MAXPATHNAMELEN
#define MAXPATHNAMELEN 1024
#endif

static void pick_user (uid_t *up, gid_t *gp, gid_t *ngp);


static void
usage (void)
{
    fprintf (stderr, "Usage: tcreate aname\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    Npcfid *afid, *root, *fid;
    char *aname;
    const int fd = 0; /* stdin */
    uid_t uid;
    gid_t gid, ngid;

    diod_log_init (argv[0]);

    if (argc != 2)
        usage ();
    aname = argv[1];

    if (geteuid () != 0) /* the server actually must be running as root */
        msg_exit ("must run as root");

    pick_user (&uid, &gid, &ngid);

    if (!(fs = npc_start (fd, 8192+24, 0)))
        errn_exit (np_rerror (), "npc_start");
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0)
        errn_exit (np_rerror (), "npc_auth");
    if (!(root = npc_attach (fs, afid, aname, uid)))
        errn_exit (np_rerror (), "npc_attach");
    if (afid && npc_clunk (afid) < 0)
        errn_exit (np_rerror (), "npc_clunk afid");

    /* should succeed */
    if (!(fid = npc_create_bypath (root, "foo", 0644, O_RDONLY, gid)))
        errn_exit (np_rerror (), "npc_create_bypath as %d:%d with gid %d",
                   uid, gid, gid);
    if (npc_clunk (fid) < 0)
        errn_exit (np_rerror (), "npc_clunk");
    msg ("create foo with good gid succeeded");

    /* should fail */
    if ((fid = npc_create_bypath (root, "foo2", 0644, O_RDONLY, ngid)))
        msg_exit ("npc_create_bypath as %d:%d with gid %d succeeded: FAIL",
                  uid, gid, ngid);
    msg ("create foo2 with bad gid failed");

    if (npc_clunk (root) < 0)
        errn_exit (np_rerror (), "npc_clunk root");

    npc_finish (fs);

    diod_log_fini ();

    exit (0);
}

/* pick a valid uid+gid and a valid gid that isn't in its supplementary
 * groups.
 */
static void
pick_user (uid_t *up, gid_t *gp, gid_t *ngp)
{
    gid_t sg[64];
    uid_t uid;
    uid_t gid, ngid = 0;
    int i, nsg = 64;
    struct passwd *pw;
    struct group *gr;
    char *uname;

    setpwent ();
    while ((pw = getpwent ())) {
        if (pw->pw_uid != 0)
            break;
    }
    endpwent ();
    if (!pw)
        msg_exit ("could not select uid");
    uid = pw->pw_uid;
    gid = pw->pw_gid;
    uname = strdup (pw->pw_name);
    if (getgrouplist (uname, gid, sg, &nsg) < 0)
        err_exit ("could not get supplementary groups for %s", uname);

    setgrent ();
    while ((gr = getgrent ())) {
        for (i = 0; i < nsg; i++) {
            if (gr->gr_gid == sg[i])
                break;
        }
        if (i == nsg) {
            ngid = gr->gr_gid;
            break;
        }
    }
    if (gr == NULL)
        msg_exit ("could not select ngid");
    endgrent ();
     
    free (uname); 
    *up = uid;
    *gp = gid;
    *ngp = ngid;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
