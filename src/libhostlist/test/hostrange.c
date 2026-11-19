/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include "src/libhostlist/hostname.h"
#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <errno.h>
#include <stdbool.h>

#include "src/libtap/tap.h"
#include "src/libhostlist/hostrange.h"

void test_create_single ()
{
    struct hostrange *hr;

    ok (hostrange_create_single (NULL) == NULL && errno == EINVAL,
        "hostrange_create_single (NULL) returns EINVAL");
    hr = hostrange_create_single ("");
    ok (hr != NULL,
        "hostrange_create_single() empty string");
    is (hr->prefix, "",
        "hostrange_create_single() got expected prefix");
    ok (hr->singlehost,
        "hr->singlehost is true");
    ok (hr->lo == hr->hi && hr->lo == 0 && hr->width == 0,
        "hr->lo,hi,width have expected values");
    hostrange_destroy (hr);

    hr = hostrange_create_single ("hostname");
    ok (hr != NULL,
        "hostrange_create_single() works");
    is (hr->prefix, "hostname",
        "hostrange_create_single() got expected prefix");
    ok (hr->singlehost,
        "hr->singlehost is true");
    ok (hr->lo == hr->hi && hr->lo == 0 && hr->width == 0,
        "hr->lo,hi,width have expected values");
    hostrange_destroy (hr);
}

void test_create ()
{
    struct hostrange *hr;

    ok (hostrange_create (NULL, 0, 0, 0) == NULL && errno == EINVAL,
        "hostrange_create (NULL, 0, 0, 0) returns EINVAL");
    ok (hostrange_create ("foo", 1, 0, 0) == NULL && errno == EINVAL,
        "hostrange_create ('foo', 1, 0, 0) returns EINVAL");
    ok (hostrange_create ("foo", 0, 1, -1) == NULL && errno == EINVAL,
        "hostrange_create ('foo', 0, 1, -1) returns EINVAL");

    hr = hostrange_create ("foo", 0, 0, 0);
    ok (hr != NULL,
        "hostrange_create ('foo', 0, 0, 0) works");
    is (hr->prefix, "foo",
        "hostrange prefix is expected");
    ok (hr->lo == 0 && hr->hi == 0 && hr->width == 0,
        "hostrange components are expected values");
    hostrange_destroy (hr);

    hr = hostrange_create ("foo", 10, 20, 3);
    ok (hr != NULL,
        "hostrange_create ('foo', 10, 20, 3) works");
    is (hr->prefix, "foo",
        "hostrange prefix is expected");
    ok (hr->lo == 10 && hr->hi == 20 && hr->width == 3,
        "hostrange components are expected values");
    hostrange_destroy (hr);

}

void test_copy ()
{
    struct hostrange *hr = hostrange_create ("foo", 0, 10, 3);
    if (!hr)
        BAIL_OUT ("hostrange_create failed");

    struct hostrange *hr2 = hostrange_copy (hr);
    if (!hr2)
        BAIL_OUT ("hostrange_copy failed");

    is (hr2->prefix, hr->prefix,
        "hostrange_copy copies prefix");
    ok (hr->hi == hr2->hi && hr->lo == hr2->lo && hr->width == hr2->width,
        "hostrange_copy worked");

    hostrange_destroy (hr);
    hostrange_destroy (hr2);
}

void test_count ()
{
    struct hostrange *hr = hostrange_create ("foo", 0, 10, 3);
    if (!hr)
        BAIL_OUT ("hostrange_create failed");
    ok (hostrange_count (hr) == 11,
        "hostrange_count works with hostrange");
    hostrange_destroy (hr);

    hr = hostrange_create ("foo", 12, 12, 0);
    if (!hr)
        BAIL_OUT ("hostrange_create failed");
    ok (hostrange_count (hr) == 1,
        "hostrange_count works with foo12");
    hostrange_destroy (hr);

    hr = hostrange_create_single ("bar");
    if (!hr)
        BAIL_OUT ("hostrange_create_single failed");
    ok (hostrange_count (hr) == 1,
        "hostrange_count == 1 for singlehost");
    hostrange_destroy (hr);

    errno = 0;
    ok (hostrange_count (NULL) == 0 && errno == EINVAL,
        "hostrange_count (NULL) returns 0 with errno set");
}

