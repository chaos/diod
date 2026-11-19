/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>

#include "src/libtap/tap.h"
#include "src/libhostlist/hostlist.h"

void test_basic ()
{
    struct hostlist *hl = hostlist_create ();
    if (!hl)
        BAIL_OUT ("hostlist_create failed");
    ok (hostlist_count (hl) == 0,
        "hostlist_create creates empty hostlist");
    hostlist_destroy (hl);

    hl = hostlist_decode (NULL);
    ok (hostlist_count (hl) == 0,
        "hostlist_decode (NULL) returns empty hostlist");
    hostlist_destroy (hl);

    ok (hostlist_decode ("foo[0-1048576]") == NULL && errno == ERANGE,
        "hostlist_decode () fails with ERANGE for too large host range");

    ok (hostlist_copy (NULL) == NULL,
        "hostlist_copy (NULL) returns NULL");
    ok (hostlist_append (NULL, "foo") < 0 && errno == EINVAL,
        "hostlist_append (NULL, 'foo') returns EINVAL");

    hl = hostlist_create ();
    ok (hostlist_append (hl, NULL) == 0,
        "hostlist_append (hl, NULL) returns 0");
    ok (hostlist_append (hl, "") == 0,
        "hostlist_append (hl, '') returns 0");

    ok (hostlist_append_list (NULL, NULL) < 0 && errno == EINVAL,
        "hostlist_append_list (NULL, NULL) returns EINVAL");

    ok (hostlist_nth (NULL, 0) == NULL && errno == EINVAL,
        "hostlist_nth (NULL, 0) returns NULL");
    ok (hostlist_nth (hl, -1) == NULL && errno == EINVAL,
        "hostlist_nth (hl, -1) returns EINVAL");

    ok (hostlist_find (NULL, NULL) < 0 && errno == EINVAL,
        "hostlist_find (NULL, NULL) returns EINVAL");

    ok (hostlist_delete (NULL, NULL) < 0 && errno == EINVAL,
        "hostlist_delete (NULL, NULL) returns EINVAL");

    ok (hostlist_count (NULL) == 0,
        "hostlist_count(NULL) returns 0");

    lives_ok ({hostlist_sort (NULL);},
              "hostlist_sort (NULL) doesn't crash");

    lives_ok ({hostlist_uniq (NULL);},
              "hostlist_uniq (NULL) doesn't crash");

    hostlist_destroy (hl);
}

void test_encode_decode_basic ()
{
    struct hostlist * hl;
    char *s;

    ok (hostlist_encode (NULL) == NULL && errno == EINVAL,
        "hostlist_encode (NULL) returns EINVAL");
    ok (hostlist_decode (NULL) == NULL && errno == EINVAL,
        "hostlist_decode (NULL) returns EINVAL");

    hl = hostlist_decode ("");
    if (!hl)
        BAIL_OUT ("hostlist_encode failed");

    ok (hl && hostlist_count (hl) == 0,
        "hostlist_encode ('') creates hostlist with zero size");

    s = hostlist_encode (hl);
    if (!s)
        BAIL_OUT ("hostlist_decode failed");
    is (s, "",
        "hostlist_decode of empty list returns empty string");
    free (s);
    hostlist_destroy (hl);

}

