/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#define _XOPEN_SOURCE 600
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include "npfs.h"

#define ROOTPERM 	0755
#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

typedef struct File File;

struct File {
	u8*	data;
	u64	datasize;
};

static void rfs_connclose(Npconn *conn);
static int rfs_read(Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *);
static int rfs_write(Npfilefid* file, u64 offset, u32 count, u8* data, Npreq *);
static int rfs_wstat(Npfile*, Npstat*);
static void rfs_destroy(Npfile*);
static Npfile* rfs_create(Npfile *dir, char *name, u32 perm, Npuser *uid, 
	Npgroup *gid, char *extension);
static Npfile* rfs_first(Npfile *dir);
static Npfile* rfs_next(Npfile *dir, Npfile *prevchild);
static int rfs_wstat(Npfile*, Npstat*);
static int rfs_remove(Npfile *dir, Npfile *file);

static Npsrv *srv;
static Npfile *root;
static u64 qidpath;
static int blksize;

static char *Enospace = "no space left";

Npfileops fileops = {
	.read = rfs_read,
	.write = rfs_write,
	.wstat = rfs_wstat,
	.destroy = rfs_destroy,
};

Npdirops dirops = {
	.create = rfs_create,
	.first = rfs_first,
	.next = rfs_next,
	.wstat = rfs_wstat,
	.remove = rfs_remove,
	.destroy = rfs_destroy,
};

static File* file_alloc(void);

void
usage()
{
	fprintf(stderr, "ramfs: -d -u user -w nthreads -b blocksize "
		"-o mount-options mount-point\n");
	exit(-1);
}

int
main(int argc, char **argv)
{
	int c, debuglevel, nwthreads, fd;
	pid_t pid;
	Npuser *user;
	char *opts, *logfile, *s;

	debuglevel = 0;
	blksize = 8192;
	nwthreads = 1;
	opts = "";
	logfile = "/tmp/ramfs.log";
	user = np_default_users->uid2user(np_default_users, getuid());
	while ((c = getopt(argc, argv, "du:w:b:o:l:")) != -1) {
		switch (c) {
		case 'd':
			debuglevel = 1;
			break;

		case 'u':
			user = np_default_users->uname2user(np_default_users, optarg);
			break;

		case 'b':
			blksize = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'o':
			opts = optarg;
			break;

		case 'l':
			logfile = optarg;
			break;

		default:
			fprintf(stderr, "invalid option\n");
		}
	}

	if (!user) {
		fprintf(stderr, "invalid user\n");
		return -1;
	}

	fd = open(logfile, O_WRONLY | O_APPEND | O_CREAT, 0666);
	if (fd < 0) {
		fprintf(stderr, "cannot open log file %s: %d\n", logfile, errno);
		return -1;
	}

	close(0);
	close(1);
	close(2);
	if (dup2(fd, 2) < 0) {
		fprintf(stderr, "dup failed: %d\n", errno);
		return -1;
	}

	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid != 0)
		return 0;

	setsid();
	chdir("/");

	root = npfile_alloc(NULL, strdup(""), ROOTPERM|Dmdir, qidpath++, 
		&dirops, file_alloc());
	npfile_incref(root);
	root->parent = root;
	root->atime = time(NULL);
	root->mtime = root->atime;
	root->uid = user;
	root->gid = user->dfltgroup;
	root->muid = user;

	srv = np_pipesrv_create(nwthreads);
	if (!srv)
		return -1;

	npfile_init_srv(srv, root);
	if (optind >= argc)
		usage();

	srv->debuglevel = debuglevel;
	srv->connclose = rfs_connclose;
	np_pipesrv_mount(srv, argv[optind], user->uname, 0, opts);

	while (1) {
		sleep(100);
	}

	return 0;
}

static File*
file_alloc(void)
{
	File *f;

	f = malloc(sizeof(*f));
	f->data = NULL;
	f->datasize = 0;

	return f;
}

static int
file_truncate(File *f, u64 size)
{
	int n;
	u8* buf;

	if (size == 0) {
		free(f->data);
		f->data = NULL;
		f->datasize = 0;
		return 0;
	}
		
	n = (size/blksize + (size%blksize?1:0)) * blksize;
	buf = realloc(f->data, n);
	if (!buf)
		return -1;

	f->data = buf;
	f->datasize = n;
	return 0;
}

static void
rfs_connclose(Npconn *conn)
{
	exit(0);
}

static int
rfs_read(Npfilefid* fid, u64 offset, u32 count, u8* data, Npreq *req)
{
	int n;
	Npfile *file;
	File *f;

	file = fid->file;
	f = file->aux;
	n = count;
	if (file->length < offset+count)
		n = file->length - offset;

	if (n < 0)
		n = 0;

	memmove(data, f->data + offset, n);
	return n;
}

