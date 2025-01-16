/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <pthread.h>
#include "npfs.h"

static pthread_key_t error_key;
static pthread_once_t error_once = PTHREAD_ONCE_INIT;

static void
np_init_error_key(void)
{
	pthread_key_create(&error_key, NULL);
}

void
np_uerror(unsigned long ecode)
{
	pthread_once(&error_once, np_init_error_key);
	pthread_setspecific(error_key, (void *)ecode);
}

unsigned long
np_rerror(void)
{
	pthread_once(&error_once, np_init_error_key);
	return (unsigned long)pthread_getspecific(error_key);
}

