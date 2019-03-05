/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_JOB_EVENTLOG_H
#define _FLUX_CORE_JOB_EVENTLOG_H

#include <stdbool.h>
#include <stdint.h>
#include <flux/core.h>

#include "job_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Eventlog streaming functions, return a single event per response.
 * The flux_job_eventlog_lookup_cancel() function can be called
 * to end the stream early.
 */
flux_future_t *flux_job_eventlog_lookup (flux_t *h, int flags, flux_jobid_t id);
int flux_job_eventlog_lookup_get (flux_future_t *f, const char **event);
int flux_job_eventlog_lookup_cancel (flux_future_t *f);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_JOB_EVENTLOG_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