void test_iteration_basic ()
{
    struct hostlist *hl = hostlist_create ();
    if (hl == NULL)
        BAIL_OUT ("hostlist_create failed!");

    ok (hostlist_first (NULL) == NULL && errno == EINVAL,
        "hostlist_first (NULL) returns EINVAL");
    ok (hostlist_last (NULL) == NULL && errno == EINVAL,
        "hostlist_last (NULL) returns EINVAL");
    ok (hostlist_next (NULL) == NULL && errno == EINVAL,
        "hostlist_next (NULL) returns EINVAL");
    ok (hostlist_current (NULL) == NULL && errno == EINVAL,
        "hostlist_current (NULL) returns EINVAL");
    ok (hostlist_remove_current (NULL) < 0 && errno == EINVAL,
        "hostlist_remove_current (NULL) returns EINVAL");

    ok (hostlist_first (hl) == NULL,
        "hostlist_first on empty hostlist returns NULL");
    ok (hostlist_last (hl) == NULL,
        "hostlist_last on empty hostlist returns NULL");
    ok (hostlist_current (hl) == NULL,
        "hostlist_current on empty hostlist returns NULL");
    ok (hostlist_next (hl) == NULL,
        "hostlist_next on empty hostlist returns NULL");
    ok (hostlist_remove_current (hl) == 0,
        "hostlist_remove_current on empty list returns 0");

    hostlist_destroy (hl);
}

void test_invalid_decode ()
{
    const char *input[] = {
        "[]",
        "foo[]",
        "foo[",
        "foo[1,3",
        "foo[[1,3]",
        "foo]",
        "foo[x-y]",
        "foo[0-1,2--5]",
        NULL,
    };
    for (const char **entry = input; *entry; entry++) {
        struct hostlist *hl = hostlist_decode (*entry);
        ok (hl == NULL,
            "hostlist_decode (%s) returns NULL", *entry);
        if (hl) {
            char *s = hostlist_encode (hl);
            diag ("%s", s);
            free (s);
        }
    }
}


struct codec_test {
    char *input;
    char *output;
    int count;
};

struct codec_test codec_tests[] = {
    { "foo-1a-2,foo-1a-3",                      "foo-1a-[2-3]",        2 },
    { "foo1,foo2,foo3,fooi",                    "foo[1-3],fooi",       4 },
    { "foo1,fooi,foo2,foo3",                    "foo1,fooi,foo[2-3]",  4 },
    { "fooi,foo1,foo2,foo3",                    "fooi,foo[1-3]",       4 },
    { "fooi,foo1,foo2,foo3,foo5,foo7,foo8",     "fooi,foo[1-3,5,7-8]", 7 },
    { "1,2,3,4,5,9",                            "[1-5,9]",             6 },
    { ",1,2,3,4,5,9",                           "[1-5,9]",             6 },
    { ",1,2,3,4,5,9,",                          "[1-5,9]",             6 },
    { "[1-5]",                                  "[1-5]",               5 },
    { "foo[1,3]-bar",                           "foo1-bar,foo3-bar",   2 },
    { "[00-03]p",                               "00p,01p,02p,03p",     4 },
    { "p[00-3]p",                               "p00p,p01p,p02p,p03p", 4 },
    { "14636",                                  "14636",               1 },
    { "mcr[336-359,488-550,553,556,559,561,567,569-571,573-575,578,581,584,587-589,592,594,597,600-602,605,608,610,618-622,627,634,636-670,687-696,699-733,735-742,744-760,762-773]",
       "mcr[336-359,488-550,553,556,559,561,567,569-571,573-575,578,581,584,587-589,592,594,597,600-602,605,608,610,618-622,627,634,636-670,687-696,699-733,735-742,744-760,762-773]", 237 },
    { 0 },
};

void test_encode_decode ()
{
    struct codec_test *t = codec_tests;
    while (t && t->input) {
        char *result;
        struct hostlist *hl = hostlist_decode (t->input);
        struct hostlist *copy = hostlist_copy (hl);
        if (!hl)
            BAIL_OUT ("hostlist_decode failed");
        ok (hostlist_count (hl) == t->count,
            "hostlist_decode returned count=%d", hostlist_count (hl));
        result = hostlist_encode (hl);
        if (!result)
            BAIL_OUT ("hostlist_encode failed");
        is (result, t->output,
            "hostlist_decode: %s -> %s", t->input, result);
        free (result);

        /* Ensure copy works */
        result = hostlist_encode (copy);
        is (result, t->output,
            "hostlist_copy worked");
        free (result);

        hostlist_destroy (hl);
        hostlist_destroy (copy);
        t++;
    }
}

