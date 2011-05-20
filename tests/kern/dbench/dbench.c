/* 
   Copyright (C) by Andrew Tridgell <tridge@samba.org> 1999-2007
   Copyright (C) 2001 by Martin Pool <mbp@samba.org>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

/* TODO: We could try allowing for different flavours of synchronous
   operation: data sync and so on.  Linux apparently doesn't make any
   distinction, however, and for practical purposes it probably
   doesn't matter.  On NFSv4 it might be interesting, since the client
   can choose what kind it wants for each OPEN operation. */

#include "dbench.h"
#include "popt.h"
#include <sys/sem.h>

struct options options = {
	.timelimit           = 600,
	.loadfile            = DATADIR "/client.txt",
	.directory           = ".",
	.tcp_options         = TCP_OPTIONS,
	.nprocs              = 10,
	.sync_open           = 0,
	.sync_dirs           = 0,
	.do_fsync            = 0,
	.fsync_frequency     = 0,
	.warmup              = -1,
	.targetrate          = 0.0,
	.ea_enable           = 0,
	.clients_per_process = 1,
	.server              = "localhost",
};

static struct timeval tv_start;
static struct timeval tv_end;
static int barrier=-1;
static double throughput;

static FILE *open_loadfile(void)
{
	FILE		*f;

	if ((f = fopen(options.loadfile, "rt")) != NULL)
		return f;

	fprintf(stderr,
		"dbench: error opening '%s': %s\n", options.loadfile,
		strerror(errno));

	return NULL;
}


static struct child_struct *children;

static void sem_cleanup() {
	if (!(barrier==-1)) 
		semctl(barrier,0,IPC_RMID);
}

static void sig_alarm(int sig)
{
	double total_bytes = 0;
	int total_lines = 0;
	int i;
	int nclients = options.nprocs * options.clients_per_process;
	int in_warmup = 0;
	double t;
	static int in_cleanup;
	double latency;
	struct timeval tnow;
	int num_active = 0;
	int num_finished = 0;
	(void)sig;

	tnow = timeval_current();

	for (i=0;i<nclients;i++) {
		total_bytes += children[i].bytes - children[i].bytes_done_warmup;
		if (children[i].bytes == 0) {
			in_warmup = 1;
		} else {
			num_active++;
		}
		total_lines += children[i].line;
		if (children[i].cleanup_finished) {
			num_finished++;
		}
	}

	t = timeval_elapsed(&tv_start);

	if (!in_warmup && options.warmup>0 && t > options.warmup) {
		tv_start = tnow;
		options.warmup = 0;
		for (i=0;i<nclients;i++) {
			children[i].bytes_done_warmup = children[i].bytes;
			children[i].worst_latency = 0;
			memset(&children[i].op, 0, sizeof(children[i].op));
		}
		goto next;
	}
	if (t < options.warmup) {
		in_warmup = 1;
	} else if (!in_warmup && !in_cleanup && t > options.timelimit) {
		for (i=0;i<nclients;i++) {
			children[i].done = 1;
		}
		tv_end = tnow;
		in_cleanup = 1;
	}
	if (t < 1) {
		goto next;
	}

	latency = 0;
	if (!in_cleanup) {
		for (i=0;i<nclients;i++) {
			latency = MAX(children[i].max_latency, latency);
			latency = MAX(latency, timeval_elapsed2(&children[i].lasttime, &tnow));
			children[i].max_latency = 0;
			if (latency > children[i].worst_latency) {
				children[i].worst_latency = latency;
			}
		}
	}

        if (in_warmup) {
                printf("%4d  %8d  %7.2f MB/sec  warmup %3.0f sec  latency %.03f ms\n", 
                       num_active, total_lines/nclients, 
                       1.0e-6 * total_bytes / t, t, latency*1000);
        } else if (in_cleanup) {
                printf("%4d  cleanup %3.0f sec\n", nclients - num_finished, t);
        } else {
                printf("%4d  %8d  %7.2f MB/sec  execute %3.0f sec  latency %.03f ms\n", 
                       nclients, total_lines/nclients, 
                       1.0e-6 * total_bytes / t, t, latency*1000);
		throughput = 1.0e-6 * total_bytes / t;
        }

	fflush(stdout);
next:
	signal(SIGALRM, sig_alarm);
	alarm(PRINT_FREQ);
}


