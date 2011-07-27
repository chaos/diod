/* tsetfsuidsupp.c - do suppl. groups work as expected with fsuid/fsgid? */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/fsuid.h>
#include <grp.h>

#include "diod_log.h"
#include "test.h"

#define TEST_UID 100
#define TEST_GID 100
#define TEST_SGID 101

static int
check_fsid (uid_t uid, gid_t gid)
{
    int fd;
    char path[] = "/tmp/testfsuidsupp.XXXXXX";
    struct stat sb;

    fd = _mkstemp (path);
    _fstat (fd, &sb);
    _unlink (path);

    return (sb.st_uid == uid && sb.st_gid == gid);
}

/* Create a test file and return its path.
 */
static char *
create_file (uid_t uid, gid_t gid, mode_t mode)
{
    int fd;
    static char path[] = "/tmp/testfsuidsupp.XXXXXX";

    fd = _mkstemp (path);
    _fchown (fd, 0, TEST_SGID);
    _fchmod (fd, 0040);

    msg ("file created %d:%d mode 0%o", uid, gid, mode);

    return path;
}

/* Return 0 if 'path' cannot be opened for reading,
 * nonzero if it can.
 */
static int
read_file (char *path)
{
    int fd;

    if ((fd = open (path, O_RDONLY)) < 0) {
        msg ("file is NOT readable");
        return 0;
    }
    close (fd);
    msg ("file is readable");
    return 1;
}

static void
change_fsid (uid_t olduid, gid_t oldgid, uid_t uid, gid_t gid)
{
    int u;
    int g;

    if (uid != olduid) {
        u = setfsuid (uid);
        if (u == -1)
            err ("setfsuid");
        else if (u != olduid)
            msg ("setfsuid returned %d (wanted %d)", u, olduid);
    }
    if (gid != oldgid) {
        g = setfsgid (gid);
        if (g == -1)
            err ("setfsgid");
        else if (g != oldgid)
            msg ("setfsgid returned %d (wanted %d)", g, oldgid);
    }
    if (!check_fsid (uid, gid))
        msg_exit ("setfsuid/setfsgid failed");
    msg ("fsid changed to %d:%d", uid, gid);
}

int main(int argc, char *argv[])
{
    char *path;
    gid_t gids[] = { TEST_SGID };

    diod_log_init (argv[0]);

    assert (geteuid () == 0);

    /* clear supplemental groups */
    _setgroups (0, NULL);
    msg ("supplemental groups cleared");

    assert (geteuid () == 0);
    path = create_file (0, TEST_SGID, 0440);

    change_fsid (0, 0, TEST_UID, TEST_GID);
    assert (!read_file (path));

    change_fsid (TEST_UID, TEST_GID, TEST_UID, TEST_SGID);
    assert (read_file (path));

    change_fsid (TEST_UID, TEST_SGID, TEST_UID, TEST_GID);
    assert (!read_file (path));

    /* set TEST_SGID in supplemental groups */
    _setgroups (1, gids);
    msg ("%d added to supplemental groups", gids[0]);
    assert (read_file (path));

    /* clear supplemental groups */
    _setgroups (0, NULL);
    msg ("supplemental groups cleared");
    assert (!read_file (path));

    /* clean up */
    change_fsid (TEST_UID, TEST_GID, 0, 0);
    _unlink (path);

    msg ("test complete");
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
