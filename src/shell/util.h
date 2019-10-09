/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_UTIL_H
#define _SHELL_UTIL_H

/* substitute rank with {{taskid}} template in path */
int shell_util_taskid_path (const char *path,
                            int rank,
                            char *pathbuf,
                            int pathbuflen);

#endif /* !_SHELL_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