void test_append ()
{
    struct hostlist *hl = hostlist_create ();
    struct hostlist *hl2;
    int n;
    char *s;

    ok (hostlist_append (hl, "") == 0,
        "hostlist_append ("") returns 0");
    ok (hostlist_count (hl) == 0,
        "hostlist_count returns 0");
    ok (hostlist_append (hl, "foo12") == 1,
        "hostlist_append ('foo12') returns 1");
    ok (hostlist_append (hl, "foo[4,1-2]") == 3,
        "hostlist_append ('foo[4,1-2]') == 3");
    ok (hostlist_count (hl) == 4,
        "hostlist_count is now 4");
    if (!(s = hostlist_encode (hl)))
        BAIL_OUT ("hostlist_encode failed!");
    is (s, "foo[12,4,1-2]",
        "hostlist is encoded to %s", s);
    free (s);

    hl2 = hostlist_decode ("bar[26-30]");
    if (!hl2)
        BAIL_OUT ("hostlist_create failed");

    n = hostlist_append_list (hl, hl2);
    ok (n == 5,
        "hostlist_append_list returned %d", n);

    ok (hostlist_count (hl) == 9,
        "hostlist_count is now 9");

    if (!(s = hostlist_encode (hl)))
        BAIL_OUT ("hostlist_encode failed");
    is (s, "foo[12,4,1-2],bar[26-30]",
        "hostlist is now %s", s);
    free (s);

    hostlist_destroy (hl);
    hostlist_destroy (hl2);
}

void test_nth ()
{
    int count;
    const char *host;
    struct hostlist *hl = hostlist_create ();
    if (!hl)
        BAIL_OUT ("hostlist_create failed");

    ok (hostlist_nth (hl, 0) == NULL && errno == ENOENT,
        "hostlist_nth (hl, 0) on empty list returns ENOENT");

    count = hostlist_append (hl, "foo[1-2,4,5],bar");
    ok (count == 5,
        "Added 5 hosts to hostlist");

    ok (hostlist_nth (hl, count) == NULL && errno == ENOENT,
        "hostlist_nth (hl, hostlist_count (hl)) returns ENOENT");

    if (!(host = hostlist_nth (hl, 0)))
        BAIL_OUT ("hostlist_nth (hl, 0) failed!");
    is (host, "foo1",
        "hostlist_nth (hl, 0) returns %s", host);
    is (hostlist_current (hl), "foo1",
        "hostlist_nth (hl, 0) leaves cursor at %s", hostlist_current (hl));

    if (!(host = hostlist_nth (hl, 4)))
        BAIL_OUT ("hostlist_nth (hl, 4) failed");
    is (host, "bar",
        "hostlist_nth (hl, 4) returns %s", host);
    is (hostlist_current (hl), "bar",
        "hostlist_nth (hl, 4) leaves cursor at %s", hostlist_current (hl));

    if (!(host = hostlist_nth (hl, 2)))
        BAIL_OUT ("hostlist_nth (hl, 2) failed");
    is (host, "foo4",
        "hostlist_nth (hl, 2) returns %s", host);
    hostlist_destroy (hl);
}

struct find_test {
    char *input;
    char *arg;
    int rc;
};

static struct find_test find_tests[] = {
    { "tst0",          "tst",        -1  },
    { "tst0,tst",      "tst",         1  },
    { "tst,tst0",      "tst",         0  },
    { "tst",           "tst0",       -1  },
    { "foo[1-5]-eth0", "foo3-eth0",   2  },
    { "foo[1-5]",      "foo3-eth0",  -1  },
    { "[0-5]",         "3",           3  },
    { "[0-5]i",        "0",          -1  },
    { "i[0-5]",        "i00",        -1  },
    { "i[00-05]",      "i00",         0  },
    { "i[00-05]",      "i04",         4  },
    { "f00[7-8]",      "f007",        0  },
    { "f00[7-8,10]",   "f0010",       2  },
    { "f0010001[07-08]","f001000108", 1  },
    { "cornp2",        "corn",       -1  },
    { "cornp2",        "corn2",      -1  },
    { "corn-p2",       "corn2",      -1  },
    { "corn1-p2",      "corn2",      -1  },
    { 0 },
};

