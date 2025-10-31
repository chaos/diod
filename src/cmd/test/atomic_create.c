/************************************************************\
 * Copyright 2010 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the diod 9P server project.
 * For details, see https://github.com/chaos/diod.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
\************************************************************/

/* tatomic.c - test O_CREAT|O_EXCL atomic create */

/* posted to v9fs-developer by M. Mohan Kumar */

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	int fd;

	/* first create the file */
	fd = open(argv[1], O_CREAT|O_WRONLY, 0644);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	close(fd);

	/* Now opening same file with O_CREAT|O_EXCL should fail */
	fd = open(argv[1], O_CREAT|O_EXCL, 0644);
	if (!(fd < 0 && errno == EEXIST))
		return 1;
	close(fd);
	return 0;
}
