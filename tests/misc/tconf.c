/* tconf.c - test config file */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#define _BSD_SOURCE         /* daemon */
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <string.h>
#include <poll.h>

#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"

#define OPTIONS "c:e"

#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"config-file",     required_argument,  0, 'c'},
    {"exports",         no_argument,        0, 'e'},
    {0, 0, 0, 0},
};
#else
#define GETOPT(ac,av,opt,lopt) getopt (ac,av,opt)
#endif

static void
usage(void)
{
    fprintf (stderr,
"Usage: tconf [OPTIONS]\n"
"   -c,--config-file FILE  set config file path\n"
"   -e,--exports           dump exports\n"
    );
    exit (1);
}

static void
dump_exports (void)
{
    List l = diod_conf_get_exports ();
    ListIterator itr = list_iterator_create (l);
    Export *x;

    if (!itr)
        msg_exit ("out of memory");
    while ((x = list_next (itr))) {
        printf("path=%s opts=%s users=%s hosts=%s\n",
            x->path ? x->path : "NULL",
            x->opts ? x->opts : "NULL",
            x->users ? x->users : "NULL",
            x->hosts ? x->hosts : "NULL");
    }
    list_iterator_destroy (itr);
}

int
main (int argc, char *argv[])
{
    char *copt = NULL;
    int c;

    diod_log_init (argv[0]);
    diod_conf_init ();

    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'c':   /* --config-file PATH */
                copt = optarg;
                break;
            default:
                break;
        }
    }
    diod_conf_init_config_file (copt);

    /* Command line overrides config file.
     */
    optind = 0;
    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'c':   /* --config-file PATH (already handled) */
                break;
            case 'e':   /* --exports */
                dump_exports ();
                break;
            default:
                usage ();
                /*NOTREACHED*/
        }
    }
    if (optind < argc)
        usage ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