static const struct {
	const char *name;
	size_t offset;
} op_names[] = {
#define OP_NAME(opname) { #opname, offsetof(struct opnames, op_ ## opname) }
	OP_NAME(NTCreateX),
	OP_NAME(Close),
	OP_NAME(Rename),
	OP_NAME(Unlink),
	OP_NAME(Deltree),
	OP_NAME(Rmdir),
	OP_NAME(Mkdir),
	OP_NAME(Qpathinfo),
	OP_NAME(Qfileinfo),
	OP_NAME(Qfsinfo),
	OP_NAME(Sfileinfo),
	OP_NAME(Find),
	OP_NAME(WriteX),
	OP_NAME(ReadX),
	OP_NAME(LockX),
	OP_NAME(UnlockX),
	OP_NAME(Flush),
};

static void show_one_latency(struct opnames *ops, struct opnames *ops_all)
{
	int i, n = (sizeof(op_names)/sizeof(op_names[0]));
	printf(" Operation      Count    AvgLat    MaxLat\n");
	printf(" ----------------------------------------\n");
	for (i=0;i<n;i++) {
		struct op *op1, *op_all;
		op1    = (struct op *)(op_names[i].offset + (char *)ops);
		op_all = (struct op *)(op_names[i].offset + (char *)ops_all);
		if (op_all->count == 0) continue;
		printf(" %-12s %7u %9.03f %9.03f\n",
		       op_names[i].name, op1->count, 
		       1000*op1->total_time/op1->count,
		       op1->max_latency*1000);
	}
	printf("\n");
}

static void report_latencies(void)
{
	struct opnames sum;
	int i, j, n = (sizeof(op_names)/sizeof(op_names[0]));
	struct op *op1, *op2;
	struct child_struct *child;

	memset(&sum, 0, sizeof(sum));
	for (i=0;i<n;i++) {
		op1 = (struct op *)(op_names[i].offset + (char *)&sum);
		for (j=0;j<options.nprocs * options.clients_per_process;j++) {
			child = &children[j];
			op2 = (struct op *)(op_names[i].offset + (char *)&child->op);
			op1->count += op2->count;
			op1->total_time += op2->total_time;
			op1->max_latency = MAX(op1->max_latency, op2->max_latency);
		}
	}
	show_one_latency(&sum, &sum);

	if (!options.per_client_results) {
		return;
	}

	printf("Per client results:\n");
	for (i=0;i<options.nprocs * options.clients_per_process;i++) {
		child = &children[i];
		printf("Client %u did %u lines and %.0f bytes\n", 
		       i, child->line, child->bytes - child->bytes_done_warmup);
		show_one_latency(&child->op, &sum);		
	}
}

/* this creates the specified number of child processes and runs fn()
   in all of them */
