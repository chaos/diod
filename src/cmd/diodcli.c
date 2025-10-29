/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* diodcli.c - poke at 9p server using libnpclient
 *
 * Environment used in the test suite:
 *
 * DIOD_SERVER_FD
 *   Attach to pre-connected file server and don't close it.
 *
 * DIOD_SERVER_ANAME
 *   Attach to the specified aname.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#if HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#if HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#else
#include <attr/xattr.h>
#endif

#include "src/libnpfs/npfs.h"
#include "src/libnpclient/npclient.h"

#include "src/liblsd/list.h"
#include "src/libdiod/diod_log.h"
#include "src/libdiod/diod_sock.h"
#include "src/libdiod/diod_auth.h"

typedef int (*subcommand_f)(Npcfid *root, int argc, char **argv);

struct subcmd {
    const char *name;
    const char *desc;
    subcommand_f cmd;
};

static char *prog;

/* Try to Twalk an open fid.
 * This mimics what the linux kernel v9fs client does when
 * we ls -l a mounted directory.
 */
int cmd_open_walk (Npcfid *root, int argc, char **argv)
{
    char *filename;
    Npcfid *fid;
    int rc = -1;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    fid = npc_open_bypath (root, filename, O_RDONLY);
    if (fid == NULL) {
        errn (np_rerror (), "open %s", filename);
        return -1;
    }
    if (npc_walk (fid, "..") < 0) {
        errn (np_rerror (), "walk %s/..", filename);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk (fid) < 0) {
        errn (np_rerror (), "clunk %s", filename);
        return -1;
    }
    return rc;
}

/* Tread should work on opened fids even if the file has been removed
 */
int cmd_open_remove_read (Npcfid *root, int argc, char **argv)
{
    char *filename;
    Npcfid *fid;
    int rc = -1;
    char buf[3];

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    fid = npc_open_bypath (root, filename, O_RDONLY);
    if (fid == NULL) {
        errn (np_rerror (), "open %s", filename);
        return -1;
    }
    if (npc_remove_bypath (root, filename) < 0) {
        errn (np_rerror (), "remove %s", filename);
        goto done;
    }
    if (npc_read (fid, buf, sizeof (buf)) != sizeof(buf)) {
        errn (np_rerror (), "read 3 bytes of %s", filename);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk(fid) < 0) {
        errn (np_rerror (), "clunk %s", filename);
        return -1;
    }
    return rc;
}

/* Tgetattr should work on opened fids even if the file has been removed
 */
int cmd_open_remove_getattr (Npcfid *root, int argc, char **argv)
{
    char *filename;
    Npcfid *fid;
    struct stat sb;
    int rc = -1;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    if (npc_stat (root, filename, &sb) < 0) {
        errn (np_rerror (), "stat %s", filename);
        return -1;
    }
    fid = npc_open_bypath (root, filename, O_RDONLY);
    if (fid == NULL) {
        errn (np_rerror (), "open %s", filename);
        return -1;
    }
    if (npc_remove_bypath (root, filename) < 0) {
        errn (np_rerror (), "remove %s", filename);
        goto done;
    }
    if (npc_fstat (fid, &sb) < 0) {
        errn (np_rerror (), "fstat %s", filename);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk(fid) < 0) {
        errn (np_rerror (), "clunk %s", filename);
        return -1;
    }
    return rc;
}

/* Tsetattr should work on opened fids even if the file has been removed
 */
int cmd_open_remove_setattr (Npcfid *root, int argc, char **argv)
{
    char *filename;
    Npcfid *fid;
    struct stat sb;
    int rc = -1;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    if (npc_stat (root, filename, &sb) < 0) {
        errn (np_rerror (), "stat %s", filename);
        return -1;
    }
    fid = npc_open_bypath (root, filename, O_RDONLY);
    if (fid == NULL) {
        errn (np_rerror (), "open %s", filename);
        return -1;
    }
    if (npc_remove_bypath (root, filename) < 0) {
        errn (np_rerror (), "remove %s", filename);
        goto done;
    }
    if (npc_fchmod (fid, 0444) < 0) {
        errn (np_rerror (), "fchmod %s", filename);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk(fid) < 0) {
        errn (np_rerror (), "clunk %s", filename);
        return -1;
    }
    return rc;
}

