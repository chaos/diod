/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tlua.c - is lua configured? */

#if HAVE_CONFIG_H
#include "config.h"
#endif

int
main (int argc, char *argv[])
{
#if HAVE_CONFIG_FILE
	return 0;
#else
	return 1;
#endif
}
