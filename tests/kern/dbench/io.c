/* 
   dbench version 2
   
   Copyright (C) by Andrew Tridgell <tridge@samba.org> 1999, 2001
   Copyright (C) 2001 by Martin Pool <mbp@samba.org>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

/* Wrappers for system calls that catch errors. */

#include "dbench.h"

#define MAX_FILES 1000

static char buf[70000];
extern int line_count;

static struct {
	int fd;
	int handle;
} ftable[MAX_FILES];

void do_unlink(char *fname)
{
	strupper(fname);

	if (unlink(fname) != 0) {
		printf("(%d) unlink %s failed (%s)\n", 
		       line_count, fname, strerror(errno));
	}
}

void expand_file(int fd, int size)
{
	int s;
	while (size) {
		s = MIN(sizeof(buf), size);
		write(fd, buf, s);
		size -= s;
	}
}

void do_open(char *fname, int handle, int size)
{
	int fd, i;
	int flags = O_RDWR|O_CREAT;
	struct stat st;
	static int count;

	strupper(fname);

	if (size == 0) flags |= O_TRUNC;

	fd = open(fname, flags, 0600);
	if (fd == -1) {
		printf("(%d) open %s failed for handle %d (%s)\n", 
		       line_count, fname, handle, strerror(errno));
		return;
	}
	fstat(fd, &st);
	if (size > st.st_size) {
#if DEBUG
		printf("(%d) expanding %s to %d from %d\n", 
		       line_count, fname, size, (int)st.st_size);
#endif
		expand_file(fd, size - st.st_size);
	} else if (size < st.st_size) {
		printf("truncating %s to %d from %d\n", 
		       fname, size, (int)st.st_size);
		ftruncate(fd, size);
	}
	for (i=0;i<MAX_FILES;i++) {
		if (ftable[i].handle == 0) break;
	}
	if (i == MAX_FILES) {
		printf("file table full for %s\n", fname);
		exit(1);
	}
	ftable[i].handle = handle;
	ftable[i].fd = fd;
	if (count++ % 100 == 0) {
		printf(".");
	}
}

void do_write(int handle, int size, int offset)
{
	int i;

	if (buf[0] == 0) memset(buf, 1, sizeof(buf));

	for (i=0;i<MAX_FILES;i++) {
		if (ftable[i].handle == handle) break;
	}
	if (i == MAX_FILES) {
#if 1
		printf("(%d) do_write: handle %d was not open size=%d ofs=%d\n", 
		       line_count, handle, size, offset);
#endif
		return;
	}
	lseek(ftable[i].fd, offset, SEEK_SET);
	if (write(ftable[i].fd, buf, size) != size) {
		printf("write failed on handle %d (%s)\n", handle, strerror(errno));
	}
}

void do_read(int handle, int size, int offset)
{
	int i;
	for (i=0;i<MAX_FILES;i++) {
		if (ftable[i].handle == handle) break;
	}
	if (i == MAX_FILES) {
		printf("(%d) do_read: handle %d was not open size=%d ofs=%d\n", 
		       line_count, handle, size, offset);
		return;
	}
	lseek(ftable[i].fd, offset, SEEK_SET);
	read(ftable[i].fd, buf, size);
}

void do_close(int handle)
{
	int i;
	for (i=0;i<MAX_FILES;i++) {
		if (ftable[i].handle == handle) break;
	}
	if (i == MAX_FILES) {
		printf("(%d) do_close: handle %d was not open\n", 
		       line_count, handle);
		return;
	}
	close(ftable[i].fd);
	ftable[i].handle = 0;
}

void do_mkdir(char *fname)
{
	strupper(fname);

	if (mkdir(fname, 0700) != 0) {
#if DEBUG
		printf("mkdir %s failed (%s)\n", 
		       fname, strerror(errno));
#endif
	}
}

void do_rmdir(char *fname)
{
	strupper(fname);

	if (rmdir(fname) != 0) {
		printf("rmdir %s failed (%s)\n", 
		       fname, strerror(errno));
	}
}

void do_rename(char *old, char *new)
{
	strupper(old);
	strupper(new);

	if (rename(old, new) != 0) {
		printf("rename %s %s failed (%s)\n", 
		       old, new, strerror(errno));
	}
}


void do_stat(char *fname, int size)
{
	struct stat st;

	strupper(fname);

	if (stat(fname, &st) != 0) {
		printf("(%d) do_stat: %s size=%d %s\n", 
		       line_count, fname, size, strerror(errno));
		return;
	}
	if (S_ISDIR(st.st_mode)) return;

	if (st.st_size != size) {
		printf("(%d) do_stat: %s wrong size %d %d\n", 
		       line_count, fname, (int)st.st_size, size);
	}
}

void do_create(char *fname, int size)
{
	do_open(fname, 5000, size);
	do_close(5000);
}