int create_file (Npcfid *root, char *name, u32 mode, char *content, size_t len)
{
    Npcfid *fid;
    int rc = -1;

    fid = npc_create_bypath (root, name, O_TRUNC|O_RDWR, mode, getgid ());
    if (fid == NULL) {
        errn (np_rerror (), "create %s", name);
        return -1;
    }
    if (npc_write (fid, content, len) != len) {
        errn (np_rerror (), "write %s", name);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk (fid) == -1) {
        errn (np_rerror (), "clunk %s", name);
        return -1;
    }
    return rc;
}

/* Tsetattr on a fid opened from path should not affect path when path
 * is no longer the same file
 */
int cmd_open_remove_create_setattr (Npcfid *root, int argc, char **argv)
{
    char *filename;
    Npcfid *fid;
    struct stat sb;
    int rc = -1;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    if (create_file (root, filename, 0644, filename, 3) < 0)
        return -1;
    fid = npc_open_bypath (root, filename, O_RDONLY);
    if (fid == NULL) {
        errn (np_rerror (), "open %s", filename);
        return -1;
    }
    if (npc_remove_bypath (root, filename) < 0) {
        errn (np_rerror (), "remove %s", filename);
        goto done;
    }
    if (create_file (root, filename, 0644, "foo", 3) < 0)
        goto done;
    if (npc_fchmod (fid, 0444) < 0) {
        errn (np_rerror (), "fchmod 0444 %s", filename);
        goto done;
    }
    if (npc_fstat (fid, &sb) < 0) {
        errn (np_rerror (), "fstat %s", filename);
        goto done;
    }
    if ((sb.st_mode & 0x777) != 0444) {
        msg ("fchmod didn't change sb.st_mode");
        goto done;
    }
    if (npc_stat (root, filename, &sb) < 0) {
        errn (np_rerror (), "stat %s", filename);
        goto done;
    }
    if ((sb.st_mode & 0777) != 0644) {
        msg ("stat changed %o", sb.st_mode);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk(fid) < 0) {
        errn (np_rerror (), "clunk %s", filename);
        return -1;
    }
    return rc;
}

// see issue chaos/diod#93
int cmd_create_rename (Npcfid *root, int argc, char **argv)
{
    Npcfid *fid;
    int rc = -1;

    if (argc != 3) {
        fprintf (stderr, "Usage: %s %s name newname\n", prog, argv[0]);
        return -1;
    }
    char *name = argv[1];
    char *newname = argv[2];
    fid = npc_create_bypath (root, name, 0, 0, getgid ());
    if (fid == NULL) {
        errn (np_rerror (), "create %s", name);
        return -1;
    }
    if (npc_rename (fid, root, newname) < 0) {
        errn (np_rerror (), "rename %s %s", name, newname);
        goto done;
    }
    name = newname; // in case of clunk error, show the new name
    rc = 0;
done:
    if (npc_clunk (fid) < 0) {
        errn (np_rerror (), "clunk %s", name);
        return -1;
    }
    return rc;
}

int cmd_listxattr (Npcfid *root, int argc, char **argv)
{
    char *filename;
    char *buf;
    size_t len;
    int rc = -1;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];

    len = npc_listxattr (root, filename, NULL, 0);
    if (len < 0) {
        errn (np_rerror (), "%s", filename);
        return -1;
    }
    if (!(buf = calloc (1, len)))
        err_exit ("could not allocate buffer");
    len = npc_listxattr (root, filename, buf, len);
    if (len < 0) {
        errn (np_rerror (), "%s", filename);
        goto done;
    }
    int i, count;
    for (i = 0, count = 0; i < len && buf[i] != '\0'; count++) {
        char *key = &buf[i];
        int klen = len - i;

        /* N.B. Skip non-user.*, or we may see security.selinux
         * (depending on host config) and fail test output comparison.
         */
        if (!strncmp (&buf[i], "user.", klen < 5 ? klen : 5)) {
            printf ("%.*s\n", klen, key);
            fflush (stdout);
        }
        while (i < len && buf[i] != '\0')
            i++;
        i++;
    }
    rc = 0;
done:
    free (buf);
    return rc;
}

