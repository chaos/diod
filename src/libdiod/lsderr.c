#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>

#include "diod_log.h"

void
lsd_fatal_error (char *file, int line, char *mesg)
{
    msg_exit ("fatal error: %s: %s::%d", mesg, file, line);
}

void *
lsd_nomem_error (char *file, int line, char *mesg)
{
    msg ("out of memory: %s: %s::%d", mesg, file, line);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

