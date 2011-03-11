/* topt.c - test didomount/opt.c */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>

#include "diod_log.h"

#include "opt.h"

int
main (int argc, char *argv[])
{
    Opt o = opt_create ();
    char *s;

    diod_log_init (argv[0]);

    opt_add (o, "mickey=%d", 42);
    opt_add (o, "goofey=%s", "yes");
    opt_add (o, "donald");
    opt_add_cslist (o, "foo,bar,baz");
    opt_add (o, "lastone");

    s = opt_string (o);
    msg ("opt string='%s'", s);
    free (s);

    assert (opt_find (o, "mickey"));
    assert (opt_find (o, "bar"));
    assert (!opt_find (o, "barn"));

    opt_add_cslist_override (o, "mickey=string,foo=12,bar=15,baz");
    s = opt_string (o);
    msg ("opt string='%s'", s);
    free (s);

    opt_destroy (o);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
