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

/* dumbed down in the course of moving to 9p2000.L -jg 
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <dirent.h>
#include "9p.h"
#include "npfs.h"
#include "npfile.h"
#include "npfsimpl.h"

static void npfile_incref_nolock(Npfile *f);

Npfile*
npfile_alloc(Npfile *parent, char *name, mode_t mode, ino_t inum,
	void *ops, void *aux)
{
	Npfile *f;

	/* limit modes until richer permission checking is implemented */
	assert(S_ISREG(mode) || S_ISDIR(mode));
	if (S_ISDIR(mode)) {
		assert((mode & 0777) == 0555);
	} else {
		assert((mode & 0777) == 0444 || (mode & 0777) == 0666);
	}

	if (!(f = malloc(sizeof(*f))))
		goto done;
	if (!(f->name = strdup(name))) {
		free (f);
		f = NULL;
		goto done;
	}
	pthread_mutex_init(&f->lock, NULL);
	f->refcount = 0;
	f->parent = parent;
	f->qid.path = inum;
	f->qid.version = 0;
	f->qid.type = S_ISDIR(mode) ?  P9_QTDIR : P9_QTFILE;
	f->stat.st_mode = mode;
	f->stat.st_uid = 0;
	f->stat.st_gid = 0;
	f->stat.st_nlink = 1;
	f->stat.st_rdev = 0;
	f->stat.st_size = 0;
	f->stat.st_blksize = 512;
	f->stat.st_blocks = 0;
	f->stat.st_atim.tv_sec = f->stat.st_atim.tv_nsec = 0;
	f->stat.st_mtim.tv_sec = f->stat.st_mtim.tv_nsec = 0;
	f->stat.st_ctim.tv_sec = f->stat.st_ctim.tv_nsec = 0;
	f->ops = ops;
	f->aux = aux;
	f->next = NULL;
	f->prev = NULL;
	f->dirfirst = NULL;
	f->dirlast = NULL;

	if (parent) {
		npfile_incref_nolock(parent);
		f->stat.st_atim.tv_sec = parent->stat.st_atim.tv_sec;
		f->stat.st_atim.tv_nsec = parent->stat.st_atim.tv_nsec;
		f->stat.st_mtim.tv_sec = parent->stat.st_mtim.tv_sec;
		f->stat.st_mtim.tv_nsec = parent->stat.st_mtim.tv_nsec;
		f->stat.st_uid = parent->stat.st_uid;
		f->stat.st_gid = parent->stat.st_gid;
	}
done:
	return f;
}

static void
npfile_incref_nolock(Npfile *f)
{
	assert(f->refcount >= 0);
	f->refcount++;
}

void
npfile_incref(Npfile *f)
{
	if (!f)
		return;
	pthread_mutex_lock(&f->lock);
	npfile_incref_nolock(f);
	pthread_mutex_unlock(&f->lock);
}

static int
npfile_decrefimpl(Npfile *f, int lock)
{
	int ret;
	Npfileops *fops;
	Npdirops *dops;

	if (!f)
		return 0;
	if (lock)
		pthread_mutex_lock(&f->lock);
	if (f->refcount <= 0)
		*(char *) 0 = 3;
	ret = --f->refcount;
	if (!ret) {
		if (f->ops) {
			if (S_ISDIR(f->stat.st_mode)) {
				dops = f->ops;
				if (dops->destroy)
					(*dops->destroy)(f);
			} else {
				fops = f->ops;
				if (fops->destroy)
					(*fops->destroy)(f);
			}
		}
		pthread_mutex_unlock(&f->lock);
		pthread_mutex_destroy(&f->lock);
		free(f->name);
		free(f);
	} else
		pthread_mutex_unlock(&f->lock);

	return ret;
}

int
npfile_decref(Npfile *f)
{
	return npfile_decrefimpl(f, 1);
}

