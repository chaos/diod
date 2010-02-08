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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <linux/kdev_t.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "npfs.h"

#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

typedef struct Fid Fid;

struct Fid {
	char*		path;
	int		omode;
	int		fd;
	DIR*		dir;
	int		diroffset;
	char*		direntname;
	struct stat	stat;
};

Npsrv *srv;
int debuglevel;

char *Estatfailed = "stat failed";
char *Ebadfid = "fid unknown or out of range";
char *Enoextension = "empty extension while creating special file";
char *Eformat = "incorrect extension format";
char *Ecreatesocket = "cannot create socket";
//char *E = "";

static int fidstat(Fid *fid);
static void ustat2qid(struct stat *st, Npqid *qid);
static u8 ustat2qidtype(struct stat *st);
static u32 umode2npmode(mode_t umode, int dotu);
static mode_t npstat2umode(Npstat *st, int dotu);
static void ustat2npwstat(char *path, struct stat *st, Npwstat *wstat, int dotu);

static void npfs_connclose(Npconn *conn);
static Npfcall* npfs_attach(Npfid *fid, Npfid *afid, Npstr *uname, Npstr *aname);
static int npfs_clone(Npfid *fid, Npfid *newfid);
static int npfs_walk(Npfid *fid, Npstr* wname, Npqid *wqid);
static Npfcall* npfs_open(Npfid *fid, u8 mode);
static Npfcall* npfs_create(Npfid *fid, Npstr* name, u32 perm, u8 mode);
static Npfcall* npfs_read(Npfid *fid, u64 offset, u32 count, Npreq *req);
static Npfcall* npfs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req);
static Npfcall* npfs_clunk(Npfid *fid);
static Npfcall* npfs_remove(Npfid *fid);
static Npfcall* npfs_stat(Npfid *fid);
static Npfcall* npfs_wstat(Npfid *fid, Npstat *stat);
static void npfs_fiddestroy(Npfid *fid);

static int
get_local_addr(char *hostname, char *addr, int addrlen)
{
	int sock;
	socklen_t n;
	struct sockaddr_in saddr;
	struct hostent *hostinfo;

	sock = socket(PF_INET, SOCK_STREAM, 0);

	saddr.sin_family = AF_INET;
	saddr.sin_port = htons (22);
	hostinfo = gethostbyname (hostname);
	if (hostinfo == NULL) {
		fprintf (stderr, "Unknown host %s.\n", hostname);
		return -1;
        }

	saddr.sin_addr = *(struct in_addr *) hostinfo->h_addr;
	if (0 > connect (sock, (struct sockaddr *) &saddr, sizeof (saddr))) {
		perror ("connect");
		return -1;
	}

	n = sizeof(saddr);
	getsockname(sock, (struct sockaddr *) &saddr, &n);
	snprintf(addr, addrlen, "%s", inet_ntoa(saddr.sin_addr));
	shutdown(sock, SHUT_RDWR);
	return 0;
}

int
main(int argc, char **argv)
{
	int i;
	int port, nwthreads;
	pid_t pid;
	char sport[20], hostname[20];
	char **sargv;
	Npuser *user;

	user = np_uid2user(getuid());
	port = 0;
	nwthreads = 16;
	srv = np_socksrv_create_tcp(nwthreads, &port);
	if (!srv)
		return -1;
	srv->dotu = 1;
	srv->connclose = npfs_connclose;
	srv->attach = npfs_attach;
	srv->clone = npfs_clone;
	srv->walk = npfs_walk;
	srv->open = npfs_open;
	srv->create = npfs_create;
	srv->read = npfs_read;
	srv->write = npfs_write;
	srv->clunk = npfs_clunk;
	srv->remove = npfs_remove;
	srv->stat = npfs_stat;
	srv->wstat = npfs_wstat;
	srv->fiddestroy = npfs_fiddestroy;
	srv->debuglevel = debuglevel;
	np_srv_start(srv);

	pid = fork();
	if (pid < 0)
		return -1;
	else if (pid != 0) 
		while (1)
			sleep(100);

	/* child */
	if (get_local_addr(argv[1], hostname, sizeof(hostname)))
		return -1;

	sprintf(sport, "%d", port);
	sargv = calloc(argc + 6, sizeof(char *));
	sargv[0] = "ssh";
	sargv[1] = "-t";
	sargv[2] = argv[1];
	sargv[3] = "/usr/sbin/cpuhelper";
	sargv[4] = user->uname;
	sargv[5] = hostname;
	sargv[6] = sport;
	
	for(i = 2; i < argc; i++)
		sargv[i+5] = argv[i];

	execvp("ssh", sargv);

	return 0;
}

