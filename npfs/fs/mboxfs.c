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
#include <libetpan/libetpan.h>
#include "npfs.h"

#define ROOTPERM 	0755
#define NELEM(x)	(sizeof(x)/sizeof((x)[0]))

enum {
	QRAW = 1,
	QRAWBODY,
	QRAWHEADER,
	QSUBJECT,
	QTO,
	QCC,
	QFROM,
	QBODY,
	QDATE,
	QHEADER,
	QREPLYTO,
	QTYPE,
};

typedef struct Folder Folder;
typedef struct Message Message;
typedef struct Mime Mime;

struct Folder {
	int			msgnum;
	struct mailstorage*	storage;
	struct mailfolder*	folder;
};

struct Message {
	int			idx;
	char*			id;
	mailmessage*		msg;
};

struct Mime {
	mailmessage*		msg;
	struct mailmime*	mime;
	struct mailimf_fields*	fields;
	int			qpidx;
};

static void connclose(Npconn *conn);
static Npfile* folder_first(Npfile *);
static Npfile* folder_next(Npfile *, Npfile *);
static void folder_destroy(Npfile *);
static Npfile* message_first(Npfile *);
static Npfile* message_next(Npfile *, Npfile *);
static void message_destroy(Npfile *);
static u32 raw_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static void raw_destroy(Npfile *);
static u32 rawbody_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static u32 rawheader_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static u32 field_read(Npfilefid *, u64 offset, u32 count, u8* data, Npreq *);
static u32 body_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static Npfile* mime_first(Npfile *);
static Npfile* mime_next(Npfile *, Npfile *);
static u32 body_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static u32 type_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static u32 mimeheader_read(Npfilefid *, u64, u32, u8 *, Npreq *);
static void create_mime_files(Npfile *dir, Mime *m);

static Npsrv *srv;
static Npfile *root;

Npdirops folder_ops = {
	.first = folder_first,
	.next = folder_next,
	.destroy = folder_destroy,
};

Npdirops message_ops = {
	.first = message_first,
	.next = message_next,
	.destroy = message_destroy,
};

Npdirops mime_ops = {
	.first = mime_first,
	.next = mime_next,
};

Npfileops raw_ops = {
	.read = raw_read,
	.destroy = raw_destroy,
};	

Npfileops rawbody_ops = {
	.read = rawbody_read,
};

Npfileops rawheader_ops = {
	.read = rawheader_read,
};

Npfileops field_ops = {
	.read = field_read,
};

Npfileops body_ops = {
	.read = body_read,
};

Npfileops type_ops = {
	.read = type_read,
};

Npfileops mimeheader_ops = {
	.read = mimeheader_read,
};

void
usage()
{
	fprintf(stderr, "mboxfs: -d -u user -w nthreads "
		"-o mount-options mbox mount-point\n");
	exit(-1);
}

int
main(int argc, char **argv)
{
	int n, c, debuglevel, nwthreads, fd, pid;
	Npuser *user;
	char *opts, *s, *logfile;
	char *mbox, *mntpt;
	Folder *folder;

	debuglevel = 0;
	nwthreads = 1;
	opts = "";
	logfile = "/tmp/mboxfs.log";
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

	if (optind+2 != argc)
		usage();

	mbox = argv[optind];
	mntpt = argv[optind + 1];

	folder = malloc(sizeof(*folder));
	folder->storage = mailstorage_new(NULL);
	if (!folder->storage) {
		fprintf(stderr, "error initializing storage\n");
		return -1;
	}

	n = mbox_mailstorage_init(folder->storage, mbox, 0, NULL, NULL);
	if (n != MAIL_NO_ERROR) {
		fprintf(stderr, "error initializing storage\n");
		return -1;
	}

	folder->folder = mailfolder_new(folder->storage, mbox, NULL);
	if (!folder->folder) {
		fprintf(stderr, "error initializing folder\n");
		return -1;
	}

	n = mailfolder_connect(folder->folder);
	if (n != MAIL_NO_ERROR) {
		fprintf(stderr, "error initializing folder\n");
		return -1;
	}
/*
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
*/

	root = npfile_alloc(NULL, strdup(""), ROOTPERM|Dmdir, 0, &folder_ops, folder);
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
	srv->connclose = connclose;
	np_pipesrv_mount(srv, mntpt, user->uname, 0, opts);

	while (1) {
		sleep(100);
	}

	return 0;
}

