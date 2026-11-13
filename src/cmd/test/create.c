/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* create.c - create a file with open(O_CREAT) and nothing else
 *
 * touch(1) is not usable for testing whether a create is denied: it
 * calls open(O_CREAT) and then utimensat(), and reports the second
 * failure as "setting times of" even though the file was created.  A
 * test that greps its stderr cannot tell the two apart.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	int fd;

	if (argc != 2) {
		fprintf(stderr, "Usage: create FILE\n");
		return 1;
	}
	if ((fd = open(argv[1], O_CREAT|O_WRONLY, 0644)) < 0) {
		fprintf(stderr, "open %s: %s\n", argv[1], strerror(errno));
		return 1;
	}
	close(fd);
	return 0;
}