static void
npfs_connclose(Npconn *conn)
{
	exit(0);
}

static int
fidstat(Fid *fid)
{
	if (stat(fid->path, &fid->stat) < 0)
		return errno;

	if (S_ISDIR(fid->stat.st_mode))
		fid->stat.st_size = 0;

	return 0;
}

static Fid*
npfs_fidalloc() {
	Fid *f;

	f = malloc(sizeof(*f));

	f->path = NULL;
	f->omode = -1;
	f->fd = -1;
	f->dir = NULL;
	f->diroffset = 0;
	f->direntname = NULL;

	return f;
}

static void
npfs_fiddestroy(Npfid *fid)
{
	Fid *f;

	f = fid->aux;
	if (!f)
		return;

	if (f->fd != -1)
		close(f->fd);

	if (f->dir)
		closedir(f->dir);

	free(f->path);
	free(f);
}

static void
create_rerror(int ecode)
{
	char buf[256];

	if (strerror_r(ecode, buf, sizeof(buf)))
		strcpy(buf, "unknown error");

	np_werror(buf, ecode);
}

static int
omode2uflags(u8 mode)
{
	int ret;

	ret = 0;
	switch (mode & 3) {
	case Oread:
		ret = O_RDONLY;
		break;

	case Ordwr:
		ret = O_RDWR;
		break;

	case Owrite:
		ret = O_WRONLY;
		break;

	case Oexec:
		ret = O_RDONLY;
		break;
	}

	if (mode & Otrunc)
		ret |= O_TRUNC;

	if (mode & Oappend)
		ret |= O_APPEND;

	return ret;
}

static void
ustat2qid(struct stat *st, Npqid *qid)
{
	int n;

	qid->path = 0;
	n = sizeof(qid->path);
	if (n > sizeof(st->st_ino))
		n = sizeof(st->st_ino);
	memmove(&qid->path, &st->st_ino, n);
	qid->version = st->st_mtime ^ (st->st_size << 8);
	qid->type = ustat2qidtype(st);
}

static u8
ustat2qidtype(struct stat *st)
{
	u8 ret;

	ret = 0;
	if (S_ISDIR(st->st_mode))
		ret |= Qtdir;

	if (S_ISLNK(st->st_mode))
		ret |= Qtsymlink;

	return ret;
}

static u32
umode2npmode(mode_t umode, int dotu)
{
	u32 ret;

	ret = umode & 0777;
	if (S_ISDIR(umode))
		ret |= Dmdir;

	if (dotu) {
		if (S_ISLNK(umode))
			ret |= Dmsymlink;
		if (S_ISSOCK(umode))
			ret |= Dmsocket;
		if (S_ISFIFO(umode))
			ret |= Dmnamedpipe;
		if (S_ISBLK(umode))
			ret |= Dmdevice;
		if (S_ISCHR(umode))
			ret |= Dmdevice;
		if (umode & S_ISUID)
			ret |= Dmsetuid;
		if (umode & S_ISGID)
			ret |= Dmsetgid;
	}

	return ret;
}