void test_find ()
{
    struct find_test *t = find_tests;

    while (t && t->input) {
        int rc;
        struct hostlist *hl = hostlist_decode (t->input);
        if (!hl)
            BAIL_OUT ("hostlist_decode (%s) failed!", t->input);
        rc = hostlist_find (hl, t->arg);
        ok (rc == t->rc,
            "hostlist_find ('%s', '%s') returned %d",
            t->input, t->arg, rc);
        if (t->rc >= 0)
            is (hostlist_current (hl), t->arg,
                "hostlist_find leaves cursor pointing to found host");
        hostlist_destroy (hl);
        t++;
    }
}

void test_find_hostname ()
{
    struct find_test *t = find_tests;

    ok (hostlist_find_hostname (NULL, NULL) == -1 && errno == EINVAL,
        "hostlist_find_hostname (NULL, NULL) returns EINVAL");

    while (t && t->input) {
        int rc;
        struct hostlist_hostname *hn;
        struct hostlist *hl = hostlist_decode (t->input);
        if (!hl)
            BAIL_OUT ("hostlist_decode (%s) failed!", t->input);
        if (!(hn = hostlist_hostname_create (t->arg)))
            BAIL_OUT ("hostlist_hostname_create (%s) failed!", t->arg);
        rc = hostlist_find_hostname (hl, hn);
        ok (rc == t->rc,
            "hostlist_find_hostname ('%s', '%s') returned %d",
            t->input, t->arg, rc);
        if (t->rc >= 0)
            is (hostlist_current (hl), t->arg,
                "hostlist_find leaves cursor pointing to found host");
        hostlist_hostname_destroy (hn);
        hostlist_destroy (hl);
        t++;
    }
}


struct delete_test {
    char *input;
    char *delete;
    int rc;
    char *result;
};

static struct delete_test delete_tests[] = {
    { "foo[2-5]",      "foo6",         0, "foo[2-5]"            },
    { "foo[2-5]",      "foo3",         1, "foo[2,4-5]"          },
    { "foo[2-5],fooi", "fooi",         1, "foo[2-5]"            },
    { "foo[2-5],fooi", "foo3",         1, "foo[2,4-5],fooi"     },
    { "foo[2-5],fooi", "foo[1-2]",     1, "foo[3-5],fooi"       },
    { "foo[0-7]",      "foo[1,0,2-7]", 8, ""                    },
    { "foo[2-4]-eth2", "foo3-eth2",    1, "foo2-eth2,foo4-eth2" },
    { 0 },
};

void test_delete ()
{
    struct delete_test *t = delete_tests;

    while (t && t->input) {
        char *s;
        int rc;
        struct hostlist *hl = hostlist_decode (t->input);
        if (!hl)
            BAIL_OUT ("hostlist_decode (%s) failed!", t->input);
        rc = hostlist_delete (hl, t->delete);
        ok (rc == t->rc,
            "del ('%s', '%s') returned %d",
            t->input, t->delete, rc);
        s = hostlist_encode (hl);
        is (s, t->result,
            "result = '%s'", s);
        free (s);

        hostlist_destroy (hl);
        t++;
    }
}

struct sortuniq_test {
    char *input;
    char *sorted;
    char *uniq;
};

