/*************************************************************\
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 * Copyright (C) 2010 by Lawrence Livermore National Security, LLC.
 *
 * This file is part of npfs, a framework for 9P synthetic file systems.
 * For details see https://sourceforge.net/projects/npfs.
 *
 * SPDX-License-Identifier: MIT
 *************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>

#include "npfs.h"

void
np_strzero(Npstr *str)
{
	str->str = NULL;
	str->len = 0;
}

char *
np_strdup(Npstr *str)
{
	char *ret;

	ret = malloc(str->len + 1);
	if (ret) {
		memmove(ret, str->str, str->len);
		ret[str->len] = '\0';
	}

	return ret;
}

int
np_strcmp(Npstr *str, char *cs)
{
	int ret;

	ret = strncmp(str->str, cs, str->len);
	if (!ret && cs[str->len])
		ret = 1;

	return ret;
}

int
np_strncmp(Npstr *str, char *cs, int len)
{
	int ret;

	if (str->len >= len)
		ret = strncmp(str->str, cs, len);
	else
		ret = np_strcmp(str, cs);

	return ret;
}

int
np_str9cmp (Npstr *s1, Npstr *s2)
{
	if (s1->len != s2->len)
		return 1;
	return strncmp (s1->str, s2->str, s1->len);
}

#define CHUNKSIZE 80
static int
vaspf (char **sp, int *lp, const char *fmt, va_list ap)
{
	char *s = *sp;
	int len = *lp;
	int n, ret = -1;
	int slen = s ? strlen (s) : 0;

	if (!s) {
		len = CHUNKSIZE;
		if (!(s = malloc(len)))
			goto done;
	}
	for (;;) {
		va_list vacpy;

		va_copy(vacpy, ap);
		n = vsnprintf(s + slen, len - slen, fmt, vacpy);
		va_end(vacpy);
		if (n != -1 && n < len - slen)
			break;
		len += CHUNKSIZE;
		if (!(s = realloc (s, len)))
			goto done;
	}
	*lp = len;
	*sp = s;
	ret = 0;
done:
	return ret;
}

int
aspf (char **sp, int *lp, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start (ap, fmt);
	n = vaspf (sp, lp, fmt, ap);
	va_end (ap);

	return n;
}

void
spf (char *s, int len, const char *fmt, ...)
{
	va_list ap;
	int n = strlen (s);

	len -= n;
	s += n;
	NP_ASSERT (len > 0);

	va_start (ap, fmt);
	if (vsnprintf (s, len, fmt, ap) >= len)
		strncpy (&s[len - 4], "...", 4);
	va_end (ap);
}

#if NPSTATS_RWCOUNT_BINS != 12
#error fix hardwired rwcount bins in np_[en,de]code_tpools_str
#endif

int
np_decode_tpools_str (char *s, Npstats *stats)
{
	int n;

	n = sscanf (s, "%ms %d %d " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64,
			&stats->name, &stats->numreqs, &stats->numfids,
			&stats->rbytes, &stats->wbytes,
			&stats->nreqs[Tstatfs],
			&stats->nreqs[Tlopen],
			&stats->nreqs[Tlcreate],
			&stats->nreqs[Tsymlink],
			&stats->nreqs[Tmknod],
			&stats->nreqs[Trename],
			&stats->nreqs[Treadlink],
			&stats->nreqs[Tgetattr],
			&stats->nreqs[Tsetattr],
			&stats->nreqs[Txattrwalk],
			&stats->nreqs[Txattrcreate],
			&stats->nreqs[Treaddir],
			&stats->nreqs[Tfsync],
			&stats->nreqs[Tlock],
			&stats->nreqs[Tgetlock],
			&stats->nreqs[Tlink],
			&stats->nreqs[Tmkdir],
			&stats->nreqs[Tversion],
			&stats->nreqs[Tauth],
			&stats->nreqs[Tattach],
			&stats->nreqs[Tflush],
			&stats->nreqs[Twalk],
			&stats->nreqs[Tread],
			&stats->nreqs[Twrite],
			&stats->nreqs[Tclunk],
			&stats->nreqs[Tremove],
			&stats->rcount[0],
			&stats->rcount[1],
			&stats->rcount[2],
			&stats->rcount[3],
			&stats->rcount[4],
			&stats->rcount[5],
			&stats->rcount[6],
			&stats->rcount[7],
			&stats->rcount[8],
			&stats->rcount[9],
			&stats->rcount[10],
			&stats->rcount[11],
			&stats->wcount[0],
			&stats->wcount[1],
			&stats->wcount[2],
			&stats->wcount[3],
			&stats->wcount[4],
			&stats->wcount[5],
			&stats->wcount[6],
			&stats->wcount[7],
			&stats->wcount[8],
			&stats->wcount[9],
			&stats->wcount[10],
			&stats->wcount[11]);
	if (n != 55) {
		if (stats->name) {
			free (stats->name);
			stats->name = NULL;
		}
		return -1;
	}
	return 0;
}

int
np_encode_tpools_str (char **s, int *len, Npstats *stats)
{
	return aspf (s, len, "%s %d %d " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"%"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" " \
		"\n",
			stats->name, stats->numreqs, stats->numfids,
			stats->rbytes, stats->wbytes,
			stats->nreqs[Tstatfs],
			stats->nreqs[Tlopen],
			stats->nreqs[Tlcreate],
			stats->nreqs[Tsymlink],
			stats->nreqs[Tmknod],
			stats->nreqs[Trename],
			stats->nreqs[Treadlink],
			stats->nreqs[Tgetattr],
			stats->nreqs[Tsetattr],
			stats->nreqs[Txattrwalk],
			stats->nreqs[Txattrcreate],
			stats->nreqs[Treaddir],
			stats->nreqs[Tfsync],
			stats->nreqs[Tlock],
			stats->nreqs[Tgetlock],
			stats->nreqs[Tlink],
			stats->nreqs[Tmkdir],
			stats->nreqs[Tversion],
			stats->nreqs[Tauth],
			stats->nreqs[Tattach],
			stats->nreqs[Tflush],
			stats->nreqs[Twalk],
			stats->nreqs[Tread],
			stats->nreqs[Twrite],
			stats->nreqs[Tclunk],
			stats->nreqs[Tremove],
			stats->rcount[0],
			stats->rcount[1],
			stats->rcount[2],
			stats->rcount[3],
			stats->rcount[4],
			stats->rcount[5],
			stats->rcount[6],
			stats->rcount[7],
			stats->rcount[8],
			stats->rcount[9],
			stats->rcount[10],
			stats->rcount[11],
			stats->wcount[0],
			stats->wcount[1],
			stats->wcount[2],
			stats->wcount[3],
			stats->wcount[4],
			stats->wcount[5],
			stats->wcount[6],
			stats->wcount[7],
			stats->wcount[8],
			stats->wcount[9],
			stats->wcount[10],
			stats->wcount[11]);
}