static mode_t
npstat2umode(Npstat *st, int dotu)
{
	u32 npmode;
	mode_t ret;

	npmode = st->mode;
	ret = npmode & 0777;
	if (npmode & Dmdir)
		ret |= S_IFDIR;

	if (dotu) {
		if (npmode & Dmsymlink)
			ret |= S_IFLNK;
		if (npmode & Dmsocket)
			ret |= S_IFSOCK;
		if (npmode & Dmnamedpipe)
			ret |= S_IFIFO;
		if (npmode & Dmdevice) {
			if (st->extension.str[0] == 'c')
				ret |= S_IFCHR;
			else
				ret |= S_IFBLK;
		}
	}

	if (!(ret&~0777))
		ret |= S_IFREG;

	if (npmode & Dmsetuid)
		ret |= S_ISUID;
	if (npmode & Dmsetgid)
		ret |= S_ISGID;

	return ret;
}

static void
ustat2npwstat(char *path, struct stat *st, Npwstat *wstat, int dotu)
{
	int err;
	Npuser *u;
	Npgroup *g;
	char *s, ext[256];

	memset(wstat, 0, sizeof(*wstat));
	ustat2qid(st, &wstat->qid);
	wstat->mode = umode2npmode(st->st_mode, dotu);
	wstat->atime = st->st_atime;
	wstat->mtime = st->st_mtime;
	wstat->length = st->st_size;

	u = np_uid2user(st->st_uid);
	g = np_gid2group(st->st_gid);
	
	wstat->uid = u?u->uname:"???";
	wstat->gid = g?g->gname:"???";
	wstat->muid = "";

	wstat->extension = NULL;
	if (dotu) {
		wstat->n_uid = st->st_uid;
		wstat->n_gid = st->st_gid;

		if (wstat->mode & Dmsymlink) {
			err = readlink(path, ext, sizeof(ext) - 1);
			if (err < 0)
				err = 0;

			ext[err] = '\0';
		} else if (wstat->mode & Dmdevice) {
			snprintf(ext, sizeof(ext), "%c %llu %llu", 
				S_ISCHR(st->st_mode)?'c':'b',
				MAJOR(st->st_rdev), MINOR(st->st_rdev));
		} else {
			ext[0] = '\0';
		}

		wstat->extension = strdup(ext);
	}

	s = strrchr(path, '/');
	if (s)
		wstat->name = s + 1;
	else
		wstat->name = path;
}

static Npfcall*
npfs_attach(Npfid *nfid, Npfid *nafid, Npstr *uname, Npstr *aname)
{
	int err;
	Npfcall* ret;
	Fid *fid;
	Npqid qid;
	char *user;

	user = NULL;
	ret = NULL;

	if (nafid != NULL) {
		np_werror(Enoauth, EIO);
		goto done;
	}

	fid = npfs_fidalloc();
	fid->omode = -1;
	user = np_strdup(uname);
	nfid->user = np_uname2user(user);
	free(user);
	if (!nfid->user) {
		free(fid);
		np_werror(Eunknownuser, EIO);
		goto done;
	}
//	np_change_user(nfid->user);

	fid->omode = -1;
	if (aname->len==0 || *aname->str!='/')
		fid->path = strdup("/");
	else
		fid->path = np_strdup(aname);
	
	nfid->aux = fid;
	err = fidstat(fid);
	if (err < 0) {
		create_rerror(err);
		goto done;
	}

	ustat2qid(&fid->stat, &qid);
	ret = np_create_rattach(&qid);
	np_fid_incref(nfid);

done:
	return ret;
}

static int
npfs_clone(Npfid *fid, Npfid *newfid)
{
	Fid *f, *nf;

	f = fid->aux;
	nf = npfs_fidalloc();
	nf->path = strdup(f->path);
	newfid->aux = nf;

	return 1;	
}


static int
npfs_walk(Npfid *fid, Npstr* wname, Npqid *wqid)
{
	int n;
	Fid *f;
	struct stat st;
	char *path;

	f = fid->aux;
//	np_change_user(fid->user);
	n = fidstat(f);
	if (n < 0)
		create_rerror(n);

	n = strlen(f->path);
	path = malloc(n + wname->len + 2);
	memcpy(path, f->path, n);
	path[n] = '/';
	memcpy(path + n + 1, wname->str, wname->len);
	path[n + wname->len + 1] = '\0';

	if (stat(path, &st) < 0) {
		free(path);
		create_rerror(errno);
		return 0;
	}

	free(f->path);
	f->path = path;
	ustat2qid(&st, wqid);

	return 1;
}

