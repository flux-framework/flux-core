/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_SCHEDUTIL_JOBKEY_H
#define _FLUX_SCHEDUTIL_JOBKEY_H

#include <stdbool.h>
#include <flux/core.h>

int schedutil_jobkey (char *buf, int bufsz, bool active,
                      flux_jobid_t id, const char *key);

#endif /* !_FLUX_SCHEDUTIL_JOBKEY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
