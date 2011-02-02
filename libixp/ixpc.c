/* Copyright ©2007-2010 Kris Maglione <maglione.k at Gmail>
 * See LICENSE file for license details.
 */
#define IXP_NO_P9_
#define IXP_P9_STRUCTS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ixp_local.h>

/* Temporary */
#define fatal(...) ixp_eprint("ixpc: fatal: " __VA_ARGS__); \

static IxpClient *client;

static void
usage(void) {
	fprintf(stderr,
		   "usage: %1$s [-a <address>] {create | read | ls [-ld] | remove | write | append} <file>\n"
		   "       %1$s [-a <address>] xwrite <file> <data>\n"
		   "       %1$s -v\n", argv0);
	exit(1);
}

/* Utility Functions */
static void
write_data(IxpCFid *fid, char *name) {
	void *buf;
	uint len;

	buf = emalloc(fid->iounit);;
	do {
		len = read(0, buf, fid->iounit);
		if(len >= 0 && ixp_write(fid, buf, len) != len)
			fatal("cannot write file '%s': %s\n", name, ixp_errbuf());
	} while(len > 0);

	free(buf);
}

static int
comp_stat(const void *s1, const void *s2) {
	Stat *st1, *st2;

	st1 = (Stat*)s1;
	st2 = (Stat*)s2;
	return strcmp(st1->name, st2->name);
}

static void
setrwx(long m, char *s) {
	static char *modes[] = {
		"---", "--x", "-w-",
		"-wx", "r--", "r-x",
		"rw-", "rwx",
	};
	strncpy(s, modes[m], 3);
}

static char *
str_of_mode(uint mode) {
	static char buf[16];

	buf[0]='-';
	if(mode & P9_DMDIR)
		buf[0]='d';
	buf[1]='-';
	setrwx((mode >> 6) & 7, &buf[2]);
	setrwx((mode >> 3) & 7, &buf[5]);
	setrwx((mode >> 0) & 7, &buf[8]);
	buf[11] = 0;
	return buf;
}

static char *
str_of_time(uint val) {
	static char buf[32];

	ctime_r((time_t*)&val, buf);
	buf[strlen(buf) - 1] = '\0';
	return buf;
}

static void
print_stat(Stat *s, int details) {
	if(details)
		fprintf(stdout, "%s %s %s %5lu %s %s\n", str_of_mode(s->mode),
				s->uid, s->gid, s->length, str_of_time(s->mtime), s->name);
	else {
		if((s->mode&P9_DMDIR) && strcmp(s->name, "/"))
			fprintf(stdout, "%s/\n", s->name);
		else
			fprintf(stdout, "%s\n", s->name);
	}
}

/* Service Functions */
static int
xappend(int argc, char *argv[]) {
	IxpCFid *fid;
	IxpStat *stat;
	char *file;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	file = EARGF(usage());
	fid = ixp_open(client, file, P9_OWRITE);
	if(fid == nil)
		fatal("Can't open file '%s': %s\n", file, ixp_errbuf());
	
	stat = ixp_stat(client, file);
	fid->offset = stat->length;
	ixp_freestat(stat);
	free(stat);
	write_data(fid, file);
	return 0;
}

static int
xwrite(int argc, char *argv[]) {
	IxpCFid *fid;
	char *file;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	file = EARGF(usage());
	fid = ixp_open(client, file, P9_OWRITE);
	if(fid == nil)
		fatal("Can't open file '%s': %s\n", file, ixp_errbuf());

	write_data(fid, file);
	return 0;
}

static int
xawrite(int argc, char *argv[]) {
	IxpCFid *fid;
	char *file, *buf, *arg;
	int nbuf, mbuf, len;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	file = EARGF(usage());
	fid = ixp_open(client, file, P9_OWRITE);
	if(fid == nil)
		fatal("Can't open file '%s': %s\n", file, ixp_errbuf());

	nbuf = 0;
	mbuf = 128;
	buf = emalloc(mbuf);
	while(argc) {
		arg = ARGF();
		len = strlen(arg);
		if(nbuf + len > mbuf) {
			mbuf <<= 1;
			buf = ixp_erealloc(buf, mbuf);
		}
		memcpy(buf+nbuf, arg, len);
		nbuf += len;
		if(argc)
			buf[nbuf++] = ' ';
	}

	if(ixp_write(fid, buf, nbuf) == -1)
		fatal("cannot write file '%s': %s\n", file, ixp_errbuf());
	return 0;
}

