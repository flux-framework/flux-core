/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_POPEN2_H
#define _UTIL_POPEN2_H

struct popen2_child;

struct popen2_child *popen2 (const char *path, char *const argv[]);

int popen2_get_fd (struct popen2_child *p);

int pclose2 (struct popen2_child *p);

#endif /* !_UTIL_POPEN2_H */
