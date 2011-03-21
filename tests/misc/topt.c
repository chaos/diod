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
    int i;

    diod_log_init (argv[0]);

    opt_addf (o, "mickey=%d", 42);
    opt_addf (o, "goofey=%s", "yes");
    opt_addf (o, "donald");
    opt_addf (o, "foo,bar,baz");
    opt_addf (o, "lastone");

    s = opt_csv (o);
    msg ("opt string='%s'", s);
    free (s);

    assert (opt_find (o, "mickey"));
    assert (opt_find (o, "bar"));
    assert (!opt_find (o, "barn"));

    assert (opt_scanf (o, "mickey=%d", &i));
    assert (i == 42);

    opt_addf (o, "mickey=string,foo=%d,bar=%d,baz", 12, 15);
    s = opt_csv (o);
    msg ("opt string='%s'", s);
    free (s);

    opt_destroy (o);

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