static void
connclose(Npconn *conn)
{
	exit(0);
}

static int
sizebits(int size)
{
	int bits;

	bits = 0;
	while (size >>= 1)
		bits++;

	return bits;
}

static void
add_file(Npfile *dir, Npfile *file)
{
	if (dir->dirlast) {
		dir->dirlast->next = file;
		file->prev = dir->dirlast;
	} else 
		dir->dirfirst = file;

	dir->dirlast = file;
	npfile_incref(file);
}

static Npfile* 
folder_first(Npfile *dir)
{
	int i, n;
	Folder *fld;
	Message *m;
	Npfile *nf;
	struct mailmessage_list *msg_list;
	mailmessage *msg;
	char buf[16];

	fld = dir->aux;
	if (!dir->dirfirst) {
		n = mailsession_get_messages_list(fld->folder->fld_session, &msg_list);
		if (n != MAIL_NO_ERROR) {
			np_werror("cannot get message list", EIO);
			return NULL;
		}

		fld->msgnum = carray_count(msg_list->msg_tab);
		for(i = 0; i < carray_count(msg_list->msg_tab); i++) {
			msg = carray_get(msg_list->msg_tab, i);
			m = malloc(sizeof(*m));
			m->idx = msg->msg_index;
			m->id = strdup(msg->msg_uid);
			m->msg = NULL;
			sprintf(buf, "%d", m->idx);
			nf = npfile_alloc(dir, strdup(buf), 0500|Dmdir, ((u64)m->idx)<<32,
				&message_ops, m);
			add_file(dir, nf);
		}

		mailmessage_list_free(msg_list);
	}

	if (dir->dirfirst)
		npfile_incref(dir->dirfirst);

	return dir->dirfirst;
}

static Npfile*
folder_next(Npfile *dir, Npfile *prevchild)
{
	npfile_incref(prevchild->next);
	return prevchild->next;
}

static void
folder_destroy(Npfile *f)
{
	Folder *folder;

	fprintf(stderr, "folder_destroy\n");
	folder = f->aux;
	if (!folder)
		return;

	mailfolder_free(folder->folder);
	mailstorage_free(folder->storage);
	f->aux = NULL;
}

static Npfile*
message_first(Npfile *dir)
{
	int n, bits;
	Message *m;
	Folder *fld;
	Mime *mm;
	struct mailmime *mime;

	m = dir->aux;
	fld = dir->parent->aux;

	bits = sizebits(fld->msgnum) + 1;
	if (!dir->dirfirst) {
		n = mailfolder_get_message_by_uid(fld->folder, m->id, &m->msg);
		if (n != MAIL_NO_ERROR) {
			np_werror("cannot get message", EIO);
			return NULL;
		}

		n = mailmessage_get_bodystructure(m->msg, &mime);
		if (n != MAIL_NO_ERROR) {
			np_werror("cannot get body structure", EIO);
			return NULL;
		}

		mm = malloc(sizeof(*mm));
		mm->msg = m->msg;
		mm->mime = mime;
		mm->fields = NULL;
		mm->qpidx = bits;
		create_mime_files(dir, mm);
	}

	if (dir->dirfirst)
		npfile_incref(dir->dirfirst);

	return dir->dirfirst;
}

static Npfile*
message_next(Npfile *dir, Npfile *prevchild)
{
	npfile_incref(prevchild->next);
	return prevchild->next;
}

