/* tattachmt.c - simulate multiple simultaneous mout requests */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <errno.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <pwd.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"

#include "diod_log.h"
#include "diod_auth.h"

const int fd = 0; /* stdin */
const int numgetattrs = 16;

typedef struct {
    Npcfsys *fs;
    uid_t uid;
    char *aname;
    pthread_t t;
} thd_t;

static void
usage (void)
{
    fprintf (stderr, "Usage: tattachmt numusers numthreads aname\n");
    exit (1);
}

/* attach to aname and issue a getattr,
 * similar to what a v9fs mount looks like to the server.
 */
void *
client (void *arg)
{
    thd_t *t = (thd_t *)arg;
    Npcfid *afid = NULL, *root;
    struct stat sb;
    int i;

    if (!(afid = npc_auth (t->fs, t->aname, t->uid,
                           diod_auth_client_handshake)) && np_rerror () != 0) {
        errn (np_rerror (), "npc_auth");
        goto done;
    }
    if (!(root = npc_attach (t->fs, afid, t->aname, t->uid))) {
        errn (np_rerror (), "npc_attach");
        goto done;
    }
    if (afid && npc_clunk (afid) < 0)
        errn (np_rerror (), "npc_clunk afid");
    for (i = 0; i < numgetattrs; i++) {
        if (npc_getattr (root, &sb) < 0) {
            errn (np_rerror (), "npc_getattr");
            goto done;
        }
    }
    if (npc_clunk (root) < 0) {
        errn (np_rerror (), "npc_clunk root");
        goto done;
    }
done:
    return NULL;
}

int
main (int argc, char *argv[])
{
    Npcfsys *fs;
    char *aname;
    thd_t *t;
    int i, err, numthreads, numusers;
    uid_t *uids;
    struct passwd *pw;

    diod_log_init (argv[0]);

    if (argc != 4)
        usage ();
    numusers = strtoul (argv[1], NULL, 10); 
    numthreads = strtoul (argv[2], NULL, 10); 
    aname = argv[3];

    if (!(t = malloc (sizeof(*t) * numthreads)))
        msg_exit ("out of memory");
    if (!(uids = malloc (sizeof(*uids) * numusers)))
        msg_exit ("out of memory");

    if (numusers > 1) {
        setpwent ();
        for (i = 0; i < numusers; i++) {
            if (!(pw = getpwent ()))
                msg_exit ("could not look up %d users", numusers);
            uids[i] = pw->pw_uid;
        }
        endpwent ();
    } else if (numusers == 1) {
        uids[0] = geteuid();
    } else {
        msg_exit ("numusers must be >= 1"); 
    }
    
    if (!(fs = npc_start (fd, 8192+24, NPC_MULTI_RPC)))
        errn_exit (np_rerror (), "npc_start");

    for (i = 0; i < numthreads; i++) {
	t[i].fs = fs;
	t[i].uid = uids[i % numusers];
	t[i].aname = aname;
	err = pthread_create (&t[i].t, NULL, client, &t[i]);
        if (err)
            errn_exit (err, "pthread_create");
    } 

    for (i = 0; i < numthreads; i++) {
	pthread_join (t[i].t, NULL);
    }

    npc_finish (fs);

    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