void test_delete ()
{
    struct hostrange *hr;
    struct hostrange *result;

    hr = hostrange_create ("foo", 0, 10, 0);
    if (!hr)
        BAIL_OUT ("hostrange_create failed");

    result = hostrange_delete_host (hr, 0);
    ok (result == NULL && hr->lo == 1,
        "hostrange_delete first host works");
    result = hostrange_delete_host (hr, 10);
    ok (result == NULL && hr->hi == 9,
        "hostrange_delete first host works");
    result = hostrange_delete_host (hr, 5);
    ok (result != NULL,
        "hostrange_delete host in middle of range returns result");
    ok (result->lo == 6 && result->hi == 9,
        "hostrange_delete returns corrects range in result");
    ok (hr->lo == 1 && hr->hi == 4,
        "hostrange_delete adjusts range of original hostrange");

    hostrange_destroy (hr);
    hostrange_destroy (result);
}

struct cmp_test {
    struct hostrange h1;
    struct hostrange h2;
    int result;
};

struct cmp_test cmp_tests[] = {
    { { "foo", 3, 0, 15, 0, 0 },
      { "foo", 3, 1, 15, 0, 0 },
      -1, },
    { { "foo", 3, 0, 15, 0, 0 },
      { "foo", 3, 0, 15, 0, 0 },
      0, },
    { { "foo", 3, 0, 15, 0, 0 },
      { "foo", 3, 0,  0, 0, 1 },
      1, },
    { { "bar", 3, 0,  0, 0, 1 },
      { "foo", 3, 0,  0, 0, 1 },
      -1, },
    { { "", 0,   0,  5, 0, 0 },
      { "",  0,  5,  5, 0, 0 },
      -1,
    },
    { { "", 0,   0,  5, 0, 0 },
      { "", 0,   0,  5, 2, 0 },
      -1,
    },
    { { "", 0,   12,  12, 0, 0 },
      { "", 0,   15,  15, 0, 0 },
      -1,
    },
    { { "", 0,   15,  15, 0, 0 },
      { "", 0,   12,  12, 0, 0 },
      1,
    },
    { { 0 }, { 0 }, 0 },
};

char *hrstr (struct hostrange *hr)
{
    int n = 0;
    char buf [128];

    n = snprintf (buf, sizeof (buf), "%s", hr->prefix);
    if (!hr->singlehost) {
        buf[n++] = '[';
        n += hostrange_numstr (hr, sizeof (buf) - n, buf + n);
        buf[n++] = ']';
        buf[n] = '\0';
    }
    return strdup (buf);
}

void test_cmp ()
{
    struct cmp_test *t = cmp_tests;
    while (t && t->h1.prefix) {
        char *s1 = hrstr (&t->h1);
        char *s2 = hrstr (&t->h2);
        int result = hostrange_cmp (&t->h1, &t->h2);
        ok (t->result == result,
            "hostrange_cmp (%s, %s) = %d, expected %d",
             s1, s2, result, t->result);
        t++;
        free (s1);
        free (s2);
    }
}

