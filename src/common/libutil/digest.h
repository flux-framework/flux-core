/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_DIGEST_H
#define _UTIL_DIGEST_H

#include <sys/types.h>

/* len - optionally return length of data read in file */
char * digest_file (const char *path, size_t *len);

#endif /* !_UTIL_DIGEST_H */
