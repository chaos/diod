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
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <linux/unistd.h>

#define MS_MOVE 8192

static char *host;
static int port;
static char *uname;
static char *cmd;
static char **args;
static char mntpt[256];

int
procfn(void *a)
{
	int n;
	char opts[256];
	char mdir[256];

	snprintf(opts, sizeof(opts), "name=%s,port=%d,debug=1", uname, port);
	n = mount(host, mntpt, "9P", 0, opts);
	if (n < 0) {
		perror("mount");
		return -1;
	}

	snprintf(mdir, sizeof(mdir), "%s/dev", mntpt);
	n = mount("/dev", mdir, NULL, MS_BIND, NULL);
	if (n < 0) {
		perror("bind dev");
		return -1;
	}

	snprintf(mdir, sizeof(mdir), "%s/proc", mntpt);
	n = mount("/proc", mdir, NULL, MS_BIND, NULL);
	if (n < 0) {
		perror("bind proc");
		return -1;
	}

	snprintf(mdir, sizeof(mdir), "%s/sys", mntpt);
	n = mount("/sys", mdir, NULL, MS_BIND, NULL);
	if (n < 0) {
		perror("bind sys");
		return -1;
	}

	chdir(mntpt);

	n = mount(mntpt, "/", NULL, MS_MOVE, NULL);
	if (n < 0) {
		perror("move mount");
		return -1;
	}

	chroot(".");

	execv(cmd, args);
	snprintf(mdir, sizeof(mdir), "execv %s", cmd);
	perror(mdir);
	return -1;
}

static void
usage()
{
	fprintf(stderr, "cpud uname host port\n");
	exit(-1);
}

int
main(int argc, char **argv)
{
	int i, nargs;
	char *s;
	pid_t pid, p;
	char stk[10000];

	if (argc < 4)
		usage();

	uname = argv[1];
	host = argv[2];
	port = strtol(argv[3], &s, 10);
	if (*s != 0)
		usage();

	nargs = argc - 3;
	if (nargs < 2)
		nargs = 2;

	cmd = "/bin/bash";
	args = calloc(nargs, sizeof(char *));
	if (argc > 4) {
		cmd = argv[4];
		for(i = 4; i < argc; i++)
			args[i-4] = argv[i];
	} else {
		cmd = "/bin/bash";
		args[1] = "--login";
	}

	args[0] = cmd;

	snprintf(mntpt, sizeof(mntpt), "/tmp/cpu%d", getpid());
	mkdir(mntpt, 0700);
	pid = clone(procfn, stk + 5000, CLONE_NEWNS | CLONE_FILES 
		| CLONE_PTRACE | SIGCHLD, NULL);

	if ((int) pid == -1) {
		perror("clone");
		exit(-1);
	}

	p = waitpid(pid, NULL, 0);
	if ((int) p == -1) {
		perror("waitpid");
		exit(-1);
	}

	if (!rmdir(mntpt))
		perror("rmdir");

	return 0;
}