static void
message_destroy(Npfile *file)
{
	Message *m;

	fprintf(stderr, "message_destroy\n");
	m = file->aux;
	mailmessage_free(m->msg);
	free(m->id);
	free(m);
	file->aux = NULL;
}

static u32 
raw_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	size_t msgsize;
	Npfile *file;
	Mime *m;
	char *msg;

	file = fid->file;
	m = file->aux;

	n = mailmessage_fetch_section(m->msg, m->mime, &msg, &msgsize);
	if (n != MAIL_NO_ERROR) {
		np_werror("cannot fetch message", EIO);
		return 0;
	}

	n = count;
	if (msgsize < offset+count)
		n = msgsize - offset;

	if (n < 0)
		n = 0;

	memmove(data, msg + offset, n);
	mailmessage_fetch_result_free(m->msg, msg);

	return n;
}

static void
raw_destroy(Npfile *file)
{
	fprintf(stderr, "raw_destroy\n");
}

static u32 
rawbody_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	size_t msgsize;
	Npfile *file;
	Mime *m;
	char *msg;

	file = fid->file;
	m = file->aux;

	n = mailmessage_fetch_section_body(m->msg, m->mime, &msg, &msgsize);
	if (n != MAIL_NO_ERROR) {
		np_werror("cannot fetch message", EIO);
		return 0;
	}

	n = count;
	if (msgsize < offset+count)
		n = msgsize - offset;

	if (n < 0)
		n = 0;

	memmove(data, msg + offset, n);

	mailmessage_fetch_result_free(m->msg, msg);
	return n;
}

static u32 
rawheader_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	size_t msgsize;
	Npfile *file;
	Mime *m;
	char *msg;

	file = fid->file;
	m = file->aux;

	n = mailmessage_fetch_section_header(m->msg, m->mime, &msg, &msgsize);
	if (n != MAIL_NO_ERROR) {
		np_werror("cannot fetch message", EIO);
		return 0;
	}

	n = count;
	if (msgsize < offset+count)
		n = msgsize - offset;

	if (n < 0)
		n = 0;

	memmove(data, msg + offset, n);
	mailmessage_fetch_result_free(m->msg, msg);
	return n;
}

static struct mailimf_field*
mime_get_field(Mime *m, int type)
{
	int n;
	size_t len, k;
	char *data;
	clistiter *it;
	struct mailimf_field * fld;

	if (!m->fields) {
		n = mailmessage_fetch_section_header(m->msg, m->mime, &data, &len);
		if (n != MAIL_NO_ERROR) {
			np_werror("cannot get section headers", EIO);
			return NULL;
		}

		k = 0;
		n = mailimf_fields_parse(data, len, &k, &m->fields);
		mailmessage_fetch_result_free(m->msg, data);
		if (n != MAILIMF_NO_ERROR) {
			np_werror("cannot parse section headers", EIO);
			return NULL;
		}
	}

	if (!m->fields)
		return NULL;

	for(it = clist_begin(m->fields->fld_list); it != NULL; it = clist_next(it)) {
		fld = clist_content(it);
		if (fld->fld_type == type)
			return fld;
	}

	return NULL;
}

static struct mailmime_field*
mime_get_mime_field(Mime *m, int type)
{
	clistiter *it;
	struct mailmime_field *fld;

	for(it = clist_begin(m->mime->mm_mime_fields->fld_list); it != NULL; it = clist_next(it)) {
		fld = clist_content(it);
		if (fld->fld_type == type)
			return fld;
	}

	return NULL;
}

static void
create_field_file(Npfile *dir, Mime *m, char *name, 
	int fldname, u64 qpath)
{
	Npfile *nf;
	struct mailimf_field* fld;

	fld = mime_get_field(m, fldname);
	nf = npfile_alloc(dir, strdup(name), 0400, dir->qid.path|qpath, &field_ops, fld);
	add_file(dir, nf);
}

