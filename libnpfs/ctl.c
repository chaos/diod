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

/* ctl.c - handle simple synthetic files for stats tools, etc */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>

#include "9p.h"
#include "npfs.h"
#include "xpthread.h"
#include "npfsimpl.h"

typedef struct {
	Npfile	*file;
	void	*data;
} Fid;

static char *_ctl_get_version (char *name, void *a);
static char *_ctl_get_date (char *name, void *a);
static char *_ctl_get_proc (char *name, void *arg);

static int
_next_inum (void)
{
	static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	static int i = 1;
	int ret;

	xpthread_mutex_lock (&lock);
	ret = i++;
	xpthread_mutex_unlock (&lock);
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
np_ctl_delfile (Npfile *file)
{
	Npfile *ff, *tmp;

	if (file) {
		for (ff = file->child; ff != NULL; ) {
			tmp = ff->next; 
			np_ctl_delfile (ff);
			ff = tmp;
		}
		if (file->name)
			free (file->name);
		free (file);
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
	file->qid.path = _next_inum ();
	file->qid.type = type | P9_QTTMP;
	file->qid.version = 0;
	if ((type & P9_QTDIR)) {
		file->mode = S_IFDIR;
		file->mode |= S_IRUSR | S_IRGRP | S_IROTH;
		file->mode |= S_IXUSR | S_IXGRP | S_IXOTH;
	} else {
		file->mode = S_IFREG;
		file->mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	file->uid = 0;
	file->gid = 0;
	(void)gettimeofday (&file->atime, NULL);
	(void)gettimeofday (&file->mtime, NULL);
	(void)gettimeofday (&file->ctime, NULL);

	return file;	
error:
	np_ctl_delfile (file);
	return NULL;
}

#define GET_PROC_CHUNK 4096
static char *
_ctl_get_proc (char *name, void *arg)
{
	char path[PATH_MAX + 1];
	int ssize = 0;
	char *s = NULL;
	int fd = -1, n, len;

	snprintf (path, sizeof(path), "/proc/%s", name);
	for (n = 0; n < strlen (path); n++) {
		if (path[n] == '.')
			path[n] = '/';	
	}
	if ((fd = open (path, O_RDONLY)) < 0) {
		np_uerror (errno);
		goto error;
	}
	len = 0;
	do {
		if (!s) {
			ssize = GET_PROC_CHUNK;
			s = malloc (ssize);
		} else if (ssize - len == 1) {
			ssize += GET_PROC_CHUNK;
			s = realloc (s, ssize);
		}
		if (!s) {
			np_uerror (ENOMEM);
			goto error;
		}
		n = read (fd, s + len, ssize - len - 1);
		if (n > 0)
			len += n;
	} while (n > 0);
	if (n < 0) {
		np_uerror (errno);
		goto error;
	}
	(void)close (fd);
	s[len] = '\0';
	return s;

error:
	if (s)
		free (s);
	if (fd >= 0)
		(void)close (fd);
	return NULL;	
}

Npfile *
np_ctl_addfile (Npfile *parent, char *name, SynGetF getf, void *arg, int flags)
{
	Npfile *file;

	if (!(parent->qid.type & P9_QTDIR)) {
		np_uerror (EINVAL);
		return NULL;
	}
	if (!(file = _alloc_file (name, P9_QTFILE)))
		return NULL;
	if ((flags & NP_CTL_FLAGS_SINK))
		file->mode |= S_IWUSR | S_IWGRP | S_IWOTH;
	file->getf = getf;
	file->getf_arg = arg;
	file->flags = flags;
	file->next = parent->child;
	parent->child = file;
	(void)gettimeofday(&parent->mtime, NULL);

	return file;
}

Npfile *
np_ctl_adddir (Npfile *parent, char *name)
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
	(void)gettimeofday (&parent->mtime, NULL);

	return file;
}

void
np_ctl_finalize (Npsrv *srv)
{
	Npfile *root = srv->ctlroot;

	if (root)
		np_ctl_delfile (root);
	srv->ctlroot = NULL;		
}

int
np_ctl_initialize (Npsrv *srv)
{
	Npfile *root = NULL;

	if (!(root = _alloc_file ("root", P9_QTDIR)))
		goto error;
	srv->ctlroot = root;

	if (!np_ctl_addfile (root, "version", _ctl_get_version, NULL, 0))
		goto error;
	if (!np_ctl_addfile (root, "date", _ctl_get_date, NULL, 0))
		goto error;
	if (!np_ctl_addfile (root, "zero", NULL, NULL, NP_CTL_FLAGS_ZEROSRC))
		goto error;
	if (!np_ctl_addfile (root, "zero100", NULL, NULL,
			     NP_CTL_FLAGS_ZEROSRC | NP_CTL_FLAGS_DELAY100MS))
		goto error;
	if (!np_ctl_addfile (root, "null", NULL, NULL, NP_CTL_FLAGS_SINK))
		goto error;
	if (!np_ctl_addfile (root, "null100", NULL, NULL,
			     NP_CTL_FLAGS_SINK | NP_CTL_FLAGS_DELAY100MS))
		goto error;
	if (!np_ctl_addfile (root, "meminfo", _ctl_get_proc, NULL, 0))
		goto error;
	if (!np_ctl_addfile (root, "net.rpc.nfs", _ctl_get_proc, NULL, 0))
		goto error;
	return 0;
error:
	np_ctl_finalize (srv);
	return -1;
}

static char *
_ctl_get_version (char *name, void *a)
{
        char *s = NULL;
        int len = 0;

        if (aspf (&s, &len, "%s\n", META_ALIAS) < 0)
                np_uerror (ENOMEM);
        return s;
}

static char *
_ctl_get_date (char *name, void *a)
{
	struct timeval tv;
	struct timezone tz;
        char *s = NULL;
        int len = 0;

	if (gettimeofday (&tv, &tz) < 0) {
		np_uerror (errno);
		goto error;
	}
	if (aspf (&s, &len, "%lu.%lu %d.%d\n",
					tv.tv_sec,         tv.tv_usec,
					tz.tz_minuteswest, tz.tz_dsttime) < 0) {
		np_uerror (ENOMEM);
		goto error;
	}
	return s;
error:
	return NULL;
}

/**
 ** Server callbacks
 **/

Npfcall *
np_ctl_attach(Npfid *fid, Npfid *afid, char *aname)
{
	Npfcall *rc = NULL;
	Fid *f = NULL;
	Npsrv *srv = fid->conn->srv;
	Npfile *root = srv->ctlroot;

	NP_ASSERT(aname && !strcmp (aname, "ctl"));
	if (!root)
		goto error;
	if (!(f = _alloc_fid (root)))
		goto error;
	if (!(rc = np_create_rattach (&root->qid))) {
		np_uerror (ENOMEM);
		goto error;
	}
	fid->type = root->qid.type;
	fid->aux = f;
	return rc;
error:
	if (f)
		_free_fid (f);
	return NULL;
}

int
np_ctl_clone(Npfid *fid, Npfid *newfid)
{
	Fid *f = fid->aux;
	Fid *nf;

	NP_ASSERT(f != NULL);
	NP_ASSERT(f->file != NULL);
	NP_ASSERT(f->file->name != NULL);
	if (!(nf = _alloc_fid (f->file))) {
		np_uerror (ENOMEM);
		return 0;
	}
	newfid->aux = nf;
	return 1;
}

int
np_ctl_walk(Npfid *fid, Npstr *wname, Npqid *wqid)
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
np_ctl_fiddestroy (Npfid *fid)
{
	Fid *f = fid->aux;

	_free_fid (f);
}

Npfcall *
np_ctl_clunk(Npfid *fid)
{
	Npfcall *rc;

	if (!(rc = np_create_rclunk ()))
		np_uerror (ENOMEM);

	return rc;
}

Npfcall *
np_ctl_lopen(Npfid *fid, u32 mode)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;

	if (((mode & O_WRONLY) || (mode & O_RDWR))
				&& !(f->file->flags & NP_CTL_FLAGS_SINK)) {
		np_uerror (EACCES);
		goto done;
	}
	if (((mode & O_RDONLY) || (mode & O_RDWR)) && !f->file->getf
				&& !(fid->type & P9_QTDIR)
				&& !(f->file->flags & NP_CTL_FLAGS_ZEROSRC)){
		np_uerror (EACCES);
		goto done;
	}
	NP_ASSERT(f->data == NULL);

	if (!(rc = np_create_rlopen (&f->file->qid, 0))) {
		np_uerror (ENOMEM);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_ctl_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;
	int len;

	if ((f->file->flags & NP_CTL_FLAGS_DELAY100MS))
		usleep (100*1000);
	if ((f->file->flags & NP_CTL_FLAGS_ZEROSRC)) {
		if ((rc = np_alloc_rread (count)))
			memset (rc->u.rread.data, 0, count);
		else 
			np_uerror (ENOMEM);
		goto done;
	}
	if (!f->data && f->file->getf) {
		f->data = f->file->getf (f->file->name, f->file->getf_arg);
		if (!f->data && np_rerror ())
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
	(void)gettimeofday (&f->file->atime, NULL);
done:
	return rc;
}

Npfcall *
np_ctl_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req)
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
	(void)gettimeofday (&f->file->atime, NULL);
done:
	return rc;
}

Npfcall *
np_ctl_getattr(Npfid *fid, u64 request_mask)
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
np_ctl_setattr (Npfid *fid, u32 valid, u32 mode, u32 uid, u32 gid, u64 size,
              u64 atime_sec, u64 atime_nsec, u64 mtime_sec, u64 mtime_nsec)
{
	Npfcall *rc;

	/* do nothing for now - we exist only for setattr on /dev/null */

	if (!(rc = np_create_rsetattr()))
		np_uerror (ENOMEM);
	return rc;
}

Npfcall *
np_ctl_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;	

	/* limited write capability for now */
	if (!(f->file->flags & NP_CTL_FLAGS_SINK)) {
		np_uerror (ENOSYS);
		goto done;
	}
	if ((f->file->flags & NP_CTL_FLAGS_DELAY100MS))
		usleep (100*1000);
	if (!(rc = np_create_rwrite (count))) {
		np_uerror (ENOMEM);
		goto done;
	}
done:
	return rc;
}