static int 
rfs_write(Npfilefid* fid, u64 offset, u32 count, u8* data, Npreq *req)
{
	int n;
	Npfile *file;
	File *f;

	file = fid->file;
	f = file->aux;
	if (fid->omode & Oappend)
		offset = file->length;

	n = count;
	if (file->length < offset+count) {
		pthread_mutex_lock(&file->lock);
		if (file_truncate(f, offset+count)) {
			np_werror(Enospace, ENOSPC);
			pthread_mutex_unlock(&file->lock);
			return 0;
		}

		if (offset+count > f->datasize) {
			if (f->datasize - offset > 0)
				n = f->datasize - offset;
			else
				n = 0;
		}

		if (n) {
			if (file->length < offset)
				memset(f->data + file->length, 0, offset - 
					file->length);
			file->length = offset + count;
		}
		pthread_mutex_unlock(&file->lock);
	}

	if (n)
		memmove(f->data + offset, data, n);

	return n;
}

static int 
rfs_wstat(Npfile *file, Npstat *stat)
{
	File *f;
	Npfile *nfile;
	char *sname, *oldname;
	int lockparent;
	u64 length, oldlength;
	u32 oldperm;
	u32 oldmtime;

	f = file->aux;
	oldlength = ~0;
	oldperm = ~0;
	oldmtime = ~0;
	oldname = NULL;

	lockparent = stat->name.len != 0;
	if (lockparent)
		pthread_mutex_lock(&file->parent->lock);

	if (stat->name.len != 0) {
		sname = np_strdup(&stat->name);
		nfile = npfile_find(file->parent, sname);
		if (nfile) {
			free(sname);
			np_werror(Eexist, EEXIST);
			goto error;
		} else 
			npfile_decref(nfile);

		oldname = file->name;
		file->name = sname;
	}

	if (stat->length != (u64) ~0) {
		oldlength = file->length;
		length = stat->length;
		if (file_truncate(f, length)) {
			np_werror(Enospace, ENOSPC);
			goto error;
		}

		if (length > f->datasize)
			length = f->datasize;

		if (file->length < length)
			memset(f->data + file->length, 0, length - file->length);

		file->length = length;
	}

	if (stat->mode != (u32) ~0) {
		oldperm = file->mode;
		file->mode = stat->mode;
	}

	if (stat->mtime != (u32)~0) {
		oldmtime = file->mtime;
		file->mtime = stat->mtime;
	}

	free(oldname);
	if (lockparent)
		pthread_mutex_unlock(&file->parent->lock);

	return 1;

error:
	if (oldname) {
		free(file->name);
		file->name = oldname;
	}

	if (oldperm != ~0)
		file->mode = oldperm;

	if (oldmtime != ~0)
		file->mtime = oldmtime;

	if (oldlength != ~0) {
		file->length = oldlength;
		file_truncate(f, oldlength);
	}

	if (lockparent)
		pthread_mutex_unlock(&file->parent->lock);

	return 0;
}

static void 
rfs_destroy(Npfile* file)
{
	File *f;

	f = file->aux;
	free(f->data);
	free(f);
}

static Npfile* 
rfs_create(Npfile *dir, char *name, u32 perm, Npuser *uid, Npgroup *gid, 
	char *extension)
{
	Npfile *file;
	File *d, *f;
	void *ops;

	if (perm & Dmlink) {
		np_werror(Eperm, EPERM);
		return NULL;
	}

	d = dir->aux;
	f = file_alloc();
	if (perm&Dmdir)
		ops = &dirops;
	else
		ops = &fileops;

	file = npfile_alloc(dir, name, perm, qidpath++, ops, f);
	file->uid = uid;
	file->gid = gid;
	file->muid = uid;
	npfile_incref(file);
	npfile_incref(file);

	if (dir->dirlast) {
		dir->dirlast->next = file;
		file->prev = dir->dirlast;
	} else
		dir->dirfirst = file;

	dir->dirlast = file;
	file->extension = strdup(extension);

	return file;
}

static Npfile* 
rfs_first(Npfile *dir)
{
	npfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Npfile*
rfs_next(Npfile *dir, Npfile *prevchild)
{
	npfile_incref(prevchild->next);
	return prevchild->next;
}

static int
rfs_remove(Npfile *dir, Npfile *file)
{
	if (dir->dirfirst == file)
		dir->dirfirst = file->next;
	else
		file->prev->next = file->next;

	if (file->next)
		file->next->prev = file->prev;

	if (file == dir->dirlast)
		dir->dirlast = file->prev;

	file->prev = NULL;
	file->next = NULL;
	file->parent = NULL;

	return 1;
}
