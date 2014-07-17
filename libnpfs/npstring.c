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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <inttypes.h>

#include "9p.h"
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
        vsnprintf (s, len, fmt, ap); /* ignore overflow */
        va_end (ap);
}

#if NPSTATS_RWCOUNT_BINS != 12
#error fix hardwired rwcount bins in np_[en,de]code_tpools_str
#endif

int
np_decode_tpools_str (char *s, Npstats *stats)
{
	int n;

	n = sscanf (s, "%as %d %d " \
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
			&stats->nreqs[P9_TSTATFS],
			&stats->nreqs[P9_TLOPEN],
			&stats->nreqs[P9_TLCREATE],
			&stats->nreqs[P9_TSYMLINK],
			&stats->nreqs[P9_TMKNOD],
			&stats->nreqs[P9_TRENAME],
			&stats->nreqs[P9_TREADLINK],
			&stats->nreqs[P9_TGETATTR],
			&stats->nreqs[P9_TSETATTR],
			&stats->nreqs[P9_TXATTRWALK],
			&stats->nreqs[P9_TXATTRCREATE],
			&stats->nreqs[P9_TREADDIR],
			&stats->nreqs[P9_TFSYNC],
			&stats->nreqs[P9_TLOCK],
			&stats->nreqs[P9_TGETLOCK],
			&stats->nreqs[P9_TLINK],
			&stats->nreqs[P9_TMKDIR],
			&stats->nreqs[P9_TVERSION],
			&stats->nreqs[P9_TAUTH],
			&stats->nreqs[P9_TATTACH],
			&stats->nreqs[P9_TFLUSH],
			&stats->nreqs[P9_TWALK],
			&stats->nreqs[P9_TREAD],
			&stats->nreqs[P9_TWRITE],
			&stats->nreqs[P9_TCLUNK],
			&stats->nreqs[P9_TREMOVE],
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
			stats->nreqs[P9_TSTATFS],
			stats->nreqs[P9_TLOPEN],
			stats->nreqs[P9_TLCREATE],
			stats->nreqs[P9_TSYMLINK],
			stats->nreqs[P9_TMKNOD],
			stats->nreqs[P9_TRENAME],
			stats->nreqs[P9_TREADLINK],
			stats->nreqs[P9_TGETATTR],
			stats->nreqs[P9_TSETATTR],
			stats->nreqs[P9_TXATTRWALK],
			stats->nreqs[P9_TXATTRCREATE],
			stats->nreqs[P9_TREADDIR],
			stats->nreqs[P9_TFSYNC],
			stats->nreqs[P9_TLOCK],
			stats->nreqs[P9_TGETLOCK],
			stats->nreqs[P9_TLINK],
			stats->nreqs[P9_TMKDIR],
			stats->nreqs[P9_TVERSION],
			stats->nreqs[P9_TAUTH],
			stats->nreqs[P9_TATTACH],
			stats->nreqs[P9_TFLUSH],
			stats->nreqs[P9_TWALK],
			stats->nreqs[P9_TREAD],
			stats->nreqs[P9_TWRITE],
			stats->nreqs[P9_TCLUNK],
			stats->nreqs[P9_TREMOVE],
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
