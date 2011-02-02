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
#include "9p.h"
#include "npfs.h"
#include "npfile.h"
#include "npfsimpl.h"

static void npfile_incref_nolock(Npfile *f);

Npfile*
npfile_alloc(Npfile *parent, char *name, u32 mode, u64 qpath,
	void *ops, void *aux)
{
	Npfile *f;

	f = malloc(sizeof(*f));
//	if (mode & P9_DMDIR) 
//		fprintf(stderr, "npfile_alloc %p %s\n", f, name);
	pthread_mutex_init(&f->lock, NULL);
	f->refcount = 0;
	f->parent = parent;
	f->qid.type = mode>>24;
	f->qid.version = 0;
	f->qid.path = qpath;
	f->mode = mode;
	f->atime = 0;
	f->mtime = 0;
	f->length = 0;
	f->name = strdup(name);
	f->uid = NULL;
	f->gid = NULL;
	f->muid = NULL;
	f->excl = 0;
	f->extension = NULL;
	f->ops = ops;
	f->aux = aux;
	f->next = NULL;
	f->prev = NULL;
	f->dirfirst = NULL;
	f->dirlast = NULL;

	if (parent) {
		npfile_incref_nolock(parent);
		f->atime = parent->atime;
		f->mtime = parent->mtime;
		f->uid = parent->uid;
		f->gid = parent->gid;
		f->muid = f->uid;
	}

	return f;
}