void test_join ()
{
    struct hostrange * hr1 = hostrange_create ("foo", 0, 10, 0);
    struct hostrange * hr2 = hostrange_create ("foo", 5, 15, 0);
    struct hostrange * hr3 = hostrange_create ("foo", 5, 15, 3);
    struct hostrange * hr4 = hostrange_create_single ("foo");
    struct hostrange * hr5 = hostrange_create_single ("bar");
    struct hostrange * hr6 = hostrange_create ("foo", 16, 20, 0);

    if (!hr1 || !hr2 || !hr3 || !hr4 || !hr5 || !hr6)
        BAIL_OUT ("hostrange_create failed");

    int rc = hostrange_join (hr2, hr3);
    ok (rc < 0,
        "hostrange_join fails when widths do not match (got rc==%d)", rc);
    ok (hostrange_join (hr5, hr4) < 0,
        "hostrange_join fails when prefixes do not match");
    ok (hostrange_join (hr1, hr6) < 0,
        "hostrange_join fails when ranges do not match");

    ok (hostrange_join (hr4, hr4) == 1,
        "hostrange_join identical hosts returns 1");

    ok (hostrange_join (hr1, hr2) == 6,
        "hostrange_join (foo[0-10], foo[5-15]) == 6");
    ok (hr1->lo == 0 && hr1->hi == 15,
        "hostrange joined hosts in first argument");
    diag ("hr1=%s[%lu-%lu], hr2=%s[%lu-%lu]",
          hr1->prefix, hr1->lo, hr1->hi,
          hr2->prefix, hr2->lo, hr2->hi);

    ok (hostrange_join (hr2, hr6) == 0,
        "hostrange_join returns zero for perfect overlap");
    ok (hr2->lo == 5 && hr2->hi == 20,
        "hostrange joined hosts in first argument");
    diag ("hr2=%s[%lu-%lu], hr6=%s[%lu-%lu]",
          hr2->prefix, hr2->lo, hr2->hi,
          hr6->prefix, hr6->lo, hr6->hi);

    hostrange_destroy (hr1);
    hostrange_destroy (hr2);
    hostrange_destroy (hr3);
    hostrange_destroy (hr4);
    hostrange_destroy (hr5);
    hostrange_destroy (hr6);
}

void test_intersect ()
{
    struct hostrange *hr1 = hostrange_create ("foo", 5, 10, 0);
    struct hostrange *hr2 = hostrange_create ("foo", 9, 15, 0);
    struct hostrange *hr3 = hostrange_create ("foo", 11, 15, 0);
    struct hostrange *hr4 = hostrange_create ("foo", 8, 9, 0);
    struct hostrange *hr5 = hostrange_create_single ("foo");
    struct hostrange *result;

    result = hostrange_intersect (hr1, hr2);
    ok (result != NULL,
        "hostrange_intersect works");
    is (result->prefix, "foo",
        "hostrange_intersect returned range with prefix");
    ok (result->lo == 9 && result->hi == 10,
        "hostrange_intersect got expected result");
    hostrange_destroy (result);

    ok (hostrange_intersect (hr1, hr3) == NULL,
        "hostrange_intersect returns NULL for nonintersecting sets");

    result = hostrange_intersect (hr1, hr4);
    ok (result != NULL,
        "hostrange_intersect works");
    ok (hostrange_cmp (result, hr4) == 0,
        "hostrange_intersect got expected result");
    hostrange_destroy (result);

    ok (hostrange_intersect (hr5, hr1) == NULL,
        "hostrange_intersect returns NULL if one of the hosts is a singlehost");

    hostrange_destroy (hr1);
    hostrange_destroy (hr2);
    hostrange_destroy (hr3);
    hostrange_destroy (hr4);
    hostrange_destroy (hr5);
}

struct within_test {
    char *hostname;
    char *prefix;
    bool singlehost;
    unsigned long lo;
    unsigned long hi;
    int width;
    int result;
};

