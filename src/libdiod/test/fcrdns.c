/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* test the check that a name from a reverse lookup maps back to the
 * client address
 *
 * Most tests call diod_sock_addr_in_list() with hand-built addrinfo
 * lists standing in for a forward lookup, so they do not depend on the
 * resolver.  Only the last tests do a real lookup, of localhost.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

#include "src/libtap/tap.h"
#include "src/libdiod/diod_sock.h"

/* Fill in 'ss' with 'ip', which may have an IPv6 scope ID ("%N").
 */
static struct sockaddr *make_addr (struct sockaddr_storage *ss,
                                   const char *ip, int port)
{
    char buf[INET6_ADDRSTRLEN];
    char *p;

    memset (ss, 0, sizeof (*ss));
    if (strchr (ip, ':')) {
        struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;
        snprintf (buf, sizeof (buf), "%s", ip);
        if ((p = strchr (buf, '%'))) {
            *p++ = '\0';
            sin6->sin6_scope_id = strtoul (p, NULL, 10);
        }
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = htons (port);
        if (inet_pton (AF_INET6, buf, &sin6->sin6_addr) != 1)
            BAIL_OUT ("inet_pton %s failed", ip);
    } else {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;
        sin->sin_family = AF_INET;
        sin->sin_port = htons (port);
        if (inet_pton (AF_INET, ip, &sin->sin_addr) != 1)
            BAIL_OUT ("inet_pton %s failed", ip);
    }
    return (struct sockaddr *)ss;
}

/* Build a forward lookup result from a NULL-terminated list of addresses.
 */
#define MAXLIST 4
struct fake_lookup {
    struct addrinfo ai[MAXLIST];
    struct sockaddr_storage ss[MAXLIST];
};

static struct addrinfo *make_list (struct fake_lookup *f, const char **ips)
{
    int i;

    memset (f, 0, sizeof (*f));
    for (i = 0; ips[i] != NULL; i++) {
        if (i == MAXLIST)
            BAIL_OUT ("too many addresses");
        make_addr (&f->ss[i], ips[i], 0);
        f->ai[i].ai_family = f->ss[i].ss_family;
        f->ai[i].ai_addr = (struct sockaddr *)&f->ss[i];
        f->ai[i].ai_next = ips[i + 1] ? &f->ai[i + 1] : NULL;
    }
    return i > 0 ? &f->ai[0] : NULL;
}

static int in_list (const char *client, const char **ips)
{
    struct sockaddr_storage ss;
    struct fake_lookup f;

    return diod_sock_addr_in_list (make_addr (&ss, client, 1023),
                                   make_list (&f, ips));
}

static void test_in_list (void)
{
    const char *v4[] = { "10.0.0.1", NULL };
    const char *v4multi[] = { "10.0.0.2", "10.0.0.1", NULL };
    const char *v6[] = { "2001:db8::1", NULL };
    const char *ll[] = { "fe80::1", NULL };
    const char *mixed[] = { "2001:db8::1", "10.0.0.1", NULL };
    const char *empty[] = { NULL };

    ok (in_list ("10.0.0.1", v4) == 1,
        "IPv4 address matches itself");
    ok (in_list ("10.0.0.3", v4) == 0,
        "IPv4 address does not match another address");
    ok (in_list ("10.0.0.1", v4multi) == 1,
        "IPv4 address matches a later entry in the list");
    ok (in_list ("10.0.0.1", empty) == 0,
        "nothing matches an empty list");
    ok (in_list ("2001:db8::1", v6) == 1,
        "IPv6 address matches itself");
    ok (in_list ("2001:db8::2", v6) == 0,
        "IPv6 address does not match another address");
    ok (in_list ("::ffff:10.0.0.1", v4) == 1,
        "IPv4-mapped IPv6 address matches the IPv4 address");
    ok (in_list ("::ffff:10.0.0.3", v4) == 0,
        "IPv4-mapped IPv6 address does not match another IPv4 address");
    ok (in_list ("::ffff:10.0.0.1", mixed) == 1,
        "IPv4-mapped IPv6 address matches IPv4 in a mixed list");
    ok (in_list ("2001:db8::1", v4) == 0,
        "IPv6 address does not match an IPv4-only list");
    ok (in_list ("fe80::1%2", ll) == 1,
        "link-local address with a scope ID matches without one");
}

static void test_lookup (void)
{
    struct addrinfo hints, *res = NULL;
    struct sockaddr_storage ss;
    int have_localhost;

    memset (&hints, 0, sizeof (hints));
    hints.ai_family = AF_INET;
    have_localhost = (getaddrinfo ("localhost", NULL, &hints, &res) == 0);
    if (res)
        freeaddrinfo (res);

    skip (!have_localhost, 2, "localhost does not resolve to IPv4");
    ok (diod_sock_name_matches_addr ("localhost",
                                     make_addr (&ss, "127.0.0.1", 0)) == 1,
        "localhost matches 127.0.0.1");
    ok (diod_sock_name_matches_addr ("localhost",
                                     make_addr (&ss, "192.0.2.1", 0)) == 0,
        "localhost does not match 192.0.2.1");
    end_skip;
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_in_list ();
    test_lookup ();

    done_testing ();
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