static void
create_mime_files(Npfile *dir, Mime *m)
{
	int i, n;
	Npfile *nf;
	Mime *nm;
	struct mailmime *mm;
	clistiter *it;
	char buf[16];

	switch (m->mime->mm_type) {
	case MAILMIME_MESSAGE:
		create_field_file(dir, m, "subject", MAILIMF_FIELD_SUBJECT, QSUBJECT);
		create_field_file(dir, m, "from", MAILIMF_FIELD_FROM, QFROM);
		create_field_file(dir, m, "to", MAILIMF_FIELD_TO, QTO);
		create_field_file(dir, m, "cc", MAILIMF_FIELD_CC, QCC);
		create_field_file(dir, m, "replyto", MAILIMF_FIELD_REPLY_TO, QREPLYTO);
		create_field_file(dir, m, "date", MAILIMF_FIELD_ORIG_DATE, QDATE);
		nf = npfile_alloc(dir, strdup("raw"), 0400, dir->qid.path|QRAW, &raw_ops, m);
		add_file(dir, nf);
		nf = npfile_alloc(dir, strdup("rawbody"), 0400, dir->qid.path|QRAWBODY, 
			&rawbody_ops, m);
		add_file(dir, nf);
		nf = npfile_alloc(dir, strdup("rawheader"), 0400, dir->qid.path|QRAWHEADER, 
			&rawheader_ops, m);
		add_file(dir, nf);

		mm = m->mime->mm_data.mm_message.mm_msg_mime;
		if (mm->mm_type == MAILMIME_SINGLE) {
			nm = malloc(sizeof(*nm));
			nm->msg = m->msg;
			nm->mime = mm;
			nm->fields = NULL;
			nm->qpidx = m->qpidx;
			create_mime_files(dir, nm);
		} else if (mm->mm_type == MAILMIME_MULTIPLE) {
			nm = malloc(sizeof(*nm));
			nm->msg = m->msg;
			nm->mime = mm;
			nm->fields = NULL;
			nm->qpidx = m->qpidx;
			create_mime_files(dir, nm);
		}
		break;

	case MAILMIME_MULTIPLE:
		n = sizebits(clist_count(m->mime->mm_data.mm_multipart.mm_mp_list));
		if (dir != root)
			n += m->qpidx;

		nf = npfile_alloc(dir, strdup("type"), 0400, dir->qid.path|QTYPE, 
			&type_ops, m);
		add_file(dir, nf);

		nf = npfile_alloc(dir, strdup("mimeheader"), 0400, dir->qid.path|QRAWHEADER, 
			&mimeheader_ops, m);
		add_file(dir, nf);

		for(i = 0, it = clist_begin(m->mime->mm_data.mm_multipart.mm_mp_list)
		; it != NULL; it = clist_next(it), i++) {
			mm = clist_content(it);
			nm = malloc(sizeof(*nm));
			nm->msg = m->msg;
			nm->mime = mm;
			nm->fields = NULL;
			nm->qpidx = n;
			sprintf(buf, "%d", i);
			nf = npfile_alloc(dir, strdup(buf), 0500|Dmdir,
				dir->qid.path | i<<n, &mime_ops, nm);
			add_file(dir, nf);
		}
		break;

	case MAILMIME_SINGLE:
		nf = npfile_alloc(dir, strdup("type"), 0400, dir->qid.path|QTYPE, 
			&type_ops, m);
		add_file(dir, nf);
		nf = npfile_alloc(dir, strdup("mimeheader"), 0400, dir->qid.path|QRAWHEADER, 
			&mimeheader_ops, m);
		add_file(dir, nf);
		nf = npfile_alloc(dir, strdup("body"), 0400, dir->qid.path|QBODY, &body_ops, m);
		add_file(dir, nf);
		break;
	}
}