struct within_test within_tests[] = {
    {
        .hostname = "foo",
        .prefix = "foo",
        .singlehost = true,
        .result = 0,
    },
    {
        .hostname = "bar",
        .prefix = "foo",
        .singlehost = true,
        .result = -1,
    },
    {
        .hostname = "foo0",
        .prefix = "foo",
        .lo = 0,
        .hi = 10,
        .width = 1,
        .result = 0,
    },
    {
        .hostname = "foo5",
        .prefix = "foo",
        .lo = 0,
        .hi = 10,
        .width = 1,
        .result = 5,
    },
    {
        .hostname = "foo10",
        .prefix = "foo",
        .lo = 0,
        .hi = 10,
        .width = 1,
        .result = 10 ,
    },
    {
        .hostname = "foo01",
        .prefix = "foo",
        .lo = 0,
        .hi = 10,
        .width = 1,
        .result = -1,
    },
    {
        .hostname = "foo03",
        .prefix = "foo0",
        .lo = 0,
        .hi = 5,
        .width = 1,
        .result = 3,
    },
    {
        .hostname = "foo11",
        .prefix = "foo",
        .lo = 0,
        .hi = 10,
        .width = 1,
        .result = -1,
    },
    {
        .hostname = "bar5",
        .prefix = "foo",
        .lo = 0,
        .hi = 10,
        .width = 1,
        .result = -1,
    },
    {
        .hostname = "foo",
        .prefix = "foo",
        .lo = 0,
        .hi = 10,
        .width = 1,
        .result = -1,
    },
    { .hostname = NULL },
};

void test_within ()
{
    struct within_test *t;

    t = within_tests;
    while (t && t->hostname) {
        int result;
        struct hostrange *hr;
        struct stack_hostname hn_storage = {};
        struct stack_hostname *hn = hostname_stack_create (&hn_storage, t->hostname);

        if (hn == NULL)
            BAIL_OUT ("hostname_create failed!");

        if (t->singlehost)
            hr = hostrange_create_single (t->prefix);
        else
            hr = hostrange_create (t->prefix, t->lo, t->hi, t->width);

        result = hostrange_hn_within (hr, hn);
        ok (result == t->result,
            "hostrange_hn_within (%s[%lu-%lu], %s) returned %d",
            t->prefix, t->lo, t->hi, t->hostname, result);

        hostrange_destroy (hr);
        t++;
    }
}

struct tostring_test {
    char *prefix;
    bool singlehost;
    unsigned long lo;
    unsigned long hi;
    int width;

    ssize_t rc;
    const char *tostring;

    size_t numstr_rc;
    const char *numstr;
};

void test_host_tostring ()
{
    char *host;
    struct hostrange *hr = hostrange_create ("foo", 1, 10, 0);
    if (!hr)
        BAIL_OUT ("hostrange_create failed!");

    ok (hostrange_host_tostring (NULL, 0) == NULL && errno == EINVAL,
        "hostrange_host_tostring (NULL, 0) returns EINVAL");
    ok (hostrange_host_tostring (hr, -1) == NULL && errno == EINVAL,
        "hostrange_host_tostring (hr, -1) returns EINVAL");
    ok (hostrange_host_tostring (hr, 42) == NULL && errno == ERANGE,
        "hostrange_host_tostring (hr, 42) return ERANGE");

    host = hostrange_host_tostring (hr, 0);
    is (host, "foo1",
        "hostrange_host_tostring (hr, 0) returns first host");
    free (host);

    host = hostrange_host_tostring (hr, hostrange_count (hr) - 1);
    is (host, "foo10",
        "hostrange_host_tostring (hr, count - 1) returns last host");
    free (host);

    host = hostrange_host_tostring (hr, 4);
    is (host, "foo5",
        "hostrange_host_tostring (hr, 4) returns expected host");
    free (host);


    hr->width = 3;
    host = hostrange_host_tostring (hr, 4);
    is (host, "foo005",
        "hostrange_host_tostring (hr, 4) preserves width");
    free (host);

    hostrange_destroy (hr);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_create_single ();
    test_create ();
    test_copy ();
    test_count ();
    test_delete ();
    test_cmp ();
    test_join ();
    test_intersect ();
    test_within ();
    test_host_tostring ();

    done_testing ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