static Npfcall*
npfs_open(Npfid *fid, u8 mode)
{
	int err;
	Fid *f;
	Npqid qid;

	f = fid->aux;
//	np_change_user(fid->user);
	if ((err = fidstat(f)) < 0)
		create_rerror(err);

	if (S_ISDIR(f->stat.st_mode)) {
		f->dir = opendir(f->path);
		if (!f->dir)
			create_rerror(errno);
	} else {
		f->fd = open(f->path, omode2uflags(mode));
		if (f->fd < 0)
			create_rerror(errno);
	}

	err = fidstat(f);
	if (err < 0)
		create_rerror(err);

	f->omode = mode;
	ustat2qid(&f->stat, &qid);
	return np_create_ropen(&qid, 0);
}

static Npfcall*
npfs_create(Npfid *fid, Npstr* name, u32 perm, u8 mode)
{
	int n, err, omode;
	Fid *f;
	Npfcall *ret;
	Npqid qid;
	char *npath;
	struct stat st;

	ret = NULL;
	omode = mode;
	f = fid->aux;
	if ((err = fidstat(f)) < 0)
		create_rerror(err);

	n = strlen(f->path);
	npath = malloc(n + name->len + 2);
	memmove(npath, f->path, n);
	npath[n] = '/';
	memmove(npath + n + 1, name->str, name->len);
	npath[n + name->len + 1] = '\0';

	if (stat(npath, &st)==0 || errno!=ENOENT) {
		np_werror(Eexist, EEXIST);
		goto out;
	}

	if (!fid->conn->dotu
	&& perm&(Dmnamedpipe|Dmsymlink|Dmlink|Dmdevice|Dmsocket)) {
		np_werror(Eperm, EPERM);
		goto out;
	}

	if (perm & Dmdir) {
		if (mkdir(npath, perm & 0777) < 0) {
			create_rerror(errno);
			goto out;
		}

		if (stat(npath, &f->stat) < 0) {
			create_rerror(errno);
			rmdir(npath);
			goto out;
		}
		
		f->dir = opendir(npath);
		if (!f->dir) {
			create_rerror(errno);
			remove(npath);
			goto out;
		}
	} else if (perm & Dmnamedpipe) {
		if (mknod(npath, S_IFIFO | (perm&0777), 0) < 0) {
			create_rerror(errno);
			goto out;
		}

		if (stat(npath, &f->stat) < 0) {
			create_rerror(errno);
			remove(npath);
			goto out;
		}
	} else if (perm & (Dmsymlink|Dmlink|Dmdevice)) {
		// do nothing, the files are created by wstat
		omode = Ouspecial;
		if (perm & Dmsymlink)
			qid.type = Qtsymlink;
		else if (perm & Dmlink)
			qid.type = Qtlink;
		else
			qid.type = Qttmp;

		qid.version = ~0;
		qid.path = ~0;
	} else if (perm & Dmsocket) {
		np_werror(Ecreatesocket, EIO);
		goto out;
	} else {
		f->fd = open(npath, O_CREAT|O_EXCL|omode2uflags(mode), 
			perm & 0777);
		if (f->fd < 0) {
			create_rerror(errno);
			goto out;
		}

		if (stat(npath, &f->stat) < 0) {
			create_rerror(errno);
			remove(npath);
			goto out;
		}
	}

	free(f->path);
	f->path = npath;
	f->omode = omode;
	npath = NULL;

	if ((perm & (Dmsymlink|Dmlink|Dmdevice)) == 0)
		ustat2qid(&f->stat, &qid);

	ret = np_create_rcreate(&qid, 0);

out:
	free(npath);
	return ret;
}