static void
npfile_ref(Npfile *file, Npfilefid *fid)
{
	Npfileops *fops;
	Npdirops *dops;

	if (file->ops) {
		if (S_ISDIR(file->stat.st_mode)) {
			dops = file->ops;
			if (dops->ref)
				(*dops->ref)(file, fid);
		} else {
			fops = file->ops;
			if (fops->ref)
				(*fops->ref)(file, fid);
		}
	}
}

static void
npfile_unref(Npfile *file, Npfilefid *fid)
{
	Npfileops *fops;
	Npdirops *dops;

	if (file->ops) {
		if (S_ISDIR(file->stat.st_mode)) {
			dops = file->ops;
			if (dops->unref)
				(*dops->unref)(file, fid);
		} else {
			fops = file->ops;
			if (fops->unref)
				(*fops->unref)(file, fid);
		}
	}
}

Npfile *
npfile_find(Npfile *dir, char *name)
{
	Npfile *f;
	Npdirops *dops;

	if (strcmp(name, "..") == 0)
		return dir->parent;

	pthread_mutex_lock(&dir->lock);
	dops = dir->ops;
	if (!dops->first || !dops->next) {
		np_uerror(EPERM);
		pthread_mutex_unlock(&dir->lock);
		return NULL;
	}

	for(f = (*dops->first)(dir); f != NULL; 
		f = (*dops->next)(dir, f)) {

		if (strcmp(name, f->name) == 0)
			break;
		npfile_decref(f);
	}
	pthread_mutex_unlock(&dir->lock);

	return f;
}

static void
npfile_modified(Npfile *f, Npuser *u)
{
	// you better have the file locked ...
	f->stat.st_mtim.tv_sec = time(NULL);
	f->stat.st_mtim.tv_nsec = 0;
	f->qid.version++;
}

static void
npfile_accessed(Npfile *f, Npuser *u)
{
	// you better have the file locked ...
	f->stat.st_atim.tv_sec = time(NULL);
	f->stat.st_atim.tv_nsec = 0;
}

Npfilefid*
npfile_fidalloc(Npfile *file, Npfid *fid) {
	Npfilefid *f;

	if ((f = malloc(sizeof(*f))) == NULL)
		goto done;
	pthread_mutex_init(&f->lock, NULL);
	f->omode = ~0;
	f->fid = fid;
	/* aux, diroffset and dirent can be non-zero only for open fids */
	f->aux = 0;
	f->diroffset = 0;
	f->dirent = NULL;
	f->file = file;
	npfile_incref(f->file);
	npfile_ref(file, f);
done:
	return f;
}

void
npfile_fiddestroy(Npfid *fid)
{
	Npfilefid *f = fid->aux;
	Npfile *file;
	Npfileops *fops;

	if (!f)
		return;
	file = f->file;
	if (f->omode != ~0) {
		if (!S_ISDIR(file->stat.st_mode)) {
			fops = file->ops;
			if (fops->closefid)
				(*fops->closefid)(f);
		}
		if (f->dirent)
			npfile_decref(f->dirent);
	}

	npfile_unref(file, f);
	npfile_decref(file);
	free(f);
}

Npfcall*
npfile_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname)
{
	Npfile *root = (Npfile*) fid->conn->srv->treeaux;
	Npfilefid *f;
	Npfcall *ret = NULL;

	if (!(f = npfile_fidalloc(root, fid))) {
		np_uerror(ENOMEM);
		goto done;
	}
	fid->aux = f;
	np_fid_incref(fid);
	if (!(ret = np_create_rattach(&root->qid))) {
		np_uerror(ENOMEM);
		goto done;
	}
done:
	return ret;
}

int
npfile_clone(Npfid *fid, Npfid *newfid)
{
	Npfilefid *f = fid->aux;
	Npfilefid *nf;
	int ret = 0;

	if (!(nf = npfile_fidalloc(f->file, newfid))) {
		np_uerror(ENOMEM);
		goto done;
	}
	newfid->aux = nf;
	ret = 1;
done:
	return ret;
}

