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
	File	*file;
	void	*data;
	File	*saved_dir_position;
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
_alloc_fid (File *file)
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

/* FIXME: not yet thread safe */
void
np_syn_delfile (File *file)
{
	File *ff, *tmp;

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

static File *
_alloc_file (char *name, u8 type)
{
	File *file = NULL;

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

	return file;	
error:
	np_syn_delfile (file);
	return NULL;
}

/* FIXME: not yet thread safe */
int
np_syn_addfile (File *parent, char *name, u8 type, SynGetF getf, void *arg)
{
	File *file;

	if (!(parent->qid.type & P9_QTDIR)) {
		np_uerror (EINVAL);
		return -1;
	}
	if ((type & P9_QTDIR) && (getf || arg)) {
		np_uerror (EINVAL);
		return -1;
	}
	if (!(file = _alloc_file (name, type)))
		return -1;
	file->getf = getf;
	file->getf_arg = arg;
	file->next = parent->child;
	parent->child = file;
	return 0;
}

void
np_syn_finalize (Npsrv *srv)
{
	File *root = srv->synroot;

	if (root)
		np_syn_delfile (root);
	srv->synroot = NULL;		
}

int
np_syn_initialize (Npsrv *srv)
{
	File *root = NULL;

	if (!(root = _alloc_file ("root", P9_QTDIR)))
		return -1;
	srv->synroot = root;
	return 0;
}

Npfcall *
np_syn_attach(Npfid *fid, Npfid *afid, char *aname)
{
	Npfcall *rc = NULL;
	Fid *f = NULL;
	Npsrv *srv = fid->conn->srv;
	File *root = srv->synroot;

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
	File *ff;

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
		f->data = f->file->getf (f->file->getf_arg);
	}
	if (!f->data)
		goto done;
	len = strlen (f->data);
	if (offset > len)
		offset = len;
	if (count > len - offset)
		count = len - offset;
	if (!(rc = np_create_rread (count, (u8 *)f->data + offset))) {
		np_uerror (ENOMEM);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_syn_readdir(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;
	File *ff;
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
done:
	return rc;
}

Npfcall *
np_syn_getattr(Npfid *fid, u64 request_mask)
{
	Fid *f = fid->aux;
	Npfcall *rc = NULL;
	struct timeval now;
	int mode = 0;

	if (gettimeofday (&now, NULL) < 0) {
		np_uerror (errno);
		goto done;
	}
	if ((f->file->qid.type & P9_QTDIR)) {
		mode |= S_IFDIR;
		mode |= S_IRUSR | S_IRGRP | S_IROTH;
		mode |= S_IXUSR | S_IXGRP | S_IXOTH;
	} else {
		mode |= S_IFREG;
		if (f->file->getf)
			mode |= S_IRUSR | S_IRGRP | S_IROTH;
	}
	if (!(rc = np_create_rgetattr(request_mask, &f->file->qid, mode,
					0, /* uid */
					0, /* gid */
					1, /* nlink */
					0, /* rdev */
					0, /* size */
					0, /* blksize */
					0, /* blocks */
					now.tv_sec, now.tv_usec*1000, /* atim */
					now.tv_sec, now.tv_usec*1000, /* mtim */
					now.tv_sec, now.tv_usec*1000, /* ctim */
                                        0, 0, 0, 0))) {
		np_uerror (ENOMEM);
		goto done;
	}
done:
	return rc;
}

Npfcall *
np_syn_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Npfcall *rc = NULL;

	np_uerror (ENOSYS);

	return rc;
}

