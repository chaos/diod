/*****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 *  Written by Jim Garlick <garlick@llnl.gov> LLNL-CODE-423279
 *  All Rights Reserved.
 *
 *  This file is part of the Distributed I/O Daemon (diod).
 *  For details, see <http://code.google.com/p/diod/>.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License (as published by the
 *  Free Software Foundation) version 2, dated June 1991.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY
 *  or FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA or see
 *  <http://www.gnu.org/licenses/>.
 *****************************************************************************/

/* syn.c - handle simple synthetic files for stats tools, etc */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/time.h>
#include <dirent.h>

#include "9p.h"
#include "npfs.h"
#include "npfsimpl.h"

typedef struct {
	Npfile	*file;
	void	*data;
} Fid;

static int
_next_inum (void)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	static int i = 1;
	int ret = -1, err;

	if ((err = pthread_mutex_lock (&lock))) {
		np_uerror (err);
		goto done;
	}
	ret = i++;
	if ((err = pthread_mutex_unlock (&lock))) {
		np_uerror (err);
		ret = -1;
		goto done;
	}
done:
	return ret;
}

static void
_free_fid (Fid *f)
{
	if (f) {
		if (f->data)
			free(f->data);
		free (f);
	}
}

static Fid *
_alloc_fid (Npfile *file)
{
	Fid *f = NULL;

	if (!(f = malloc (sizeof (*f)))) {
		np_uerror (ENOMEM);
		return NULL;
	}
	memset (f, 0, sizeof (*f));
	f->file = file;
	return f;
}

void
np_syn_delfile (Npfile *file)
{
	Npfile *ff, *tmp;

	if (file) {
		for (ff = file->child; ff != NULL; ) {
			tmp = ff->next; 
			np_syn_delfile (ff);
			ff = tmp;
		}
		if (file->name)
			free (file->name);
		free (file);
	}	
}

/* this is not that critical so we ignore errors */
typedef enum {ATIME, CTIME, MTIME} whichtime_t;
static void
_update_time (Npfile *file, whichtime_t wt)
{
	switch (wt) {
	case ATIME:
		(void)gettimeofday (&file->atime, NULL);
		break;
	case MTIME:
		(void)gettimeofday (&file->mtime, NULL);
		break;
	case CTIME:
		(void)gettimeofday (&file->ctime, NULL);
		break;
	}
}

typedef enum {DIRMODE, FILEMODE} whichmode_t;
static void
_update_mode (Npfile *file, whichmode_t wm)
{
	switch (wm) {
	case DIRMODE:
		file->mode = S_IFDIR;
		file->mode |= S_IRUSR | S_IRGRP | S_IROTH;
		file->mode |= S_IXUSR | S_IXGRP | S_IXOTH;
		break;
	case FILEMODE:
		file->mode = S_IFREG;
		file->mode |= S_IRUSR | S_IRGRP | S_IROTH;
		break;
	}
}

static Npfile *
_alloc_file (char *name, u8 type)
{
	Npfile *file = NULL;

	if (!(file = malloc (sizeof (*file)))) {
		np_uerror (ENOMEM);
		goto error;
	}
	memset (file, 0, sizeof (*file));
	if (!(file->name = strdup (name))) {
		np_uerror (ENOMEM);
		goto error;
	}
	if ((file->qid.path = _next_inum ()) < 0) {
		np_uerror (ENOMEM);
		goto error;
	}
	file->qid.type = type | P9_QTTMP;
	file->qid.version = 0;
	if ((type & P9_QTDIR))
		_update_mode (file, DIRMODE);
	else
		_update_mode (file, FILEMODE);
	file->uid = 0;
	file->gid = 0;
	_update_time (file, ATIME);
	_update_time (file, MTIME);
	_update_time (file, CTIME);

	return file;	
error:
	np_syn_delfile (file);
	return NULL;
}

Npfile *
np_syn_addfile (Npfile *parent, char *name, SynGetF getf, void *arg)
{
	Npfile *file;

	if (!(parent->qid.type & P9_QTDIR)) {
		np_uerror (EINVAL);
		return NULL;
	}
	if (!(file = _alloc_file (name, P9_QTFILE)))
		return NULL;
	file->getf = getf;
	file->getf_arg = arg;
	file->next = parent->child;
	parent->child = file;
	_update_time (parent, MTIME);

	return file;
}

Npfile *
np_syn_mkdir (Npfile *parent, char *name)
{
	Npfile *file;

	if (!(parent->qid.type & P9_QTDIR)) {
		np_uerror (EINVAL);
		return NULL;
	}
	if (!(file = _alloc_file (name, P9_QTDIR)))
		return NULL;
	file->getf = NULL;
	file->getf_arg = NULL;
	file->next = parent->child;
	parent->child = file;
	_update_time (parent, MTIME);

	return file;
}

void
np_syn_finalize (Npsrv *srv)
{
	Npfile *root = srv->synroot;

	if (root)
		np_syn_delfile (root);
	srv->synroot = NULL;		
}