static Npfile*
mime_first(Npfile *dir)
{
	if (!dir->dirfirst) {
		create_mime_files(dir, dir->aux);
	}

	if (dir->dirfirst)
		npfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

static Npfile*
mime_next(Npfile *dir, Npfile* prevchild)
{
	npfile_incref(prevchild->next);
	return prevchild->next;
}

static int
cutstr(u8 *target, int toffset, int tcount, char *src, int soffset)
{
	int b, e;
	int slen;

	if (!src)
		return 0;

	slen = strlen(src);
	if (toffset > soffset+slen)
		return 0;

	if (soffset > toffset+tcount)
		return 0;

	b = soffset;
	if (b < toffset)
		b = toffset;

	e = soffset+slen;
	if (e > (toffset+tcount))
		e = toffset+tcount;

	memmove(target+(b-toffset), src+(b-soffset), e-b);
	return e-b;
}

static u32 
print_mailaddr(u8* data, u64 offset, u32 count, struct mailimf_mailbox_list *mb, int n)
{
	clistiter *it;
	char buf[256];
	struct mailimf_mailbox *mbox;

	for(it = clist_begin(mb->mb_list); it != NULL; it = clist_next(it)) {
		mbox = clist_content(it);

		if (mbox->mb_display_name)
			snprintf(buf, sizeof(buf), "%s <%s>\n", 
				mbox->mb_display_name, mbox->mb_addr_spec);
		else
			snprintf(buf, sizeof(buf), "%s\n", mbox->mb_addr_spec);

		n += cutstr(data, offset, count, buf, n);
		if (n >= count)
			break;
	}

	return n;
}

static u32 
print_mailaddrlist(u8* data, u64 offset, u32 count, struct mailimf_address_list *alist)
{
	int n ;
	clistiter *it, *it2;
	struct mailimf_mailbox *mbox;
	struct mailimf_address *addr;
	char buf[256];

	n = 0;
	for(it = clist_begin(alist->ad_list); it != NULL; it = clist_next(it)) {
		addr = clist_content(it);
		if (addr->ad_type == MAILIMF_ADDRESS_GROUP) {
			for(it2 = clist_begin(addr->ad_data.ad_group->grp_mb_list->mb_list);
				it2 != NULL; it2 = clist_next(it2)) {

				mbox = clist_content(it2);
				n += print_mailaddr(data, offset, count, 
					clist_content(it2), n);

				if (n > count)
					break;
			}
		} else if (addr->ad_type == MAILIMF_ADDRESS_MAILBOX) {
			mbox = addr->ad_data.ad_mailbox;
			if (mbox->mb_display_name)
				snprintf(buf, sizeof(buf), "%s <%s>\n", 
					mbox->mb_display_name, mbox->mb_addr_spec);
			else
				snprintf(buf, sizeof(buf), "%s\n", mbox->mb_addr_spec);

			n += cutstr(data, offset, count, buf, n);
		}

		if (n > count)
			break;
	}

	return n;
}


static u32 
field_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n;
	Npfile *file;
	struct mailimf_field* fld;
//	Mime *m;
	struct mailimf_date_time *dt;
	struct tm tm;
	time_t t;
	char buf[64];

	file = fid->file;
	fld = file->aux;
//	m = file->parent->aux;

	n = 0;
	switch (fld->fld_type) {
	case MAILIMF_FIELD_SUBJECT:
		n = cutstr(data, offset, count, 
			fld->fld_data.fld_subject->sbj_value, 0);
		break;

	case MAILIMF_FIELD_FROM:
		n = print_mailaddr(data, offset, count, 
			fld->fld_data.fld_from->frm_mb_list, 0);
		break;

	case MAILIMF_FIELD_TO:
		n = print_mailaddrlist(data, offset, count, 
			fld->fld_data.fld_to->to_addr_list);
		break;

	case MAILIMF_FIELD_CC:
		n = print_mailaddrlist(data, offset, count, 
			fld->fld_data.fld_cc->cc_addr_list);
		break;

	case MAILIMF_FIELD_REPLY_TO:
		n = print_mailaddrlist(data, offset, count, 
			fld->fld_data.fld_reply_to->rt_addr_list);
		break;

	case MAILIMF_FIELD_ORIG_DATE:
		dt = fld->fld_data.fld_orig_date->dt_date_time;
		tm.tm_sec = dt->dt_sec;
		tm.tm_min = dt->dt_min;
		tm.tm_hour = dt->dt_hour;
		tm.tm_mday = dt->dt_day;
		tm.tm_mon = dt->dt_month - 1;
		tm.tm_year = dt->dt_year - 1900;
		t = mktime(&tm);
		localtime_r(&t, &tm);
		asctime_r(&tm, buf);
		buf[strlen(buf) - 1] = 0;
		n += cutstr(data, offset, count, buf, n);
//		snprintf(buf, sizeof(buf), "%04d-%02d-%02d", dt->dt_year, 
//			dt->dt_month, dt->dt_day);
//		n = cutstr(data, offset, count, buf, 0);
		snprintf(buf, sizeof(buf), " %d", dt->dt_zone);
		n += cutstr(data, offset, count, buf, n);
		break;
	}

	return n;
}