static u32
npfs_read_dir(Fid *f, u8* buf, u64 offset, u32 count, int dotu)
{
	int i, n, plen;
	char *dname, *path;
	struct dirent *dirent;
	struct stat st;
	Npwstat wstat;

	if (offset == 0) {
		rewinddir(f->dir);
		f->diroffset = 0;
	}

	plen = strlen(f->path);
	n = 0;
	dirent = NULL;
	dname = f->direntname;
	while (n < count) {
		if (!dname) {
			dirent = readdir(f->dir);
			if (!dirent)
				break;

			if (strcmp(dirent->d_name, ".") == 0
			|| strcmp(dirent->d_name, "..") == 0)
				continue;

			dname = dirent->d_name;
		}

		path = malloc(plen + strlen(dname) + 2);
		sprintf(path, "%s/%s", f->path, dname);
		
		if (stat(path, &st) < 0) {
			free(path);
			create_rerror(errno);
			return 0;
		}

		ustat2npwstat(path, &st, &wstat, dotu);
		i = np_serialize_stat(&wstat, buf + n, count - n - 1, dotu);
		free(wstat.extension);
		free(path);
		path = NULL;
		if (i==0)
			break;

		dname = NULL;
		n += i;
	}

	if (f->direntname) {
		free(f->direntname);
		f->direntname = NULL;
	}

	if (dirent)
		f->direntname = strdup(dirent->d_name);

	f->diroffset += n;
	return n;
}

static Npfcall*
npfs_read(Npfid *fid, u64 offset, u32 count, Npreq *req)
{
	int n;
	Fid *f;
	Npfcall *ret;

	f = fid->aux;
	ret = np_alloc_rread(count);
//	np_change_user(fid->user);
	if (f->dir)
		n = npfs_read_dir(f, ret->data, offset, count, fid->conn->dotu);
	else {
		n = pread(f->fd, ret->data, count, offset);
		if (n < 0)
			create_rerror(errno);
	}

	if (np_haserror()) {
		free(ret);
		ret = NULL;
	} else
		np_set_rread_count(ret, n);

	return ret;
}

static Npfcall*
npfs_write(Npfid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	Fid *f;

	f = fid->aux;
//	np_change_user(fid->user);
	n = pwrite(f->fd, data, count, offset);
	if (n < 0)
		create_rerror(errno);

	return np_create_rwrite(n);
}

static Npfcall*
npfs_clunk(Npfid *fid)
{
	Fid *f;
	Npfcall *ret;

	f = fid->aux;
	ret = np_create_rclunk();
	np_fid_decref(fid);
	return ret;
}

static Npfcall*
npfs_remove(Npfid *fid)
{
	Fid *f;
	Npfcall *ret;

	f = fid->aux;
//	np_change_user(fid->user);
	if (remove(f->path) < 0) {
		create_rerror(errno);
		goto out;
	}

	ret = np_create_rremove();

out:
	np_fid_decref(fid);
	return ret;

}

static Npfcall*
npfs_stat(Npfid *fid)
{
	int err;
	Fid *f;
	Npfcall *ret;
	Npwstat wstat;

	f = fid->aux;
//	np_change_user(fid->user);
	err = fidstat(f);
	if (err < 0)
		create_rerror(err);

	ustat2npwstat(f->path, &f->stat, &wstat, fid->conn->dotu);

	ret = np_create_rstat(&wstat, fid->conn->dotu);
	free(wstat.extension);

	return ret;
}

