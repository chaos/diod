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
typedef struct Fid Fid;

struct File {
	pthread_mutex_t	lock;
	int		refcount;
	char*		name;
	u32		perm;
	u64		length;
	u32		atime;
	u32		mtime;
	Npuser*		uid;
	Npgroup*	gid;
	Npuser*		muid;
	char*		extension;
	Npqid		qid;
	int 		excl;

	File*		parent;
	File*		next;		/* siblings, protected by parent lock */
	File*		prev;

	File*		dirents;	/* if directory */
	File*		dirlast;

	u8*		data;
	u64		datasize;
};

struct Fid {
	File*	file;
	int	omode;
	u64	diroffset;
	File*	dirent;
};

static Npsrv *srv;
static File *root;
static u64 qidpath;
static int blksize;

static char *Enoextension = "empty extension while creating special file";
static char *Enospace = "no space left";
//static char *E = "";

static Npfcall* ramfs_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname);
static int ramfs_clone(Npfid *fid, Npfid *newfid);
static int ramfs_walk(Npfid *fid, Npstr* wname, Npqid *wqid);
static Npfcall* ramfs_open(Npfid *fid, u8 mode);
static Npfcall* ramfs_create(Npfid *fid, Npstr *name, u32 perm, u8 mode, 
	Npstr *extension);
static Npfcall* ramfs_read(Npfid *fid, u64 offset, u32 count, Npreq *);
static Npfcall* ramfs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *);
static Npfcall* ramfs_clunk(Npfid *fid);
static Npfcall* ramfs_remove(Npfid *fid);
static Npfcall* ramfs_stat(Npfid *fid);
static Npfcall* ramfs_wstat(Npfid *fid, Npstat *stat);
static void ramfs_fiddestroy(Npfid *fid);

static void
file_incref0(File *f)
{
	f->refcount++;
}

static void
file_incref(File *f)
{
	if (!f)
		return;

	pthread_mutex_lock(&f->lock);
	file_incref0(f);
	pthread_mutex_unlock(&f->lock);
}

static int
file_decrefimpl(File *f, int lock)
{
	int ret;

	if (!f)
		return 0;

	if (lock)
		pthread_mutex_lock(&f->lock);

	ret = --f->refcount;
	if (!ret) {
		assert(f->dirents == NULL);
		free(f->name);
		free(f->extension);
		free(f->data);
		if (lock) 
			pthread_mutex_unlock(&f->lock);

		pthread_mutex_destroy(&f->lock);
		free(f);
	} else if (lock)
		pthread_mutex_unlock(&f->lock);

	return ret;
}

static int
file_decref0(File *f)
{
	return file_decrefimpl(f, 0);
}

static int
file_decref(File *f)
{
	return file_decrefimpl(f, 1);
}

static int
truncate_file(File *f, u64 size)
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

static File *
find_file(File *dir, char *name)
{
	File *ret;
	File *f;

	if (strcmp(name, "..") == 0)
		return dir->parent;

	ret = NULL;
	pthread_mutex_lock(&dir->lock);
	for(f = dir->dirents; f != NULL; f = f->next)
		if (strcmp(name, f->name) == 0) {
			ret = f;
			break;
		}
	pthread_mutex_unlock(&dir->lock);

	return ret;
}

static int
check_perm(File *dir, Npuser *user, int perm)
{
	int n;

	if (!user)
		return 0;

	if (!perm)
		return 1;

	n = dir->perm & 7;
	if (dir->uid == user) {
		n |= (dir->perm >> 6) & 7;
		n |= (dir->perm >> 3) & 7;
	}

	if (!(n & perm)) {
		np_werror(Eperm, EPERM);
		return 0;
	}

	return 1;
}

static File*
file_create(File *parent, char *name, int perm, Npuser *user)
{
	File *file;

	file = malloc(sizeof(*file));
	pthread_mutex_init(&file->lock, NULL);
	file->refcount = 0;
	file->name = name;
	file->perm = perm;
	file->length = 0;
	file->atime = time(NULL);
	file->mtime = time(NULL);
	file->uid = user;
	file->gid = user->dfltgroup;
	file->muid = user;
	file->extension = NULL;
	file->excl = 0;

	file->parent = parent;
	file->next = NULL;
	file->prev = NULL;
	file->dirents = NULL;
	file->dirlast = NULL;
	file->data = NULL;
	file->datasize = 0;
	truncate_file(file, 0);
	file->qid.type = perm >> 24;
	file->qid.version = 0;
	file->qid.path = qidpath++;

	return file;
}

