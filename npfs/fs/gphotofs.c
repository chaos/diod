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
#include <gphoto2/gphoto2.h>
#include "npfs.h"

#define ROOTPERM 	0755
#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

typedef struct File File;

struct File {
	char*		path;
	CameraFile*	camfile;
	const char*	data;
	int		nopen;
};

static u32 gpfs_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static u32 gpfs_write(Npfilefid *, u64, u32, u8 *, Npreq *);
static int gpfs_wstat(Npfile *, Npstat *);
static void gpfs_destroy(Npfile *);
static Npfile* gpfs_create(Npfile *, char *, u32, Npuser *, Npgroup *);
static Npfile* gpfs_first(Npfile *);
static Npfile* gpfs_next(Npfile *, Npfile *);
static int gpfs_wstat(Npfile *, Npstat *);
static int gpfs_remove(Npfile *, Npfile *);
static int gpfs_openfid(Npfilefid *);
static void gpfs_closefid(Npfilefid *);

static Npuser *user;
static Npsrv *srv;
static Npfile *root;
static u64 qidpath;
static Camera *cam;
static GPContext *ctx;

Npfileops fileops = {
	.read = gpfs_read,
	.write = gpfs_write,
	.wstat = gpfs_wstat,
	.destroy = gpfs_destroy,
	.openfid = gpfs_openfid,
	.closefid = gpfs_closefid,
};

Npdirops dirops = {
	.create = gpfs_create,
	.first = gpfs_first,
	.next = gpfs_next,
	.wstat = gpfs_wstat,
	.remove = gpfs_remove,
	.destroy = gpfs_destroy,
};

static File* file_alloc(char *);

void
usage()
{
	fprintf(stderr, "gphotofs: -d -u user -w nthreads "
		"-o mount-options mount-point\n");
	exit(-1);
}

int
main(int argc, char **argv)
{
	int err, c, debuglevel, nwthreads;
	char *opts, *s;

	debuglevel = 0;
	nwthreads = 1;
	opts = "";
	user = np_uid2user(getuid());
	while ((c = getopt(argc, argv, "du:w:o:")) != -1) {
		switch (c) {
		case 'd':
			debuglevel = 1;
			break;

		case 'u':
			user = np_uname2user(optarg);
			break;

		case 'w':
			nwthreads = strtol(optarg, &s, 10);
			if (*s != '\0')
				usage();
			break;

		case 'o':
			opts = optarg;
			break;

		default:
			fprintf(stderr, "invalid option\n");
		}
	}

	if (!user) {
		fprintf(stderr, "invalid user\n");
		return -1;
	}

	ctx = gp_context_new();
	gp_camera_new(&cam);

	err = gp_camera_init(cam, ctx);
	if (err) {
		fprintf(stderr, "cannot initialize the camera: %d\n", err);
		return -1;
	}

	root = npfile_alloc(NULL, strdup(""), ROOTPERM|Dmdir, qidpath++, 
		&dirops, file_alloc(""));

	root->parent = root;
	npfile_incref(root);
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
	np_pipesrv_mount(srv, argv[optind], user->uname, 0, opts);

	while (1) {
		sleep(100);
	}

	return 0;
}

static File*
file_alloc(char *path)
{
	File *f;

	f = malloc(sizeof(*f));
	f->path = strdup(path);
	f->camfile = NULL;
	f->data = NULL;
	f->nopen = 0;

	return f;
}

static u32 
gpfs_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
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

static u32 
gpfs_write(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	np_werror("permission denied", EPERM);
	return 0;
}

static int 
gpfs_wstat(Npfile *file, Npstat *stat)
{
	np_werror("permission denied", EPERM);
	return 0;
}

static void 
gpfs_destroy(Npfile* file)
{
	File *f;

	fprintf(stderr, "destroy file: %s\n", file->name);
	f = file->aux;
	gp_file_unref(f->camfile);
	free(f->path);
	free(f);
}