static u32 
body_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n, encoding;
	Npfile *file;
	Mime *m;
	char *buf, *decbuf;
	size_t buflen, declen, k;
	struct mailmime_field *efld;

	file = fid->file;
	m = file->aux;
	n = mailmessage_fetch_section(m->msg, m->mime, &buf, &buflen);
	if (n != MAIL_NO_ERROR) {
		np_werror("cannot fetch message", EIO);
		return 0;
	}

	encoding = MAILMIME_MECHANISM_8BIT;
	efld = mime_get_mime_field(m, MAILMIME_FIELD_TRANSFER_ENCODING);
	if (efld)
		encoding = efld->fld_data.fld_encoding->enc_type;
	k = 0;
	n = mailmime_part_parse(buf, buflen, &k,
		encoding, &decbuf, &declen);

	if (n != MAIL_NO_ERROR) {
		np_werror("cannot parse part", EIO);
		mailmessage_fetch_result_free(m->msg, buf);
		return 0;
	}
	
	n = count;
	if (declen < offset+count)
		n = declen - offset;

	if (n < 0)
		n = 0;

	if (n)
		memmove(data, decbuf + offset, n);

	mailmime_decoded_part_free(decbuf);

	return n;
}

static u32
print_content_type(u8* data, u64 offset, u32 count, 
	struct mailmime_content *cnt, int n)
{
	int m;
	char buf[64];
	clistiter *it;
	struct mailmime_parameter *mp;

	m = 0;
	switch (cnt->ct_type->tp_type) {
	case MAILMIME_TYPE_DISCRETE_TYPE:
		switch (cnt->ct_type->tp_data.tp_discrete_type->dt_type) {
		case MAILMIME_DISCRETE_TYPE_TEXT:
			m = snprintf(buf, sizeof(buf), "text");
			break;

		case MAILMIME_DISCRETE_TYPE_IMAGE:
			m = snprintf(buf, sizeof(buf), "image");
			break;

		case MAILMIME_DISCRETE_TYPE_AUDIO:
			m = snprintf(buf, sizeof(buf), "audio");
			break;

		case MAILMIME_DISCRETE_TYPE_VIDEO:
			m = snprintf(buf, sizeof(buf), "video");
			break;

		case MAILMIME_DISCRETE_TYPE_APPLICATION:
			m = snprintf(buf, sizeof(buf), "application");
			break;

		case MAILMIME_DISCRETE_TYPE_EXTENSION:
			m = snprintf(buf, sizeof(buf), "%s", 
				cnt->ct_type->tp_data.tp_discrete_type->dt_extension);
			break;
		}

		break;

	case MAILMIME_TYPE_COMPOSITE_TYPE:
		switch (cnt->ct_type->tp_data.tp_composite_type->ct_type) {
		case MAILMIME_COMPOSITE_TYPE_MESSAGE:
			m = snprintf(buf, sizeof(buf), "message");
			break;
		
		case MAILMIME_COMPOSITE_TYPE_MULTIPART:
			m = snprintf(buf, sizeof(buf), "multipart");
			break;
		
		case MAILMIME_COMPOSITE_TYPE_EXTENSION:
			m = snprintf(buf, sizeof(buf), "%s", 
				cnt->ct_type->tp_data.tp_composite_type->ct_token);
			break;
		}
		
		break;

	default:
		return 0;
	}

