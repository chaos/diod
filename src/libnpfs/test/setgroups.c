/*************************************************************\
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

/* Check that setgroups(2) works as expected with setfsuid/fsgid.
 *
 * setgroups(2) affects the entire process as dictated by POSIX.
 * Check that threads can use the underlying system call, SYS_setgroups,
 * to independently change their supplementary groups.
 */

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
#include <stdbool.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/fsuid.h>
#include <grp.h>

#include "src/libtest/thread.h"
#include "src/libtest/state.h"
#include "src/libtap/tap.h"

static void check_fsid (uid_t uid, gid_t gid)
{
    int fd;
    char path[] = "/tmp/testfsuidsupp.XXXXXX";
    struct stat sb;

    fd = mkstemp (path);
    if (fd < 0)
        BAIL_OUT ("mkstemp: %s", strerror (errno));
    if (fstat (fd, &sb) < 0)
        BAIL_OUT ("fstat: %s", strerror (errno));
    if (unlink (path) < 0)
        BAIL_OUT ("unlink: %s", strerror (errno));

    ok (uid == sb.st_uid && gid == sb.st_gid,
        "now files are created with %d:%d", uid, gid);
}

/* Create a test file and return its path.
 */
static char *create_file (uid_t uid, gid_t gid, mode_t mode)
{
    int fd;
    static char path[] = "/tmp/testfsuidsupp.XXXXXX";

    fd = mkstemp (path);
    if (fd < 0)
        BAIL_OUT ("mkstemp: %s", strerror (errno));
    if (fchown (fd, 0, 101) < 0)
        BAIL_OUT ("fchown: %s", strerror (errno));
    if (fchmod (fd, 0040) < 0)
        BAIL_OUT ("fchmod: %s", strerror (errno));

    diag ("created %s %d:%d mode 0%o", path, uid, gid, mode);

    return path;
}

static void check_open_rdonly (char *path, bool expect_success)
{
    int fd;

    fd = open (path, O_RDONLY);
    if (expect_success)
        ok (fd >= 0, "successfully opened %s", path);
    else
        ok (fd < 0, "failed to open %s (as expected)", path);
    close (fd);
}

static void change_fsid (uid_t olduid, gid_t oldgid, uid_t uid, gid_t gid)
{
    int u;
    int g;

    if (uid != olduid) {
        u = setfsuid (uid);
        ok (u == olduid, "setfsuid %d->%d works", olduid, uid);
        if (u == -1)
            diag ("setfsuid: %s", strerror (errno));
        else if (u != olduid)
            diag ("setfsuid returned %d (wanted %d)", u, olduid);
    }
    if (gid != oldgid) {
        g = setfsgid (gid);
        ok (g == oldgid, "setfsgid %d->%d works", oldgid, gid);
        if (g == -1)
            diag ("setfsgid: %s", strerror (errno));
        else if (g != oldgid)
            diag ("setfsgid returned %d (wanted %d)", g, oldgid);
    }
    check_fsid (uid, gid);
}

void test_single_thread (void)
{
    char *path;
    gid_t gids[] = { 101 };

    /* clear supplemental groups */
    if (setgroups (0, NULL) < 0)
        BAIL_OUT ("setgroups: %s", strerror (errno));
    diag ("supplemental groups cleared");

    path = create_file (0, 101, 0440);

    change_fsid (0, 0, 100, 100);
    check_open_rdonly (path, false);

    change_fsid (100, 100, 100, 101);
    check_open_rdonly (path, true);

    change_fsid (100, 101, 100, 100);
    check_open_rdonly (path, false);

    /* set 101 in supplemental groups */
    if (setgroups (1, gids) < 0)
        BAIL_OUT ("setgroups: %s", strerror (errno));
    diag ("%d added to supplemental groups", gids[0]);
    check_open_rdonly (path, true);

    /* clear supplemental groups */
    if (setgroups (0, NULL) < 0)
        BAIL_OUT ("setgroups: %s", strerror (errno));
    diag ("supplemental groups cleared");
    check_open_rdonly (path, false);

    /* clean up */
    change_fsid (100, 100, 0, 0);
    if (unlink (path) < 0)
        BAIL_OUT ("unlink %s: %s", path, strerror (errno));
}

typedef enum { S0, S1, S2, S3, S4, S5 } state_t;

static const char *strgroups (char *buf, size_t size, gid_t *g, size_t len)
{
    buf[0] = '\0';
    for (int i = 0; i < len; i++) {
        const char *pad = i == 0 ? "" : " ";
        snprintf (buf + strlen (buf), size - strlen (buf), "%s%d", pad, g[i]);
    }
    return buf;
}

static void check_groups (char *s, char *expect)
{
    gid_t g[32];
    int n;
    char buf[256];

    n = getgroups (1, g);
    if (n < 0)
        BAIL_OUT ("%s: getgroups: %s", s, strerror (errno));
    strgroups (buf, sizeof (buf), g, n);
    is (buf, expect, "%s: getgroups returned [%s]", s, expect);
}

static void *proc1 (void *a)
{
    gid_t g[] = { 101 };

    check_groups ("task1", "");
    test_state_change (S1);

    test_state_wait (S2);
    check_groups ("task1", "");

    diag ("task1: SYS_setgroups [101]");
    if (syscall (SYS_setgroups, 1, g) < 0)
        BAIL_OUT ("task1: SYS_setgroups: %s", strerror (errno));
    check_groups ("task1", "101");
    test_state_change (S3);

    test_state_wait (S4);
    return NULL;
}

static void *proc2 (void *a)
{
    gid_t g[] = { 100 };

    test_state_wait (S1);
    check_groups ("task2", "");

    diag ("task2: SYS_setgroups [100]");
    if (syscall (SYS_setgroups, 1, g) < 0)
        BAIL_OUT ("task2: SYS_setgroups: %s", strerror (errno));
    check_groups ("task2", "100");
    test_state_change (S2);

    test_state_wait (S3);
    check_groups ("task2", "100");
    test_state_change (S4);
    return NULL;
}

void test_multi_thread (void)
{
    pthread_t t1, t2;

    test_state_init (S0);

    diag ("task0: SYS_setgroups []");
    if (syscall (SYS_setgroups, 0, NULL) < 0)
        BAIL_OUT ("task0: SYS_setgroups: %s", strerror (errno));
    check_groups ("task0", "");

    test_thread_create (&t1, proc1, NULL);
    test_thread_create (&t2, proc2, NULL);

    test_thread_join (t2, NULL);
    test_thread_join (t1, NULL);

    check_groups ("task0", "");
}

int main(int argc, char *argv[])
{
    if (geteuid () != 0 || getenv ("FAKEROOTKEY") != NULL)
        plan (SKIP_ALL, "this test must run as root");
    plan (NO_PLAN);

    test_single_thread ();
    test_multi_thread ();

    done_testing ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