static Npfile* 
gpfs_create(Npfile *dir, char *name, u32 perm, Npuser *uid, Npgroup *gid)
{
	np_werror("permission denied", EPERM);
	return NULL;
}

static void
read_files(Npfile *dir)
{
	int i, n;
	int plen, nlen;
	char *folder, *p;
	Npfile *nf;
	File *d;
	CameraList *cl;
	const char *fname;
	CameraFileInfo finfo;

	d = dir->aux;
/*	if (dir == root)
		folder = strdup("/");
	else {
*/
	p = d->path;
	if (strcmp(p, "/") == 0)
		p = "";

       		plen = strlen(p);
		nlen = strlen(dir->name);
		folder = malloc(plen + nlen + 2);
		memcpy(folder, p, plen);
		folder[plen] = '/';
		strcpy(folder + plen + 1, dir->name);
//	}

	gp_list_new(&cl);
	gp_camera_folder_list_files(cam, folder, cl, ctx);
	n = gp_list_count(cl);
	for(i = 0; i < n; i++) {
		gp_list_get_name(cl, i, &fname);
		gp_camera_file_get_info(cam, folder, fname, &finfo, ctx);

		nf = npfile_alloc(dir, (char *) fname, 0500, qidpath++, 
			&fileops, file_alloc(folder));
		nf->mtime = finfo.file.mtime;
		nf->atime = nf->mtime;
		nf->uid = user;
		nf->gid = user->dfltgroup;
		nf->muid = user;
		nf->length = finfo.file.size;
		if (dir->dirlast) {
			dir->dirlast->next = nf;
			nf->prev = dir->dirlast;
		} else 
			dir->dirfirst = nf;

		dir->dirlast = nf;
		npfile_incref(nf);
	}

	gp_camera_folder_list_folders(cam, folder, cl, ctx);
	n = gp_list_count(cl);
	for(i = 0; i < n; i++) {
		gp_list_get_name(cl, i, &fname);

		nf = npfile_alloc(dir, (char *) fname, 0500|Dmdir, qidpath++, 
			&dirops, file_alloc(folder));
		nf->uid = user;
		nf->gid = user->dfltgroup;
		nf->muid = user;
		if (dir->dirlast) {
			dir->dirlast->next = nf;
			nf->prev = dir->dirlast;
		} else 
			dir->dirfirst = nf;

		dir->dirlast = nf;
		npfile_incref(nf);
	}
}

static Npfile* 
gpfs_first(Npfile *dir)
{
	if (!dir->dirfirst)
		read_files(dir);

	npfile_incref(dir->dirfirst);			
	return dir->dirfirst;
}

static Npfile*
gpfs_next(Npfile *dir, Npfile *prevchild)
{
	npfile_incref(prevchild->next);
	return prevchild->next;
}

static int
gpfs_remove(Npfile *dir, Npfile *file)
{
	np_werror("permission denied", EPERM);
	return 0;
}

static int 
gpfs_openfid(Npfilefid *fid)
{
	int n;
	Npfile *file;
	File *f;
	unsigned long len;

	file = fid->file;
	f = file->aux;

	if (!f->nopen) {
		gp_file_new(&f->camfile);
		n = gp_camera_file_get(cam, f->path, file->name, 
			GP_FILE_TYPE_RAW, f->camfile, ctx);
		if (n) {
			fprintf(stderr, "error getting file: %d\n", n);
			np_werror("error getting file", EIO);
			return 0;
		}

		gp_file_get_data_and_size(f->camfile, &f->data, &len);
	}

	f->nopen++;
	return 1;
}


static void
gpfs_closefid(Npfilefid *fid)
{
	Npfile *file;
	File *f;

	file = fid->file;
	f = file->aux;
	f->nopen--;
	if (!f->nopen) {
		gp_file_unref(f->camfile);
		f->camfile = NULL;
	}	
}
