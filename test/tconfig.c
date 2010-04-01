/* tconfig.c - test for configure results */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "diod_log.h"

int
main (int argc, char *argv[])
{
    int rc = 1;

    diod_log_init (argv[0]);

    if (argc != 2) {
        msg ("Usage: tconfig {munge|lua|wrappers}");
        exit (1);
    }

    if (!strcmp (argv[1], "munge")) {
#if HAVE_LIBMUNGE
        rc = 0;
#endif
    } else if (!strcmp (argv[1], "lua")) {
#if HAVE_LIBLUA
        rc = 0;
#endif
    } else if (!strcmp (argv[1], "wrappers")) {
#if HAVE_TCP_WRAPPERS
        rc = 0;
#endif
    } else {
        msg ("unknown config parameter: `%s'", argv[1]);
    }
    
    exit (rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
