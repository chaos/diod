/* 
   dbench version 4

   Copyright (C) 1999-2007 by Andrew Tridgell <tridge@samba.org>
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

#include "dbench.h"

#define MAX_FILES 200

struct ftable {
	char *name;
	int fd;
	int handle;
};

static int find_handle(struct child_struct *child, int handle)
{
	struct ftable *ftable = child->private;
	int i;
	for (i=0;i<MAX_FILES;i++) {
		if (ftable[i].handle == handle) return i;
	}
	printf("(%d) ERROR: handle %d was not found\n", 
	       child->line, handle);
	exit(1);
}


/* Find the directory holding a file, and flush it to disk.  We do
   this in -S mode after a directory-modifying mode, to simulate the
   way knfsd tries to flush directories.  MKDIR and similar operations
   are meant to be synchronous on NFSv2. */
static void sync_parent(struct child_struct *child, const char *fname)
{
	char *copy_name;
	int dir_fd;
	char *slash;

	if (strchr(fname, '/')) {
		copy_name = strdup(fname);
		slash = strrchr(copy_name, '/');
		*slash = '\0';
	} else {
		copy_name = strdup(".");
	} 
	
	dir_fd = open(copy_name, O_RDONLY);
	if (dir_fd == -1) {
		printf("[%d] open directory \"%s\" for sync failed: %s\n",
		       child->line, copy_name, strerror(errno));
	} else {
#if defined(HAVE_FDATASYNC)
		if (fdatasync(dir_fd) == -1) {
#else
		if (fsync(dir_fd) == -1) {
#endif
			printf("[%d] datasync directory \"%s\" failed: %s\n",
			       child->line, copy_name,
			       strerror(errno));
		}
		if (close(dir_fd) == -1) {
			printf("[%d] close directory failed: %s\n",
			       child->line, strerror(errno));
		}
	}
	free(copy_name);
}

static void xattr_fd_read_hook(struct child_struct *child, int fd)
{
#if HAVE_EA_SUPPORT
	char buf[44];
	if (options.ea_enable) {
		memset(buf, 0, sizeof(buf));
		sys_fgetxattr(fd, "user.DosAttrib", buf, sizeof(buf));
	}
#else
	(void)fd;
#endif
	(void)child;
}

static void xattr_fname_read_hook(struct child_struct *child, const char *fname)
{
#if HAVE_EA_SUPPORT
	if (options.ea_enable) {
		char buf[44];
		sys_getxattr(fname, "user.DosAttrib", buf, sizeof(buf));
	}
#else
	(void)fname;
#endif
	(void)child;
}

static void xattr_fd_write_hook(struct child_struct *child, int fd)
{
#if HAVE_EA_SUPPORT
	if (options.ea_enable) {
		struct timeval tv;
		char buf[44];
		sys_fgetxattr(fd, "user.DosAttrib", buf, sizeof(buf));
		memset(buf, 0, sizeof(buf));
		/* give some probability of sharing */
		if (random() % 10 < 2) {
			time_t t = time(NULL); /* reworked - jg */
			memcpy(buf, &t, sizeof(t));
		} else {
			gettimeofday(&tv, NULL);
			memcpy(buf, &tv, sizeof(tv));
		}
		if (sys_fsetxattr(fd, "user.DosAttrib", buf, sizeof(buf), 0) != 0) {
			printf("[%d] fsetxattr failed - %s\n", 
			       child->line, strerror(errno));
			exit(1);
		}
	}
#else
	(void)fd;
#endif
}

static int expected_status(const char *status)
{
	if (strcmp(status, "NT_STATUS_OK") == 0) {
		return 0;
	}
	if (strncmp(status, "0x", 2) == 0 &&
	    strtoul(status, NULL, 16) == 0) {
		return 0;
	}
	return -1;
}

/*
  simulate pvfs_resolve_name()
*/
static void resolve_name(struct child_struct *child, const char *name)
{
	struct stat st;
	char *dname, *fname;
	DIR *dir;
	char *p;
	struct dirent *d;

	if (name == NULL) return;

	if (stat(name, &st) == 0) {
		xattr_fname_read_hook(child, name);
		return;
	}

	if (options.no_resolve) {
		return;
	}

	dname = strdup(name);
	p = strrchr(dname, '/');
	if (!p) return;
	*p = 0;
	fname = p+1;

	dir = opendir(dname);
	if (!dir) {
		free(dname);
		return;
	}
	while ((d = readdir(dir))) {
		if (strcasecmp(fname, d->d_name) == 0) break;
	}
	closedir(dir);
	free(dname);
}

static void failed(struct child_struct *child)
{
	child->failed = 1;
	printf("ERROR: child %d failed at line %d\n", child->id, child->line);
	exit(1);
}

void nb_setup(struct child_struct *child)
{
	struct ftable *ftable;
	ftable = calloc(MAX_FILES, sizeof(struct ftable));
	child->private = ftable;
	child->rate.last_time = timeval_current();
	child->rate.last_bytes = 0;
}

void nb_unlink(struct child_struct *child, const char *fname, int attr, const char *status)
{
	(void)attr;

	resolve_name(child, fname);

	if (unlink(fname) != expected_status(status)) {
		printf("[%d] unlink %s failed (%s) - expected %s\n", 
		       child->line, fname, strerror(errno), status);
		failed(child);
	}
	if (options.sync_dirs) sync_parent(child, fname);
}

void nb_mkdir(struct child_struct *child, const char *dname, const char *status)
{
	struct stat st;
	(void)child;
	(void)status;
	resolve_name(child, dname);
	if (options.stat_check && stat(dname, &st) == 0) {
		return;
	}
	mkdir(dname, 0777);
}

void nb_rmdir(struct child_struct *child, const char *fname, const char *status)
{
	struct stat st;
	resolve_name(child, fname);

	if (options.stat_check && 
	    (stat(fname, &st) != 0 || !S_ISDIR(st.st_mode))) {
		return;
	}

	if (rmdir(fname) != expected_status(status)) {
		printf("[%d] rmdir %s failed (%s) - expected %s\n", 
		       child->line, fname, strerror(errno), status);
		failed(child);
	}
	if (options.sync_dirs) sync_parent(child, fname);
}

void nb_createx(struct child_struct *child, const char *fname, 
		uint32_t create_options, uint32_t create_disposition, int fnum,
		const char *status)
{
	int fd, i;
	int flags = O_RDWR;
	struct stat st;
	struct ftable *ftable = (struct ftable *)child->private;

	resolve_name(child, fname);

	if (options.sync_open) flags |= O_SYNC;

	if (create_disposition == FILE_CREATE) {
		if (options.stat_check && stat(fname, &st) == 0) {
			create_disposition = FILE_OPEN;
		} else {
			flags |= O_CREAT;
		}
	}

	if (create_disposition == FILE_OVERWRITE ||
	    create_disposition == FILE_OVERWRITE_IF) {
		flags |= O_CREAT | O_TRUNC;
	}

	if (create_options & FILE_DIRECTORY_FILE) {
		/* not strictly correct, but close enough */
		if (!options.stat_check || stat(fname, &st) == -1) {
			mkdir(fname, 0700);
		}
	}

	if (create_options & FILE_DIRECTORY_FILE) flags = O_RDONLY|O_DIRECTORY;

	fd = open(fname, flags, 0600);
	if (fd == -1 && errno == EISDIR) {
		flags = O_RDONLY|O_DIRECTORY;
		fd = open(fname, flags, 0600);
	}
	if (fd == -1) {
		if (expected_status(status) == 0) {
			printf("[%d] open %s failed for handle %d (%s)\n", 
			       child->line, fname, fnum, strerror(errno));
		}
		return;
	}
	if (expected_status(status) != 0) {
		printf("[%d] open %s succeeded for handle %d\n", 
		       child->line, fname, fnum);
		close(fd);
		return;
	}
	
	for (i=0;i<MAX_FILES;i++) {
		if (ftable[i].handle == 0) break;
	}
	if (i == MAX_FILES) {
		printf("file table full for %s\n", fname);
		exit(1);
	}
	ftable[i].name = strdup(fname);
	ftable[i].handle = fnum;
	ftable[i].fd = fd;

	fstat(fd, &st);

	if (!S_ISDIR(st.st_mode)) {
		xattr_fd_write_hook(child, fd);
	}
}

void nb_writex(struct child_struct *child, int handle, int offset, 
	       int size, int ret_size, const char *status)
{
	int i = find_handle(child, handle);
	void *buf;
	struct stat st;
	struct ftable *ftable = (struct ftable *)child->private;
	ssize_t ret;

	if (options.fake_io) {
		child->bytes += ret_size;
		child->bytes_since_fsync += ret_size;
		return;
	}

	(void)status;

	buf = calloc(size, 1);

	if (options.one_byte_write_fix &&
	    size == 1 && fstat(ftable[i].fd, &st) == 0) {
		if (st.st_size > offset) {
			unsigned char c;
			pread(ftable[i].fd, &c, 1, offset);
			if (c == ((unsigned char *)buf)[0]) {
				free(buf);
				child->bytes += size;
				return;
			}
		} else if (((unsigned char *)buf)[0] == 0) {
			ftruncate(ftable[i].fd, offset+1);
			free(buf);
			child->bytes += size;
			return;
		} 
	}

	ret = pwrite(ftable[i].fd, buf, size, offset);
	if (ret == -1) {
		printf("[%d] write failed on handle %d (%s)\n", 
		       child->line, handle, strerror(errno));
		exit(1);
	}
	if (ret != ret_size) {
		printf("[%d] wrote %d bytes, expected to write %d bytes on handle %d\n", 
		       child->line, (int)ret, (int)ret_size, handle);
		exit(1);
	}

	if (options.do_fsync) fsync(ftable[i].fd);

	free(buf);

	child->bytes += size;
	child->bytes_since_fsync += size;
}

void nb_readx(struct child_struct *child, int handle, int offset, 
	      int size, int ret_size, const char *status)
{
	int i = find_handle(child, handle);
	void *buf;
	struct ftable *ftable = (struct ftable *)child->private;

	if (options.fake_io) {
		child->bytes += ret_size;
		return;
	}

	(void)status;

	buf = malloc(size);

	if (pread(ftable[i].fd, buf, size, offset) != ret_size) {
		printf("[%d] read failed on handle %d (%s)\n", 
		       child->line, handle, strerror(errno));
	}

	free(buf);

	child->bytes += size;
}

void nb_close(struct child_struct *child, int handle, const char *status)
{
	struct ftable *ftable = (struct ftable *)child->private;
	int i = find_handle(child, handle);
	(void)status;
	close(ftable[i].fd);
	ftable[i].handle = 0;
	if (ftable[i].name) free(ftable[i].name);
	ftable[i].name = NULL;
}

void nb_rename(struct child_struct *child, const char *old, const char *new, const char *status)
{
	resolve_name(child, old);
	resolve_name(child, new);

	if (options.stat_check) {
		struct stat st;
		if (stat(old, &st) != 0 && expected_status(status) == 0) {
			printf("[%d] rename %s %s failed - file doesn't exist\n",
			       child->line, old, new);
			failed(child);
			return;
		}
	}

	if (rename(old, new) != expected_status(status)) {
		printf("[%d] rename %s %s failed (%s) - expected %s\n", 
		       child->line, old, new, strerror(errno), status);
		failed(child);
	}
	if (options.sync_dirs) sync_parent(child, new);
}

void nb_flush(struct child_struct *child, int handle, const char *status)
{
	struct ftable *ftable = (struct ftable *)child->private;
	int i = find_handle(child, handle);
	(void)status;
	fsync(ftable[i].fd);
}

void nb_qpathinfo(struct child_struct *child, const char *fname, int level, 
		  const char *status)
{
	(void)child;
	(void)level;
	(void)status;
	resolve_name(child, fname);
}

void nb_qfileinfo(struct child_struct *child, int handle, int level, const char *status)
{
	struct ftable *ftable = (struct ftable *)child->private;
	struct stat st;
	int i = find_handle(child, handle);
	(void)child;
	(void)level;
	(void)status;
	fstat(ftable[i].fd, &st);
	xattr_fd_read_hook(child, ftable[i].fd);
}

void nb_qfsinfo(struct child_struct *child, int level, const char *status)
{
	struct statvfs st;

	(void)level;
	(void)status;

	statvfs(child->directory, &st);
}

void nb_findfirst(struct child_struct *child, const char *fname, int level, int maxcnt, 
		  int count, const char *status)
{
	DIR *dir;
	struct dirent *d;
	char *p;

	(void)child;
	(void)level;
	(void)count;
	(void)status;

	resolve_name(child, fname);

	if (strpbrk(fname, "<>*?\"") == NULL) {
		return;
	}

	p = strrchr(fname, '/');
	if (!p) return;
	*p = 0;
	dir = opendir(fname);
	if (!dir) return;
	while (maxcnt && (d = readdir(dir))) maxcnt--;
	closedir(dir);
}

void nb_cleanup(struct child_struct *child)
{
	char *dname;

	asprintf(&dname, "%s/clients/client%d", child->directory, child->id);
	nb_deltree(child, dname);
	free(dname);

	asprintf(&dname, "%s%s", child->directory, "/clients");
	rmdir(dname);
	free(dname);
}

void nb_deltree(struct child_struct *child, const char *dname)
{
	DIR *d;
	struct dirent *de;
	(void)child;
	
	d = opendir(dname);
	if (d == NULL) return;

	for (de=readdir(d);de;de=readdir(d)) {
		struct stat st;
		char *fname = NULL;
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0) {
			continue;
		}
		asprintf(&fname, "%s/%s", dname, de->d_name);
		if (fname == NULL) {
			printf("Out of memory\n");
			exit(1);
		}
		if (stat(fname, &st) != 0) {
			continue;
		}
		if (S_ISDIR(st.st_mode)) {
			nb_deltree(child, fname);
		} else {
			if (unlink(fname) != 0) {
				printf("[%d] unlink '%s' failed - %s\n",
				       child->line, fname, strerror(errno));
			}
		}
		free(fname);
	}
	closedir(d);
}