static int
xcreate(int argc, char *argv[]) {
	IxpCFid *fid;
	char *file;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	file = EARGF(usage());
	fid = ixp_create(client, file, 0777, P9_OWRITE);
	if(fid == nil)
		fatal("Can't create file '%s': %s\n", file, ixp_errbuf());

	if((fid->qid.type&P9_DMDIR) == 0)
		write_data(fid, file);

	return 0;
}

static int
xremove(int argc, char *argv[]) {
	char *file;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	file = EARGF(usage());
	if(ixp_remove(client, file) == 0)
		fatal("Can't remove file '%s': %s\n", file, ixp_errbuf());
	return 0;
}

static int
xread(int argc, char *argv[]) {
	IxpCFid *fid;
	char *file, *buf;
	int count;

	ARGBEGIN{
	default:
		usage();
	}ARGEND;

	file = EARGF(usage());
	fid = ixp_open(client, file, P9_OREAD);
	if(fid == nil)
		fatal("Can't open file '%s': %s\n", file, ixp_errbuf());

	buf = emalloc(fid->iounit);
	while((count = ixp_read(fid, buf, fid->iounit)) > 0)
		write(1, buf, count);

	if(count == -1)
		fatal("cannot read file/directory '%s': %s\n", file, ixp_errbuf());

	return 0;
}

static int
xls(int argc, char *argv[]) {
	IxpMsg m;
	Stat *stat;
	IxpCFid *fid;
	char *file, *buf;
	int lflag, dflag, count, nstat, mstat, i;

	lflag = dflag = 0;

	ARGBEGIN{
	case 'l':
		lflag++;
		break;
	case 'd':
		dflag++;
		break;
	default:
		usage();
	}ARGEND;

	file = EARGF(usage());

	stat = ixp_stat(client, file);
	if(stat == nil)
		fatal("cannot stat file '%s': %s\n", file, ixp_errbuf());

	if(dflag || (stat->mode&P9_DMDIR) == 0) {
		print_stat(stat, lflag);
		ixp_freestat(stat);
		return 0;
	}
	ixp_freestat(stat);

	fid = ixp_open(client, file, P9_OREAD);
	if(fid == nil)
		fatal("Can't open file '%s': %s\n", file, ixp_errbuf());

	nstat = 0;
	mstat = 16;
	stat = emalloc(sizeof(*stat) * mstat);
	buf = emalloc(fid->iounit);
	while((count = ixp_read(fid, buf, fid->iounit)) > 0) {
		m = ixp_message(buf, count, MsgUnpack);
		while(m.pos < m.end) {
			if(nstat == mstat) {
				mstat <<= 1;
				stat = ixp_erealloc(stat, sizeof(*stat) * mstat);
			}
			ixp_pstat(&m, &stat[nstat++]);
		}
	}

	qsort(stat, nstat, sizeof(*stat), comp_stat);
	for(i = 0; i < nstat; i++) {
		print_stat(&stat[i], lflag);
		ixp_freestat(&stat[i]);
	}
	free(stat);

	if(count == -1)
		fatal("cannot read directory '%s': %s\n", file, ixp_errbuf());
	return 0;
}

typedef struct exectab exectab;
struct exectab {
	char *cmd;
	int (*fn)(int, char**);
} etab[] = {
	{"append", xappend},
	{"write", xwrite},
	{"xwrite", xawrite},
	{"read", xread},
	{"create", xcreate},
	{"remove", xremove},
	{"ls", xls},
	{0, 0}
};

int
main(int argc, char *argv[]) {
	char *cmd, *address;
	exectab *tab;
	int ret;

	address = getenv("IXP_ADDRESS");

	ARGBEGIN{
	case 'v':
		printf("%s-" VERSION ", ©2007 Kris Maglione\n", argv0);
		exit(0);
	case 'a':
		address = EARGF(usage());
		break;
	default:
		usage();
	}ARGEND;

	cmd = EARGF(usage());

	if(!address)
		fatal("$IXP_ADDRESS not set\n");

	client = ixp_mount(address);
	if(client == nil)
		fatal("%s\n", ixp_errbuf());

	for(tab = etab; tab->cmd; tab++)
		if(strcmp(cmd, tab->cmd) == 0) break;
	if(tab->cmd == 0)
		usage();

	ret = tab->fn(argc, argv);

	ixp_unmount(client);
	return ret;
}