static void create_procs(int nprocs, void (*fn)(struct child_struct *, const char *))
{
	int nclients = nprocs * options.clients_per_process;
	int i, status;
	int synccount;
	struct timeval tv;
	FILE *load;
	struct sembuf sbuf;
	double t;

	load = open_loadfile();
	if (load == NULL) {
		exit(1);
	}

	if (nprocs < 1) {
		fprintf(stderr,
			"create %d procs?  you must be kidding.\n",
			nprocs);
		return;
	}

	children = shm_setup(sizeof(struct child_struct)*nclients);
	if (!children) {
		printf("Failed to setup shared memory\n");
		return;
	}

	memset(children, 0, sizeof(*children)*nclients);

	for (i=0;i<nclients;i++) {
		children[i].id = i;
		children[i].cleanup = 0;
		children[i].directory = options.directory;
		children[i].starttime = timeval_current();
		children[i].lasttime = timeval_current();
	}

	if (atexit(sem_cleanup) != 0) {
		printf("can't register cleanup function on exit\n");
		exit(1);
	}
	sbuf.sem_num =  0;
	if ( !(barrier = semget(IPC_PRIVATE,1,IPC_CREAT | S_IRUSR | S_IWUSR)) ) {
		printf("failed to create barrier semaphore \n");
	}
	sbuf.sem_flg =  SEM_UNDO;
	sbuf.sem_op  =  1;
	if (semop(barrier, &sbuf, 1) == -1) {
		printf("failed to initialize the barrier semaphore\n");
		exit(1);
	}
	sbuf.sem_flg =  0;

	for (i=0;i<nprocs;i++) {
		if (fork() == 0) {
			int j;

			setlinebuf(stdout);

			for (j=0;j<options.clients_per_process;j++) {
				nb_setup(&children[i*options.clients_per_process + j]);
			}

			sbuf.sem_op = 0;
			if (semop(barrier, &sbuf, 1) == -1) {
				printf("failed to use the barrier semaphore in child %d\n",getpid());
				exit(1);
			}

			semctl(barrier,0,IPC_RMID);

			fn(&children[i*options.clients_per_process], options.loadfile);
			_exit(0);
		}
	}

	synccount = 0;
	tv = timeval_current();
	do {
		synccount = semctl(barrier,0,GETZCNT);
		t = timeval_elapsed(&tv);
		printf("%d of %d processes prepared for launch %3.0f sec\n", synccount, nprocs, t);
		if (synccount == nprocs) break;
		usleep(100*1000);
	} while (timeval_elapsed(&tv) < 30);

	if (synccount != nprocs) {
		printf("FAILED TO START %d CLIENTS (started %d)\n", nprocs, synccount);
		return;
	}

	printf("releasing clients\n");
	tv_start = timeval_current();
	sbuf.sem_op  =  -1;
	if (semop(barrier, &sbuf, 1) == -1) {
		printf("failed to release barrier\n");
		exit(1);
	}

	semctl(barrier,0,IPC_RMID);

	signal(SIGALRM, sig_alarm);
	alarm(PRINT_FREQ);

	for (i=0;i<nprocs;) {
		if (waitpid(0, &status, 0) == -1) continue;
		if (WEXITSTATUS(status) != 0) {
			printf("Child failed with status %d\n",
			       WEXITSTATUS(status));
			exit(1);
		}
		i++;
	}

	alarm(0);
	sig_alarm(SIGALRM);

	printf("\n");

	report_latencies();
}


static void show_usage(void)
{
	printf("usage: dbench [OPTIONS] nprocs\n" \
	       "usage: tbench [OPTIONS] nprocs <server>\n" \
	       "options:\n" \
	       "  -v               show version\n" \
	       "  -t timelimit     run time in seconds (default 600)\n" \
	       "  -D directory     base directory to run in\n" \
	       "  -c loadfile      set location of the loadfile\n" \
	       "  -R               target rate (MByte/sec)\n" \
	       "  -s               synchronous file IO\n" \
	       "  -F               fsync on write\n" \
	       "  -S               synchronous directories (mkdir, unlink...)\n" \
	       "  -x               enable EA support\n" \
	       "  -T options       set socket options for tbench\n");
	exit(1);
}



