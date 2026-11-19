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
#include <errno.h>
#include <string.h>

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
logdest = \"/tmp/diod.log\"\n\
exportall = 1\n\
\n\
exports = { \"/g/g1\" }\n";

    diag ("checking config 2");

    create_testfile (path, content);
    diod_conf_init ();
    diod_conf_init_config_file (path);

    s = diod_conf_get_logdest ();
    is (s, "/tmp/diod.log", "logdest is /tmp/diod.log");
    s = diod_conf_get_configpath ();
    is (s, path, "configpath is %s", path);
    ok (diod_conf_get_debuglevel () == DFLT_DEBUGLEVEL, "debuglevel is default");
    ok (diod_conf_get_nwthreads () == 64, "nwthreads is 64");
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

/* The lua standard libraries that diod opens should be usable from the
 * config file.  Before lua 5.2 this worked by side effect of luaopen_*();
 * after, luaL_requiref() is required to set the globals.
 */
void test_config3 (void)
{
    char *s;
    char path[] = "/tmp/config.XXXXXX";
    const char *content = "\
squashuser = string.format (\"%s%d\", \"nobody\", 1)\n\
nwthreads = math.floor (64.5)\n\
listen = { table.concat ({ \"1.2.3.4\", \"42\" }, \":\") }\n\
exports = { \"/g/g1\" }\n";

    diag ("checking config 3 (standard libraries)");

    create_testfile (path, content);
    diod_conf_init ();
    diod_conf_init_config_file (path);

    s = diod_conf_get_squashuser ();
    is (s, "nobody1", "string library is available");
    ok (diod_conf_get_nwthreads () == 64, "math library is available");

    ListIterator itr;
    if (!(itr = list_iterator_create (diod_conf_get_listen ())))
        BAIL_OUT ("could not create list iterator for listen");
    ok ((s = list_next (itr)) != NULL
        && !strcmp (s, "1.2.3.4:42")
        && list_next (itr) == NULL,
        "table library is available");
    list_iterator_destroy (itr);

    diod_conf_fini ();
    unlink (path);
}

/* exportall, exportopts, and the export option flags.
 * A per-export opts attribute overrides the global exportopts; exports
 * without one inherit it.
 */
void test_config4 (void)
{
    char *s;
    ListIterator itr;
    char path[] = "/tmp/config.XXXXXX";
    const char *content = "\
exportall = 1\n\
exportopts = \"sharefd\"\n\
exports = {\n\
	\"/g/g1\",\n\
	{ path=\"/g/g2\", opts=\"ro\" },\n\
	{ path=\"/g/g3\", opts=\"ro,suppress,sharefd,privport,noauth\" },\n\
}\n";

    diag ("checking config 4 (export options)");

    create_testfile (path, content);
    diod_conf_init ();
    diod_conf_init_config_file (path);

    ok (diod_conf_get_exportall () != 0, "exportall is true");
    s = diod_conf_get_exportopts ();
    is (s, "sharefd", "exportopts is sharefd");

    Export *item;
    if (!(itr = list_iterator_create (diod_conf_get_exports ())))
        BAIL_OUT ("could not create list iterator for exports");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g1")
        && item->opts && !strcmp (item->opts, "sharefd")
        && item->oflags == XFLAGS_SHAREFD,
        "entry with no opts inherits exportopts");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g2")
        && item->opts && !strcmp (item->opts, "ro")
        && item->oflags == XFLAGS_RO,
        "entry with opts overrides exportopts");
    ok ((item = list_next (itr)) != NULL
        && !strcmp (item->path, "/g/g3")
        && item->oflags == (XFLAGS_RO | XFLAGS_SUPPRESS | XFLAGS_SHAREFD
                            | XFLAGS_PRIVPORT | XFLAGS_NOAUTH),
        "all export options are parsed into oflags");
    ok (list_next (itr) == NULL, "that was the last item");
    list_iterator_destroy (itr);

    diod_conf_fini ();
    unlink (path);
}

/* The remaining boolean config keys, set to their non-default values.
 * config1 and config2 only check that these are left at their defaults.
 */
void test_config5 (void)
{
    char path[] = "/tmp/config.XXXXXX";
    const char *content = "\
hostname_lookup = 0\n\
statfs_passthru = 1\n\
userdb = 0\n\
exports = { \"/g/g1\" }\n";

    diag ("checking config 5 (remaining options)");

    create_testfile (path, content);
    diod_conf_init ();
    diod_conf_init_config_file (path);

    ok (diod_conf_get_hostname_lookup () == 0, "hostname_lookup is false");
    ok (diod_conf_get_statfs_passthru () != 0, "statfs_passthru is true");
    ok (diod_conf_get_userdb () == 0, "userdb is false");

    diod_conf_fini ();
    unlink (path);
}

