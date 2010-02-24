/* diodmount.c - mount a diod file system */

/* Usage: diodmount host:path dir
 *     All users can access dir.
 *     The server is shared by all mounts of this type for all users.
 *
 * Usage: diodmount -u USER host:path dir
 *     Only USER can access dir.
 *     The server is shared by all mounts of this type for all users.
 *       
 * Usage: diodmount -p -u USER host:path dir
 *     Only USER can access dir.
 *     The server is shared by all mounts by USER of this type.
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
#include <mntent.h>
#define GPL_LICENSED 1
#include <munge.h>

#include "npfs.h"

#include "diod_log.h"
#include "diod_upool.h"

#define OPTIONS "u:pc:d:n"
#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"mount-user",      required_argument,   0, 'u'},
    {"private-server",  no_argument,         0, 'p'},
    {"diodctl-port",    required_argument,   0, 'c'},
    {"diod-port",       required_argument,   0, 'd'},
    {"no-mtab",         no_argument,         0, 'n'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void  _parse_device     (char *device, char **anamep, char **ipp);
static void  _create_mungecred (char **credp, char *payload);
static void  _diod_mount       (char *ip, char *dir, char *aname, char *port);
static void  _diodctl_mount    (char *ip, char *dir, char *port);
static char *_diodctl_getport  (char *dir);
static void  _umount           (const char *target);
static int   _update_mtab      (char *dev, char *dir, char *opt);

static void
usage (void)
{
    fprintf (stderr,
"Usage: diodmount [OPTIONS] device directory\n"
"   -u,--mount-user USER         set up the mount so only USER can use it\n"
"   -p,--private-server          get private server instance for this user\n"
"   -c,--diodctl-port PORT       connect to diodctl using PORT (only if -p)\n"
"   -d,--diod-port PORT          connect to diod using PORT (only if not -p)\n"
"   -n,--no-mtab                 do not update /etc/mtab\n"
);
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *device, *dir, *aname, *ip;
    int c;
    struct stat sb;
    int popt = 0;
    int nopt = 0;
    char *uopt = NULL;
    char *copt = NULL;
    char *dopt = NULL;

    diod_log_init (argv[0]);

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'u':   /* --mount-user USER */
                uopt = optarg;
                break;
            case 'p':   /* --private-server */
                popt = 1;
                break;
            case 'd':   /* --diod-port PORT */
                dopt = optarg;
                break;
            case 'c':   /* --diodctl-port PORT */
                copt = optarg;
                break;
            case 'n':   /* --no-mtab */
                nopt = 1;
                break;
            default:
                usage ();
        }
    }

    if (optind != argc - 2)
        usage ();
    device = argv[optind++];
    dir = argv[optind++];

    if (popt && !uopt)
        msg_exit ("--private-server requested but no --mount-user given");
    if (popt && dopt)
        msg_exit ("--private-server and --diod-port cannot be used together");
    if (copt && !popt)
        msg_exit ("--diodctl-port can only be used with --private-server");
    if (uopt && !strcmp (uopt, "root") && !popt)
        msg_exit ("--mount-user root can only be used with --private-server");

    if (stat (dir, &sb) < 0)
        err_exit (dir);
    if (!S_ISDIR (sb.st_mode))
        msg_exit ("%s: not a directory", dir);

    if (geteuid () != 0)
        msg_exit ("effective uid is not root");

    /* If not mounting as root, seteuid to the user whose munge
     * credentials will be used for the initial attach.
     */
    if (uopt)
        diod_become_user (uopt, 0, 0);

    _parse_device (device, &aname, &ip);

    if (popt) {
        char *port;

        _diodctl_mount (ip, dir, copt);
        port = _diodctl_getport (dir);
        _umount (dir);
        if (!port)
            exit (1); 
        _diod_mount (ip, dir, aname, port);
        free (port);
    } else
        _diod_mount (ip, dir, aname, dopt);

    free (aname);
    free (ip);

    if (!nopt) {
        if (!_update_mtab (device, dir, MNTOPT_DEFAULTS)) {
            _umount (dir);
            exit (1);
        }
    }

    exit (0);
}

static int
_update_mtab (char *dev, char *dir, char *opt)
{
    uid_t saved_euid = geteuid ();
    FILE *f;
    int ret = 0;
    struct mntent mnt;

    mnt.mnt_fsname = dev;
    mnt.mnt_dir = dir;
    mnt.mnt_type = "diod";
    mnt.mnt_opts = opt;
    mnt.mnt_freq = 0;
    mnt.mnt_passno = 0;

    if (seteuid (0) < 0) {
        err ("failed to set effective uid to root");
        goto done;
    }
    if (!(f = setmntent (_PATH_MOUNTED, "a"))) {
        err (_PATH_MOUNTED);
        goto done;
    }
    if (addmntent (f, &mnt) != 0) {
        msg ("failed to add entry to %s", _PATH_MOUNTED);
        endmntent (f);
        goto done;
    }
    endmntent (f);
    if (seteuid (saved_euid) < 0) {
        err ("failed to restore effective uid to %d", saved_euid);
        goto done;
    }
    ret = 1;
done:
    return ret;
}