int
np_syn_initialize (Npsrv *srv)
{
	Npfile *root = NULL;

	if (!(root = _alloc_file ("root", P9_QTDIR)))
		return -1;
	srv->synroot = root;
	return 0;
}

/**
 ** Server callbacks
 **/

Npfcall *
np_syn_attach(Npfid *fid, Npfid *afid, char *aname)
{
	Npfcall *rc = NULL;
	Fid *f = NULL;
	Npsrv *srv = fid->conn->srv;
	Npfile *root = srv->synroot;

	assert (aname == NULL);
	if (!root)
		goto error;
	if (!(fid->aux = _alloc_fid (root)))
		goto error;
	if (!(rc = np_create_rattach (&root->qid))) {
		np_uerror (ENOMEM);
		goto error;
	}
	fid->type = root->qid.type;
	np_fid_incref (fid);
	return rc;
error:
	if (f)
		_free_fid (f);
	if (rc)
		free (rc);
	return NULL;
}

int
np_syn_clone(Npfid *fid, Npfid *newfid)
{
	Fid *f = fid->aux;
	Fid *nf;

	assert (f != NULL);
	assert (f->file != NULL);
	assert (f->file->name != NULL);
	if (!(nf = _alloc_fid (f->file))) {
		np_uerror (ENOMEM);
		return 0;
	}
	newfid->aux = nf;
	return 1;
}

int
np_syn_walk(Npfid *fid, Npstr *wname, Npqid *wqid)
{
	Fid *f = fid->aux;
	int ret = 0;
	Npfile *ff;

	for (ff = f->file->child; ff != NULL; ff = ff->next) {
		if (np_strcmp (wname, ff->name) == 0)
			break;
	}
	if (!ff) {
		np_uerror (ENOENT);
		goto done;
	}
	f->file = ff;
	wqid->path = ff->qid.path;
	wqid->type = ff->qid.type;
	wqid->version = ff->qid.version;
	ret = 1;
done:
	return ret;
}

void
np_syn_fiddestroy (Npfid *fid)
{
	Fid *f = fid->aux;

	_free_fid (f);
}

Npfcall *
np_syn_clunk(Npfid *fid)
{
	Npfcall *rc;

	if (!(rc = np_create_rclunk ()))
		np_uerror (ENOMEM);

	return rc;
}

Npfcall *
np_syn_lopen(Npfid *fid, u32 mode)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;

	if ((mode & O_WRONLY) || (mode & O_RDWR)) {
		np_uerror (EACCES);
		goto done;
	}
	if (!(fid->type & P9_QTDIR) && !f->file->getf) {
		np_uerror (EACCES);
		goto done;
	}
	assert (f->data == NULL);

	if (!(rc = np_create_rlopen (&f->file->qid, 0))) {
		np_uerror (ENOMEM);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_syn_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;
	int len;

	if (!f->data) {
		assert (f->file->getf != NULL);
		if (!(f->data = f->file->getf (f->file->getf_arg)))
			if (np_rerror ()) /* NULL is a valid (empty) result */
				goto done;
	}
	len = f->data ? strlen (f->data) : 0;
	if (offset > len)
		offset = len;
	if (count > len - offset)
		count = len - offset;
	if (!(rc = np_create_rread (count, (u8 *)f->data + offset))) {
		np_uerror (ENOMEM);
		goto done;
	}
	_update_time (f->file, ATIME);
done:
	return rc;
}

Npfcall *
np_syn_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;
	Npfile *ff;
	int off = 0;
	int i, n = 0;

	if (!(rc = np_create_rreaddir (count))) {
		np_uerror (ENOMEM);
		goto done;
	}
	for (ff = f->file->child; ff != NULL; ff = ff->next) {
		if (off >= offset) {
			i = np_serialize_p9dirent (&ff->qid, off + 1,
				(ff->qid.type & P9_QTDIR) ? DT_DIR : DT_REG,
				ff->name, rc->u.rreaddir.data + n, count - n);
			if (i == 0)
				break;
			n += i;
		}
		off++;
	}
	np_finalize_rreaddir (rc, n);
	_update_time (f->file, ATIME);
done:
	return rc;
}

Npfcall *
np_syn_getattr(Npfid *fid, u64 request_mask)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;

	rc = np_create_rgetattr(request_mask, &f->file->qid, f->file->mode,
			f->file->uid, f->file->gid, 1, 0, 0, 0, 0,
			f->file->atime.tv_sec, f->file->atime.tv_usec*1000,
			f->file->mtime.tv_sec, f->file->mtime.tv_usec*1000,
			f->file->ctime.tv_sec, f->file->ctime.tv_usec*1000,
			0, 0, 0, 0);
	if (!rc)
		np_uerror (ENOMEM);
	return rc;
}

Npfcall *
np_syn_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Npfcall *rc = NULL;

	np_uerror (ENOSYS);

	return rc;
}