int
npfile_walk(Npfid *fid, Npstr *wname, Npqid *wqid)
{
	Npfilefid *f = fid->aux;
	Npfile *dir = f->file;
	Npfile *nfile = NULL;
	char *name = NULL;

	if (!(name = np_strdup(wname))) {
		np_uerror(ENOMEM);
		goto done;
	}
	nfile = npfile_find(dir, name);
	if (!nfile) {
		np_uerror(ENOENT);
		goto done;
	}
	npfile_unref(dir, f);
	f->file = nfile;
	npfile_decref(dir);
	npfile_ref(nfile, f);

	*wqid = nfile->qid;
done:		
	if (name)
		free(name);
	return (nfile != NULL);
}

int
npfile_checkperm(Npfile *file, Npuser *user, u32 mode)
{
	/* FIXME: need to check ug against uid/gid */
	switch (mode & O_ACCMODE) {
		case O_RDONLY:
			if (!(file->stat.st_mode & S_IROTH))
				return 0;
			break;
		case O_WRONLY:
			if (!(file->stat.st_mode & S_IWOTH))
				return 0;
			break;
		case O_RDWR:
			if (!(file->stat.st_mode & S_IROTH))
				return 0;
			if (!(file->stat.st_mode & S_IWOTH))
				return 0;
			break;
	}
	return 1;
}

Npfcall*
npfile_lopen(Npfid *fid, u32 mode)
{
	Npfilefid *f = fid->aux;
	Npfile *file = f->file;
	Npfileops *fops;
	Npfcall *ret = NULL;

	pthread_mutex_lock(&file->lock);
	fops = file->ops;
	if (!npfile_checkperm(file, fid->user, mode)) {
		pthread_mutex_unlock(&file->lock);
		np_uerror(EPERM);
		goto done;
	}
	pthread_mutex_unlock(&file->lock);
	if (S_ISDIR(file->stat.st_mode)) {
		f->diroffset = 0;
		f->dirent = NULL;
	} else if (fops->openfid) {
		if (!(*fops->openfid)(f))
			goto done;
	}
	f->omode = mode;
	if (!(ret = np_create_ropen(&file->qid, 0))) {
		np_uerror(ENOMEM);
		goto done;
	}
done:
	return ret;
}

Npfcall*
npfile_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	int i, n;
	Npfilefid *f = fid->aux;
	Npfile *file = f->file;
	Npfile *cf, *cf1;
	Npdirops *dops;
	Npfcall *ret;

	pthread_mutex_lock(&file->lock);
	if (!(ret = np_create_rreaddir(count))) {
		np_uerror(ENOMEM);
		goto done;
	}
	dops = file->ops;
	if (!dops->first || !dops->next) {
		np_uerror(EPERM);
		goto done;
	}
	if (offset == 0) {
		if (f->dirent)
			npfile_decref(f->dirent);
		f->dirent = (*dops->first)(file);
		f->diroffset = 0;
	}
	n = 0;
	cf = f->dirent;
	while (n<count && cf!=NULL) {
		i = np_serialize_p9dirent(&cf->qid, f->diroffset + n,
			S_ISDIR(cf->stat.st_mode) ? DT_DIR : DT_REG, cf->name,
			ret->u.rreaddir.data+n, count-n-1);
		if (i==0)
			break;

		n += i;
		cf1 = (dops->next)(file, cf);
		npfile_decref(cf);
		cf = cf1;
	}

	f->diroffset += n;
	f->dirent = cf;
	npfile_accessed(file, fid->user);

	if (ret)
		np_finalize_rreaddir(ret, n);

done:
	pthread_mutex_unlock(&file->lock);
	return ret;
}

Npfcall*
npfile_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	Npfilefid *f = fid->aux;
	Npfile *file = f->file;
	Npfileops *fops;
	Npfcall *ret = NULL;
	int n;

	fops = file->ops;
	if (S_ISDIR(file->stat.st_mode)) {
		np_uerror(EPERM);
		goto done;
	}
	if ((f->omode & O_ACCMODE) == O_WRONLY) {
		np_uerror(EPERM);
		goto done;
	}
	if (!fops->read) {
		np_uerror(EPERM);
		goto done;
	}
	if (!(ret = np_alloc_rread(count))) {
		np_uerror(ENOMEM);
		goto done;
	}
	n = (*fops->read)(f, offset, count, ret->data, req);
	if (n < 0) {
		free(ret);
		ret = NULL;
		goto done;
	}
	pthread_mutex_lock(&file->lock);
	npfile_accessed(file, fid->user);
	pthread_mutex_unlock(&file->lock);

	np_set_rread_count(ret, n);
