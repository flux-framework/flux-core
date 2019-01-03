/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_READ_ALL_H
#define _UTIL_READ_ALL_H

#include <sys/types.h>

ssize_t write_all (int fd, const void *buf, size_t len);
ssize_t read_all (int fd, void **bufp);

#endif /* !_UTIL_READ_ALL_H */
