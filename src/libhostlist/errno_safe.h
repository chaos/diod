/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef LIBUTIL_ERRNO_SAFE_H
#define LIBUTIL_ERRNO_SAFE_H 1

#define ERRNO_SAFE_WRAP(fun, ...) do { \
	int saved_errno = errno; \
	(fun)(__VA_ARGS__); \
	errno = saved_errno; \
} while (0)

#endif