int cmd_delxattr (Npcfid *root, int argc, char **argv)
{
    char *filename;
    char *attrname;

    if (argc != 3) {
        fprintf (stderr, "Usage: %s %s filename attrname\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    attrname = argv[2];

    if (npc_setxattr (root, filename, attrname, NULL, 0, 0) < 0) {
        errn (np_rerror (), "%s", attrname);
        return -1;
    }
    return 0;
}

void usage_setxattr (const char *cmdname)
{
    fprintf (stderr,
             "Usage: %s %s"
             " [--create | --replace] filename attrname value\n",
             prog,
             cmdname);
}

int cmd_setxattr (Npcfid *root, int argc, char **argv)
{
    char *filename;
    char *option = NULL;
    char *attrname;
    char *attrvalue;
    int flags = 0;
    int index;

    if (argc != 5 && argc != 4) {
        usage_setxattr (argv[0]);
        return -1;
    }
    index = 1;
    if (argc == 5)
        option = argv[index++];
    filename = argv[index++];
    attrname = argv[index++];
    attrvalue = argv[index++];

    if (option) {
        if (!strcmp (option, "--create"))
            flags |= XATTR_CREATE;
        else if (!strcmp (option, "--replace"))
            flags |= XATTR_REPLACE;
        else
            usage_setxattr (argv[0]);
    }

    if (npc_setxattr (root,
                      filename,
                      attrname,
                      attrvalue,
                      strlen (attrvalue),
                      flags) < 0) {
        errn (np_rerror (), "%s", attrname);
        return -1;
    }
    return 0;
}

/* Regression test for missing bounds check
 * See chaos/diod#110
 */
int cmd_setxattr_wildoffset (Npcfid *root, int argc, char **argv)
{
    char *filename;
    char *attrname;
    char *attrvalue;
    Npcfid *fid;
    u64 wild_offset = 2684469248;
    int rc = -1;

    if (argc != 4) {
        fprintf (stderr,
                 "Usage: %s %s filename attrname value\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    attrname = argv[2];
    attrvalue = argv[3];

    if (!(fid = npc_walk (root, filename))) {
        errn (np_rerror (), "walk %s", filename);
        return -1;
    }
    if (npc_xattrcreate (fid, attrname, strlen (attrvalue), XATTR_CREATE) < 0) {
        errn (np_rerror (), "xattrcreate %s", filename);
        goto done;
    }
    if (npc_pwrite (fid, attrvalue, strlen (attrvalue), wild_offset) < 0) {
        errn (np_rerror (), "pwrite %s", filename);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk (fid) < 0) {
        errn (np_rerror (), "clunk %s", filename);
        return -1;
    }
    return rc;
}

int cmd_getxattr (Npcfid *root, int argc, char **argv)
{
    char *filename;
    char *attrname;
    char *attrvalue;
    ssize_t n;
    int rc = -1;

    if (argc != 3) {
        fprintf (stderr, "Usage: %s %s filename attrname\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    attrname = argv[2];

    /* First see how big our buffer should be.
     */
    n = npc_getxattr (root, filename, attrname, NULL, 0);
    if (n < 0) {
        errn (np_rerror (), "%s", attrname);
        return -1;
    }
    if (!(attrvalue = calloc (1, n)))
        err_exit ("allocating buffer");
    /* Now fetch the value.
     */
    n = npc_getxattr (root, filename, attrname, attrvalue, n);
    if (n < 0) {
        errn (np_rerror (), "%s", attrname);
        goto done;
    }
    printf ("%.*s\n", (int)n, attrvalue);
    fflush (stdout);
    rc = 0;
done:
    free (attrvalue);
    return rc;
}

/* equivalent to
 *   stat -c "mode=%f owner=%u:%g size=%s blocks=%b blocksize=%o
 *            links=%h device=%t:%T mtime=%Y ctime=%Z atime=%X"
 */
void print_stat (struct stat *sb)
{
    printf ("mode=%x owner=%d:%d size=%ju blocks=%ju blocksize=%lu"
            " links=%lu device=%x:%x mtime=%ju ctime=%ju atime=%ju\n",
            sb->st_mode,
            sb->st_uid,
            sb->st_gid,
            (uintmax_t)sb->st_size,
            (uintmax_t)sb->st_blocks,
            sb->st_blksize,
            sb->st_nlink,
            major (sb->st_rdev),
            minor (sb->st_rdev),
            (uintmax_t)sb->st_mtime,
            (uintmax_t)sb->st_ctime,
            (uintmax_t)sb->st_atime);
}

int cmd_stat (Npcfid *root, int argc, char **argv)
{
    char *filename;
    struct stat sb;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    if (npc_stat (root, filename, &sb) < 0) {
        errn (np_rerror (), "%s", filename);
        return -1;
    }
    print_stat (&sb);
    return 0;
}

int cmd_mkdir (Npcfid *root, int argc, char **argv)
{
    char *directory;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s directory\n", prog, argv[0]);
        return -1;
    }
    directory = argv[1];
    if (npc_mkdir_bypath (root, directory, 0755) < 0) {
        errn (np_rerror (), "%s", directory);
        return -1;
    }
    return 0;
}

int cmd_write (Npcfid *root, int argc, char **argv)
{
    char *filename;
    Npcfid *fid;
    int n;
    u8 buf[65536];
    int rc = -1;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];

    fid = npc_create_bypath (root, filename, O_WRONLY, 0644, getegid ());
    if (!fid) {
        errn (np_rerror (), "%s", filename);
        return -1;
    }

    while ((n = read (STDIN_FILENO, buf, sizeof (buf))) > 0) {
        int done = 0;
        int m;
        do {
            m = npc_write (fid, buf + done, n - done);
            if (m < 0) {
                errn (np_rerror (), "%s write", filename);
                goto done;
            }
            done += m;
        } while (done < n);
    }
    if (n < 0)
        err_exit ("stdin read");
    rc = 0;
done:
    if (npc_clunk (fid) < 0) {
        errn (np_rerror (), "%s clunk", filename);
        return -1;
    }
    return rc;
}

int cmd_read (Npcfid *root, int argc, char **argv)
{
    char *filename;
    Npcfid *fid;
    int n;
    u8 buf[65536];
    int rc = -1;

    if (argc != 2) {
        fprintf (stderr, "Usage: %s %s filename\n", prog, argv[0]);
        return -1;
    }
    filename = argv[1];
    fid = npc_open_bypath (root, filename, O_RDONLY);
    if (!fid) {
        errn (np_rerror (), "%s", filename);
        return -1;
    }

    while ((n = npc_read (fid, buf, sizeof (buf))) > 0) {
        int done = 0;
        int m;
        do {
            if ((m = write (STDOUT_FILENO, buf + done, n - done)) < 0)
                err_exit ("stdout write");
            done += m;
        } while (done < n);
    }
    if (n < 0) {
        errn (np_rerror (), "%s read", filename);
        goto done;
    }
    rc = 0;
done:
    if (npc_clunk (fid) < 0) {
        errn (np_rerror (), "%s clunk", filename);
        return -1;
    }
    return rc;
}

int cmd_null (Npcfid *root, int argc, char **argv)
{
    if (argc != 1) {
        fprintf (stderr, "Usage: %s\n", argv[0]);
        return -1;
    }
    return 0;
}

static int run_subcmd (struct subcmd *subcmd,
                       int server_fd,
                       uid_t uid,
                       int msize,
                       char *aname,
                       int argc,
                       char **argv)
{
    Npcfsys *fs = NULL;
    Npcfid *afid = NULL;
    Npcfid *root = NULL;
    int ret = -1;

    if (!(fs = npc_start (server_fd, server_fd, msize, 0))) {
        errn (np_rerror (), "start");
        goto done;
    }
    if (!(afid = npc_auth (fs, aname, uid, diod_auth)) && np_rerror () != 0) {
        errn (np_rerror (), "auth");
        goto done;
    }
    if (!(root = npc_attach (fs, afid, aname, uid))) {
        errn (np_rerror (), "attach");
        goto done;
    }

    if (subcmd->cmd (root, argc, argv) < 0)
        goto done;

    ret = 0;
done:
    if (root && npc_clunk (root) < 0) {
        errn (np_rerror (), "clunk root");
        ret = -1;
    }
    if (afid && npc_clunk (afid) < 0) {
        errn (np_rerror (), "clunk auth afid");
        ret = -1;
    }
    if (fs)
        npc_finish (fs);
    return ret;
}

static const char *options = "+a:s:m:u:p";

static const struct option longopts[] = {
    {"aname",   required_argument,      0, 'a'},
    {"server",  required_argument,      0, 's'},
    {"msize",   required_argument,      0, 'm'},
    {"uid",     required_argument,      0, 'u'},
    {"privport",no_argument,            0, 'p'},
    {0, 0, 0, 0},
};

static struct subcmd subcmds[] = {
    {
        .name = "null",
        .desc = "do nothing, just attach and clunk",
        .cmd = cmd_null,
    },
    {
        .name = "read",
        .desc = "copy 9p file to stdout",
        .cmd = cmd_read,
    },
    {
        .name = "write",
        .desc = "copy stdin to 9p file",
        .cmd = cmd_write,
    },
    {
        .name = "mkdir",
        .desc = "create directory",
        .cmd = cmd_mkdir,
    },
    {
        .name = "stat",
        .desc = "display file statistics",
        .cmd = cmd_stat,
    },
    {
        .name = "getxattr",
        .desc = "get extended attribute",
        .cmd = cmd_getxattr,
    },
    {
        .name = "setxattr",
        .desc = "set extended attribute",
        .cmd = cmd_setxattr,
    },
    {
        .name = "delxattr",
        .desc = "delete extended attribute",
        .cmd = cmd_delxattr,
    },
    {
        .name = "listxattr",
        .desc = "list extended attributes",
        .cmd = cmd_listxattr,
    },
    {
        .name = "bug-setxattr-offsetcheck",
        .desc = "Tsetxattr with wild offset",
        .cmd = cmd_setxattr_wildoffset,
    },
    {
        .name = "bug-open-walk",
        .desc = "Twalk on open fid",
        .cmd = cmd_open_walk,
    },
    {
        .name = "bug-open-rm-read",
        .desc = "Tread on open fid after remove",
        .cmd = cmd_open_remove_read,
    },
    {
        .name = "bug-open-rm-getattr",
        .desc = "Tgetattr on open fid after remove",
        .cmd = cmd_open_remove_getattr,
    },
    {
        .name = "bug-open-rm-setattr",
        .desc = "Tsetattr on open fid after remove",
        .cmd = cmd_open_remove_setattr,
    },
    {
        .name = "bug-open-move-setattr",
        .desc = "Tsetattr on open fid after move",
        .cmd = cmd_open_remove_create_setattr,
    },
    {
        .name = "bug-create-rename",
        .desc = "Trename on a newly created fid",
        .cmd = cmd_create_rename,
    },
};

static void
usage (void)
{
    fprintf (stderr,
"Usage: %s [OPTIONS] subcommand [SUBOPTIONS] ...\n"
"   -a,--aname NAME       file system (default ctl)\n"
"   -s,--server HOST:PORT server (default localhost:564)\n"
"   -m,--msize            msize (default 65536)\n"
"   -u,--uid              authenticate as uid (default is your euid)\n"
"   -t,--timeout SECS     give up after specified seconds\n"
"   -p,--privport         connect from a privileged port (root user only)\n"
"Subcommands:\n",
    prog);
    for (int i = 0; i < sizeof (subcmds) / sizeof (subcmds[0]); i++) {
        fprintf (stderr, "    %-30s %s\n", subcmds[i].name, subcmds[i].desc);
    }
    exit (1);
}

int
main (int argc, char *argv[])
{
    char *aname = NULL;
    char *server = NULL;
    int msize = 65536;
    uid_t uid = geteuid ();
    int flags = 0;
    int c;
    int server_fd;
    struct subcmd *subcmd = NULL;
    bool not_my_serverfd = false;

    prog = basename (argv[0]);
    diod_log_init (prog);

    opterr = 0;
    while ((c = getopt_long (argc, argv, options, longopts, NULL)) != -1) {
        switch (c) {
            case 'a':   /* --aname NAME */
                aname = optarg;
                break;
            case 's':   /* --server HOST[:PORT] or /path/to/socket */
                server = optarg;
                break;
            case 'm':   /* --msize SIZE */
                msize = strtoul (optarg, NULL, 10);
                break;
            case 'u':   /* --uid UID */
                uid = strtoul (optarg, NULL, 10);
                break;
            case 'p':   /* --privport */
                flags |= DIOD_SOCK_PRIVPORT;
                break;
            default:
                usage ();
        }
    }
    if (optind == argc)
        usage ();
    for (int i = 0; i < sizeof (subcmds) / sizeof (subcmds[0]); i++) {
        if (!strcmp (subcmds[i].name, argv[optind]))
            subcmd = &subcmds[i];
    }
    if (!subcmd)
        usage ();

    const char *s = getenv ("DIOD_SERVER_FD");
    if (server || !s) {
        server_fd = diod_sock_connect (server, flags);
        if (server_fd < 0)
            exit (1);
    }
    else {
        server_fd = strtoul (s, NULL, 10);
        not_my_serverfd = true;
    }
    if (!aname)
        aname = getenv ("DIOD_SERVER_ANAME");

    if (run_subcmd (subcmd,
                    server_fd,
                    uid,
                    msize,
                    aname,
                    argc - optind,
                    argv + optind) < 0)
        exit (1);

    if (!not_my_serverfd)
        close (server_fd);

    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
