/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_WALLCLOCK_H
#define _UTIL_WALLCLOCK_H

#define WALLCLOCK_MAXLEN 33

int wallclock_get_zulu (char *buf, size_t len);

#endif /* !_UTIL_WALLCLOCK_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
