/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_FSD_H
#define _UTIL_FSD_H

#include <stddef.h>

/*  Attempt to parse a string duration 's' as Flux Standard Duration
 *   string (floating point seconds with optional suffix).
 */
int fsd_parse_duration (const char *s, double *dp);

/*  Format 'duration' in floating point seconds into a human readable
 *   string in Flux Standard Duration form.
 */
int fsd_format_duration (char *buf, size_t len, double duration);

#endif /* !_UTIL_FSD_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
