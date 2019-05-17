/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_SCHEDUTIL_READY_H
#    define _FLUX_SCHEDUTIL_READY_H

#    include <flux/core.h>

/* Send ready request to job-manager, selecting interface 'mode'
 * ("single", "unlimited", ...).  'queue_depth', if non-NULL,
 * is set to the number of jobs in SCHED state that have not yet requested
 * resources.  Returns 0 on success, -1 on failure with errno set.
 */
int schedutil_ready (flux_t *h, const char *mode, int *queue_depth);

#endif /* !_FLUX_SCHEDUTIL_READY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