static void
file2wstat(File *f, Npwstat *wstat)
{
	wstat->size = 0;
	wstat->type = 0;
	wstat->dev = 0;
	wstat->qid = f->qid;
	wstat->mode = f->perm;
	wstat->atime = f->atime;
	wstat->mtime = f->mtime;
	wstat->length = f->length;
	wstat->name = f->name;
	wstat->uid = f->uid->uname;
	wstat->gid = f->gid->gname;
	wstat->muid = f->muid->uname;
	wstat->extension = f->extension;
	wstat->n_uid = f->uid->uid;
	wstat->n_gid = f->gid->gid;
	wstat->n_muid = f->muid->uid;
}

static Fid*
fidalloc() {
	Fid *f;

	f = malloc(sizeof(*f));
	f->file = NULL;
	f->omode = -1;
	f->diroffset = 0;
	f->dirent = NULL;

	return f;
}

static void
ramfs_connclose(Npconn *conn)
{
	exit(0);
}

static void
ramfs_fiddestroy(Npfid *fid)
{
	Fid *f;

	f = fid->aux;
	if (!f)
		return;

	file_decref(f->file);
	file_decref(f->dirent);
	free(f);
}

static Npfcall*
ramfs_attach(Npfid *nfid, Npfid *nafid, Npstr *uname, Npstr *aname)
{
	Npfcall* ret;
	Fid *fid;
	Npuser *user;

	user = NULL;
	ret = NULL;

	if (nafid != NULL) {
		np_werror(Enoauth, EIO);
		return NULL;
	}

	if (!check_perm(root, user, 4)) 
		goto done;

	nfid->user = user;

	fid = fidalloc();
	fid->omode = -1;
	fid->omode = -1;
	fid->file = root;
	file_incref(root);
	nfid->aux = fid;
	np_fid_incref(nfid);

	ret = np_create_rattach(&fid->file->qid);
done:
	return ret;
}

static int
ramfs_clone(Npfid *fid, Npfid *newfid)
{
	Fid *f, *nf;

	f = fid->aux;
	nf = fidalloc();
	nf->file = f->file;
	file_incref(f->file);
	newfid->aux = nf;

	return 1;
}

static int
ramfs_walk(Npfid *fid, Npstr* wname, Npqid *wqid)
{
	char *name;
	Fid *f;
	File *file, *nfile;

	f = fid->aux;
	file = f->file;

	if (!check_perm(file, fid->user, 1))
		return 0;

	name = np_strdup(wname);
	nfile = find_file(file, name);
	free(name);
	if (!nfile) {
		np_werror(Enotfound, ENOENT);
		return 0;
	}

	file_incref(nfile);
	file_decref(file);
	f->file = nfile;

	*wqid = nfile->qid;
	return 1;
}

static Npfcall*
ramfs_open(Npfid *fid, u8 mode)
{
	int m;
	Fid *f;
	File *file;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	m = 0;
	switch (mode & 3) {
	case Oread:
		m = 4;
		break;

	case Owrite:
		m = 2;
		break;

	case Ordwr:
		m = 6;
		break;

	case Oexec:
		m = 1;
		break;
	}

	if (mode & Otrunc)
		m |= 2;

	file = f->file;
	pthread_mutex_lock(&file->lock);
	if (!check_perm(file, fid->user, m))
		goto done;

	if (file->perm & Dmdir) {
		f->diroffset = 0;
		f->dirent = file->dirents;
		file_incref(f->dirent);
	} else {
		if (mode & Otrunc)
			truncate_file(file, 0);
	}
	if (mode & Oexcl) {
		if (file->excl) {
			np_werror(Eopen, EPERM);
			goto done;
		}
		file->excl = 1;
	}

	f->omode = mode;
	ret = np_create_ropen(&file->qid, 0);

done:
	pthread_mutex_unlock(&file->lock);
	return ret;
}

