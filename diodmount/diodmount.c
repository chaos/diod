/* diod_mount.c - mount a diod file system */

/* Usage: diodmount device dir
 * This program runs setuid root, but sets up the mount to be used (only)
 * by the real uid.  Access by other users including root will be rejected.
 *
 * N.B. For a brief moment we mount the /diodctl control file system on the
 * mount point to obtain the port number for a server running as the real uid.
 *
 * FIXME: error handling needs some work.  We need to unmount /diodctl
 * if we fail before the other file system gets mounted.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _GNU_SOURCE     /* asprintf */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#define GPL_LICENSED 1
#include <munge.h>

#include "diod_log.h"

#define OPTIONS "?"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void _parse_device (char *device, char **anamep, char **ipp);
static void _create_mungecred (char **credp);
static void _get_server (char *dir, char **portp);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodmount [OPTIONS] device directory\n");
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *device, *dir, *options, *uname, *aname, *ip, *port;
    int c;
    struct stat sb;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case '?':
            default:
                usage ();
        }
    }

    if (optind != argc - 2)
        usage ();
    device = argv[optind++];
    dir = argv[optind++];

    if (geteuid () != 0)
        msg_exit ("effective uid is not root");

    if (stat (dir, &sb) < 0)
        err_exit (dir);
    if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s: not a directory", dir);
    _parse_device (device, &aname, &ip);

    /* mount /diodctl temporarily to get the server port to use */
    _create_mungecred (&uname);
    if (asprintf (&options, "uname=%s,aname=/diodctl,msize=65560", uname) < 0)
        msg_exit ("out of memory");
    if (mount (ip, dir, "9p", 0, options) < 0)
        err_exit ("mount");
    _get_server (dir, &port);
    if (umount (dir) < 0)
        err_exit ("umount");
    free (options);
    free (uname);

    usleep(500000); /* FIXME: allow time for diod to call listen */

    /* now mount the real file system */
    _create_mungecred (&uname);
    if (asprintf (&options, "port=%s,uname=%s,aname=%s,msize=65560", port, uname, aname) < 0)
        msg_exit ("out of memory");

    msg ("options: %s", options);

    if (mount (ip, dir, "9p", 0, options) < 0)
        err_exit ("mount");
    free (options);
    free (uname);

    exit (0);
}

/* Poke at the diodctl file system to get a diod port allocated.
 */
static void
_get_server (char *dir, char **portp)
{
    char *ctl, *server;
    FILE *f;
    char buf[NI_MAXHOST + PATH_MAX + 2];
    char *port;

    sleep (1); /* FIXME: avoid a segfault in Npfile land  */

    /* swap real and effective uid/gid's */
    if (setegid (getgid ()) < 0 || seteuid (getuid ()) < 0)
        err_exit ("failed to temporarily drop root privileges");

    if (asprintf (&ctl, "%s/ctl", dir) < 0)
        msg_exit ("out of memory");
    if (asprintf (&server, "%s/server", dir) < 0)
        msg_exit ("out of memory");

    /* poke ctl to ask for new server (if needed) */
    if (!(f = fopen (ctl, "w")))
        err_exit ("error opening ctl file");
    if (fprintf (f, "new") < 0)
        err_exit ("error writing ctl file");
    fclose (f);

    /* read server to get host:port */
    if (!(f = fopen (server, "r")))
        err_exit ("error opening server file");
    if (!(fgets (buf, sizeof(buf), f)))
        err_exit ("unexpected EOF reading server file");
    fclose (f);
    port = strchr (buf, ':');
    if (!port)
        err_exit ("server file did not contain host:port");
    port++;
    if (port [strlen(port) - 1] == '\n')
        port [strlen(port) - 1] = '\0';

    /* swap back */
    if (seteuid (0) < 0 || setegid (0) < 0)
        err_exit ("failed to restore root privileges");

    msg ("obtained port %s", port);

    *portp = port;
}

/* Create a munge credential for the real user id.
 */
static void
_create_mungecred (char **credp)
{
    munge_ctx_t ctx;
    munge_err_t err;
    char *mungecred;

    if (!(ctx = munge_ctx_create ()))
        err_exit ("out of memory");
 
    /* swap real and effective uid/gid's */
    if (setegid (getgid ()) < 0 || seteuid (getuid ()) < 0)
        err_exit ("failed to temporarily drop root privileges");

    err = munge_encode (&mungecred, ctx, NULL, 0);
    if (err != EMUNGE_SUCCESS)
        msg_exit ("munge_encode: %s", munge_strerror (err));

    /* swap back */
    if (seteuid (0) < 0 || setegid (0) < 0)
        err_exit ("failed to restore root privileges");

    munge_ctx_destroy (ctx);

    *credp = mungecred;
}

/* Given a "device" in host:aname format, parse out the host (converting
 * to ip address for in-kernel v9fs), and the aname.
 */
static void
_parse_device (char *device, char **anamep, char **ipp)
{
    char *host = device, *aname;
    struct addrinfo hints, *res;
    static char ip[NI_MAXHOST];
    int error;

    aname = strchr (device, ':');
    if (!aname)
        msg_exit ("device is not in host:directory format");
    *aname ++ = '\0';

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if ((error = getaddrinfo (host, NULL, &hints, &res)))
        err_exit ("getaddrinfo: %s: %s", host, gai_strerror(error));
    if (res == NULL)
        err_exit ("%s has no address info", host);
    /* FIXME: we take the first entry in the res array */
    if (getnameinfo (res->ai_addr, res->ai_addrlen, ip, sizeof(ip),
                     NULL, 0, NI_NUMERICHOST) < 0)
        err_exit ("%s has no address", host);
    *anamep = aname;
    *ipp = ip; 
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