/* Values set on the command line take precedence over the config file,
 * so that rereading the config file does not clobber them.
 */
void test_config6 (void)
{
    char *s;
    ListIterator itr;
    char path[] = "/tmp/config.XXXXXX";
    const char *content = "\
nwthreads = 64\n\
auth_required = 0\n\
hostname_lookup = 0\n\
statfs_passthru = 1\n\
userdb = 0\n\
allsquash = 1\n\
squashuser = \"daemon\"\n\
logdest = \"/tmp/diod.log\"\n\
listen = { \"1.2.3.4:42\" }\n\
exportall = 1\n\
exports = { \"/g/g1\" }\n";

    diag ("checking config 6 (cmdline overrides config file)");

    create_testfile (path, content);
    diod_conf_init ();

    /* Simulate command line options being parsed before the config file
     * is read.
     */
    diod_conf_set_nwthreads (32);
    diod_conf_set_auth_required (1);
    diod_conf_set_hostname_lookup (1);
    diod_conf_set_statfs_passthru (0);
    diod_conf_set_userdb (1);
    diod_conf_set_allsquash (0);
    diod_conf_set_squashuser ("bin");
    diod_conf_set_logdest ("stdout");
    diod_conf_set_exportall (0);
    diod_conf_clr_listen ();
    diod_conf_add_listen ("5.6.7.8:99");

    diod_conf_init_config_file (path);

    ok (diod_conf_get_nwthreads () == 32, "nwthreads was not overridden");
    ok (diod_conf_get_auth_required () != 0,
        "auth_required was not overridden");
    ok (diod_conf_get_hostname_lookup () != 0,
        "hostname_lookup was not overridden");
    ok (diod_conf_get_statfs_passthru () == 0,
        "statfs_passthru was not overridden");
    ok (diod_conf_get_userdb () != 0, "userdb was not overridden");
    ok (diod_conf_get_allsquash () == 0, "allsquash was not overridden");
    s = diod_conf_get_squashuser ();
    is (s, "bin", "squashuser was not overridden");
    s = diod_conf_get_logdest ();
    is (s, "stdout", "logdest was not overridden");
    ok (diod_conf_get_exportall () == 0, "exportall was not overridden");

    if (!(itr = list_iterator_create (diod_conf_get_listen ())))
        BAIL_OUT ("could not create list iterator for listen");
    ok ((s = list_next (itr)) != NULL
        && !strcmp (s, "5.6.7.8:99")
        && list_next (itr) == NULL,
        "listen was not overridden");
    list_iterator_destroy (itr);

    diod_conf_fini ();
    unlink (path);
}

/* Malformed config files call msg_exit().  Run each parse in a child
 * process so the test can continue.
 */
static void check_config_fails (const char *content, const char *msg)
{
    char path[] = "/tmp/config.XXXXXX";

    create_testfile (path, content);
    dies_ok ({
        diod_conf_init ();
        diod_conf_init_config_file (path);
        diod_conf_fini ();
    }, "%s", msg);
    unlink (path);
}

void test_config7 (void)
{
    diag ("checking config 7 (malformed config files)");

    check_config_fails ("exports = { \"/g/g1\" }\n syntax error here\n",
                        "lua syntax error is fatal");
    check_config_fails ("nwthreads = \"foo\"\n",
                        "non-number nwthreads is fatal");
    check_config_fails ("squashuser = { }\n",
                        "non-string squashuser is fatal");
    check_config_fails ("listen = \"1.2.3.4:42\"\n",
                        "non-table listen is fatal");
    check_config_fails ("exports = \"/g/g1\"\n",
                        "non-table exports is fatal");
    check_config_fails ("exports = { { opts=\"ro\" } }\n",
                        "exports entry without path is fatal");
    check_config_fails ("exports = { { path={ } } }\n",
                        "non-string exports entry path is fatal");
    check_config_fails ("exports = { { path=\"/g/g1\", opts=\"xxx\" } }\n",
                        "unknown export option is fatal");
    check_config_fails ("exportopts = \"xxx\"\n exports = { \"/g/g1\" }\n",
                        "unknown global export option is fatal");
}

/* io and os are deliberately not made available to the config file.
 */
void test_config8 (void)
{
    diag ("checking config 8 (io and os are unavailable)");

    check_config_fails ("exports = { io.open (\"/dev/null\"):read () }\n",
                        "io library is unavailable");
    check_config_fails ("exports = { os.getenv (\"HOME\") }\n",
                        "os library is unavailable");
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
    test_config3 ();
    test_config4 ();
    test_config5 ();
    test_config6 ();
    test_config7 ();
    test_config8 ();

    diod_log_fini ();

    done_testing ();
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