static Npfcall*
ramfs_create(Npfid *fid, Npstr *name, u32 perm, u8 mode, Npstr *extension)
{
	int m;
	Fid *f;
	File *dir, *file, *nf;
	char *sname;

	sname = NULL;
	file = NULL;

	if (perm & Dmlink) {
		np_werror(Eperm, EPERM);
		goto error;
	}

	f = fid->aux;
	dir = f->file;
	sname = np_strdup(name);
	nf = find_file(dir, sname);
	if (nf) {
		np_werror(Eexist, EEXIST);
		goto error;
	}

	if (!strcmp(sname, ".") || !strcmp(sname, "..")) {
		np_werror(Eexist, EEXIST);
		goto error;
	}

	if (!check_perm(dir, fid->user, 2))
		goto error;

	if (perm & Dmdir)
		perm &= ~0777 | (dir->perm & 0777);
	else 
		perm &= ~0666 | (dir->perm & 0666);

	pthread_mutex_lock(&dir->lock);
	file = file_create(dir, sname, perm, dir->uid);
	file_incref(file);
	if (dir->dirlast) {
		dir->dirlast->next = file;
		file->prev = dir->dirlast;
	} else
		dir->dirents = file;

	dir->dirlast = file;
	dir->muid = fid->user;
	dir->mtime = time(NULL);
	dir->qid.version++;
	pthread_mutex_unlock(&dir->lock);

	/* we have to decref the dir because we remove it from the fid,
	   then we have to incref it because it has a new child file,
	   let's just skip playing with the ref */
	f->file = file;
	f->omode = mode;
	file_incref(file);

	if (perm&(Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice|Dmsocket)) {
		if (!fid->conn->dotu) {
			np_werror(Eperm, EPERM);
			goto error;
		}

		file->extension = np_strdup(extension);
	} else {
		m = 0;
		switch (mode & 3) {
		case Oread:
			m = 4;
			break;

		case Owrite:
			m = 2;
			break;

		case Ordwr:
			m = 6;
			break;

		case Oexec:
			m = 1;
			break;
		}

		if (mode & Otrunc)
			m |= 2;

		if (!check_perm(file, fid->user, m)) {
			file_decref(file);
			goto error;
		}

		if (mode & Oexcl)
			file->excl = 1;

		if (file->perm & Dmdir) {
			f->diroffset = 0;
			f->dirent = file->dirents;
			file_incref(f->dirent);
		}
	}

	return np_create_rcreate(&file->qid, 0);

error:
	return NULL;
}

static Npfcall*
ramfs_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	int i, n;
	Fid *f;
	File *file, *cf;
	Npfcall *ret;
	u8* buf;
	Npwstat wstat;

	f = fid->aux;
	buf = malloc(count);
	file = f->file;
	if (file->perm & Dmdir) {
		pthread_mutex_lock(&file->lock);
		if (offset == 0 && f->diroffset != 0) {
			file_decref(f->dirent);
			f->dirent = file->dirents;
			file_incref(f->dirent);
			f->diroffset = 0;
		}

		n = 0;
		cf = f->dirent;
		for(n = 0, cf = f->dirent; n<count && cf != NULL; cf = cf->next) {
			file2wstat(cf, &wstat);
			i = np_serialize_stat(&wstat, buf + n, count - n - 1,
				fid->conn->dotu);

			if (i==0)
				break;

			n += i;
		}

		f->diroffset += n;
		file_incref(cf);
		file_decref(f->dirent);
		f->dirent = cf;
		pthread_mutex_unlock(&file->lock);
	} else {
		n = count;
		if (file->length < offset+count)
			n = file->length - offset;

		if (n < 0)
			n = 0;

		memmove(buf, file->data + offset, n);
	}

	pthread_mutex_lock(&file->lock);
	file->atime = time(NULL);
	pthread_mutex_unlock(&file->lock);

	ret = np_create_rread(n, buf);
	free(buf);
	return ret;
}

static Npfcall*
ramfs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Fid *f;
	File *file;

	f = fid->aux;
	file = f->file;
	if (f->omode & Oappend)
		offset = file->length;

	if (file->length < offset+count) {
		pthread_mutex_lock(&file->lock);
		if (truncate_file(file, offset+count)) {
			pthread_mutex_unlock(&file->lock);
			np_werror(Enospace, ENOSPC);
			return NULL;
		}

		if (offset+count > file->datasize) {
			if (file->datasize - offset > 0)
				count = file->datasize - offset;
			else
				count = 0;
		}

		if (count) {
			if (file->length < offset)
				memset(file->data+file->length, 0, 
					offset - file->length);

			file->length = offset+count;
		}
		pthread_mutex_unlock(&file->lock);
	}

	if (count)
		memmove(file->data + offset, data, count);

	pthread_mutex_lock(&file->lock);
	file->mtime = time(NULL);
	file->atime = time(NULL);
	file->qid.version++;
	file->muid = fid->user;
	pthread_mutex_unlock(&file->lock);
	return np_create_rwrite(count);
}

static Npfcall*
ramfs_clunk(Npfid *fid)
{
	np_fid_decref(fid);
	return np_create_rclunk();
}

static Npfcall*
ramfs_remove(Npfid *fid)
{
	Fid *f;
	File *file;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	file = f->file;
	pthread_mutex_lock(&file->lock);
	if (file->perm&Dmdir && file->dirents) {
		pthread_mutex_unlock(&file->lock);
		np_werror(Enotempty, EIO);
		return NULL;
	}
	pthread_mutex_unlock(&file->lock);
		
	pthread_mutex_lock(&file->parent->lock);
	if (!check_perm(file->parent, fid->user, 2))
		goto done;

	if (file->parent->dirents == file)
		file->parent->dirents = file->next;
	else
		file->prev->next = file->next;

	if (file->next)
		file->next->prev = file->prev;

	if (file == file->parent->dirlast)
		file->parent->dirlast = file->prev;

	file->prev = NULL;
	file->next = NULL;

	file->parent->muid = fid->user;
	file->parent->mtime = time(NULL);
	file->parent->qid.version++;

	file_decref(file);
	file_decref0(file->parent);
	ret = np_create_rremove();
	np_fid_decref(fid);

done:
	pthread_mutex_unlock(&file->parent->lock);
	return ret;

}