static Npfcall*
npfs_create_special(Npfid *fid, Npstat *stat)
{
	int nfid, err;
	int mode, major, minor;
	char ctype;
	mode_t umode;
	Npfid *ofid;
	Fid *f, *of;
	Npfcall *ret;
	char *ext;

	ret = NULL;
	f = fid->aux;
	if (!stat->extension.len) {
		np_werror(Enoextension, EIO);
		return NULL;
	}

	umode = npstat2umode(stat, fid->conn->dotu);

	ext = np_strdup(&stat->extension);
	if (stat->mode & Dmsymlink) {
		if (symlink(ext, f->path) < 0) {
			create_rerror(errno);
			goto out;
		}
	} else if (stat->mode & Dmlink) {
		if (sscanf(ext, "%d", &nfid) == 0) {
			np_werror(Eformat, EIO);
			goto out;
		}

		ofid = np_fid_find(fid->conn, nfid);
		if (!ofid) {
			np_werror(Eunknownfid, EIO);
			goto out;
		}

		of = ofid->aux;
		if (link(of->path, f->path) < 0) {
			create_rerror(errno);
			goto out;
		}
	} else if (stat->mode & Dmdevice) {
		if (sscanf(ext, "%c %u %u", &ctype, &major, &minor) != 3) {
			np_werror(Eformat, EIO);
			goto out;
		}

		mode = 0;
		switch (ctype) {
		case 'c':
			mode = S_IFCHR;
			break;

		case 'b':
			mode = S_IFBLK;
			break;

		default:
			np_werror(Eformat, EIO);
			goto out;
		}

		mode |= stat->mode & 0777;
		if (mknod(f->path, mode, MKDEV(major, minor)) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	f->omode = 0;
	if (chmod(f->path, umode) < 0) {
		create_rerror(errno);
		goto out;
	}

	err = fidstat(f);
	if (err < 0) {
		create_rerror(err);
		goto out;
	}

	ret = np_create_rwstat();
out:
	free(ext);
	return ret;
}


static Npfcall*
npfs_wstat(Npfid *fid, Npstat *stat)
{
	int err;
	Fid *f;
	Npfcall *ret;
	uid_t uid;
	gid_t gid;
	char *npath, *p, *s;
	Npuser *user;
	Npgroup *group;
	struct utimbuf tb;

	ret = NULL;
	f = fid->aux;
//	np_change_user(fid->user);
	if (f->omode!=-1 && f->omode&Ouspecial && fid->conn->dotu) {
		ret = npfs_create_special(fid, stat);
		goto out;
	}

	err = fidstat(f);
	if (err < 0) {
		create_rerror(err);
		goto out;
	}

	if (fid->conn->dotu) {
		uid = stat->n_uid;
		gid = stat->n_gid;
	} else {
		uid = (uid_t) -1;
		gid = (gid_t) -1;
	}

	if (uid == -1 && stat->uid.len) {
		s = np_strdup(&stat->uid);
		user = np_uname2user(s);
		free(s);
		if (!user) {
			np_werror(Eunknownuser, EIO);
			goto out;
		}

		uid = user->uid;
	}

	if (gid == -1 && stat->gid.len) {
		s = np_strdup(&stat->gid);
		group = np_gname2group(s);
		free(s);
		if (!group) {
			np_werror(Eunknownuser, EIO);
			goto out;
		}

		gid = group->gid;
	}

	if (stat->mode != (u32)~0) {
		if (stat->mode&Dmdir && !S_ISDIR(f->stat.st_mode)) {
			np_werror(Edirchange, EIO);
			goto out;
		}

		if (chmod(f->path, npstat2umode(stat, fid->conn->dotu)) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (stat->mtime != (u32)~0) {
		tb.actime = 0;
		tb.modtime = stat->mtime;
		if (utime(f->path, &tb) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (gid != -1) {
		if (chown(f->path, uid, gid) < 0) {
			create_rerror(errno);
			goto out;
		}
	}

	if (stat->name.len != 0) {
		p = strrchr(f->path, '/');
		if (!p)
			p = f->path + strlen(f->path);

		npath = malloc(stat->name.len + (p - f->path) + 2);
		memcpy(npath, f->path, p - f->path);
		npath[p - f->path] = '/';
		memcpy(npath + (p - f->path) + 1, stat->name.str, stat->name.len);
		npath[(p - f->path) + 1 + stat->name.len] = 0;
		if (strcmp(npath, f->path) != 0) {
			if (rename(f->path, npath) < 0) {
				create_rerror(errno);
				goto out;
			}

			free(f->path);
			f->path = npath;
		}
	}

	if (stat->length != ~0) {
		if (truncate(f->path, stat->length) < 0) {
			create_rerror(errno);
			goto out;
		}
	}
	ret = np_create_rwstat();
	
out:
	return ret;
}
