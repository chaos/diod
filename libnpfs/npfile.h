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

typedef struct Npfile Npfile;
typedef struct Npfilefid Npfilefid;
typedef struct Npfileops Npfileops;
typedef struct Npdirops Npdirops;

struct Npfile {
	pthread_mutex_t	lock;
	int		refcount;
	Npfile*		parent;
	Npqid		qid;
	u32		mode;
	u32		atime;
	u32		mtime;
	u64		length;
	char*		name;
	Npuser*		uid;
	Npgroup*	gid;
	Npuser*		muid;
	char*		extension;
	int		excl;
	void*		ops;
	void*		aux;

	/* not used -- provided for user's convenience */
	Npfile*		next;
	Npfile*		prev;
	Npfile*		dirfirst;
	Npfile*		dirlast;
};

struct Npfileops {
	void		(*ref)(Npfile *, Npfilefid *);
	void		(*unref)(Npfile *, Npfilefid *);
	int		(*read)(Npfilefid* file, u64 offset, u32 count, 
				u8 *data, Npreq *req);
	int		(*write)(Npfilefid* file, u64 offset, u32 count, 
				u8 *data, Npreq *req);
	int		(*wstat)(Npfile*, Npstat*);
	void		(*destroy)(Npfile*);
	int		(*openfid)(Npfilefid *);
	void		(*closefid)(Npfilefid *);
};

struct Npdirops {
	void		(*ref)(Npfile *, Npfilefid *);
	void		(*unref)(Npfile *, Npfilefid *);
	Npfile*		(*create)(Npfile *dir, char *name, u32 perm, 
				Npuser *uid, Npgroup *gid, char *extension);
	Npfile*		(*first)(Npfile *dir);
	Npfile*		(*next)(Npfile *dir, Npfile *prevchild);
	int		(*wstat)(Npfile*, Npstat*);
	int		(*remove)(Npfile *dir, Npfile *file);
	void		(*destroy)(Npfile*);
	Npfilefid*	(*allocfid)(Npfile *);
	void		(*destroyfid)(Npfilefid *);
};

struct Npfilefid {
	pthread_mutex_t	lock;
	Npfid*		fid;
	Npfile*		file;
	int		omode;
	void*		aux;
	u64		diroffset;
	Npfile*		dirent;
};

Npfile* npfile_alloc(Npfile *parent, char *name, u32 mode, u64 qpath, 
	void *ops, void *aux);
void npfile_incref(Npfile *);
int npfile_decref(Npfile *);
Npfile *npfile_find(Npfile *, char *);
int npfile_checkperm(Npfile *file, Npuser *user, int perm);
void npfile_init_srv(Npsrv *, Npfile *);

Npfilefid* npfile_fidalloc(Npfile *file, Npfid *fid); /* added jg */
void npfile_fiddestroy(Npfid *fid);
Npfcall *npfile_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname);
int npfile_clone(Npfid *fid, Npfid *newfid);
int npfile_walk(Npfid *fid, Npstr *wname, Npqid *wqid);
Npfcall *npfile_open(Npfid *fid, u8 mode);
Npfcall *npfile_create(Npfid *fid, Npstr* name, u32 perm, u8 mode, Npstr* extension);
Npfcall *npfile_read(Npfid *fid, u64 offset, u32 count, Npreq *req);
Npfcall *npfile_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req);
Npfcall *npfile_clunk(Npfid *fid);
Npfcall *npfile_remove(Npfid *fid);
Npfcall *npfile_stat(Npfid *fid);
Npfcall *npfile_wstat(Npfid *fid, Npstat *stat);
