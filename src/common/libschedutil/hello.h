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

/* Callback for ingesting allocated R's.
 * Return 0 on success, -1 on failure with errno set.
 * Failure of the callback aborts iteration and causes schedutil_hello()
 * to return -1 with errno passed through.
 */
typedef int (hello_f)(flux_t *h, const char *R, void *arg);

/* Send hello announcement to job-manager.
 * The job-manager responds with a list of jobs that have resources assigned.
 * This function looks up R for each job and passes it 'cb' with 'arg'.
 */
int schedutil_hello (flux_t *h, hello_f *cb, void *arg);

#endif /* !_FLUX_SCHEDUTIL_HELLO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
