/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_SCHEDUTIL_HELLO_H
#define _FLUX_SCHEDUTIL_HELLO_H

#include <flux/core.h>

#include "init.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Send hello announcement to job-manager.
 * The job-manager responds with a list of jobs that have resources assigned.
 * This function looks up R for each job and passes R + metadata to
 * ops->hello callback.
 */
int schedutil_hello (schedutil_t *util);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_SCHEDUTIL_HELLO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