	snprintf(buf+m, sizeof(buf)-m, "/%s", cnt->ct_subtype);

	n += cutstr(data, offset, count, buf, n);
	for(it = clist_begin(cnt->ct_parameters); it != NULL; it = clist_next(it)) {
		mp = clist_content(it);
		n += cutstr(data, offset, count, ";", n);
		n += cutstr(data, offset, count, mp->pa_name, n);
		n += cutstr(data, offset, count, "=", n);
		n += cutstr(data, offset, count, mp->pa_value, n);
		if (n >= count)
			break;
	}

	return n;

}

static u32 
type_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	Npfile *file;
	Mime *m;

	file = fid->file;
	m = file->aux;

	return print_content_type(data, offset, count, m->mime->mm_content_type, 0);
}

static u32
print_transfer_encoding(u8* data, u64 offset, u32 count, 
	struct mailmime_mechanism *fld, int n)
{

	switch (fld->enc_type) {
	case MAILMIME_MECHANISM_7BIT:
		n += cutstr(data, offset, count, "7bit", n);
		break;

	case MAILMIME_MECHANISM_8BIT:
		n += cutstr(data, offset, count, "8bit", n);
		break;

	case MAILMIME_MECHANISM_BINARY:
		n += cutstr(data, offset, count, "binary", n);
		break;

	case MAILMIME_MECHANISM_QUOTED_PRINTABLE:
		n += cutstr(data, offset, count, "quoted-printable", n);
		break;

	case MAILMIME_MECHANISM_BASE64:
		n += cutstr(data, offset, count, "base64", n);
		break;

	case MAILMIME_MECHANISM_TOKEN:
		n += cutstr(data, offset, count, fld->enc_token, n);
	}

	return n;
}

static u32
print_disposition(u8* data, u64 offset, u32 count, 
	struct mailmime_disposition *fld, int n)
{
	struct mailmime_disposition_type *dt;
	struct mailmime_disposition_parm *dp;
	clistiter *it;
	char buf[64];

	dt = fld->dsp_type;
	switch (dt->dsp_type) {
	case MAILMIME_DISPOSITION_TYPE_INLINE:
		n += cutstr(data, offset, count, "inline", n);
		break;

	case MAILMIME_DISPOSITION_TYPE_ATTACHMENT:
		n += cutstr(data, offset, count, "attachment", n);
		break;

	case MAILMIME_DISPOSITION_TYPE_EXTENSION:
		n += cutstr(data, offset, count, dt->dsp_extension, n);
		break;
	}

