/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_MONOTIME_H
#    define _UTIL_MONOTIME_H

#    include <time.h>
#    include <stdbool.h>

double monotime_since (struct timespec t0); /* milliseconds */
void monotime (struct timespec *tp);
bool monotime_isset (struct timespec t);

#endif /* !_UTIL_MONOTIME_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
