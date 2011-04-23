/* tnpcli.c - skeletal libnpclient program (valgrind me */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <pthread.h>

#include "9p.h"
#include "npfs.h"
#include "npclient.h"
#include "npcimpl.h"

#include "diod_log.h"

#define TEST_MSIZE 8192

int
main (int argc, char *argv[])
{
    Npcfsys *fs;

    diod_log_init (argv[0]);

    if (!(fs = npc_create_fsys (0, TEST_MSIZE)))
        err_exit ("npc_create_fsys");
    npc_finish (fs); /* closes stdin */

    diod_log_fini ();
    exit (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