void nb_sfileinfo(struct child_struct *child, int handle, int level, const char *status)
{
	struct ftable *ftable = (struct ftable *)child->private;
	int i = find_handle(child, handle);
	struct utimbuf tm;
	struct stat st;
	(void)child;
	(void)handle;
	(void)level;
	(void)status;
	xattr_fd_read_hook(child, ftable[i].fd);

	fstat(ftable[i].fd, &st);

	tm.actime = st.st_atime - 10;
	tm.modtime = st.st_mtime - 12;

	utime(ftable[i].name, &tm);

	if (!S_ISDIR(st.st_mode)) {
		xattr_fd_write_hook(child, ftable[i].fd);
	}
}

void nb_lockx(struct child_struct *child, int handle, uint32_t offset, int size, 
	      const char *status)
{
	struct ftable *ftable = (struct ftable *)child->private;
	int i = find_handle(child, handle);
	struct flock lock;

	(void)child;
	(void)status;

	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = offset;
	lock.l_len = size;
	lock.l_pid = 0;

	fcntl(ftable[i].fd, F_SETLKW, &lock);
}

void nb_unlockx(struct child_struct *child,
		int handle, uint32_t offset, int size, const char *status)
{
	struct ftable *ftable = (struct ftable *)child->private;
	int i = find_handle(child, handle);
	struct flock lock;

	(void)child;
	(void)status;

	lock.l_type = F_UNLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = offset;
	lock.l_len = size;
	lock.l_pid = 0;

	fcntl(ftable[i].fd, F_SETLKW, &lock);
}

void nb_sleep(struct child_struct *child, int usec, const char *status)
{
	(void)child;
	(void)usec;
	(void)status;
	usleep(usec);
}