	for(it = clist_begin(fld->dsp_parms); it != NULL; it = clist_next(it)) {
		dp = clist_content(it);
		n += cutstr(data, offset, count, "; ", n);

		switch (dp->pa_type) {
		case MAILMIME_DISPOSITION_PARM_FILENAME:
			n += cutstr(data, offset, count, "filename=", n);
			n += cutstr(data, offset, count, 
				dp->pa_data.pa_filename, n);
			break;

		case MAILMIME_DISPOSITION_PARM_CREATION_DATE:
			n += cutstr(data, offset, count, "creation-date=", n);
			n += cutstr(data, offset, count, 
				dp->pa_data.pa_creation_date, n);
			break;

		case MAILMIME_DISPOSITION_PARM_MODIFICATION_DATE:
			n += cutstr(data, offset, count, "modification-date=", n);
			n += cutstr(data, offset, count, 
				dp->pa_data.pa_modification_date, n);
			break;

		case MAILMIME_DISPOSITION_PARM_READ_DATE:
			n += cutstr(data, offset, count, "read-date=", n);
			n += cutstr(data, offset, count, 
				dp->pa_data.pa_read_date, n);
			break;

		case MAILMIME_DISPOSITION_PARM_SIZE:
			n += cutstr(data, offset, count, "size=", n);
			snprintf(buf, sizeof(buf), "%d", dp->pa_data.pa_size);
			n += cutstr(data, offset, count, buf, n);
			break;

		case MAILMIME_DISPOSITION_PARM_PARAMETER:
			n += cutstr(data, offset, count, 
				dp->pa_data.pa_parameter->pa_name, n);
			n += cutstr(data, offset, count, "=", n);
			n += cutstr(data, offset, count, 
				dp->pa_data.pa_parameter->pa_value, n);
			break;
		}
	}

	return n;
}

static u32
print_language(u8* data, u64 offset, u32 count, struct mailmime_language *cnt, int n)
{
	return 0;
}

static u32 
mimeheader_read(Npfilefid *fid, u64 offset, u32 count, u8 *data, Npreq *req)
{
	int n, encoding;
	Npfile *file;
	Mime *m;
	char buf[256];
	struct mailmime_field *fld;
	clistiter *it;
	int ct;

	file = fid->file;
	m = file->aux;
	ct = 0;
	n = 0;
	for(it = clist_begin(m->mime->mm_mime_fields->fld_list); it != NULL; it = clist_next(it)) {
		fld = clist_content(it);
		switch (fld->fld_type) {
		case MAILMIME_FIELD_TYPE:
			n += cutstr(data, offset, count, "Content-Type: ", n);
			n += print_content_type(data, offset, count,
				fld->fld_data.fld_content, n);
			ct = 1;
			break;

		case MAILMIME_FIELD_TRANSFER_ENCODING:
			n += cutstr(data, offset, count, "Transfer-Encoding: ", n);
			n += print_transfer_encoding(data, offset, count, 
				fld->fld_data.fld_encoding, n);
			break;

		case MAILMIME_FIELD_ID:
			n += cutstr(data, offset, count, "Id: ", n);
			n += cutstr(data, offset, count, 
				fld->fld_data.fld_id, n);
			break;

		case MAILMIME_FIELD_DESCRIPTION:
			n += cutstr(data, offset, count, "Description: ", n);
			n += cutstr(data, offset, count, 
				fld->fld_data.fld_description, n);
			break;

		case MAILMIME_FIELD_VERSION:
			n += cutstr(data, offset, count, "MIME-Version: ", n);
			snprintf(buf, sizeof(buf), "%d.%d", 
				fld->fld_data.fld_version>>16, 
				fld->fld_data.fld_version & 0xffff);
			n += cutstr(data, offset, count, buf, n);
			break;

		case MAILMIME_FIELD_DISPOSITION:
			n += cutstr(data, offset, count, "Content-Disposition: ", n);
			n += print_disposition(data, offset, count, 
				fld->fld_data.fld_disposition, n);
			break;

		case MAILMIME_FIELD_LANGUAGE:
			n += cutstr(data, offset, count, "Language: ", n);
			n += print_language(data, offset, count, 
				fld->fld_data.fld_language, n);
			break;
		}

		n += cutstr(data, offset, count, "\n", n);
		if (n >= count)
			break;

	}

	if (!ct) {
		n += cutstr(data, offset, count, "Content-Type: ", n);
		n += print_content_type(data, offset, count,
			m->mime->mm_content_type, n);
		n += cutstr(data, offset, count, "\n", n);
	}

	return n;
}
