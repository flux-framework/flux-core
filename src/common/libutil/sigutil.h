/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_SIGUTIL_H
#define _UTIL_SIGUTIL_H

/* Return signal number given a string, e.g. "SIGINT" or "INT"
 */
int sigutil_signum (const char *s);

/* Return signal name given number, e.g. 10 -> "SIGUSR1"
 */
const char *sigutil_signame (int signum);

#endif /* !_UTIL_SIGUTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
