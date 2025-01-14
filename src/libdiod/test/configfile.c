/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test config file */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "src/liblsd/list.h"
#include "src/libtap/tap.h"

#include "diod_log.h"
#include "diod_conf.h"

static void create_testfile (char *template, const char *content)
{
    int fd;
    if ((fd = mkstemp (template)) < 0)
        BAIL_OUT ("could not create temporary file: %s", strerror (errno));
    int count = write (fd, content, strlen (content));
    if (count < 0)
        BAIL_OUT ("could not write to temporary file: %s", strerror (errno));
    if (count < strlen (content))
        BAIL_OUT ("short write");
    close (fd);
}

static void test_config1 (void)
{
    char *s;
    ListIterator itr;
    char path[] = "/tmp/config.XXXXXX";
    const char *content = "\
exports = {\n\
	\"/g/g1\",\n\
	\"/g/g2\",\n\
	{ path=\"/g/g3\", opts=\"ro\", users=\"jim,bob\", hosts=\"foo[1-64]\" },\n\
	\"/g/g5\",\n\
	{ path=\"/g/g4\", users=\"jim,bob\" },\n\
	\"/g/g6\",\n\
}\n";

    diag ("checking config 1");
    create_testfile (path, content);
    diod_conf_init ();
    diod_conf_init_config_file (path);

    s = diod_conf_get_logdest ();
    is (s, DFLT_LOGDEST, "logdest is default");
    s = diod_conf_get_configpath ();
    is (s, path, "configpath is %s", path);
    ok (diod_conf_get_debuglevel () == DFLT_DEBUGLEVEL, "debuglevel is default");
    ok (diod_conf_get_nwthreads () == DFLT_NWTHREADS, "nwthreads is default");
    ok (diod_conf_get_foreground () == DFLT_FOREGROUND, "foreground is default");
    ok (diod_conf_get_auth_required () == DFLT_AUTH_REQUIRED,
        "auth_required is default");
    ok (diod_conf_get_hostname_lookup () == DFLT_HOSTNAME_LOOKUP,
        "hostname_lookup is default");
    ok (diod_conf_get_statfs_passthru () == DFLT_STATFS_PASSTHRU,
        "statfs_passthru is default");
    ok (diod_conf_get_userdb () == DFLT_USERDB, "userdb is default");
    ok (diod_conf_get_allsquash () == DFLT_ALLSQUASH, "allsquash is default");
    s = diod_conf_get_squashuser ();
    is (s, DFLT_SQUASHUSER, "squashuser is default");
    ok (diod_conf_get_runasuid () == DFLT_RUNASUID, "runasuid is default");

    if (!(itr = list_iterator_create (diod_conf_get_listen ())))
        BAIL_OUT ("could not create list iterator for listen");
    ok ((s = list_next (itr)) != NULL
        && !strcmp (s, DFLT_LISTEN)
        && list_next (itr) == NULL,
        "listen is default");
    ok (diod_conf_opt_listen () == 0, "listen is read-write");
    list_iterator_destroy (itr);

    Export *item;
    if (!(itr = list_iterator_create (diod_conf_get_exports ())))
        BAIL_OUT ("could not create list iterator for exports");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g1")
        && !item->opts
        && !item->users
        && !item->hosts,
        "entry 1 was correctly parsed");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g2")
        && !item->opts
        && !item->users
        && !item->hosts,
        "entry 2 was correctly parsed");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g3")
        && item->opts && !strcmp (item->opts, "ro")
        && item->users && !strcmp (item->users, "jim,bob")
        && item->hosts && !strcmp (item->hosts, "foo[1-64]"),
        "entry 3 was correctly parsed");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g5")
        && !item->opts
        && !item->users
        && !item->hosts,
        "entry 4 was correctly parsed");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g4")
        && !item->opts
        && item->users && !strcmp (item->users, "jim,bob")
        && !item->hosts,
        "entry 5 was correctly parsed");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g6")
        && !item->opts
        && !item->users
        && !item->hosts,
        "entry 6 was correctly parsed");
    ok (list_next (itr) == NULL, "that was the last item");
    list_iterator_destroy (itr);

    diod_conf_fini ();
    unlink (path);
}

void test_config2 (void)
{
    char *s;
    ListIterator itr;
    char path[] = "/tmp/config.XXXXXX";
    const char *content = "\
nwthreads = 64\n\
auth_required = 1\n\
allsquash = 1\n\
listen = { \"1.2.3.4:42\", \"1,2,3,5:43\" }\n\
logdest = \"syslog:daemon:err\"\n\
exportall = 1\n\
\n\
exports = { \"/g/g1\" }\n";

    diag ("checking config 2");

    create_testfile (path, content);
    diod_conf_init ();
    diod_conf_init_config_file (path);

    s = diod_conf_get_logdest ();
    is (s, "syslog:daemon:err", "logdest is syslog:daemon:err");
    s = diod_conf_get_configpath ();
    is (s, path, "configpath is %s", path);
    ok (diod_conf_get_debuglevel () == DFLT_DEBUGLEVEL, "debuglevel is default");
    ok (diod_conf_get_nwthreads () == 64, "nwthreads is 64");
    ok (diod_conf_get_foreground () == DFLT_FOREGROUND, "foreground is default");
    ok (diod_conf_get_auth_required () != 0, "auth_required is true");
    ok (diod_conf_get_hostname_lookup () == DFLT_HOSTNAME_LOOKUP,
        "hostname_lookup is default");
    ok (diod_conf_get_statfs_passthru () == DFLT_STATFS_PASSTHRU,
        "statfs_passthru is default");
    ok (diod_conf_get_userdb () == DFLT_USERDB, "userdb is default");
    ok (diod_conf_get_allsquash () != 0, "allsquash is true");
    s = diod_conf_get_squashuser ();
    is (s, DFLT_SQUASHUSER, "squashuser is default");
    ok (diod_conf_get_runasuid () == DFLT_RUNASUID, "runasuid is default");

    if (!(itr = list_iterator_create (diod_conf_get_listen ())))
        BAIL_OUT ("could not create list iterator for listen");
    ok ((s = list_next (itr)) != NULL
        && !strcmp (s, "1.2.3.4:42"),
        "listen entry 1 is 1.2.3.4:42");
    // huh, apparently we allow that thru the parser?  fix?
    ok ((s = list_next (itr)) != NULL
        && !strcmp (s, "1,2,3,5:43")
        && list_next (itr) == NULL,
        "listen entry 2 is 1,2,3,5:43");
    ok (diod_conf_opt_listen () == 0, "listen is read-write");
    list_iterator_destroy (itr);

    Export *item;
    if (!(itr = list_iterator_create (diod_conf_get_exports ())))
        BAIL_OUT ("could not create list iterator for exports");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g1")
        && !item->opts
        && !item->users
        && !item->hosts
        && list_next (itr) == NULL,
        "export entry 1 is /g/g1");

    diod_conf_fini ();
    unlink (path);
}

int main (int argc, char *argv[])
{
#ifdef HAVE_CONFIG_FILE
    plan (NO_PLAN);
#else
    plan (SKIP_ALL, "diod was built without lua config file support");
#endif
    diod_log_init ("test_config.t");

    test_config1 ();
    test_config2 ();

    diod_log_fini ();

    done_testing ();
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