struct sortuniq_test sortuniq_tests[] = {
    { "foo,f,bar,baz",         "bar,baz,f,foo",         "bar,baz,f,foo"     },
    { "[5-6],[3-4],[1-2,0]",   "[0-6]",                 "[0-6]"             },
    { "[0-20],12,15",          "[0-12,12-15,15-20]",    "[0-20]"            },
    { "0,1,2,3,4,5,1,5",       "[0-1,1-5,5]",           "[0-5]"             },
    { "[0-20],45,12,15",       "[0-12,12-15,15-20,45]", "[0-20,45]"         },
    { "[0-20],45,12,015",      "[0-12,12-20,45,015]",   "[0-20,45,015]"     },
    { "bar1,bar2,foo1,foo,foo","bar[1-2],foo,foo,foo1", "bar[1-2],foo,foo1" },
    { "foo[5-6],foo3,foo4",    "foo[3-6]",              "foo[3-6]"          },
    { "foo[5-6],foo[4-7]",     "foo[4-5,5-6,6-7]",      "foo[4-7]"          },
    { "foo[0-3],foo[0-3]",     "foo[0,0-1,1-2,2-3,3]",  "foo[0-3]"          },
    { "foo[0-2],foo[0-2],foo[0-2]",
      "foo[0,0,0-1,1,1-2,2,2]",
      "foo[0-2]"
    },
    { 0 }
};

void test_sortuniq ()
{
    struct sortuniq_test *t = sortuniq_tests;

    while (t && t->input) {
        char *sorted, *uniq;
        struct hostlist *hl = hostlist_decode (t->input);
        struct hostlist *hl2 = hostlist_decode (t->input);
        if (!hl || !hl2)
            BAIL_OUT ("hostlist_decode (%s) failed!", t->input);

        hostlist_sort (hl);
        if (!(sorted = hostlist_encode (hl)))
            BAIL_OUT ("hostlist_encode failed!");
        is (sorted, t->sorted,
            "hostlist_sort(%s) = '%s'", t->input, sorted);

        hostlist_uniq (hl2);
        if (!(uniq = hostlist_encode (hl2)))
            BAIL_OUT ("hostlist_encode failed!");
        is (uniq, t->uniq,
            "hostlist_uniq(%s) = '%s'", t->input, uniq);

        free (sorted);
        free (uniq);
        hostlist_destroy (hl);
        hostlist_destroy (hl2);
        t++;
    }

}

const char *iterator_inputs[] = {
    "",
    "mcr[336-359,488-550,553,556,559,561,567,569-571,573-575,578,581,584,587-589,592,594,597,600-602,605,608,610,618-622,627,634,636-670,687-696,699-733,735-742,744-760,762-773]",
    "mcr[774-796,799-814,986,1096-1114,1147-1151]",
    "really-long-hostname-prefix[10101,55,35,2]",
    "really-really-really-super-duper-long-hostname-prefix[10101,55,35,2]",
    "[336-359,488-550,553,556,559,561,567,569-571,573-575,578,581,584,587-589,592,594,597,600-602,605,608,610,618-622,627,634,636-670,687-696,699-733,735-742,744-760,762-773]",
    "one,two,three,four,five",
    NULL
};

void test_iteration ()
{
    struct hostlist *hl;
    struct hostlist *nl;
    const char *host;
    char *result;

    for (const char **input = iterator_inputs; *input != NULL; input++) {
        char *first = NULL;
        char *last = NULL;

        if (!(hl = hostlist_decode (*input)))
            BAIL_OUT ("hostlist_decode failed!");
        if (!(nl = hostlist_create ()))
            BAIL_OUT ("hostlist_create failed!");

        if ((host = hostlist_last (hl)))
            last = strdup (host);
        if ((host = hostlist_first (hl)))
            first = strdup (host);

        while (host) {
            hostlist_append (nl, hostlist_current (hl));
            host = hostlist_next (hl);
        }

        if (!(result = hostlist_encode (nl)))
            BAIL_OUT ("hostlist_encode failed!");
        is (result, (char *) *input,
            "hostlist_next iterated %d hosts in order",
            hostlist_count (hl));

        if (first) {
            is (hostlist_first (hl), first,
                "hostlist_first resets to first host");
            is (hostlist_current (hl), first,
                "hostlist_current() works");

            if (hostlist_next (hl) && hostlist_next (hl)) {
                ok (hostlist_remove_current (hl) == 1,
                    "hostlist_remove_current works");
            }

            is (hostlist_last (hl), last,
                "hostlist_last resets to last host");
            is (hostlist_current (hl), last,
                "hostlist_current() works");
            ok (hostlist_remove_current (hl) == 1,
                "hostlist_remove_current() works at last host");
        }

        free (result);
        free (first);
        free (last);
        hostlist_destroy (hl);
        hostlist_destroy (nl);
    }
}