static Npfcall*
ramfs_stat(Npfid *fid)
{
	Fid *f;
	Npwstat wstat;

	f = fid->aux;
	file2wstat(f->file, &wstat);
	return np_create_rstat(&wstat, fid->conn->dotu);
}

static Npfcall*
ramfs_wstat(Npfid *fid, Npstat *stat)
{
	int lockparent, lockfile;
	Fid *f;
	File *file, *nf;
	Npfcall *ret;
	char *sname, *oldname;
	u64 length, oldlength;
	u32 oldperm;
	u32 oldmtime;

	ret = NULL;
	oldlength = ~0;
	oldperm = ~0;
	oldmtime = ~0;
	oldname = NULL;

	f = fid->aux;
	file = f->file;
	if (file->perm&(Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice) && fid->conn->dotu) {
		np_werror(Eperm, EPERM);
		goto out;
	}

	pthread_mutex_lock(&file->lock);
	lockfile = 1;

	lockparent = stat->name.len != 0;
	if (lockparent)
		pthread_mutex_lock(&file->parent->lock);

	oldname = NULL;
	if (stat->name.len != 0) {
		if (!check_perm(file->parent, fid->user, 2))
			goto out;

		sname = np_strdup(&stat->name);
		nf = find_file(file->parent, sname);

		if (nf) {
			free(sname);
			np_werror(Eexist, EEXIST);
			goto out;
		}

		oldname = file->name;
		file->name = sname;
	}

	if (stat->length != (u64)~0) {
		if (!check_perm(file, fid->user, 2) || file->perm&Dmdir)
			goto out;

		oldlength = file->length;
		length = stat->length;
		if (truncate_file(file, length)) {
			np_werror(Enospace, ENOSPC);
			goto out;
		}

		if (length > file->datasize)
			length = file->datasize;

		if (file->length < length)
			memset(file->data+file->length, 0, length-file->length);

		file->length = length;
	}

	if (stat->mode != (u32)~0) {
		if (file->uid != fid->user) {
			np_werror(Eperm, EPERM);
			goto out;
		}

		oldperm = file->perm;
		file->perm = stat->mode;
	}

	if (stat->mtime != (u32)~0) {
		if (file->uid != fid->user) {
			np_werror(Eperm, EPERM);
			goto out;
		}

		oldmtime = file->mtime;
		file->mtime = stat->mtime;
	}

	ret = np_create_rwstat();
	
out:
	if (np_haserror()) {
		if (oldname) {
			free(file->name);
			file->name = oldname;
		}

		if (oldperm != ~0)
			file->perm = oldperm;

		if (oldmtime != ~0)
			file->mtime = oldmtime;

		if (oldlength != ~0) {
			file->length = oldlength;
			truncate_file(file, oldlength);
		}
	} else {
		free(oldname);
		if (stat->length != ~0) {
			truncate_file(file, file->length);
			memset(file->data + file->length, 0, file->datasize - file->length);
		}
	}

	if (lockfile)
		pthread_mutex_unlock(&file->lock);

	if (lockparent)
		pthread_mutex_unlock(&file->parent->lock);

	return ret;
}

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
	nwthreads = 4;
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

	root = file_create(NULL, strdup(""), ROOTPERM | Dmdir, user);
	file_incref(root);
	root->parent = root;

	srv = np_pipesrv_create(nwthreads);
	if (!srv)
		return -1;

	srv->dotu = 1;
	srv->connclose = ramfs_connclose;
	srv->attach = ramfs_attach;
	srv->clone = ramfs_clone;
	srv->walk = ramfs_walk;
	srv->open = ramfs_open;
	srv->create = ramfs_create;
	srv->read = ramfs_read;
	srv->write = ramfs_write;
	srv->clunk = ramfs_clunk;
	srv->remove = ramfs_remove;
	srv->stat = ramfs_stat;
	srv->wstat = ramfs_wstat;
	srv->fiddestroy = ramfs_fiddestroy;
	srv->debuglevel = debuglevel;

	if (optind >= argc)
		usage();

	if (np_pipesrv_mount(srv, argv[optind], user->uname, 0, opts))
		return -1;

	while (1) {
		sleep(100);
	}

	return 0;
}