static void
_mount (const char *source, const char *target, const void *data)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (mount (source, target, "9p", 0, data))
        err_exit ("mount");
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

static void
_umount (const char *target)
{
    uid_t saved_euid = geteuid ();

    if (seteuid (0) < 0)
        err_exit ("failed to set effective uid to root");
    if (umount (target) < 0)
        err_exit ("umount %s", target);
    if (seteuid (saved_euid) < 0)
        err_exit ("failed to restore effective uid to %d", saved_euid);
}

static void
_create_mungecred (char **credp, char *payload)
{
    int paylen = payload ? strlen(payload) : 0;
    munge_ctx_t ctx;
    munge_err_t err;
    char *mungecred;

    if (!(ctx = munge_ctx_create ()))
        err_exit ("out of memory");
 
    err = munge_encode (&mungecred, ctx, payload, paylen);
    if (err != EMUNGE_SUCCESS)
        msg_exit ("munge_encode: %s", munge_strerror (err));

    munge_ctx_destroy (ctx);
    *credp = mungecred;
}

static void
_diod_mount (char *ip, char *dir, char *aname, char *port)
{
    char *options, *cred;
    char access[32];

    if (geteuid () != 0) 
        snprintf (access, sizeof (access), ",access=%d", geteuid ());

    _create_mungecred (&cred, NULL);
    /* FIXME: msize should be configurable */
    if (asprintf (&options, "port=%s,uname=%s,aname=%s,msize=65560%s",
                  port ? port : "10006", cred, aname, access) < 0) {
        msg_exit ("out of memory");
    }
    _mount (ip, dir, options);
    free (cred);
    free (options);
}

static void
_diodctl_mount (char *ip, char *dir, char *port)
{
    char *options, *cred;
    
    _create_mungecred (&cred, NULL);
    if (asprintf (&options, "port=%s,uname=%s,aname=/diodctl,access=%d",
                  port ? port : "10005", cred, geteuid ()) < 0) {
        msg_exit ("out of memory");
    }
    _mount (ip, dir, options);
    free (cred);
    free (options);
}

static char *
_diodctl_getport (char *dir)
{
    char *ctl = NULL, *server = NULL;
    FILE *f;
    int port;
    char *ret = NULL;
   
    /* poke /ctl to trigger creation of new server (if needed) */ 
    if (asprintf (&ctl, "%s/ctl", dir) < 0) {
        msg ("out of memory");
        goto done;
    }
    if (!(f = fopen (ctl, "w"))) {
        err ("error opening %s", ctl);
        goto done;
    }
    if (fprintf (f, "new") < 0) {
        err ("error writing to %s", ctl);
        fclose (f);
        goto done;
    }
    fclose (f);

    /* read port from /server */
    if (asprintf (&server, "%s/server", dir) < 0) {
        msg ("out of memory");
        goto done;
    }
    if (!(f = fopen (server, "r"))) {
        err ("error opening %s", server);
        goto done;
    }
    if (fscanf (f, "%d", &port) != 1) {
        msg ("failed to read port number from %s", server);
        fclose (f);
        goto done;
    }
    fclose (f);
    if (!(ret = malloc (NI_MAXSERV))) {
        msg ("out of memory");
        goto done;
    }
    snprintf (ret, NI_MAXSERV, "%d", port); 
done:
    if (ctl)
        free (ctl);
    if (server)
        free (server);
    return ret;
}

/* Obtain the IP address for 'name' and write it into 'ip' which is of
 * size 'len'.
 */
static char *
_name2ip (char *name)
{
    char buf[NI_MAXHOST], *ip;
    struct addrinfo hints, *res;
    int error;

    memset (&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((error = getaddrinfo (name, NULL, &hints, &res)))
        err_exit ("getaddrinfo: %s: %s", name, gai_strerror(error));
    if (!res)
        err_exit ("%s has no address info", name);

    /* FIXME: we take the first entry in the res array.
     * Should we loop through them and take the first one that works?
     */
    if (getnameinfo (res->ai_addr, res->ai_addrlen, buf, sizeof (buf),
                     NULL, 0, NI_NUMERICHOST) < 0)
        err_exit ("%s has no address", name);
    freeaddrinfo (res);
    if (!(ip = strdup (buf)))
        err_exit ("out of memory");
    return ip;
}

/* Given a "device" in host:aname format, parse out the host (converting
 * to ip address for in-kernel v9fs which can't handle hostnames),
 * and the aname.  Exit on error.
 */
static void
_parse_device (char *device, char **anamep, char **ipp)
{
    char *host, *ip, *p, *aname;

    if (!(host = strdup (device)))
        msg_exit ("out of memory");
    if (!(p = strchr (host, ':')))
        msg_exit ("device is not in host:directory format");
    *p++ = '\0';
    if (!(aname = strdup (p)))
        msg_exit ("out of memory");
    ip = _name2ip (host);
    free (host);
    
    *ipp = ip;
    *anamep = aname;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
