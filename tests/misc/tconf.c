/* tconf.c - test config file */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#if HAVE_GETOPT_H
#include <getopt.h>
#endif
#include <errno.h>
#include <assert.h>

#include "list.h"

#include "diod_log.h"
#include "diod_conf.h"

#define OPTIONS "c:em"

#if HAVE_GETOPT_LONG
#define GETOPT(ac,av,opt,lopt) getopt_long (ac,av,opt,lopt,NULL)
static const struct option longopts[] = {
    {"config-file",     required_argument,  0, 'c'},
    {"exports",         no_argument,        0, 'e'},
    {"mounts",          no_argument,        0, 'm'},
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
"   -m,--mounts            dump /proc/mounts\n"
    );
    exit (1);
}

static void
dump_exports (List l)
{
    ListIterator itr = list_iterator_create (l);
    Export *x;

    if (!itr)
        msg_exit ("out of memory");
    while ((x = list_next (itr))) {
        assert (x->path);
        if (x->opts || x->users || x->hosts) {
            printf("path=%s opts=%s(0x%x) users=%s hosts=%s\n",
                x->path,
                x->opts ? x->opts : "NULL", x->opts ? x->oflags : 0,
                x->users ? x->users : "NULL",
                x->hosts ? x->hosts : "NULL");
        } else if (x->path)
            printf("path=%s\n", x->path);
    }
    list_iterator_destroy (itr);
}

int
main (int argc, char *argv[])
{
    List e, m;
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

    diod_conf_set_exportall (1);
    if (!(m = diod_conf_get_mounts ()))
        msg_exit ("out of memory");

    if (!(e = diod_conf_get_exports ()))
        msg_exit ("out of memory");

    /* Command line overrides config file.
     */
    optind = 0;
    opterr = 0;
    while ((c = GETOPT (argc, argv, OPTIONS, longopts)) != -1) {
        switch (c) {
            case 'c':   /* --config-file PATH (already handled) */
                break;
            case 'e':   /* --exports */
                dump_exports (e);
                break;
            case 'm':   /* --mounts */
                dump_exports (m);
                break;
            default:
                usage ();
                /*NOTREACHED*/
        }
    }
    if (optind < argc)
        usage ();

    list_destroy (m);
    diod_conf_fini ();
    diod_log_fini ();

    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