struct test_next_delete {
    const char *descr;
    const char *input;
    int n;
    const char *delete;
    const char *next;
};

struct test_next_delete next_delete_tests[] = {
    { "delete host at cursor in hr",
      "foo[0-7]", 1, "foo1", "foo2"
    },
    { "delete host before cursor in hr",
      "foo[0-7]", 4, "foo1", "foo5"
    },
    { "delete host which removes hr at cursor",
      "foo[0,2,4-5]", 1, "foo2", "foo4",
    },
    { "delete host which removes hr before cursor",
      "foo[0,2,4-5]", 2, "foo2", "foo5",
    },
    { "delete current at beginning of list",
      "foo[0-15]", 0, NULL, "foo1",
    },
    { "delete current in middle of list",
      "foo[0-15]", 7, NULL, "foo8",
    },
    { "delete current in middle of list with multiple hostranges",
      "foo[0-1,3,15]", 2, NULL, "foo15",
    },
    { "single hostrange, delete host near beginning",
      "foo[0-100]", 50, "foo1", "foo51"
    },
    { "single hostrange, delete host at beginning",
      "foo[0-100]", 50, "foo0", "foo51"
    },
    { 0 },
};

void test_iteration_with_delete ()
{
    struct test_next_delete *t = next_delete_tests;

    /* Other tests */
    while (t && t->descr) {
        struct hostlist *hl = hostlist_decode (t->input);
        const char *host;
        if (!hl)
            BAIL_OUT ("hostlist_decode failed!");
        if (!hostlist_first (hl))
            BAIL_OUT ("hostlist_first failed!");
        for (int i = 0; i < t->n; i++)
            if (!hostlist_next (hl))
                BAIL_OUT ("hostlist_next i=%d failed!", i);

        if (t->delete != NULL)
            ok (hostlist_delete (hl, t->delete) == 1,
                "hostlist_delete %s from %s works",
                t->delete, t->input);
        else
            ok (hostlist_remove_current (hl) == 1,
                "hostlist_remove_current works");
        host = hostlist_next (hl);
        is (host, t->next,
            "hostlist_next returns %s", host);

        hostlist_destroy (hl);
        t++;
    }
}

/*  Ensure a very large host list can be encoded without error
 */
static void test_encode_large ()
{
    struct hostlist *hl = hostlist_create ();
    if (hl == NULL)
        BAIL_OUT ("hostlist_create");
    for (int i = 0; i < 8192; i++)
        hostlist_append (hl, "host");
    ok (hostlist_count (hl) == 8192,
        "created hostlist with 8K hosts");
    char *s = hostlist_encode (hl);
    ok (s != NULL,
        "hostlist_encode works");
    ok (strlen (s) > 0,
        "string length of result is %ld",
        strlen (s));
    free (s);
    hostlist_destroy (hl);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_encode_decode_basic ();
    test_iteration_basic ();
    test_encode_decode ();
    test_invalid_decode ();
    test_append ();
    test_nth ();
    test_find ();
    test_find_hostname ();
    test_delete ();
    test_sortuniq ();
    test_iteration ();
    test_iteration_with_delete ();
    test_encode_large ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