static void
npfile_incref_nolock(Npfile *f)
{
//	if (f->mode & P9_DMDIR)
//		fprintf(stderr, "npfile_incref %p %d callers %p, %p\n", f, f->refcount+1, __builtin_return_address(1),
//			__builtin_return_address(2));
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

//	if (f->mode & P9_DMDIR)
//		fprintf(stderr, "npfile_decrefimpl %p %d callers %p, %p\n", f, f->refcount-1, __builtin_return_address(1),
//			__builtin_return_address(2));


//	assert(f->refcount > 0);
	if (f->refcount <= 0)
		*(char *) 0 = 3;
	ret = --f->refcount;
	if (!ret) {
		if (f->ops) {
			if (f->mode & P9_DMDIR) {
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
		free(f->extension);
		free(f);
	} else
		pthread_mutex_unlock(&f->lock);

	return ret;
}

static int
npfile_decref_unlock(Npfile *f)
{
	return npfile_decrefimpl(f, 0);
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
		if (file->mode & P9_DMDIR) {
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
		if (file->mode & P9_DMDIR) {
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
		np_werror(Eperm, EPERM);
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

static int
check_perm(u32 fperm, Npuser *fuid, Npgroup *fgid, Npuser *user, u32 perm)
{
	if (!user)
		goto error;

	perm &= 7;
	if (!perm)
		return 1;

	if ((fperm&7) & perm)
		return 1;

	if (fuid==user && ((fperm>>6)&7) & perm)
		return 1;

	if ((((fperm>>3)&7) & perm) && user->upool->ismember != NULL)
		if(user->upool->ismember(user->upool, user, fgid))
			return 1;

error:
	np_werror(Eperm, EPERM);
	return 0;
}

static void
file2wstat(Npfile *file, Npwstat *wstat)
{
	wstat->size = 0;
	wstat->type = 0;
	wstat->dev = 0;
	wstat->qid = file->qid;
	wstat->mode = file->mode;
	wstat->atime = file->atime;
	wstat->mtime = file->mtime;
	wstat->length = file->length;
	wstat->name = file->name;
	wstat->uid = file->uid->uname;
	wstat->gid = file->gid->gname;
	wstat->muid = file->muid->uname;
	assert (wstat->muid != NULL);
	wstat->extension = file->extension;
	wstat->n_uid = file->uid->uid;
	wstat->n_gid = file->gid->gid;
	wstat->n_muid = file->muid->uid;
}

static void
blank_stat(Npstat *stat)
{
	stat->size = 0;
	stat->type = ~0;
	stat->dev = ~0;
	stat->qid.type = ~0;
	stat->qid.version = ~0;
	stat->qid.path = ~0;
	stat->mode = ~0;
	stat->atime = ~0;
	stat->mtime = ~0;
	stat->length = ~0;
	stat->name.len = 0;
	stat->uid.len = 0;
	stat->gid.len = 0;
	stat->muid.len = 0;
	stat->extension.len = 0;
	stat->n_uid = ~0;
	stat->n_gid = ~0;
	stat->n_muid = ~0;
}

int
npfile_checkperm(Npfile *file, Npuser *user, int perm)
{
	return check_perm(file->mode, file->uid, file->gid, user, perm);
}

static void
npfile_modified(Npfile *f, Npuser *u)
{
	// you better have the file locked ...
	f->muid = u;
	f->mtime = time(NULL);
	f->atime = f->mtime;
	f->qid.version++;
}

Npfilefid*
npfile_fidalloc(Npfile *file, Npfid *fid) {
	Npfilefid *f;

	f = malloc(sizeof(*f));
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

	return f;
}

void
npfile_fiddestroy(Npfid *fid)
{
	Npfilefid *f;
	Npfile *file;
	Npfileops *fops;

//	if (fid->conn->srv->debuglevel)
//		fprintf(stderr, "destroy fid %d\n", fid->fid);

	f = fid->aux;
	if (!f)
		return;

	file = f->file;
	if (f->omode != ~0) {
		if (!(file->mode & P9_DMDIR)) {
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
	Npfile *root;
	Npfilefid *f;

	root = (Npfile*) fid->conn->srv->treeaux;
	if(!npfile_checkperm(root, fid->user, 4))
		return NULL;

	f = npfile_fidalloc(root, fid);
	if(!f)
		return NULL;

	fid->aux = f;
	np_fid_incref(fid);

	return np_create_rattach(&root->qid);
}

int
npfile_clone(Npfid *fid, Npfid *newfid)
{
	Npfilefid *f, *nf;

	f = fid->aux;
	nf = npfile_fidalloc(f->file, newfid);
	newfid->aux = nf;

	return 1;
}

int
npfile_walk(Npfid *fid, Npstr *wname, Npqid *wqid)
{
	Npfilefid *f;
	Npfile *dir, *nfile;
	char *name;

	f = fid->aux;
	dir = f->file;

	if (!npfile_checkperm(dir, fid->user, 1))
		return 0;

	name = np_strdup(wname);
	nfile = npfile_find(dir, name);
	free(name);
	if (nfile) {
		npfile_unref(dir, f);
		f->file = nfile;
		npfile_decref(dir);
		npfile_ref(nfile, f);

		*wqid = nfile->qid;
	} else if (!np_haserror())
		np_werror(Enotfound, ENOENT);
		
	return nfile != NULL;
}

static int
mode2perm(int mode)
{
	int m;

	m = 0;
	switch (mode & 3) {
	case P9_OREAD:
		m = 4;
		break;

	case P9_OWRITE:
		m = 2;
		break;

	case P9_ORDWR:
		m = 6;
		break;

	case P9_OEXEC:
		m = 1;
		break;
	}

	if (mode & P9_OTRUNC)
		m |= 2;

	return m;
}

Npfcall*
npfile_open(Npfid *fid, u8 mode)
{
	int m;
	Npfilefid *f;
	Npfile *file;
	Npfcall *ret;
	Npfileops *fops;
	Npstat stat;

	ret = NULL;
	f = fid->aux;
	file = f->file;
	m = mode2perm(mode);
	pthread_mutex_lock(&file->lock);
	if (!npfile_checkperm(file, fid->user, m)) {
		pthread_mutex_unlock(&file->lock);
		return NULL;
	}

	if (mode & P9_OEXCL) {
		if (file->excl) {
			np_werror(Eopen, EPERM);
			pthread_mutex_unlock(&file->lock);
			return NULL;
		}

		file->excl = 1;
	}
	pthread_mutex_unlock(&file->lock);

	if (file->mode & P9_DMDIR) {
		f->diroffset = 0;
		f->dirent = NULL;
	} else {
		fops = file->ops;

		if (mode & P9_OTRUNC) {
			if (!fops->wstat) {
				np_werror(Eperm, EPERM);
				goto done;
			}

			blank_stat(&stat);
			stat.length = 0;
			if (!(*fops->wstat)(file, &stat))
				goto done;
		}

		if (fops->openfid && !(*fops->openfid)(f))
			goto done;
	}

	f->omode = mode;
	ret = np_create_ropen(&file->qid, 0);

done:
	if (!ret && mode & P9_OEXCL) {
		pthread_mutex_lock(&file->lock);
		file->excl = 1;
		pthread_mutex_unlock(&file->lock);
	}

	return ret;

}

Npfcall*
npfile_create(Npfid *fid, Npstr* name, u32 perm, u8 mode, Npstr* extension)
{
	int m;
	Npfilefid *f;
	Npfile *dir, *file, *nf;
	Npdirops *dops;
	Npfileops *fops;
	char *sname, *sext;
	Npfcall *ret;

	ret = NULL;
	sname = NULL;
	file = NULL;

	f = fid->aux;
	dir = f->file;
	sname = np_strdup(name);
	sext = np_strdup(extension);
	nf = npfile_find(dir, sname);
	if (np_haserror())
		goto done;
	else if (nf) {
		np_werror(Eexist, EEXIST);
		goto done;
	}

	if (!strcmp(sname, ".") || !strcmp(sname, "..")) {
		np_werror(Eexist, EEXIST);
		goto done;
	}

	if (!npfile_checkperm(dir, fid->user, 2))
		goto done;

	if (perm & P9_DMSYMLINK)
		perm |= 0777;

	if (perm & P9_DMDIR)
		perm &= ~0777 | (dir->mode & 0777);
	else 
		perm &= ~0666 | (dir->mode & 0666);

	m = mode2perm(mode);
	if (!check_perm(perm, fid->user, dir->gid, fid->user, m))
		goto done;

	pthread_mutex_lock(&dir->lock);
	dops = dir->ops;
	if (!dops->create) {
		np_werror(Eperm, EPERM);
		pthread_mutex_unlock(&dir->lock);
		goto done;
	}

	file = (*dops->create)(dir, sname, perm, fid->user, dir->gid, sext);
	if (!file) {
		pthread_mutex_unlock(&dir->lock);
		goto done;
	}

	pthread_mutex_lock(&file->lock);
	npfile_modified(dir, fid->user);
	pthread_mutex_unlock(&file->lock);
	pthread_mutex_unlock(&dir->lock);
	f->file = file;
	f->omode = mode;

	if (mode & P9_OEXCL)
		file->excl = 1;

	if (file->mode & P9_DMDIR) {
		f->diroffset = 0;
		f->dirent = NULL;
	} else {
		fops = file->ops;
		if (fops->openfid)
			(*fops->openfid)(f);
	}

	f->omode = mode;
	ret = np_create_rcreate(&file->qid, 0);

done:
	free(sname);
	free(sext);
	return ret;
}

Npfcall*
npfile_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	int i, n;
	Npfilefid *f;
	Npfile *file, *cf, *cf1;
	Npdirops *dops;
	Npfileops *fops;
	Npfcall *ret;
	Npwstat wstat;

	ret = NULL;
	f = fid->aux;
	ret = np_alloc_rread(count);
	file = f->file;
	if (file->mode & P9_DMDIR) {
		pthread_mutex_lock(&file->lock);
		dops = file->ops;
		if (!dops->first || !dops->next) {
			np_werror(Eperm, EPERM);
			pthread_mutex_unlock(&file->lock);
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
			file2wstat(cf, &wstat);
			i = np_serialize_stat(&wstat, ret->data + n,
				count - n - 1, np_conn_extend(fid->conn));
			if (i==0)
				break;

			n += i;
			cf1 = (dops->next)(file, cf);
			npfile_decref(cf);
			cf = cf1;
		}

		f->diroffset += n;
		f->dirent = cf;
		file->atime = time(NULL);
		pthread_mutex_unlock(&file->lock);
	} else {
		fops = file->ops;
		if (!fops->read) {
			np_werror(Eperm, EPERM);
			goto done;
		}
		n = (*fops->read)(f, offset, count, ret->data, req);
		if (n < 0) {
			free(ret);
			ret = NULL;
		}

		pthread_mutex_lock(&file->lock);
		file->atime = time(NULL);
		pthread_mutex_unlock(&file->lock);
	}

	if (ret)
		np_set_rread_count(ret, n);

done:
	return ret;
}

Npfcall*
npfile_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	Npfcall *ret;
	Npfilefid *f;
	Npfile *file;
	Npfileops *fops;
	//int ecode;
	//char *ename;

	ret = NULL;
	f = fid->aux;
	file = f->file;
	if (f->omode & P9_OAPPEND)
		offset = file->length;

	fops = file->ops;
	if (!fops->write) {
		np_werror(Eperm, EPERM);
		goto done;
	}

	n = (*fops->write)(f, offset, count, data, req);
#if 0 	/* FIXME npfile_modified () causes muid to reference the current user,
	 * but doesn't manage the user refcount, so user can be freed leaving
	 * muid dangling.  Commenting out for now - muid will just be static.
	 */
	np_rerror(&ename, &ecode);
	if (!ename || n<0) {
		pthread_mutex_lock(&file->lock);
		npfile_modified(file, fid->user);
		pthread_mutex_unlock(&file->lock);
	}
#endif
	if (n >= 0)
		ret = np_create_rwrite(n);

done:
	return ret;
}

Npfcall*
npfile_clunk(Npfid *fid)
{
//	np_fid_decref(fid);
	return np_create_rclunk();
}

Npfcall*
npfile_remove(Npfid *fid)
{
	Npfilefid *f;
	Npfile *file, *cf, *parent;
	Npfcall *ret;
	Npdirops *dops;

	ret = NULL;
	f = fid->aux;
	file = f->file;
	pthread_mutex_lock(&file->lock);
	if (file->mode & P9_DMDIR) {
		dops = file->ops;
		if (!dops->first) {
			np_werror(Eperm, EPERM);
			goto done;
		}

		cf = (*dops->first)(file);
		if (cf) {
			npfile_decref(cf);
			pthread_mutex_unlock(&file->lock);
			np_werror(Enotempty, EIO);
			goto done;
		}
	}
	pthread_mutex_unlock(&file->lock);

	parent = file->parent;
	pthread_mutex_lock(&parent->lock);
	if (!npfile_checkperm(parent, fid->user, 2)) {
		pthread_mutex_unlock(&parent->lock);
		return NULL;
	}

	dops = parent->ops;
	if (!dops->remove) {
		np_werror(Eperm, EPERM);
		pthread_mutex_unlock(&parent->lock);
		goto done;
	}

	if ((*dops->remove)(parent, file)) {
		npfile_modified(parent, fid->user);
		npfile_decref(file);
//		np_fid_decref(fid);
		npfile_decref_unlock(parent);
		ret = np_create_rremove();
	} else
		pthread_mutex_unlock(&parent->lock);

done:
	return ret;
}

Npfcall*
npfile_stat(Npfid *fid)
{
	Npfilefid *f;
	Npfile *file;
	Npwstat wstat;

	f = fid->aux;
	file = f->file;
	pthread_mutex_lock(&file->lock);
	file2wstat(file, &wstat);
	pthread_mutex_unlock(&file->lock);

	return np_create_rstat(&wstat, np_conn_extend(fid->conn));
}

Npfcall*
npfile_wstat(Npfid *fid, Npstat *stat)
{
	int n;
	Npfilefid *f;
	Npfile *file;
	Npfileops *fops;
	Npdirops *dops;
	Npfcall *ret;

	ret = NULL;
	f = fid->aux;
	file = f->file;

	pthread_mutex_lock(&file->lock);
	if (stat->name.len!=0 && !npfile_checkperm(file->parent, fid->user, 2))
		goto done;

	if (stat->length!=(u64)~0 && !npfile_checkperm(file, fid->user, 2))
		goto done;

	if (stat->mode!=(u32)~0 && file->uid!=fid->user) {
		np_werror(Eperm, EPERM);
		goto done;
	}

#if 0	/* we have already checked for write permission so allow this */
	if (stat->mtime!=(u32)~0 && file->uid!=fid->user) {
		np_werror(Eperm, EPERM);
		goto done;
	}
#endif

	if (file->mode & P9_DMDIR) {
		dops = file->ops;
		if (!dops->wstat) {
			np_werror(Eperm, EPERM);
			goto done;
		}
		n = (*dops->wstat)(file, stat);
	} else {
		fops = file->ops;
		if (!fops->wstat) {
			np_werror(Eperm, EPERM);
			goto done;
		}
		n = (*fops->wstat)(file, stat);
	}

	if (!n)
		goto done;

	ret = np_create_rwstat();

done:
	pthread_mutex_unlock(&file->lock);
	return ret;
}

void
npfile_init_srv(Npsrv *srv, Npfile *root)
{
	srv->proto_version = p9_proto_legacy;
	srv->attach = npfile_attach;
	srv->clone = npfile_clone;
	srv->walk = npfile_walk;
	srv->open = npfile_open;
	srv->create = npfile_create;
	srv->read = npfile_read;
	srv->write = npfile_write;
	srv->clunk = npfile_clunk;
	srv->remove = npfile_remove;
	srv->stat = npfile_stat;
	srv->wstat = npfile_wstat;
	srv->fiddestroy = npfile_fiddestroy;
	srv->treeaux = root;
	if (srv->msize > INT_MAX)
		srv->msize = INT_MAX;
}