static int process_opts(int argc, const char **argv)
{
	const char **extra_argv;
	int extra_argc = 0;
	struct poptOption popt_options[] = {
		POPT_AUTOHELP
		{ "timelimit", 't', POPT_ARG_INT, &options.timelimit, 0, 
		  "timelimit", "integer" },
		{ "loadfile",  'c', POPT_ARG_STRING, &options.loadfile, 0, 
		  "loadfile", "filename" },
		{ "directory", 'D', POPT_ARG_STRING, &options.directory, 0, 
		  "working directory", NULL },
		{ "tcp-options", 'T', POPT_ARG_STRING, &options.tcp_options, 0, 
		  "TCP socket options", NULL },
		{ "target-rate", 'R', POPT_ARG_DOUBLE, &options.targetrate, 0, 
		  "target throughput (MB/sec)", NULL },
		{ "sync", 's', POPT_ARG_NONE, &options.sync_open, 0, 
		  "use O_SYNC", NULL },
		{ "sync-dir", 'S', POPT_ARG_NONE, &options.sync_dirs, 0, 
		  "sync directory changes", NULL },
		{ "fsync", 'F', POPT_ARG_NONE, &options.do_fsync, 0, 
		  "fsync on write", NULL },
		{ "xattr", 'x', POPT_ARG_NONE, &options.ea_enable, 0, 
		  "use xattrs", NULL },
		{ "no-resolve", 0, POPT_ARG_NONE, &options.no_resolve, 0, 
		  "disable name resolution simulation", NULL },
		{ "clients-per-process", 0, POPT_ARG_INT, &options.clients_per_process, 0, 
		  "number of clients per process", NULL },
		{ "one-byte-write-fix", 0, POPT_ARG_NONE, &options.one_byte_write_fix, 0, 
		  "try to fix 1 byte writes", NULL },
		{ "stat-check", 0, POPT_ARG_NONE, &options.stat_check, 0, 
		  "check for pointless calls with stat", NULL },
		{ "fake-io", 0, POPT_ARG_NONE, &options.fake_io, 0, 
		  "fake up read/write calls", NULL },
		{ "skip-cleanup", 0, POPT_ARG_NONE, &options.skip_cleanup, 0, 
		  "skip cleanup operations", NULL },
		{ "per-client-results", 0, POPT_ARG_NONE, &options.per_client_results, 0, 
		  "show results per client", NULL },
		POPT_TABLEEND
	};
	poptContext pc;
	int opt;

	pc = poptGetContext(argv[0], argc, argv, popt_options, POPT_CONTEXT_KEEP_FIRST);

	while ((opt = poptGetNextOpt(pc)) != -1) {
		switch (opt) {
		default:
			fprintf(stderr, "Invalid option %s: %s\n", 
				poptBadOption(pc, 0), poptStrerror(opt));
			exit(1);
		}
	}

	/* setup the remaining options for the main program to use */
	extra_argv = poptGetArgs(pc);
	if (extra_argv) {
		extra_argv++;
		while (extra_argv[extra_argc]) extra_argc++;
	}

	if (extra_argc < 1) {
		printf("You need to specify NPROCS\n");
		poptPrintHelp(pc, stdout, 0);
		exit(1);
	}

#ifndef HAVE_EA_SUPPORT
	if (options.ea_enable) {
		printf("EA suppport not compiled in\n");
		exit(1);
	}
#endif
	
	options.nprocs = atoi(extra_argv[0]);

	if (extra_argc >= 2) {
		options.server = extra_argv[1];
	}

	return 1;
}



 int main(int argc, const char *argv[])
{
	double total_bytes = 0;
	double t, latency=0;
	int i;

	setlinebuf(stdout);

	printf("dbench version %s - Copyright Andrew Tridgell 1999-2004\n\n", VERSION);

	if (!process_opts(argc, argv))
		show_usage();

	if (options.warmup == -1) {
		options.warmup = options.timelimit / 5;
	}

        printf("Running for %d seconds with load '%s' and minimum warmup %d secs\n", 
               options.timelimit, options.loadfile, options.warmup);

	create_procs(options.nprocs, child_run);

	for (i=0;i<options.nprocs*options.clients_per_process;i++) {
		total_bytes += children[i].bytes - children[i].bytes_done_warmup;
		latency = MAX(latency, children[i].worst_latency);
	}

	t = timeval_elapsed2(&tv_start, &tv_end);

	printf("Throughput %g MB/sec%s%s  %d clients  %d procs  max_latency=%.03f ms\n", 
	       throughput,
	       options.sync_open ? " (sync open)" : "",
	       options.sync_dirs ? " (sync dirs)" : "", 
	       options.nprocs*options.clients_per_process,
	       options.nprocs, latency*1000);
	return 0;
}
