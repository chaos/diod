/*
 * Copyright (C) 2006 by Latchesar Ionkov <lucho@ionkov.net>
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <pthread.h>
#include "9p.h"
#include "npfs.h"

typedef struct Nperror Nperror;
struct Nperror {
	char*	ename;
	int	ecode;
};

static pthread_key_t error_key;
static pthread_once_t error_once = PTHREAD_ONCE_INIT;

static void
np_destroy_error(void *a)
{
	Nperror *err;

	err = a;
	free(err->ename);
	free(err);
}

void *
np_malloc(int size)
{
	void *ret;

	ret = malloc(size);
	if (!ret)
		np_werror(Ennomem, ENOMEM);

	return ret;
}

static void
np_init_error_key(void)
{
	pthread_key_create(&error_key, np_destroy_error);
}

static void
np_vwerror(Nperror *err, char *ename, int ecode, va_list ap)
{
	if (err->ename && err->ename != Ennomem) {
		free(err->ename);
		err->ename = NULL;
	}

	err->ecode = ecode;
	if (ename) {
		/* RHEL5 has issues
		vasprintf(&err->ename, ename, ap);
		  */
		err->ename = malloc(1024);
		if (!err->ename) {
			err->ename = Ennomem;
			err->ecode = ENOMEM;
		} else
			vsnprintf(err->ename, 1024, ename, ap);
	} else
		err->ename = NULL;
}
void
np_werror(char *ename, int ecode, ...)
{
	va_list ap;
	Nperror *err;

	pthread_once(&error_once, np_init_error_key);
	err = pthread_getspecific(error_key);
	if (!err) {
		err = malloc(sizeof(*err));
		if (!err) {
			fprintf(stderr, "not enough memory\n");
			return;
		}

		err->ename = NULL;
		err->ecode = 0;
		pthread_setspecific(error_key, err);
	}

	va_start(ap, ecode);
	np_vwerror(err, ename, ecode, ap);
	va_end(ap);
}

void
np_rerror(char **ename, int *ecode)
{
	Nperror *err;

	pthread_once(&error_once, np_init_error_key);
	err = pthread_getspecific(error_key);
	if (err) {
		*ename = err->ename;
		*ecode = err->ecode;
	} else {
		*ename = NULL;
		*ecode = 0;
	}
}

int
np_haserror()
{
	Nperror *err;

	pthread_once(&error_once, np_init_error_key);
	err = pthread_getspecific(error_key);
	if (err)
		return err->ename != NULL;
	else
		return 0;
}

void
np_uerror(int ecode)
{
	char buf[256];

	strerror_r(ecode, buf, sizeof(buf));
	np_werror(buf, ecode);
}

void
np_suerror(char *s, int ecode)
{
	char err[256];
	char buf[512];

	strerror_r(ecode, err, sizeof(err));
	snprintf(buf, sizeof(buf), "%s: %s", s, err);
	np_werror(buf, ecode);
}