done:
	return ret;
}

Npfcall*
npfile_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Npfilefid *f = fid->aux;
	Npfile *file = f->file;
	Npfileops *fops;
	Npfcall *ret = NULL;
	int n;

	fops = file->ops;
	if (S_ISDIR(file->stat.st_mode)) {
		np_uerror(EPERM);
		goto done;
	}
	if ((f->omode & O_ACCMODE) == O_RDONLY) {
		np_uerror(EPERM);
		goto done;
	}
	if (!fops->write) {
		np_uerror(EPERM);
		goto done;
	}
	if (f->omode & O_APPEND)
		offset = file->stat.st_size;
	n = (*fops->write)(f, offset, count, data, req);
	if (n < 0)
		goto done;
	pthread_mutex_lock(&file->lock);
	npfile_modified(file, fid->user);
	pthread_mutex_unlock(&file->lock);

	if (!(ret = np_create_rwrite(n))) {
		np_uerror (ENOMEM);
		goto done;
	}
done:
	return ret;
}

Npfcall*
npfile_clunk(Npfid *fid)
{
	Npfcall *ret = NULL;
	
	if (!(ret = np_create_rclunk()))
		np_uerror (ENOMEM);
	return ret;
}

Npfcall*
npfile_getattr(Npfid *fid, u64 request_mask)
{
	Npfilefid *f = fid->aux;
	Npfile *file = f->file;
	u64 valid = request_mask;
	Npfcall *ret = NULL;

	pthread_mutex_lock(&file->lock);
	if (!(ret = np_create_rgetattr(valid,
					&file->qid,
					file->stat.st_mode,
            				file->stat.st_uid,
					file->stat.st_gid,
					file->stat.st_nlink,
					file->stat.st_rdev,
					file->stat.st_size,
					file->stat.st_blksize,
					file->stat.st_blocks,
					file->stat.st_atim.tv_sec,
					file->stat.st_atim.tv_nsec,
					file->stat.st_mtim.tv_sec,
					file->stat.st_mtim.tv_nsec,
					file->stat.st_ctim.tv_sec,
					file->stat.st_ctim.tv_nsec,
					0, 0, 0, 0))) {
        	np_uerror (ENOMEM);
		goto done;
	}
done:
	pthread_mutex_unlock(&file->lock);
	return ret;
}

/* This is mostly a no-op with the purpose of alowing mtime
 * update on a writeable file to succeed (without effect).
 */
Npfcall*
npfile_setattr (Npfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
                u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec)
{
	Npfilefid *f = fid->aux;
	Npfile *file = f->file;
	Npfcall *ret = NULL;

	pthread_mutex_lock(&file->lock);
	if (S_ISDIR(file->stat.st_mode)) {
		np_uerror(EPERM);
		goto done;
	}
	if (!(ret = np_create_rsetattr())) {
		np_uerror(ENOMEM);
		goto done;
	}
done:
	pthread_mutex_unlock(&file->lock);
	return ret;
}

void
npfile_init_srv(Npsrv *srv, Npfile *root)
{
	srv->attach = npfile_attach;
	srv->clone = npfile_clone;
	srv->walk = npfile_walk;
	srv->lopen = npfile_lopen;
	srv->read = npfile_read;
	srv->readdir = npfile_readdir;
	srv->write = npfile_write;
	srv->clunk = npfile_clunk;
	srv->getattr = npfile_getattr;
	srv->setattr = npfile_setattr;
	srv->fiddestroy = npfile_fiddestroy;
	srv->treeaux = root;
	if (srv->msize > INT_MAX)
		srv->msize = INT_MAX;
}
